#include "ui/HorizontalPanel.h"
#include "ui/Widgets.h"
#include <imgui.h>

void HorizontalPanel::draw(ScopeState& state) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("Horizontal", nullptr, flags);

    // All controls laid out horizontally
    float itemWidth = 130.0f;

    // Time/div
    ImGui::Text("T/div");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemWidth);
    Widgets::SliderInt("##tdiv", &state.timePerDivIndex, 0, Sequence125::TIME_PER_DIV_COUNT - 1,
        formatEngineering(Sequence125::TIME_PER_DIV[state.timePerDivIndex], "s").c_str());

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Horizontal offset
    float maxOffset = state.timePerDiv() * GRID_DIVISIONS_X * 0.5f;
    ImGui::Text("Pos");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemWidth);
    Widgets::SliderFloat("##hpos", &state.horizontalOffset, -maxOffset, maxOffset,
        formatEngineering(state.horizontalOffset, "s").c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("0##hpos")) state.horizontalOffset = 0.0f;

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Record length
    ImGui::Text("Rec");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    int recLen = state.recordLength;
    if (Widgets::SliderInt("##reclen", &recLen, 1000, 100000, "%d", 1000))
        state.recordLength = recLen;

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Trigger source, level, edge (compact)
    ImGui::Text("Trig");
    ImGui::SameLine();
    const char* sourceItems[] = { "CH1", "CH2", "CH3", "CH4" };
    ImGui::SetNextItemWidth(60);
    Widgets::Combo("##trigsrc", &state.trigger.source, sourceItems, NUM_ANALOG_CHANNELS);

    ImGui::SameLine();
    float vPerDiv = state.analog[state.trigger.source].voltsPerDiv();
    float maxLevel = vPerDiv * GRID_DIVISIONS_Y * 0.5f;
    ImGui::SetNextItemWidth(90);
    Widgets::SliderFloat("##triglvl", &state.trigger.level, -maxLevel, maxLevel, "%.2fV");

    ImGui::SameLine();
    const char* edgeLabels[] = { "Rise", "Fall" };
    int edgeIdx = static_cast<int>(state.trigger.edge);
    ImGui::SetNextItemWidth(60);
    if (Widgets::Combo("##trigedge", &edgeIdx, edgeLabels, 2))
        state.trigger.edge = static_cast<TriggerEdge>(edgeIdx);

    ImGui::SameLine();
    const char* modeLabels[] = { "Auto", "Norm", "Single" };
    int modeIdx = static_cast<int>(state.trigger.mode);
    ImGui::SetNextItemWidth(65);
    if (Widgets::Combo("##trigmode", &modeIdx, modeLabels, 3))
        state.trigger.mode = static_cast<TriggerMode>(modeIdx);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Run/Stop + Single (right side)
    if (state.runMode == RunMode::Run) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("STOP", ImVec2(60, 0)))
            state.runMode = RunMode::Stop;
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.7f, 0.15f, 1.0f));
        if (ImGui::Button("RUN", ImVec2(60, 0)))
            state.runMode = RunMode::Run;
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::Button("SINGLE", ImVec2(60, 0)))
        state.runMode = RunMode::Single;

    ImGui::End();
}
