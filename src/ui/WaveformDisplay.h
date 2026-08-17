#pragma once

#include "core/ScopeState.h"
#include "core/SignalBuffer.h"
#include "core/FFTEngine.h"
#include "render/WaveformRenderer.h"
#include "ui/FFTDisplay.h"
#include "ui/CursorOverlay.h"

class WaveformDisplay {
public:
    // Non-const state: the analog area is an XY control — dragging pans the
    // timebase (X) and moves the grabbed channel's vertical offset (Y).
    void draw(ScopeState& state, const SignalData& data,
              const AnalogBuffer* mathBuffer = nullptr,
              const FFTResult* fftResult = nullptr);

private:
    WaveformRenderer m_renderer;
    FFTDisplay m_fftDisplay;
    CursorOverlay m_cursorOverlay;
    int m_dragChannel = -1; // channel grabbed by a vertical drag, -1 = none
};
