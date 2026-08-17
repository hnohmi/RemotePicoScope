#pragma once

#include "core/ScopeState.h"
#include "core/SignalBuffer.h"
#include "core/Measurements.h"
#include <cmath>

// One-shot autoscale: analyzes the current acquisition and adjusts vertical
// scale/offset, timebase, and trigger to frame the signals. Operates on the
// data already in the buffers (valid because acquisition runs continuously),
// so no multi-frame state machine is needed.
//
// Header-only (inline) to avoid a CMake source-list change. Uses fminf/fmaxf
// rather than std::min/std::max because windows.h (included in the TUs that
// use this) defines conflicting min/max macros.
namespace AutoScale {

// Returns the number of channels on which a signal was detected.
inline int apply(ScopeState& state, const SignalData& data) {
    const float kNoiseFloor = 0.005f; // 5 mV Vpp — below this, treat as no signal

    int strongest = -1;
    float strongestVpp = 0.0f;
    float strongestMean = 0.0f;
    int detected = 0;

    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        const AnalogBuffer& buf = data.analog[ch];
        if (buf.count < 2) continue;

        float vmin = buf.samples[0];
        float vmax = buf.samples[0];
        double sum = 0.0;
        for (int i = 0; i < buf.count; i++) {
            float v = buf.samples[i];
            vmin = fminf(vmin, v);
            vmax = fmaxf(vmax, v);
            sum += v;
        }
        float vpp = vmax - vmin;
        float mean = static_cast<float>(sum / buf.count);

        if (vpp < kNoiseFloor) continue; // no meaningful signal
        detected++;

        // Vertical: fit Vpp into ~6 of the 8 divisions, and center via offset.
        float targetVdiv = vpp / 6.0f;
        state.analog[ch].voltsPerDivIndex = Sequence125::findClosestIndex(
            Sequence125::VOLTS_PER_DIV, Sequence125::VOLTS_PER_DIV_COUNT, targetVdiv);
        state.analog[ch].verticalOffset = -mean; // display adds offset to sample
        state.analog[ch].enabled = true;

        if (vpp > strongestVpp) {
            strongestVpp = vpp;
            strongestMean = mean;
            strongest = ch;
        }
    }

    if (strongest < 0)
        return 0;

    // Horizontal: show ~3 periods of the strongest channel, if a frequency
    // can be measured from the current capture.
    MeasurementResult m = Measurements::compute(data.analog[strongest], data.sampleRate);
    if (m.valid && m.frequency > 0.0f) {
        float targetTimeDiv = 3.0f / (m.frequency * GRID_DIVISIONS_X);
        state.timePerDivIndex = Sequence125::findClosestIndex(
            Sequence125::TIME_PER_DIV, Sequence125::TIME_PER_DIV_COUNT, targetTimeDiv);
    }
    state.horizontalOffset = 0.0f;

    // Trigger: strongest channel, level at its midpoint (mean).
    state.trigger.source = strongest;
    state.trigger.level = strongestMean;

    return detected;
}

} // namespace AutoScale
