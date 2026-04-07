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
