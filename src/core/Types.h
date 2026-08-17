#pragma once

#include <array>
#include <cstdint>
#include <string>

// Channel colors
// Stored as ABGR for ImGui (ImU32 format)
namespace ChannelColors {
    constexpr uint32_t CH1 = 0xFF40E0FF; // Yellow  #FFE040
    constexpr uint32_t CH2 = 0xFF40FF40; // Green   #40FF40
    constexpr uint32_t CH3 = 0xFFFFA040; // Blue    #40A0FF
    constexpr uint32_t CH4 = 0xFFA040FF; // Pink    #FF40A0

    constexpr uint32_t Digital = 0xFF80FF80; // Light green for digital
    constexpr uint32_t Math = 0xFFFFFF40;   // Cyan #40FFFF

    inline uint32_t analog(int ch) {
        constexpr uint32_t colors[] = { CH1, CH2, CH3, CH4 };
        return colors[ch % 4];
    }
}

constexpr int NUM_ANALOG_CHANNELS = 4;
constexpr int NUM_DIGITAL_CHANNELS = 16;
constexpr int GRID_DIVISIONS_X = 10;
constexpr int GRID_DIVISIONS_Y = 8;
constexpr int GRID_SUBDIVISIONS = 5;

// Acquisition span in display windows (trigger centered), so the view can
// pan away from the trigger point through real captured data. Capped in
// absolute time so slow timebases don't make sweeps unbearably long.
//  - Auto record mode (live): 3 windows — the active window with the trigger
//    at its center, plus one full window of pre- and post-trigger timeshift
//    headroom. Kept short so rapid-block batches re-arm fast and the display
//    can animate trigger-by-trigger.
//  - Fixed record mode (deep memory): ACQ_WINDOW_MULT windows.
constexpr int ACQ_FAST_WINDOW_MULT = 3;
constexpr int ACQ_WINDOW_MULT = 10;
constexpr float ACQ_MAX_SPAN_SEC = 10.0f;

// Standard oscilloscope 1-2-5 sequence for V/div and time/div
namespace Sequence125 {
    // V/div values in volts: 1mV to 100V
    constexpr float VOLTS_PER_DIV[] = {
        0.001f, 0.002f, 0.005f,
        0.010f, 0.020f, 0.050f,
        0.100f, 0.200f, 0.500f,
        1.000f, 2.000f, 5.000f,
        10.00f, 20.00f, 50.00f,
        100.0f
    };
    constexpr int VOLTS_PER_DIV_COUNT = sizeof(VOLTS_PER_DIV) / sizeof(float);
    constexpr int VOLTS_PER_DIV_DEFAULT = 9; // 1V/div

    // Time/div values in seconds: 1ns to 50s
    constexpr float TIME_PER_DIV[] = {
        1.0e-9f, 2.0e-9f, 5.0e-9f,
        1.0e-8f, 2.0e-8f, 5.0e-8f,
        1.0e-7f, 2.0e-7f, 5.0e-7f,
        1.0e-6f, 2.0e-6f, 5.0e-6f,
        1.0e-5f, 2.0e-5f, 5.0e-5f,
        1.0e-4f, 2.0e-4f, 5.0e-4f,
        1.0e-3f, 2.0e-3f, 5.0e-3f,
        1.0e-2f, 2.0e-2f, 5.0e-2f,
        1.0e-1f, 2.0e-1f, 5.0e-1f,
        1.0f, 2.0f, 5.0f,
        10.0f, 20.0f, 50.0f
    };
    constexpr int TIME_PER_DIV_COUNT = sizeof(TIME_PER_DIV) / sizeof(float);
    constexpr int TIME_PER_DIV_DEFAULT = 18; // 1ms/div

    // Find closest index in a 1-2-5 array
    inline int findClosestIndex(const float* arr, int count, float value) {
        int best = 0;
        float bestDiff = 1e30f;
        for (int i = 0; i < count; i++) {
            float diff = (value > arr[i]) ? value / arr[i] : arr[i] / value;
            if (diff < bestDiff) {
                bestDiff = diff;
                best = i;
            }
        }
        return best;
    }
}

// Math channel operations
enum class MathOp : int {
    Add = 0,        // src1 + src2
    Subtract = 1,   // src1 - src2
    Multiply = 2,   // src1 * src2
    Divide = 3,     // src1 / src2
    FFT = 4,        // FFT of src1
    Derivative = 5, // d/dt of src1
    Integral = 6,   // integral of src1
    Sqrt = 7        // sqrt(|src1|)
};

