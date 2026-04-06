#pragma once

// --- REQUIRED INCLUDES ---
#include <halp/controls.hpp>
#include <halp/meta.hpp>
#include <halp/audio.hpp>
#include <halp/callback.hpp> 

#include <vector>
#include <complex>
#include <span> 

// PFFFT Header
extern "C" { 
    #include "pffft.h" 
}

// Global Constants (Visible to both files)
constexpr int FFT_SIZE = 2048;
constexpr int HOP_SIZE = 512; 

// --- PFFFT WRAPPER DECLARATION ---
struct PFFFT_Wrapper {
    PFFFT_Setup* setup;
    float* work_buffer;
    float* fft_io_buffer;
    int size;

    PFFFT_Wrapper(int fft_size);
    ~PFFFT_Wrapper();

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
        static constexpr auto uuid = "7f8a9b0c-1d2e-3f4g-5h6i-7j8k9l0m1n2o";
    };

    // 2. Inputs
    struct {
        halp::toggle<"Freeze"> freeze; 
        halp::knob_f32<"Dry/Wet", halp::range{0.0f, 1.0f}> mix{1.0f};
        halp::audio_bus<"Input", float> audio_in; 
    } inputs;

    // 3. Outputs
    struct {
        halp::audio_bus<"Output", float> audio_out;
    } outputs;

    // 4. Internal State
    struct State {
        std::vector<float> input_fifo;
        std::vector<float> output_accum;
        std::vector<float> window;
        std::vector<float> time_domain_buf;
        std::vector<std::complex<float>> freq_domain_buf;
        
        std::vector<float> frozen_mag;
        std::vector<float> frozen_phase_inc;
        std::vector<float> last_input_phase;
        std::vector<float> current_freeze_phase;

        PFFFT_Wrapper fft;

        int ptr = 0;
        bool was_frozen = false;

        State(); // Constructor declaration
    };

    std::vector<State> channels;

    // 5. Methods
    void prepare(halp::setup setup);
    void operator()(int frames);
    
    // Internal helper
    void process_frame(State& st, bool freeze);
};