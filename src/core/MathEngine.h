#pragma once

#include "core/Types.h"
#include "core/SignalBuffer.h"
#include "core/ScopeState.h"

class MathEngine {
public:
    // Compute math waveform from input signal data using the given config.
    // Result is written to outputBuffer. For FFT op, use FFTEngine instead.
    void compute(const MathChannelConfig& config,
                 const SignalData& input,
                 AnalogBuffer& output);
};
