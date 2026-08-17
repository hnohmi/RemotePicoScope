#include "render/WaveformRenderer.h"
#include "render/ViewTransform.h"
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

// Time-aware sample<->pixel mapping. The record's center (the trigger point)
// is anchored to the view center; screen density comes from the actual
// sample rate. The record is NEVER stretched to fit the window — otherwise
// changing the record length would change the apparent time/div.
namespace {
struct TimeMap {
    double samplesPerWindow = 1.0; // samples spanning the visible window
    double firstSample = 0.0;      // (fractional) sample index at left edge

    static TimeMap make(int count, float sampleRate, const ScopeState& state) {
        TimeMap m;
        double viewSpan = static_cast<double>(state.timePerDiv()) * GRID_DIVISIONS_X;
        double dt = (sampleRate > 0.0f) ? 1.0 / sampleRate
                                        : viewSpan / (count > 0 ? count : 1);
        m.samplesPerWindow = viewSpan / dt;
        // Trigger (record center) sits at view center when the horizontal
        // offset is 0; the offset pans the view through the captured span.
        m.firstSample = (count - 1) * 0.5 - m.samplesPerWindow * 0.5
                        + static_cast<double>(state.horizontalOffset) / dt;
        return m;
    }

    float xOfSample(double i, ImVec2 pos, ImVec2 size) const {
        return pos.x + static_cast<float>((i - firstSample) / samplesPerWindow) * size.x;
    }

    // (fractional) sample index under pixel column px; may be out of range.
    double sampleAtPx(double px, ImVec2 size) const {
        return firstSample + (px / size.x) * samplesPerWindow;
    }
};
} // namespace

