#include "ui/FFTDisplay.h"
#include "core/Types.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>

void FFTDisplay::draw(ImDrawList* dl, ImVec2 pos, ImVec2 size,
                      const FFTResult& fft, uint32_t color)
{
    if (!fft.valid || fft.binCount < 2) return;

    float width = size.x;
    float height = size.y;
    float dbRange = m_dbMax - m_dbMin;

    // Grid: frequency axis (horizontal) and dB axis (vertical)
    // dB grid: every 20 dB
    ImU32 gridColor = IM_COL32(50, 50, 60, 100);
    ImU32 gridColorMajor = IM_COL32(60, 60, 70, 140);
    ImU32 textColor = IM_COL32(180, 180, 190, 200);

    for (float db = m_dbMin; db <= m_dbMax; db += 20.0f) {
        float y = pos.y + height * (1.0f - (db - m_dbMin) / dbRange);
        bool isMajor = (static_cast<int>(db) % 40 == 0);
        dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + width, y),
                    isMajor ? gridColorMajor : gridColor);

        char label[16];
        snprintf(label, sizeof(label), "%+.0f dB", db);
        dl->AddText(ImVec2(pos.x + 4, y - 12), textColor, label);
    }

    // Frequency grid: logarithmic decades
    float maxFreq = fft.maxFrequency;
    if (maxFreq <= 0.0f) return;

    // Draw decade lines (10, 100, 1k, 10k, 100k, 1M, ...)
    float logMin = std::log10(fft.freqResolution);
    float logMax = std::log10(maxFreq);
    float logRange = logMax - logMin;
    if (logRange <= 0.0f) return;

    for (float decade = std::ceil(logMin); decade <= logMax; decade += 1.0f) {
        float freq = std::pow(10.0f, decade);
        float x = pos.x + width * (std::log10(freq) - logMin) / logRange;
        dl->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + height), gridColorMajor);

        char label[32];
        if (freq >= 1e6f)
            snprintf(label, sizeof(label), "%.0f MHz", freq / 1e6f);
        else if (freq >= 1e3f)
            snprintf(label, sizeof(label), "%.0f kHz", freq / 1e3f);
        else
            snprintf(label, sizeof(label), "%.0f Hz", freq);
        dl->AddText(ImVec2(x + 2, pos.y + height - 14), textColor, label);
    }

    // Draw spectrum polyline (log frequency scale)
    std::vector<ImVec2> points;
    points.reserve(std::min(fft.binCount, static_cast<int>(width)));

    // Map bins to pixels using log frequency scale
    int prevPixelX = -1;
    float pixelMax = m_dbMin; // track max dB for current pixel column

    for (int i = 1; i < fft.binCount; i++) { // skip DC bin
        float freq = i * fft.freqResolution;
        if (freq <= 0.0f) continue;

        float logFreq = std::log10(freq);
        float normX = (logFreq - logMin) / logRange;
        int pixelX = static_cast<int>(normX * width);

        if (pixelX == prevPixelX) {
            // Same pixel column — keep max
            pixelMax = std::max(pixelMax, fft.magnitudeDB[i]);
        } else {
            // New pixel column — emit previous point
            if (prevPixelX >= 0 && prevPixelX < static_cast<int>(width)) {
                float normY = 1.0f - (pixelMax - m_dbMin) / dbRange;
                normY = std::clamp(normY, 0.0f, 1.0f);
                points.push_back(ImVec2(pos.x + prevPixelX, pos.y + normY * height));
            }
            prevPixelX = pixelX;
            pixelMax = fft.magnitudeDB[i];
        }
    }
    // Emit last point
    if (prevPixelX >= 0 && prevPixelX < static_cast<int>(width)) {
        float normY = 1.0f - (pixelMax - m_dbMin) / dbRange;
        normY = std::clamp(normY, 0.0f, 1.0f);
        points.push_back(ImVec2(pos.x + prevPixelX, pos.y + normY * height));
    }

    // Draw polyline in segments
    if (points.size() >= 2) {
        for (size_t offset = 0; offset < points.size() - 1; offset += MAX_POLYLINE_POINTS - 1) {
            size_t count = std::min(static_cast<size_t>(MAX_POLYLINE_POINTS),
                                    points.size() - offset);
            if (count >= 2)
                dl->AddPolyline(&points[offset], static_cast<int>(count),
                                color, ImDrawFlags_None, 1.5f);
        }
    }

    // "FFT" label
    dl->AddText(ImVec2(pos.x + width - 30, pos.y + 4), color, "FFT");
}
