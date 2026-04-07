#pragma once

#include "core/ScopeState.h"
#include "core/SignalBuffer.h"
#include "core/FFTEngine.h"
#include "render/WaveformRenderer.h"
#include "ui/FFTDisplay.h"

class WaveformDisplay {
public:
    void draw(const ScopeState& state, const SignalData& data,
              const AnalogBuffer* mathBuffer = nullptr,
              const FFTResult* fftResult = nullptr);

private:
    WaveformRenderer m_renderer;
    FFTDisplay m_fftDisplay;
};
