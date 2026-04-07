#include "ui/TriggerPanel.h"
#include "ui/Widgets.h"
#include <imgui.h>
#include <cstdio>

void TriggerPanel::draw(ScopeState& state) {
    ImGui::Begin("Trigger");

    // Source
    const char* sourceItems[] = { "CH1", "CH2", "CH3", "CH4" };
    ImGui::SetNextItemWidth(-1);
    Widgets::Combo("Source", &state.trigger.source, sourceItems, NUM_ANALOG_CHANNELS);

    // Level
    float vPerDiv = state.analog[state.trigger.source].voltsPerDiv();
    float maxLevel = vPerDiv * GRID_DIVISIONS_Y * 0.5f;
    ImGui::Text("Level: %.3f V", state.trigger.level);
    ImGui::SetNextItemWidth(-1);
    Widgets::SliderFloat("##triglevel", &state.trigger.level, -maxLevel, maxLevel, "%.3f V");

    // Edge
    const char* edgeItems[] = { "Rising", "Falling" };
    int edgeIdx = static_cast<int>(state.trigger.edge);
    ImGui::SetNextItemWidth(-1);
    if (Widgets::Combo("Edge", &edgeIdx, edgeItems, 2))
        state.trigger.edge = static_cast<TriggerEdge>(edgeIdx);

    // Mode
    const char* modeItems[] = { "Auto", "Normal", "Single" };
    int modeIdx = static_cast<int>(state.trigger.mode);
    ImGui::SetNextItemWidth(-1);
    if (Widgets::Combo("Mode", &modeIdx, modeItems, 3))
        state.trigger.mode = static_cast<TriggerMode>(modeIdx);

    ImGui::End();
}
