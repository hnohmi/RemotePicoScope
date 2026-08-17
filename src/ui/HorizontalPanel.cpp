#include "ui/HorizontalPanel.h"
#include "ui/Widgets.h"
#include "core/MemoryEstimate.h"
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

    // Horizontal offset: pan through the captured acquisition span
    float maxOffset = state.maxHorizontalOffset();
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

    // Record length: presets up to the device's full 512 MS shared buffer,
    // with a live host-memory guesstimate so the user can size against RAM.
    static const int kRecPresets[] = {
        1000, 10000, 100000, 1000000, 5000000, 10000000,
        50000000, 100000000, 250000000, 512000000 };
    ImGui::Text("Rec");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    std::string recLabel = state.recordAuto
        ? "Auto" : MemoryEstimate::formatCount(state.recordLength);
    if (ImGui::BeginCombo("##reclen", recLabel.c_str())) {
        // Auto (live) mode: sweep sized for display; refresh rate follows
        // time/div, like a real scope. Fixed lengths are for deep captures.
        if (ImGui::Selectable("Auto", state.recordAuto))
            state.recordAuto = true;
        ImGui::Separator();
        for (int p : kRecPresets) {
            bool selected = (!state.recordAuto && state.recordLength == p);
            std::string label = MemoryEstimate::formatCount(p);
            if (ImGui::Selectable(label.c_str(), selected)) {
                state.recordAuto = false;
                state.recordLength = p;
            }
        }
        ImGui::EndCombo();
    }

    // Memory guesstimate, colored by pressure against available host RAM
    {
        MemoryEstimate::Breakdown est = MemoryEstimate::estimate(state);
        uint64_t avail = MemoryEstimate::hostAvailableBytes();
        ImVec4 col(0.6f, 0.6f, 0.65f, 1.0f);
        if (avail > 0 && est.total() > avail)
            col = ImVec4(0.95f, 0.3f, 0.25f, 1.0f);      // exceeds free RAM
        else if (avail > 0 && est.total() > avail / 2)
            col = ImVec4(0.95f, 0.7f, 0.2f, 1.0f);       // > half of free RAM

        ImGui::SameLine();
        ImGui::TextColored(col, "~%s", MemoryEstimate::formatBytes(est.total()).c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Host memory estimate for %s samples%s",
                        MemoryEstimate::formatCount(state.effectiveRecordLength()).c_str(),
                        state.recordAuto ? " (auto)" : "");
            ImGui::Separator();
            ImGui::Text("Analog buffers (4ch float): %s",
                        MemoryEstimate::formatBytes(est.analogFloat).c_str());
            ImGui::Text("Driver ADC buffers:         %s",
                        MemoryEstimate::formatBytes(est.analogAdc).c_str());
            ImGui::Text("Digital buffers:            %s",
                        MemoryEstimate::formatBytes(est.digitalBuf).c_str());
            if (est.staging)
                ImGui::Text("Retrieval staging:          %s",
                            MemoryEstimate::formatBytes(est.staging).c_str());
            if (est.mathBuf)
                ImGui::Text("Math buffer:                %s",
                            MemoryEstimate::formatBytes(est.mathBuf).c_str());
            ImGui::Separator();
            ImGui::Text("Total: %s   Host free: %s",
                        MemoryEstimate::formatBytes(est.total()).c_str(),
                        MemoryEstimate::formatBytes(avail).c_str());
            ImGui::TextDisabled("Device: 512 MS shared across enabled channels/ports;");
            ImGui::TextDisabled("the driver may clamp the count at fast timebases.");
            ImGui::EndTooltip();
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Trigger controls live in the Trigger panel (bottom-right tab group) —
    // deliberately not duplicated here.

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
