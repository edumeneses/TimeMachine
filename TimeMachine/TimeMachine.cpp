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

    output_complex[0] = {fft_io_buffer[0], 0.0f};
    output_complex[size/2] = {fft_io_buffer[1], 0.0f};
    for (int k = 1; k < size / 2; ++k) {
        output_complex[k] = {fft_io_buffer[2 * k], fft_io_buffer[2 * k + 1]};
    }
}

void PFFFT_Wrapper::inverse(const std::vector<std::complex<float>>& input_complex, std::vector<float>& output) {
    fft_io_buffer[0] = input_complex[0].real();
    fft_io_buffer[1] = input_complex[size/2].real();
    for (int k = 1; k < size / 2; ++k) {
        fft_io_buffer[2 * k] = input_complex[k].real();
        fft_io_buffer[2 * k + 1] = input_complex[k].imag();
    }
    pffft_transform_ordered(setup, fft_io_buffer, fft_io_buffer, work_buffer, PFFFT_BACKWARD);
    std::memcpy(output.data(), fft_io_buffer, size * sizeof(float));

    float scale = 1.0f / size;
    for(float& s : output) s *= scale;
}

// --- helpers ---

static inline float wrap_phase(float x) {
    const float PI = std::numbers::pi_v<float>;
    const float TWO_PI = 2.0f * PI;
    x = std::fmod(x + PI, TWO_PI);
    if (x < 0.0f) x += TWO_PI;
    return x - PI;
}

// --- TimeMachine::State ---

TimeMachine::State::State() : fft(FFT_SIZE) {
    time_domain_buf.resize(FFT_SIZE, 0.0f);
    freq_a.resize(FFT_BINS);
    freq_b.resize(FFT_BINS);
    out_spec.resize(FFT_BINS);
    frozen_mag.resize(FFT_BINS, 0.0f);
    frozen_tf.resize(FFT_BINS, 0.0f);
    synth_phase.resize(FFT_BINS, 0.0f);
    out_mag.resize(FFT_BINS, 0.0f);
    out_tf.resize(FFT_BINS, 0.0f);
    out_ring.resize(FFT_SIZE, 0.0f);

    window.resize(FFT_SIZE);
    for(int i = 0; i < FFT_SIZE; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (FFT_SIZE - 1)));
    }
}

void TimeMachine::State::init_buffers(int max_len_samples) {
    max_len = max_len_samples;
    record_ring.assign(max_len, 0.0f);
    snapshot.assign(max_len, 0.0f);
    rec_widx = 0;
    ridx = 0;
    ptr = 0;
    has_snapshot = false;
    was_frozen = false;
    last_read_start = -1;
}

