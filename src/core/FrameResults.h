#pragma once

#include "core/SignalBuffer.h"
#include "core/FFTEngine.h"
#include "core/Types.h"

// Per-frame computed results produced in the main loop (math channel output
// and FFT spectrum). These are frame-locals in main.cpp; bundling them here
// lets the remote server expose them to the CLI without recomputing.
//
// Pointers are non-owning and valid only for the duration of a single
// processCommands() call on the main thread.
struct FrameResults {
    const AnalogBuffer* math = nullptr; // time-domain math output (null unless enabled and not FFT)
    const FFTResult* fft = nullptr;     // spectrum (null unless math op == FFT)
    bool mathEnabled = false;
    MathOp mathOp = MathOp::Add;
};
