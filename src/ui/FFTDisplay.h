#pragma once

#include "core/FFTEngine.h"
#include <imgui.h>

class FFTDisplay {
public:
    // Draw FFT spectrum in the given area
    void draw(ImDrawList* dl, ImVec2 pos, ImVec2 size,
              const FFTResult& fft, uint32_t color);

private:
    static constexpr int MAX_POLYLINE_POINTS = 4096;
    float m_dbMin = -120.0f;
    float m_dbMax = 20.0f;
};
