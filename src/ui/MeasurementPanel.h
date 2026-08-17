#pragma once

#include "core/ScopeState.h"
#include "core/Measurements.h"
#include "core/SignalBuffer.h"
#include <array>

class MeasurementPanel {
public:
    // Non-const state: the panel also hosts the cursor controls.
    void draw(ScopeState& state, const SignalData& data);

private:
    std::array<MeasurementResult, NUM_ANALOG_CHANNELS> m_results;
    int m_selectedChannel = 0;

    // Throttled measurement cache: a full-buffer scan every frame stalls the
    // UI at large record lengths. Recompute at ~2 Hz, decimated to <= 1 M
    // points for the panel readout (CLI `measure` remains full-precision).
    MeasurementResult m_cached;
    AnalogBuffer m_scratch;
    double m_lastCompute = -1.0;
    int m_cachedChannel = -1;
};
