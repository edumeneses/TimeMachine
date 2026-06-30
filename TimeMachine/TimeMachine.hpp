#pragma once

// --- REQUIRED INCLUDES ---
#include <halp/controls.hpp>
#include <halp/meta.hpp>
#include <halp/audio.hpp>
#include <halp/callback.hpp>
#include <halp/layout.hpp>

#include <vector>
#include <complex>

// PFFFT Header (single-precision real transform)
extern "C" {
    #include <pffft/pffft.h>
}

// Global Constants
constexpr int FFT_SIZE = 2048;
constexpr int HOP_SIZE = 512;
constexpr int FFT_BINS = FFT_SIZE / 2 + 1;
// Longest snapshot the "Length" control can scrub over; the record buffer is
// sized for this. The active scrub window is Length seconds within it.
constexpr float MAX_SECONDS = 4.0f;

// --- PFFFT WRAPPER DECLARATION ---
struct PFFFT_Wrapper {
    PFFFT_Setup* setup = nullptr;
    float* work_buffer = nullptr;
    float* fft_io_buffer = nullptr;
    int size = 0;

    explicit PFFFT_Wrapper(int fft_size);
    ~PFFFT_Wrapper();

    // Non-copyable (owns raw aligned buffers) but movable, so the enclosing
    // State can live in a std::vector.
    PFFFT_Wrapper(const PFFFT_Wrapper&) = delete;
    PFFFT_Wrapper& operator=(const PFFFT_Wrapper&) = delete;

    PFFFT_Wrapper(PFFFT_Wrapper&& o) noexcept
        : setup(o.setup), work_buffer(o.work_buffer)
        , fft_io_buffer(o.fft_io_buffer), size(o.size) {
        o.setup = nullptr;
        o.work_buffer = nullptr;
        o.fft_io_buffer = nullptr;
        o.size = 0;
    }
    PFFFT_Wrapper& operator=(PFFFT_Wrapper&& o) noexcept {
        if (this != &o) {
            if (setup) pffft_destroy_setup(setup);
            if (work_buffer) pffft_aligned_free(work_buffer);
            if (fft_io_buffer) pffft_aligned_free(fft_io_buffer);
            setup = o.setup;
            work_buffer = o.work_buffer;
            fft_io_buffer = o.fft_io_buffer;
            size = o.size;
            o.setup = nullptr;
            o.work_buffer = nullptr;
            o.fft_io_buffer = nullptr;
            o.size = 0;
        }
        return *this;
    }

    void forward(const std::vector<float>& input, std::vector<std::complex<float>>& output_complex);
    void inverse(const std::vector<std::complex<float>>& input_complex, std::vector<float>& output);
};

// --- PLUGIN STRUCT DECLARATION ---
struct TimeMachine {
    halp_meta(name, "Time Machine")
    halp_meta(c_name, "avnd_time_machine")
    halp_meta(category, "Spectral")
    halp_meta(author, "Edu Meneses")
    halp_meta(description, "Spectral Freeze using Phase Vocoder")
    halp_meta(uuid, "7f8a9b0c-1d2e-3f4a-5b6c-7d8e9f0a1b2c")

    // Inputs (named struct so the UI can reference its members)
    struct inputs_t {
        halp::toggle<"Freeze"> freeze;
        // Crossfade between live input (dry) and the frozen sound (wet).
        halp::knob_f32<"Dry/Wet", halp::range{.min = 0.0f, .max = 1.0f, .init = 1.0f}> mix;
        // Play "needle" within the captured snapshot: 0 = start of the window,
        // 1 = most recent frame.
        halp::knob_f32<"Pointer", halp::range{.min = 0.0f, .max = 1.0f, .init = 1.0f}> pointer;
        // Pitch ratio applied to the frozen sound (1 = unchanged).
        halp::knob_f32<"Pitch", halp::range{.min = 0.25f, .max = 4.0f, .init = 1.0f}> pitch;
        // Length of the scrub window, in seconds, within the snapshot.
        halp::knob_f32<"Length", halp::range{.min = 0.05f, .max = MAX_SECONDS, .init = 0.5f}> length;
        halp::audio_bus<"Input", float> audio_in;
    } inputs;

    // Outputs
    struct {
        halp::audio_bus<"Output", float> audio_out;
    } outputs;

    // Per-channel state
    struct State {
        // Continuous record buffer (ring) + the snapshot frozen at freeze onset.
        std::vector<float> record_ring;   // max_len samples
        std::vector<float> snapshot;      // max_len samples (chronological)
        int max_len = 0;
        int rec_widx = 0;
        bool has_snapshot = false;

        // Analysis / synthesis
        std::vector<float> window;        // FFT_SIZE
        std::vector<float> time_domain_buf;
        std::vector<std::complex<float>> freq_a;   // analysis frame
        std::vector<std::complex<float>> freq_b;   // analysis frame + HOP
        std::vector<std::complex<float>> out_spec; // pitched output spectrum

        std::vector<float> frozen_mag;    // FFT_BINS, partial magnitude at the needle
        std::vector<float> frozen_tf;     // FFT_BINS, partial true frequency (in bins)
        std::vector<float> synth_phase;   // FFT_BINS, advancing synthesis phase (output bins)
        std::vector<float> out_mag;       // FFT_BINS, magnitude scattered to output bins
        std::vector<float> out_tf;        // FFT_BINS, output target frequency (in bins)

        std::vector<float> out_ring;      // FFT_SIZE overlap-add accumulator

        PFFFT_Wrapper fft;

        int ridx = 0;
        int ptr = 0;
        int last_read_start = -1;         // -1 forces a re-analysis
        bool was_frozen = false;

        State();
        void init_buffers(int max_len_samples);

        // One STFT hop. read_start/pitch are resolved by operator() from the
        // Pointer/Length/Pitch controls.
        void process_frame(bool freeze, int read_start, float pitch);
    };

    std::vector<State> channels;
    double sample_rate = 48000.0;

    void prepare(halp::setup info);
    void operator()(int frames);

    // UI
    struct ui {
        using enum halp::colors;
        using enum halp::layouts;
        halp_meta(name, "Main")
        halp_meta(layout, vbox)
        halp_meta(background, mid)

        halp::item<&inputs_t::freeze> freeze;
        halp::item<&inputs_t::mix> mix;
        halp::item<&inputs_t::pointer> pointer;
        halp::item<&inputs_t::pitch> pitch;
        halp::item<&inputs_t::length> length;
    };
};
