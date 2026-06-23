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

// Global Constants (Visible to both files)
constexpr int FFT_SIZE = 2048;
constexpr int HOP_SIZE = 512;
constexpr int FFT_BINS = FFT_SIZE / 2 + 1;

// --- PFFFT WRAPPER DECLARATION ---
struct PFFFT_Wrapper {
    PFFFT_Setup* setup = nullptr;
    float* work_buffer = nullptr;
    float* fft_io_buffer = nullptr;
    int size = 0;

    explicit PFFFT_Wrapper(int fft_size);
    ~PFFFT_Wrapper();

    // Non-copyable / non-movable: owns raw aligned buffers.
    PFFFT_Wrapper(const PFFFT_Wrapper&) = delete;
    PFFFT_Wrapper& operator=(const PFFFT_Wrapper&) = delete;

    void forward(const std::vector<float>& input, std::vector<std::complex<float>>& output_complex);
    void inverse(const std::vector<std::complex<float>>& input_complex, std::vector<float>& output);
};

// --- PLUGIN STRUCT DECLARATION ---
struct TimeMachine {
    // 1. Metadata
    struct meta {
        static constexpr auto name = "Time Machine";
        static constexpr auto c_name = "avnd_time_machine";
        static constexpr auto category = "Spectral";
        static constexpr auto author = "Edu Meneses";
        static constexpr auto description = "Spectral Freeze using Phase Vocoder";
        // Must be a valid (hex) UUID.
        static constexpr auto uuid = "7f8a9b0c-1d2e-3f4a-5b6c-7d8e9f0a1b2c";
    };

    // 2. Inputs (named struct so the UI can reference its members)
    struct inputs_t {
        halp::toggle<"Freeze"> freeze;
        halp::knob_f32<"Dry/Wet", halp::range{0.0f, 1.0f}> mix{1.0f};
        halp::audio_bus<"Input", float> audio_in;
    } inputs;

    // 3. Outputs
    struct {
        halp::audio_bus<"Output", float> audio_out;
    } outputs;

    // 4. Internal State (one per audio channel)
    struct State {
        // Ring buffers: O(1) per-sample push/pop (no per-sample std::rotate).
        std::vector<float> in_ring;       // input history, FFT_SIZE
        std::vector<float> out_ring;      // overlap-add accumulator, FFT_SIZE
        std::vector<float> window;        // analysis/synthesis window
        std::vector<float> time_domain_buf;
        std::vector<std::complex<float>> freq_domain_buf;

        std::vector<float> frozen_mag;
        std::vector<float> frozen_phase_inc;
        std::vector<float> last_input_phase;
        std::vector<float> current_freeze_phase;

        PFFFT_Wrapper fft;

        int widx = 0;   // input ring write index (oldest sample sits here)
        int ridx = 0;   // output ring read index  (next sample to emit)
        int ptr = 0;    // samples since last frame
        bool was_frozen = false;

        State(); // Constructor declaration

        // Runs one STFT hop: window -> FFT -> spectral freeze -> IFFT -> overlap-add.
        void process_frame(bool freeze);
    };

    std::vector<State> channels;

    // 5. Methods
    void prepare(halp::setup setup);
    void operator()(int frames);

    // 6. UI
    struct ui {
        using enum halp::colors;
        using enum halp::layouts;
        halp_meta(name, "Main")
        halp_meta(layout, vbox)
        halp_meta(background, mid)

        halp::item<&inputs_t::freeze> freeze;
        halp::item<&inputs_t::mix> mix;
    };
};
