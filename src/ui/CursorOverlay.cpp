#include "ui/CursorOverlay.h"
#include "render/ViewTransform.h"
#include <imgui.h>
#include <cstdio>
#include <algorithm>

void CursorOverlay::draw(ScopeState& state, ImVec2 pos, ImVec2 size) {
    if (!state.cursors.enabled) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 cursorColor = IM_COL32(200, 200, 200, 180);
    ImU32 readoutBg = IM_COL32(20, 20, 25, 200);

    float centerX = pos.x + size.x * 0.5f;
    float centerY = pos.y + size.y * 0.5f;
    float divW = size.x / GRID_DIVISIONS_X;
    float divH = size.y / GRID_DIVISIONS_Y;

    // Vertical cursors (X1, X2)
    float x1px = centerX + state.cursors.x1 * divW;
    float x2px = centerX + state.cursors.x2 * divW;

    // Dashed vertical lines
    auto drawDashedVLine = [&](float x) {
        float dashLen = 5.0f, gapLen = 3.0f;
        float y = pos.y;
        while (y < pos.y + size.y) {
            float y2 = y + dashLen;
            if (y2 > pos.y + size.y) y2 = pos.y + size.y;
            dl->AddLine(ImVec2(x, y), ImVec2(x, y2), cursorColor, 1.0f);
            y += dashLen + gapLen;
        }
    };

    auto drawDashedHLine = [&](float y) {
        float dashLen = 5.0f, gapLen = 3.0f;
        float x = pos.x;
        while (x < pos.x + size.x) {
            float x2 = x + dashLen;
            if (x2 > pos.x + size.x) x2 = pos.x + size.x;
            dl->AddLine(ImVec2(x, y), ImVec2(x2, y), cursorColor, 1.0f);
            x += dashLen + gapLen;
        }
    };

    drawDashedVLine(x1px);
    drawDashedVLine(x2px);

    // Horizontal cursors (Y1, Y2)
    float y1px = centerY - state.cursors.y1 * divH;
    float y2px = centerY - state.cursors.y2 * divH;
    drawDashedHLine(y1px);
    drawDashedHLine(y2px);

    // Delta readout box.
    // Cursors live on the display plane (divisions). Voltage readouts go
    // through the cursor *source* channel's view: the channel's V/div scales
    // the deltas, and its display offset shifts the absolute values — exactly
    // as the waveform itself is drawn. Other channels' settings are ignored.
    int src = std::clamp(state.cursors.source, 0, NUM_ANALOG_CHANNELS - 1);
    ChannelView view = ChannelView::forChannel(state.analog[src], pos, size);

    float dt = (state.cursors.x2 - state.cursors.x1) * state.timePerDiv();
    float v1 = view.voltsFromScreenDiv(state.cursors.y1);
    float v2 = view.voltsFromScreenDiv(state.cursors.y2);
    float dvVolts = v2 - v1; // offset cancels: depends only on V/div

    char readout[192];
    snprintf(readout, sizeof(readout),
        "dT: %s  1/dT: %s\nCH%d  Y1: %s  Y2: %s  dV: %s",
        formatEngineering(dt, "s").c_str(),
        (dt != 0.0f) ? formatEngineering(1.0f / dt, "Hz").c_str() : "---",
        src + 1,
        formatEngineering(v1, "V").c_str(),
        formatEngineering(v2, "V").c_str(),
        formatEngineering(dvVolts, "V").c_str());

    ImVec2 textSize = ImGui::CalcTextSize(readout);
    ImVec2 readoutPos(pos.x + size.x - textSize.x - 15, pos.y + 5);
    dl->AddRectFilled(
        ImVec2(readoutPos.x - 4, readoutPos.y - 2),
        ImVec2(readoutPos.x + textSize.x + 4, readoutPos.y + textSize.y + 2),
        readoutBg, 3.0f);
    dl->AddText(readoutPos, cursorColor, readout);
}