inline bool isMathOpUnary(MathOp op) {
    return op >= MathOp::FFT;
}

inline const char* mathOpName(MathOp op) {
    switch (op) {
        case MathOp::Add:        return "+";
        case MathOp::Subtract:   return "-";
        case MathOp::Multiply:   return "*";
        case MathOp::Divide:     return "/";
        case MathOp::FFT:        return "FFT";
        case MathOp::Derivative: return "d/dt";
        case MathOp::Integral:   return "integ";
        case MathOp::Sqrt:       return "sqrt";
    }
    return "?";
}

// FFT window types
enum class FFTWindowType : int {
    Rectangular = 0,
    Hanning = 1,
    Hamming = 2,
    BlackmanHarris = 3,
    FlatTop = 4
};

inline const char* fftWindowName(FFTWindowType w) {
    switch (w) {
        case FFTWindowType::Rectangular:    return "Rectangular";
        case FFTWindowType::Hanning:        return "Hanning";
        case FFTWindowType::Hamming:        return "Hamming";
        case FFTWindowType::BlackmanHarris: return "Blackman-Harris";
        case FFTWindowType::FlatTop:        return "Flat Top";
    }
    return "?";
}

// Signal source selection
enum class SignalSourceType : int {
    Dummy = 0,
    PicoScope = 1
};

enum class Coupling : int {
    DC = 0,
    AC = 1,
    GND = 2
};

inline const char* couplingToString(Coupling c) {
    switch (c) {
        case Coupling::DC: return "DC";
        case Coupling::AC: return "AC";
        case Coupling::GND: return "GND";
    }
    return "?";
}

enum class TriggerEdge : int {
    Rising = 0,
    Falling = 1
};

enum class TriggerType : int {
    Edge = 0,     // analog channel edge
    Digital = 1,  // single digital lane edge
    Pattern = 2   // digital pattern match
};

inline const char* triggerTypeName(TriggerType t) {
    switch (t) {
        case TriggerType::Edge:    return "edge";
        case TriggerType::Digital: return "digital";
        case TriggerType::Pattern: return "pattern";
    }
    return "edge";
}

enum class TriggerMode : int {
    Auto = 0,
    Normal = 1,
    Single = 2
};

enum class RunMode : int {
    Run = 0,
    Stop = 1,
    Single = 2
};

enum class TriggerStatus : int {
    Ready,
    Armed,
    Triggered,
    Stopped,
    Auto
};

// Parameter IDs for MIDI mapping
enum class ParameterID : uint32_t {
    // Analog channels
    CH1_VoltsPerDiv, CH1_Offset, CH1_Enable,
    CH2_VoltsPerDiv, CH2_Offset, CH2_Enable,
    CH3_VoltsPerDiv, CH3_Offset, CH3_Enable,
    CH4_VoltsPerDiv, CH4_Offset, CH4_Enable,

    // Horizontal
    TimePerDiv, HorizontalOffset,

    // Trigger
    TriggerLevel, TriggerSource, TriggerEdge,

    // Run control
    RunStop, SingleShot,

    // Cursors
    CursorX1, CursorX2, CursorY1, CursorY2,

    COUNT
};

// Mapping curve types for MIDI CC
enum class MappingCurve : int {
    Linear = 0,
    Logarithmic = 1,
    Stepped = 2,
    Toggle = 3,
    Momentary = 4
};

// Helper to format engineering notation (e.g., 1.00 ms, 500 uV)
inline std::string formatEngineering(float value, const char* unit) {
    const char* prefixes[] = { "f", "p", "n", "u", "m", "", "k", "M", "G", "T" };
    const float scales[] = { 1e-15f, 1e-12f, 1e-9f, 1e-6f, 1e-3f, 1.0f, 1e3f, 1e6f, 1e9f, 1e12f };
    const int numPrefixes = 10;

    float absVal = (value < 0) ? -value : value;
    if (absVal == 0.0f) {
        char buf[64];
        snprintf(buf, sizeof(buf), "0.00 %s", unit);
        return buf;
    }

    int bestIdx = 5; // default to no prefix
    for (int i = 0; i < numPrefixes - 1; i++) {
        if (absVal < scales[i + 1]) {
            bestIdx = i;
            break;
        }
    }

    char buf[64];
    float scaled = value / scales[bestIdx];
    snprintf(buf, sizeof(buf), "%.2f %s%s", scaled, prefixes[bestIdx], unit);
    return buf;
}
