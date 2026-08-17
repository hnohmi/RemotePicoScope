#include "ui/ChannelPanel.h"
#include "ui/Widgets.h"
#include <imgui.h>
#include <cstdio>

static void drawAnalogChannelRow(int ch, ChannelState& cs) {
    ImGui::PushID(ch);

    ImU32 color = ChannelColors::analog(ch);
    ImVec4 colVec = ImGui::ColorConvertU32ToFloat4(color);

    // Colored header bar
    char label[16];
    snprintf(label, sizeof(label), "CH%d", ch + 1);

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(colVec.x * 0.3f, colVec.y * 0.3f, colVec.z * 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(colVec.x * 0.4f, colVec.y * 0.4f, colVec.z * 0.4f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(colVec.x * 0.5f, colVec.y * 0.5f, colVec.z * 0.5f, 1.0f));
    bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);

    if (open) {
        // Row 1: Enable + Coupling + BW Limit
        ImGui::Checkbox("Enable", &cs.enabled);
        if (cs.enabled) {
            ImGui::SameLine();
            const char* couplingItems[] = { "DC", "AC", "GND" };
            int couplingIdx = static_cast<int>(cs.coupling);
            ImGui::SetNextItemWidth(50);
            if (Widgets::Combo("##coup", &couplingIdx, couplingItems, 3))
                cs.coupling = static_cast<Coupling>(couplingIdx);
            ImGui::SameLine();
            ImGui::Checkbox("BW", &cs.bandwidthLimit);

            // Row 2: V/div + Offset side by side
            float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::SetNextItemWidth(halfWidth);
            Widgets::SliderInt("##vdiv", &cs.voltsPerDivIndex, 0, Sequence125::VOLTS_PER_DIV_COUNT - 1,
                formatEngineering(Sequence125::VOLTS_PER_DIV[cs.voltsPerDivIndex], "V").c_str());
            ImGui::SameLine();
            float maxOffset = cs.voltsPerDiv() * GRID_DIVISIONS_Y * 0.5f;
            ImGui::SetNextItemWidth(halfWidth);
            Widgets::SliderFloat("##off", &cs.verticalOffset, -maxOffset, maxOffset, "%.2fV");

            // Row 3: Probe attenuation + Invert
            const char* probeItems[] = { "1x", "10x", "100x" };
            const float probeVals[] = { 1.0f, 10.0f, 100.0f };
            int probeIdx = 0;
            for (int p = 0; p < 3; p++)
                if (cs.probeAttenuation == probeVals[p]) probeIdx = p;
            ImGui::SetNextItemWidth(halfWidth);
            if (Widgets::Combo("##probe", &probeIdx, probeItems, 3))
                cs.probeAttenuation = probeVals[probeIdx];
            ImGui::SameLine();
            ImGui::Checkbox("Inv", &cs.invert);

            // Row 4: Label
            char labelBuf[32];
            snprintf(labelBuf, sizeof(labelBuf), "%s", cs.label.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##label", "label", labelBuf, sizeof(labelBuf)))
                cs.label = labelBuf;
        }
    }

    ImGui::PopID();
}

static void drawDigitalTab(ScopeState& state) {
    if (ImGui::Button("All On")) {
        for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++)
            state.digital[i].enabled = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("All Off")) {
        for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++)
            state.digital[i].enabled = false;
    }

    ImGui::Spacing();

    // Per-port logic thresholds
    {
        const char* presetNames[] = { "TTL 1.5V", "3.3V CMOS", "1.8V CMOS", "Custom" };
        const float presetVals[] = { 1.5f, 1.65f, 0.9f };
        for (int port = 0; port < 2; port++) {
            ImGui::PushID(port + 100);
            ImGui::TextUnformatted(port == 0 ? "D0-D7 thr" : "D8-15 thr");
            ImGui::SameLine();
            int presetIdx = 3;
            for (int p = 0; p < 3; p++)
                if (state.digitalThreshold[port] == presetVals[p]) presetIdx = p;
            ImGui::SetNextItemWidth(90);
            if (Widgets::Combo("##thpreset", &presetIdx, presetNames, 4)) {
                if (presetIdx < 3) state.digitalThreshold[port] = presetVals[presetIdx];
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            Widgets::SliderFloat("##thval", &state.digitalThreshold[port], -5.0f, 5.0f, "%.2fV");
            ImGui::PopID();
        }
    }

    ImGui::Spacing();

    if (ImGui::BeginTable("DigitalChannels", 2, ImGuiTableFlags_None)) {
        ImGui::TableNextColumn();
        ImGui::Text("D0 - D7");
        ImGui::Separator();
        for (int i = 0; i < 8; i++) {
            char dlabel[8];
            snprintf(dlabel, sizeof(dlabel), "D%d", i);
            ImGui::Checkbox(dlabel, &state.digital[i].enabled);
        }

        ImGui::TableNextColumn();
        ImGui::Text("D8 - D15");
        ImGui::Separator();
        for (int i = 8; i < NUM_DIGITAL_CHANNELS; i++) {
            char dlabel[8];
            snprintf(dlabel, sizeof(dlabel), "D%d", i);
            ImGui::Checkbox(dlabel, &state.digital[i].enabled);
        }

        ImGui::EndTable();
    }
}

void ChannelPanel::draw(ScopeState& state) {
    ImGui::Begin("Channels");

    if (ImGui::BeginTabBar("ChannelTabs")) {
        if (ImGui::BeginTabItem("Analog")) {
            for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
                drawAnalogChannelRow(ch, state.analog[ch]);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Digital")) {
            drawDigitalTab(state);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
