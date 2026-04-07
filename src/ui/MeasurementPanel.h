#pragma once

#include "core/ScopeState.h"
#include "core/Measurements.h"
#include "core/SignalBuffer.h"
#include <array>

class MeasurementPanel {
public:
    void draw(const ScopeState& state, const SignalData& data);

private:
    std::array<MeasurementResult, NUM_ANALOG_CHANNELS> m_results;
    int m_selectedChannel = 0;
};
