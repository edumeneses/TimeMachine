#include "TimeMachine.hpp"
#include <cmath>
#include <numbers>
#include <algorithm>
#include <cstring>

// --- PFFFT_Wrapper Implementation ---

PFFFT_Wrapper::PFFFT_Wrapper(int fft_size) : size(fft_size) {
    setup = pffft_new_setup(size, PFFFT_REAL);
    work_buffer = (float*)pffft_aligned_malloc(size * 2 * sizeof(float)); 
    fft_io_buffer = (float*)pffft_aligned_malloc(size * sizeof(float));
}

PFFFT_Wrapper::~PFFFT_Wrapper() {
    if(setup) pffft_destroy_setup(setup);
    if(work_buffer) pffft_aligned_free(work_buffer);
    if(fft_io_buffer) pffft_aligned_free(fft_io_buffer);
}

void PFFFT_Wrapper::forward(const std::vector<float>& input, std::vector<std::complex<float>>& output_complex) {
    std::memcpy(fft_io_buffer, input.data(), size * sizeof(float));
    pffft_transform_ordered(setup, fft_io_buffer, fft_io_buffer, work_buffer, PFFFT_FORWARD);
    
    // Unpack PFFFT format to std::complex
    output_complex[0] = {fft_io_buffer[0], 0.0f};
    output_complex[size/2] = {fft_io_buffer[1], 0.0f};
    for (int k = 1; k < size / 2; ++k) {
        output_complex[k] = {fft_io_buffer[2 * k], fft_io_buffer[2 * k + 1]};
    }
}

void PFFFT_Wrapper::inverse(const std::vector<std::complex<float>>& input_complex, std::vector<float>& output) {
    // Pack std::complex to PFFFT format
    fft_io_buffer[0] = input_complex[0].real();
    fft_io_buffer[1] = input_complex[size/2].real();
    for (int k = 1; k < size / 2; ++k) {
        fft_io_buffer[2 * k] = input_complex[k].real();
        fft_io_buffer[2 * k + 1] = input_complex[k].imag();
    }
    pffft_transform_ordered(setup, fft_io_buffer, fft_io_buffer, work_buffer, PFFFT_BACKWARD);
    std::memcpy(output.data(), fft_io_buffer, size * sizeof(float));
    
    // Scaling
    float scale = 1.0f / size;
    for(float& s : output) s *= scale;
}

// --- TimeMachine Implementation ---

TimeMachine::State::State() : fft(FFT_SIZE) {
    input_fifo.resize(FFT_SIZE, 0.0f);
    output_accum.resize(FFT_SIZE, 0.0f);
    time_domain_buf.resize(FFT_SIZE, 0.0f);
    freq_domain_buf.resize(FFT_SIZE / 2 + 1);

    frozen_mag.resize(FFT_SIZE / 2 + 1, 0.0f);
    frozen_phase_inc.resize(FFT_SIZE / 2 + 1, 0.0f);
    last_input_phase.resize(FFT_SIZE / 2 + 1, 0.0f);
    current_freeze_phase.resize(FFT_SIZE / 2 + 1, 0.0f);
    
    window.resize(FFT_SIZE);
    // Hanning Window
    for(int i = 0; i < FFT_SIZE; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (FFT_SIZE - 1)));
    }
}

void TimeMachine::prepare(halp::setup setup) {
    channels.clear();
    // Layout is inferred in operator()
}

void TimeMachine::operator()(int frames) {
    // Lazy Initialization of channels
    if (channels.size() != (size_t)inputs.audio_in.channels) {
            channels.resize(inputs.audio_in.channels);
    }

    const bool freeze = inputs.freeze.value;

    for (int c = 0; c < inputs.audio_in.channels; ++c) {
        auto& st = channels[c];
        
        // Access raw pointers for processing
        float* in = inputs.audio_in.channel(c, frames).data();
        float* out = outputs.audio_out.channel(c, frames).data();

        for (int i = 0; i < frames; ++i) {
            // 1. Input FIFO
            std::rotate(st.input_fifo.begin(), st.input_fifo.begin() + 1, st.input_fifo.end());
            st.input_fifo.back() = in[i];

            st.ptr++;
            if (st.ptr >= HOP_SIZE) {
                process_frame(st, freeze);
                st.ptr = 0;
            }

            // 2. Output Accumulator
            float wet = st.output_accum[0];
            float dry = in[i];
            
            std::rotate(st.output_accum.begin(), st.output_accum.begin() + 1, st.output_accum.end());
            st.output_accum.back() = 0.0f; 

            // 3. Mix
            out[i] = (dry * (1.0f - inputs.mix.value)) + (wet * inputs.mix.value);
        }
    }
}

void TimeMachine::process_frame(State& st, bool freeze) {
    // A. Apply Window
    for(int i=0; i<FFT_SIZE; ++i) {
        st.time_domain_buf[i] = st.input_fifo[i] * st.window[i];
    }

    // B. Forward FFT
    st.fft.forward(st.time_domain_buf, st.freq_domain_buf);

    const int bins = FFT_SIZE / 2 + 1;
    const float PI = std::numbers::pi_v<float>;
    const float TWO_PI = 2.0f * PI;

    // C. Spectral Processing (Phase Vocoder)
    for (int k = 0; k < bins; ++k) {
        std::complex<float> z = st.freq_domain_buf[k];
        float mag = std::abs(z);
        float phase = std::arg(z);

        if (freeze) {
            if (!st.was_frozen) {
                st.frozen_mag[k] = mag;
                
                // Calculate phase increment
                float diff = phase - st.last_input_phase[k];
                diff = std::fmod(diff + PI, TWO_PI) - PI;
                if (diff < -PI) diff += TWO_PI;
                
                st.frozen_phase_inc[k] = diff;
                st.current_freeze_phase[k] = phase; 
            }
            
            // Increment Phase
            st.current_freeze_phase[k] += st.frozen_phase_inc[k];
            st.current_freeze_phase[k] = std::fmod(st.current_freeze_phase[k] + PI, TWO_PI) - PI;
            
            // Reconstruct
            st.freq_domain_buf[k] = std::polar(st.frozen_mag[k], st.current_freeze_phase[k]);

        } else {
            // Track phase for next freeze
            st.last_input_phase[k] = phase;
        }
    }
    
    st.was_frozen = freeze;

    // D. Inverse FFT
    st.fft.inverse(st.freq_domain_buf, st.time_domain_buf);

    // E. Overlap-Add with Gain Compensation (Hanning 75% approx)
    const float gain_comp = 2.0f / 3.0f; 
    for(int i=0; i<FFT_SIZE; ++i) {
        st.output_accum[i] += st.time_domain_buf[i] * st.window[i] * gain_comp;
    }
}