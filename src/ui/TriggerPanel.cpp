#include "ui/TriggerPanel.h"
#include "ui/Widgets.h"
#include <imgui.h>
#include <cstdio>

void TriggerPanel::draw(ScopeState& state) {
    ImGui::Begin("Trigger");

    // Trigger type
    const char* typeItems[] = { "Edge (analog)", "Digital", "Pattern" };
    int typeIdx = static_cast<int>(state.trigger.type);
    ImGui::SetNextItemWidth(-1);
    if (Widgets::Combo("Type", &typeIdx, typeItems, 3))
        state.trigger.type = static_cast<TriggerType>(typeIdx);

    ImGui::Separator();

    if (state.trigger.type == TriggerType::Edge) {
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
    } else if (state.trigger.type == TriggerType::Digital) {
        // Digital lane
        ImGui::SetNextItemWidth(-1);
        Widgets::SliderInt("Lane (D)", &state.trigger.digitalSource, 0,
                           NUM_DIGITAL_CHANNELS - 1, "D%d");
        const char* edgeItems[] = { "Rising", "Falling" };
        int edgeIdx = static_cast<int>(state.trigger.edge);
        ImGui::SetNextItemWidth(-1);
        if (Widgets::Combo("Edge", &edgeIdx, edgeItems, 2))
            state.trigger.edge = static_cast<TriggerEdge>(edgeIdx);
    } else { // Pattern
        ImGui::TextDisabled("Click a lane to cycle X / 1 / 0");
        for (int lane = 0; lane < NUM_DIGITAL_CHANNELS; lane++) {
            int c = state.trigger.digitalPattern[lane];
            const char* sym = (c == 1) ? "1" : (c == 2) ? "0" : "X";
            char btn[16];
            snprintf(btn, sizeof(btn), "D%d:%s", lane, sym);
            ImVec4 col = (c == 1) ? ImVec4(0.2f, 0.7f, 0.3f, 1.0f)
                       : (c == 2) ? ImVec4(0.7f, 0.3f, 0.2f, 1.0f)
                                  : ImVec4(0.35f, 0.35f, 0.4f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, col);
            ImGui::PushID(lane);
            if (ImGui::Button(btn, ImVec2(52, 0)))
                state.trigger.digitalPattern[lane] = (c + 1) % 3;
            ImGui::PopID();
            ImGui::PopStyleColor();
            if ((lane % 4) != 3) ImGui::SameLine();
        }
        ImGui::Spacing();
    }

    ImGui::Separator();

    // Mode (all types)
    const char* modeItems[] = { "Auto", "Normal", "Single" };
    int modeIdx = static_cast<int>(state.trigger.mode);
    ImGui::SetNextItemWidth(-1);
    if (Widgets::Combo("Mode", &modeIdx, modeItems, 3))
        state.trigger.mode = static_cast<TriggerMode>(modeIdx);

    ImGui::End();
}