void TimeMachine::State::process_frame(bool freeze, int read_start, float pitch) {
    // Wet output exists only while frozen.
    if (!freeze) {
        was_frozen = false;
        return;
    }

    // On freeze onset, snapshot the rolling record buffer (chronological).
    if (!was_frozen) {
        for (int j = 0; j < max_len; ++j) {
            int idx = rec_widx + j;
            if (idx >= max_len) idx -= max_len;
            snapshot[j] = record_ring[idx];
        }
        has_snapshot = true;
        last_read_start = -1;
        std::fill(synth_phase.begin(), synth_phase.end(), 0.0f);
    }
    was_frozen = true;

    const float TWO_PI = 2.0f * std::numbers::pi_v<float>;

    // Re-analyse when the needle position changed (scrub) or on the first frame:
    // estimate each partial's magnitude and true frequency (phase-vocoder).
    if (read_start != last_read_start) {
        const int a = std::clamp(read_start, 0, std::max(0, max_len - FFT_SIZE));
        const int b = std::max(0, a - HOP_SIZE);

        for (int j = 0; j < FFT_SIZE; ++j) time_domain_buf[j] = snapshot[a + j] * window[j];
        fft.forward(time_domain_buf, freq_a);
        for (int j = 0; j < FFT_SIZE; ++j) time_domain_buf[j] = snapshot[b + j] * window[j];
        fft.forward(time_domain_buf, freq_b);

        for (int k = 0; k < FFT_BINS; ++k) {
            frozen_mag[k] = std::abs(freq_a[k]);
            // True frequency (in bins) = bin index + phase-deviation correction.
            float inc = wrap_phase(std::arg(freq_a[k]) - std::arg(freq_b[k]));
            float omega = TWO_PI * k * HOP_SIZE / FFT_SIZE;
            float dphi = wrap_phase(inc - omega);
            frozen_tf[k] = k + dphi * FFT_SIZE / (TWO_PI * HOP_SIZE);
        }
        last_read_start = read_start;
    }

    // Phase-vocoder transpose: scatter each partial to its pitch-scaled bin and
    // advance that bin's phase at the target frequency (accurate for any ratio).
    std::fill(out_mag.begin(), out_mag.end(), 0.0f);
    for (int k = 0; k < FFT_BINS; ++k) {
        float tgt = frozen_tf[k] * pitch;
        int tb = (int)std::lround(tgt);
        if (tb >= 0 && tb < FFT_BINS) {
            out_mag[tb] += frozen_mag[k];
            out_tf[tb] = tgt;
        }
    }
    for (int tb = 0; tb < FFT_BINS; ++tb) {
        if (out_mag[tb] > 0.0f) {
            synth_phase[tb] = wrap_phase(synth_phase[tb] + TWO_PI * out_tf[tb] * HOP_SIZE / FFT_SIZE);
            out_spec[tb] = std::polar(out_mag[tb], synth_phase[tb]);
        } else {
            out_spec[tb] = {0.0f, 0.0f};
        }
    }

    fft.inverse(out_spec, time_domain_buf);

    const float gain_comp = 2.0f / 3.0f; // Hann^2 @ 75% overlap -> 1.5
    for (int j = 0; j < FFT_SIZE; ++j) {
        int idx = ridx + j;
        if (idx >= FFT_SIZE) idx -= FFT_SIZE;
        out_ring[idx] += time_domain_buf[j] * window[j] * gain_comp;
    }
}

// --- TimeMachine ---

void TimeMachine::prepare(halp::setup info) {
    sample_rate = (info.rate > 0.0) ? info.rate : 48000.0;
    const int max_len = (int)(MAX_SECONDS * sample_rate);
    channels.clear();
    if (info.input_channels > 0) {
        channels.resize(info.input_channels);
        for (auto& st : channels) st.init_buffers(max_len);
    }
}

void TimeMachine::operator()(int frames) {
    const int nch = inputs.audio_in.channels;
    const int max_len = (int)(MAX_SECONDS * sample_rate);

    // Allocate per-channel state once (guard for a changed layout).
    if (channels.size() != (size_t)nch) {
        channels.resize(nch);
        for (auto& st : channels)
            if (st.max_len != max_len) st.init_buffers(max_len);
    }

    const bool freeze = inputs.freeze.value;
    const float mix = inputs.mix.value;
    const float pitch = inputs.pitch.value;

    // Needle position: Length picks the active window (most-recent seconds) of
    // the snapshot; Pointer scans within it (1 = newest frame).
    int window_len = std::clamp((int)(inputs.length.value * sample_rate), FFT_SIZE, max_len);
    int span = std::max(0, window_len - FFT_SIZE);
    int read_start = (max_len - window_len) + (int)(inputs.pointer.value * span);
    read_start = std::clamp(read_start, 0, std::max(0, max_len - FFT_SIZE));

    for (int c = 0; c < nch; ++c) {
        auto& st = channels[c];
        float* in = inputs.audio_in.channel(c, frames).data();
        float* out = outputs.audio_out.channel(c, frames).data();

        for (int i = 0; i < frames; ++i) {
            const float dry = in[i];

            // 1. Always record the live input into the rolling buffer.
            st.record_ring[st.rec_widx] = dry;
            if (++st.rec_widx >= st.max_len) st.rec_widx = 0;

            // 2. Trigger a frame every HOP_SIZE samples.
            if (++st.ptr >= HOP_SIZE) {
                st.process_frame(freeze, read_start, pitch);
                st.ptr = 0;
            }

            // 3. Pop the oldest synthesised sample (frozen sound only).
            const float wet = st.out_ring[st.ridx];
            st.out_ring[st.ridx] = 0.0f;
            if (++st.ridx >= FFT_SIZE) st.ridx = 0;

            // 4. Dry/Wet crossfade.
            out[i] = (dry * (1.0f - mix)) + (wet * mix);
        }
    }
}
