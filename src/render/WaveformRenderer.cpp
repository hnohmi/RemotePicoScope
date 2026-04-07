#include "render/WaveformRenderer.h"
#include <cmath>
#include <algorithm>

void WaveformRenderer::drawGrid(ImDrawList* dl, ImVec2 pos, ImVec2 size) const {
    const ImU32 gridMajor = IM_COL32(60, 60, 70, 255);
    const ImU32 gridMinor = IM_COL32(35, 35, 42, 255);
    const ImU32 gridCenter = IM_COL32(80, 80, 95, 255);

    float divW = size.x / GRID_DIVISIONS_X;
    float divH = size.y / GRID_DIVISIONS_Y;
    float subW = divW / GRID_SUBDIVISIONS;
    float subH = divH / GRID_SUBDIVISIONS;

    // Minor grid (subdivisions) - dotted style using small marks at intersections
    for (int ix = 0; ix <= GRID_DIVISIONS_X * GRID_SUBDIVISIONS; ix++) {
        float x = pos.x + ix * subW;
        if (ix % GRID_SUBDIVISIONS == 0) continue; // skip major lines
        // Draw small tick marks along center horizontal line
        float cy = pos.y + size.y * 0.5f;
        dl->AddLine(ImVec2(x, cy - 2), ImVec2(x, cy + 2), gridMinor, 1.0f);
    }
    for (int iy = 0; iy <= GRID_DIVISIONS_Y * GRID_SUBDIVISIONS; iy++) {
        float y = pos.y + iy * subH;
        if (iy % GRID_SUBDIVISIONS == 0) continue;
        float cx = pos.x + size.x * 0.5f;
        dl->AddLine(ImVec2(cx - 2, y), ImVec2(cx + 2, y), gridMinor, 1.0f);
    }

    // Major grid lines
    for (int ix = 0; ix <= GRID_DIVISIONS_X; ix++) {
        float x = pos.x + ix * divW;
        ImU32 col = (ix == GRID_DIVISIONS_X / 2) ? gridCenter : gridMajor;
        float thickness = (ix == GRID_DIVISIONS_X / 2) ? 1.5f : 1.0f;
        dl->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + size.y), col, thickness);
    }
    for (int iy = 0; iy <= GRID_DIVISIONS_Y; iy++) {
        float y = pos.y + iy * divH;
        ImU32 col = (iy == GRID_DIVISIONS_Y / 2) ? gridCenter : gridMajor;
        float thickness = (iy == GRID_DIVISIONS_Y / 2) ? 1.5f : 1.0f;
        dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), col, thickness);
    }

    // Border
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), gridMajor, 0.0f, 0, 1.0f);
}

void WaveformRenderer::drawAnalogChannel(
    ImDrawList* dl, ImVec2 pos, ImVec2 size,
    const AnalogBuffer& buffer,
    const ChannelState& ch, const ScopeState& state,
    uint32_t color) const
{
    if (!ch.enabled || buffer.count < 2)
        return;

    float centerY = pos.y + size.y * 0.5f;
    float pixelsPerDiv = size.y / GRID_DIVISIONS_Y;
    float vPerDiv = ch.voltsPerDiv();
    float offset = ch.verticalOffset;

    int pixelWidth = static_cast<int>(size.x);
    if (pixelWidth < 2) return;

    // Determine samples visible in the current time window
    float totalTime = state.timePerDiv() * GRID_DIVISIONS_X;
    float sampleInterval = totalTime / buffer.count;
    float samplesPerPixel = static_cast<float>(buffer.count) / pixelWidth;

    if (samplesPerPixel <= 1.0f) {
        // Fewer samples than pixels: draw polyline with interpolation
        // Break into segments to avoid ImGui alloca overflow
        std::vector<ImVec2> points;
        points.reserve(std::min(buffer.count, MAX_POLYLINE_POINTS));

        for (int i = 0; i < buffer.count; i++) {
            float x = pos.x + (static_cast<float>(i) / (buffer.count - 1)) * size.x;
            float v = buffer.samples[i];
            float y = centerY - ((v + offset) / vPerDiv) * pixelsPerDiv;
            y = std::clamp(y, pos.y - 10.0f, pos.y + size.y + 10.0f);
            points.push_back(ImVec2(x, y));

            if (static_cast<int>(points.size()) >= MAX_POLYLINE_POINTS) {
                dl->AddPolyline(points.data(), static_cast<int>(points.size()), color, 0, 1.5f);
                // Keep last point as start of next segment
                ImVec2 last = points.back();
                points.clear();
                points.push_back(last);
            }
        }
        if (points.size() >= 2)
            dl->AddPolyline(points.data(), static_cast<int>(points.size()), color, 0, 1.5f);
    } else {
        // More samples than pixels: min/max decimation
        for (int px = 0; px < pixelWidth; px++) {
            int startSample = static_cast<int>(px * samplesPerPixel);
            int endSample = static_cast<int>((px + 1) * samplesPerPixel);
            startSample = std::clamp(startSample, 0, buffer.count - 1);
            endSample = std::clamp(endSample, startSample + 1, buffer.count);

            float minVal = buffer.samples[startSample];
            float maxVal = minVal;
            for (int s = startSample + 1; s < endSample; s++) {
                float v = buffer.samples[s];
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }

            float x = pos.x + px;
            float yMin = centerY - ((maxVal + offset) / vPerDiv) * pixelsPerDiv;
            float yMax = centerY - ((minVal + offset) / vPerDiv) * pixelsPerDiv;
            yMin = std::clamp(yMin, pos.y - 1.0f, pos.y + size.y + 1.0f);
            yMax = std::clamp(yMax, pos.y - 1.0f, pos.y + size.y + 1.0f);

            if (yMax - yMin < 1.0f)
                yMax = yMin + 1.0f;

            dl->AddLine(ImVec2(x, yMin), ImVec2(x, yMax), color, 1.0f);
        }
    }
}