void WaveformRenderer::drawAnalogChannel(
    ImDrawList* dl, ImVec2 pos, ImVec2 size,
    const AnalogBuffer& buffer, float sampleRate,
    const ChannelState& ch, const ScopeState& state,
    uint32_t color) const
{
    if (!ch.enabled || buffer.count < 2)
        return;

    // Single volts->screen mapping; display offset is applied only inside
    // the view transform (stage 2), never to the sample data.
    ChannelView view = ChannelView::forChannel(ch, pos, size);

    int pixelWidth = static_cast<int>(size.x);
    if (pixelWidth < 2) return;

    // Ground (0 V) reference marker at the left edge: shows where this
    // channel's zero sits after the display offset — the per-channel
    // "center line".
    {
        float gy = std::clamp(view.yFromVolts(0.0f), pos.y, pos.y + size.y);
        dl->AddTriangleFilled(
            ImVec2(pos.x - 1, gy - 5),
            ImVec2(pos.x - 1, gy + 5),
            ImVec2(pos.x + 7, gy),
            color);
    }

    // Time-aware mapping (see TimeMap): density from the actual sample rate.
    TimeMap tm = TimeMap::make(buffer.count, sampleRate, state);

    // Visible sample range (record may cover only part of the window, or
    // extend beyond it — clip in both cases).
    int iStart = static_cast<int>(std::ceil(std::max(0.0, tm.firstSample)));
    int iEnd = static_cast<int>(std::floor(std::min(
        static_cast<double>(buffer.count - 1),
        tm.firstSample + tm.samplesPerWindow)));
    if (iEnd <= iStart) return;

    float samplesPerPixel = static_cast<float>(tm.samplesPerWindow / pixelWidth);

    if (samplesPerPixel <= 1.0f) {
        // Fewer samples than pixels: draw polyline with interpolation
        // Break into segments to avoid ImGui alloca overflow
        std::vector<ImVec2> points;
        points.reserve(std::min(iEnd - iStart + 1, MAX_POLYLINE_POINTS));

        for (int i = iStart; i <= iEnd; i++) {
            float x = tm.xOfSample(i, pos, size);
            float y = view.yFromVolts(buffer.samples[i]);
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
        // More samples than pixels: min/max decimation over the pixel range
        // actually covered by the record.
        int pxStart = static_cast<int>(std::clamp(
            tm.xOfSample(iStart, pos, size) - pos.x, 0.0f, static_cast<float>(pixelWidth)));
        int pxEnd = static_cast<int>(std::clamp(
            tm.xOfSample(iEnd, pos, size) - pos.x + 1.0f, 0.0f, static_cast<float>(pixelWidth)));

        // Cap per-frame work: examine at most ~32 samples per pixel column.
        // Beyond that, stride-subsample — narrow glitches may be missed at
        // extreme zoom-out, but the frame time stays bounded regardless of
        // record length (O(width), not O(record)).
        int stride = static_cast<int>(samplesPerPixel / 32.0f) + 1;

        for (int px = pxStart; px < pxEnd; px++) {
            double s0 = tm.sampleAtPx(px, size);
            double s1 = s0 + samplesPerPixel;
            int startSample = std::clamp(static_cast<int>(s0), 0, buffer.count - 1);
            int endSample = std::clamp(static_cast<int>(s1), startSample + 1, buffer.count);

            float minVal = buffer.samples[startSample];
            float maxVal = minVal;
            for (int s = startSample + 1; s < endSample; s += stride) {
                float v = buffer.samples[s];
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }

            float x = pos.x + px;
            float yMin = view.yFromVolts(maxVal);
            float yMax = view.yFromVolts(minVal);
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
    const DigitalBuffer& buffer, float sampleRate,
    const ScopeState& state) const
{
    if (buffer.count < 2) return;

    // Same time-aware mapping as the analog display: digital lanes share the
    // acquisition timebase and must show the same time density.
    TimeMap tm = TimeMap::make(buffer.count, sampleRate, state);
    auto sampleAt = [&](int px) -> int {
        double s = tm.sampleAtPx(px, size);
        if (s < 0.0 || s > buffer.count - 1) return -1;
        return static_cast<int>(s);
    };

    // Count enabled digital channels and buses (each occupies one row).
    int enabledCount = 0;
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++) {
        if (state.digital[i].enabled)
            enabledCount++;
    }
    int busCount = 0;
    for (const auto& b : state.buses)
        if (b.enabled && !b.lanes.empty()) busCount++;

    int totalRows = enabledCount + busCount;
    if (totalRows == 0) return;

    float laneHeight = size.y / totalRows;
    float margin = laneHeight * 0.15f;
    float signalHeight = laneHeight - 2.0f * margin;

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

        // Draw waveform (skipping pixel columns outside the record)
        bool havePrev = false;
        bool prevHigh = false;
        float prevX = pos.x;

        for (int px = 0; px <= static_cast<int>(size.x); px++) {
            int sampleIdx = sampleAt(px);
            float curX = pos.x + px;

            if (sampleIdx < 0) {
                if (havePrev) { // record ends inside the window — finalize
                    float y = prevHigh ? highY : lowY;
                    dl->AddLine(ImVec2(prevX, y), ImVec2(curX, y), color, 1.0f);
                    havePrev = false;
                }
                continue;
            }

            bool curHigh = (buffer.samples[sampleIdx] & mask) != 0;
            if (!havePrev) {
                prevHigh = curHigh;
                prevX = curX;
                havePrev = true;
                continue;
            }
            if (curHigh != prevHigh) {
                float y = prevHigh ? highY : lowY;
                dl->AddLine(ImVec2(prevX, y), ImVec2(curX, y), color, 1.0f);
                dl->AddLine(ImVec2(curX, highY), ImVec2(curX, lowY), color, 1.0f);
                prevHigh = curHigh;
                prevX = curX;
            }
        }
        if (havePrev) {
            float y = prevHigh ? highY : lowY;
            dl->AddLine(ImVec2(prevX, y), ImVec2(pos.x + size.x, y), color, 1.0f);
        }

        // Draw separator line
        if (laneIdx > 0) {
            float sepY = pos.y + laneIdx * laneHeight;
            dl->AddLine(ImVec2(pos.x, sepY), ImVec2(pos.x + size.x, sepY),
                        IM_COL32(50, 50, 60, 128), 1.0f);
        }

        laneIdx++;
    }

    // --- Bus tracks: value boxes between transitions ---
    const ImU32 busColor = IM_COL32(210, 190, 90, 230);
    for (const auto& bus : state.buses) {
        if (!bus.enabled || bus.lanes.empty()) continue;

        float laneTop = pos.y + laneIdx * laneHeight + margin;
        float topY = laneTop;
        float botY = laneTop + signalHeight;
        float midY = laneTop + signalHeight * 0.5f;

        if (laneIdx > 0) {
            float sepY = pos.y + laneIdx * laneHeight;
            dl->AddLine(ImVec2(pos.x, sepY), ImVec2(pos.x + size.x, sepY),
                        IM_COL32(50, 50, 60, 128), 1.0f);
        }
        dl->AddText(ImVec2(pos.x + 2, laneTop), busColor,
                    bus.name.empty() ? "BUS" : bus.name.c_str());

        auto fmt = [&](uint32_t v) -> std::string {
            char buf[40];
            if (bus.display == 1) {
                std::string s;
                for (int b = static_cast<int>(bus.lanes.size()) - 1; b >= 0; b--)
                    s += ((v >> b) & 1) ? '1' : '0';
                return s;
            } else if (bus.display == 2) {
                snprintf(buf, sizeof(buf), "%u", v);
            } else {
                snprintf(buf, sizeof(buf), "%X", v);
            }
            return buf;
        };

        // Value boxes between transitions, over the record's pixel range only
        bool haveSeg = false;
        float segX0 = pos.x;
        uint32_t segVal = 0;
        auto endSegment = [&](float x1) {
            dl->AddLine(ImVec2(segX0, topY), ImVec2(x1, topY), busColor, 1.0f);
            dl->AddLine(ImVec2(segX0, botY), ImVec2(x1, botY), busColor, 1.0f);
            dl->AddLine(ImVec2(x1, topY), ImVec2(x1, botY), busColor, 1.0f);
            std::string txt = fmt(segVal);
            float tw = ImGui::CalcTextSize(txt.c_str()).x;
            if (x1 - segX0 > tw + 6.0f)
                dl->AddText(ImVec2((segX0 + x1) * 0.5f - tw * 0.5f,
                                   midY - ImGui::GetTextLineHeight() * 0.5f),
                            busColor, txt.c_str());
        };
        for (int px = 0; px <= static_cast<int>(size.x); px++) {
            int sampleIdx = sampleAt(px);
            float curX = pos.x + px;
            if (sampleIdx < 0) {
                if (haveSeg) { endSegment(curX); haveSeg = false; }
                continue;
            }
            uint32_t v = bus.value(buffer.samples[sampleIdx]);
            if (!haveSeg) { haveSeg = true; segX0 = curX; segVal = v; continue; }
            if (v != segVal) { endSegment(curX); segX0 = curX; segVal = v; }
        }
        if (haveSeg) endSegment(pos.x + size.x);
        laneIdx++;
    }
}

void WaveformRenderer::drawTriggerIndicator(
    ImDrawList* dl, ImVec2 pos, ImVec2 size,
    const ScopeState& state) const
{
    int trigCh = state.trigger.source;
    if (trigCh < 0 || trigCh >= NUM_ANALOG_CHANNELS) return;

    // The trigger level is a data-domain value (volts). It is drawn through
    // the trigger source channel's view, so that channel's display offset
    // moves the level line together with its waveform — and no other
    // channel's offset can affect it.
    ChannelView view = ChannelView::forChannel(state.analog[trigCh], pos, size);
    float trigY = std::clamp(view.yFromVolts(state.trigger.level),
                             pos.y, pos.y + size.y);

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
