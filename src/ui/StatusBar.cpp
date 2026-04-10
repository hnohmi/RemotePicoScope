#include "ui/StatusBar.h"
#include "core/Version.h"
#include <imgui.h>

void StatusBar::draw(const ScopeState& state, const SignalData& data,
                     const MidiEngine& midiEngine, std::function<void()> onSettingsClicked,
                     const std::string& signalSourceName)
{
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("Status", nullptr, flags);

    // Settings button (left side)
    if (ImGui::SmallButton("Settings")) {
        if (onSettingsClicked) onSettingsClicked();
    }
    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // Signal source indicator
    if (!signalSourceName.empty()) {
        bool isPico = (state.signalSource == SignalSourceType::PicoScope);
        ImVec4 srcColor = isPico ? ImVec4(0.3f, 0.9f, 0.5f, 1.0f)
                                 : ImVec4(0.7f, 0.7f, 0.4f, 1.0f);
        ImGui::TextColored(srcColor, "%s", signalSourceName.c_str());
        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();
    }

    // MIDI status indicator
    if (midiEngine.isOpen()) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "MIDI");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "MIDI");
    }
    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // Trigger status
    const char* statusStr = "?";
    ImVec4 statusColor(0.5f, 0.5f, 0.5f, 1.0f);
    switch (state.triggerStatus) {
        case TriggerStatus::Ready:     statusStr = "Ready";   statusColor = ImVec4(0.4f, 0.8f, 0.4f, 1.0f); break;
        case TriggerStatus::Armed:     statusStr = "Armed";   statusColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f); break;
        case TriggerStatus::Triggered: statusStr = "Trig'd";  statusColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); break;
        case TriggerStatus::Stopped:   statusStr = "Stop";    statusColor = ImVec4(0.8f, 0.2f, 0.2f, 1.0f); break;
        case TriggerStatus::Auto:      statusStr = "Auto";    statusColor = ImVec4(0.2f, 0.6f, 0.8f, 1.0f); break;
    }
    ImGui::TextColored(statusColor, "[%s]", statusStr);
    ImGui::SameLine();

    // Run mode
    if (state.runMode == RunMode::Run) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "RUN");
    } else if (state.runMode == RunMode::Stop) {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "STOP");
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), "SINGLE");
    }
    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // Sample rate
    ImGui::Text("SR: %s", formatEngineering(data.sampleRate, "Sa/s").c_str());
    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // Record length
    ImGui::Text("Rec: %d pts", state.recordLength);
    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // Time/div
    ImGui::Text("T: %s/div", formatEngineering(state.timePerDiv(), "s").c_str());
    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // Trigger source and level
    ImGui::Text("Trig: CH%d %s %.2fV",
        state.trigger.source + 1,
        state.trigger.edge == TriggerEdge::Rising ? "^" : "v",
        state.trigger.level);

    // Version info on the right edge
    {
        char versionBuf[64];
        snprintf(versionBuf, sizeof(versionBuf),
            "v%s (proto %s)", Version::APP, Version::CLI_PROTOCOL);
        float w = ImGui::CalcTextSize(versionBuf).x;
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > w + 12) {
            ImGui::SameLine(0, avail - w - 8);
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.65f, 1.0f), "%s", versionBuf);
        }
    }

    ImGui::End();
}