void WaveformRenderer::drawDigitalChannels(
    ImDrawList* dl, ImVec2 pos, ImVec2 size,
    const DigitalBuffer& buffer,
    const ScopeState& state) const
{
    if (buffer.count < 2) return;

    // Count enabled digital channels
    int enabledCount = 0;
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++) {
        if (state.digital[i].enabled)
            enabledCount++;
    }
    if (enabledCount == 0) return;

    float laneHeight = size.y / enabledCount;
    float margin = laneHeight * 0.15f;
    float signalHeight = laneHeight - 2.0f * margin;
    float samplesPerPixel = static_cast<float>(buffer.count) / size.x;

    int laneIdx = 0;
    for (int ch = 0; ch < NUM_DIGITAL_CHANNELS; ch++) {
        if (!state.digital[ch].enabled) continue;

        float laneTop = pos.y + laneIdx * laneHeight + margin;
        float highY = laneTop;
        float lowY = laneTop + signalHeight;
        uint16_t mask = static_cast<uint16_t>(1 << ch);

        // Color: cycle through channel colors
        ImU32 color = IM_COL32(100, 200, 100, 220);

        // Draw channel label
        char label[8];
        snprintf(label, sizeof(label), "D%d", ch);
        dl->AddText(ImVec2(pos.x + 2, laneTop), color, label);

        // Draw waveform
        bool prevHigh = (buffer.samples[0] & mask) != 0;
        float prevX = pos.x;

        for (int px = 1; px <= static_cast<int>(size.x); px++) {
            int sampleIdx = std::min(static_cast<int>(px * samplesPerPixel), buffer.count - 1);
            bool curHigh = (buffer.samples[sampleIdx] & mask) != 0;

            if (curHigh != prevHigh || px == static_cast<int>(size.x)) {
                float curX = pos.x + px;
                // Draw horizontal line for previous state
                float y = prevHigh ? highY : lowY;
                dl->AddLine(ImVec2(prevX, y), ImVec2(curX, y), color, 1.0f);
                // Draw vertical transition
                if (curHigh != prevHigh) {
                    dl->AddLine(ImVec2(curX, highY), ImVec2(curX, lowY), color, 1.0f);
                }
                prevHigh = curHigh;
                prevX = curX;
            }
        }

        // Draw separator line
        if (laneIdx > 0) {
            float sepY = pos.y + laneIdx * laneHeight;
            dl->AddLine(ImVec2(pos.x, sepY), ImVec2(pos.x + size.x, sepY),
                        IM_COL32(50, 50, 60, 128), 1.0f);
        }

        laneIdx++;
    }
}

void WaveformRenderer::drawTriggerIndicator(
    ImDrawList* dl, ImVec2 pos, ImVec2 size,
    const ScopeState& state) const
{
    float centerY = pos.y + size.y * 0.5f;
    float pixelsPerDiv = size.y / GRID_DIVISIONS_Y;
    int trigCh = state.trigger.source;
    if (trigCh < 0 || trigCh >= NUM_ANALOG_CHANNELS) return;

    const auto& ch = state.analog[trigCh];
    float vPerDiv = ch.voltsPerDiv();
    float trigY = centerY - ((state.trigger.level + ch.verticalOffset) / vPerDiv) * pixelsPerDiv;
    trigY = std::clamp(trigY, pos.y, pos.y + size.y);

    ImU32 color = ChannelColors::analog(trigCh);

    // Draw arrow on right edge
    float arrowX = pos.x + size.x + 2;
    dl->AddTriangleFilled(
        ImVec2(arrowX, trigY),
        ImVec2(arrowX + 10, trigY - 5),
        ImVec2(arrowX + 10, trigY + 5),
        color);

    // Draw dashed trigger level line
    float dashLen = 6.0f;
    float gapLen = 4.0f;
    float x = pos.x;
    while (x < pos.x + size.x) {
        float x2 = std::min(x + dashLen, pos.x + size.x);
        dl->AddLine(ImVec2(x, trigY), ImVec2(x2, trigY), (color & 0x00FFFFFF) | 0x60000000, 1.0f);
        x += dashLen + gapLen;
    }
}
