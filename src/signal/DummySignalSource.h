#pragma once

#include "signal/ISignalSource.h"
#include <array>

class DummySignalSource : public ISignalSource {
public:
    void configure(const ScopeState& state) override;
    void acquire(SignalData& data) override;
    std::string name() const override { return "Dummy Signal Generator"; }

private:
    // Display-layer scale (probe attenuation / invert), applied at generation.
    std::array<float, NUM_ANALOG_CHANNELS> m_userScale{ 1.0f, 1.0f, 1.0f, 1.0f };
    float m_timePerDiv = 1.0e-3f;
    float m_visibleTime = 10.0e-3f;  // visible window = timePerDiv * 10
    float m_totalTime = 10.0e-3f;    // total acquisition span (multi-window)
    float m_sampleRate = 1.0e6f;
    int m_recordLength = 10000;
    float m_triggerLevel = 0.0f;
    int m_triggerSource = 0;
    float m_phase = 0.0f; // running phase for animation
};
