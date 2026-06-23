#include "TimeMachine.hpp"
#include <cmath>
#include <numbers>
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

    // Scaling (PFFFT inverse is unnormalised)
    float scale = 1.0f / size;
    for(float& s : output) s *= scale;
}

// --- TimeMachine Implementation ---

TimeMachine::State::State() : fft(FFT_SIZE) {
    in_ring.resize(FFT_SIZE, 0.0f);
    out_ring.resize(FFT_SIZE, 0.0f);
    time_domain_buf.resize(FFT_SIZE, 0.0f);
    freq_domain_buf.resize(FFT_BINS);

    frozen_mag.resize(FFT_BINS, 0.0f);
    frozen_phase_inc.resize(FFT_BINS, 0.0f);
    last_input_phase.resize(FFT_BINS, 0.0f);
    current_freeze_phase.resize(FFT_BINS, 0.0f);

    window.resize(FFT_SIZE);
    // Hann window
    for(int i = 0; i < FFT_SIZE; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (FFT_SIZE - 1)));
    }
}

void TimeMachine::prepare(halp::setup) {
    // Channel count is taken from the audio bus at runtime; nothing to preallocate
    // here since it is unknown until the first buffer arrives.
    channels.clear();
}

void TimeMachine::operator()(int frames) {
    const int nch = inputs.audio_in.channels;

    // Allocate per-channel state once (only re-runs if the layout changes).
    if (channels.size() != (size_t)nch) {
        channels.resize(nch);
    }

    const bool freeze = inputs.freeze.value;
    const float mix = inputs.mix.value;

    for (int c = 0; c < nch; ++c) {
        auto& st = channels[c];

        float* in = inputs.audio_in.channel(c, frames).data();
        float* out = outputs.audio_out.channel(c, frames).data();

        for (int i = 0; i < frames; ++i) {
            const float dry = in[i];

            // 1. Push newest sample into the input ring (overwrites oldest).
            st.in_ring[st.widx] = dry;
            if (++st.widx >= FFT_SIZE) st.widx = 0;

            // 2. Trigger a frame every HOP_SIZE samples.
            if (++st.ptr >= HOP_SIZE) {
                st.process_frame(freeze);
                st.ptr = 0;
            }

            // 3. Pop the oldest synthesized sample from the output ring.
            const float wet = st.out_ring[st.ridx];
            st.out_ring[st.ridx] = 0.0f;
            if (++st.ridx >= FFT_SIZE) st.ridx = 0;

            // 4. Dry/Wet mix.
            out[i] = (dry * (1.0f - mix)) + (wet * mix);
        }
    }
}

void TimeMachine::State::process_frame(bool freeze) {
    const float PI = std::numbers::pi_v<float>;
    const float TWO_PI = 2.0f * PI;

    // A. Window the last FFT_SIZE samples in chronological order (oldest at widx).
    for(int j = 0; j < FFT_SIZE; ++j) {
        int idx = widx + j;
        if (idx >= FFT_SIZE) idx -= FFT_SIZE;
        time_domain_buf[j] = in_ring[idx] * window[j];
    }

    // B. Forward FFT
    fft.forward(time_domain_buf, freq_domain_buf);

    // C. Spectral Processing (Phase Vocoder freeze)
    for (int k = 0; k < FFT_BINS; ++k) {
        std::complex<float> z = freq_domain_buf[k];
        float mag = std::abs(z);
        float phase = std::arg(z);

        if (freeze) {
            if (!was_frozen) {
                frozen_mag[k] = mag;

                // Per-frame phase increment estimated at freeze onset.
                float diff = phase - last_input_phase[k];
                diff = std::fmod(diff + PI, TWO_PI) - PI;
                if (diff < -PI) diff += TWO_PI;

                frozen_phase_inc[k] = diff;
                current_freeze_phase[k] = phase;
            }

            // Advance the frozen phase, wrapped to [-PI, PI).
            current_freeze_phase[k] += frozen_phase_inc[k];
            current_freeze_phase[k] = std::fmod(current_freeze_phase[k] + PI, TWO_PI) - PI;

            // Reconstruct bin from frozen magnitude + advancing phase.
            freq_domain_buf[k] = std::polar(frozen_mag[k], current_freeze_phase[k]);
        } else {
            // Track input phase so the next freeze onset has a reference.
            last_input_phase[k] = phase;
        }
    }

    was_frozen = freeze;

    // D. Inverse FFT
    fft.inverse(freq_domain_buf, time_domain_buf);

    // E. Overlap-Add into the output ring (Hann^2 @ 75% overlap -> 1.5 normalisation).
    const float gain_comp = 2.0f / 3.0f;
    for(int j = 0; j < FFT_SIZE; ++j) {
        int idx = ridx + j;
        if (idx >= FFT_SIZE) idx -= FFT_SIZE;
        out_ring[idx] += time_domain_buf[j] * window[j] * gain_comp;
    }
}
