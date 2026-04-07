#include "ui/MeasurementPanel.h"
#include "ui/Widgets.h"
#include <imgui.h>

void MeasurementPanel::draw(const ScopeState& state, const SignalData& data) {
    ImGui::Begin("Measurements");

    // Channel selector
    const char* items[] = { "CH1", "CH2", "CH3", "CH4" };
    ImGui::SetNextItemWidth(80);
    Widgets::Combo("Channel", &m_selectedChannel, items, NUM_ANALOG_CHANNELS);

    // Compute measurements for selected channel
    if (m_selectedChannel >= 0 && m_selectedChannel < NUM_ANALOG_CHANNELS &&
        state.analog[m_selectedChannel].enabled)
    {
        MeasurementResult r = Measurements::compute(
            data.analog[m_selectedChannel], data.sampleRate);

        ImU32 color = ChannelColors::analog(m_selectedChannel);
        ImVec4 colorVec = ImGui::ColorConvertU32ToFloat4(color);

        if (r.valid) {
            ImGui::TextColored(colorVec, "Frequency: %s", formatEngineering(r.frequency, "Hz").c_str());
            ImGui::TextColored(colorVec, "Period:    %s", formatEngineering(r.period, "s").c_str());
            ImGui::Separator();
            ImGui::TextColored(colorVec, "Vpp:       %s", formatEngineering(r.vpp, "V").c_str());
            ImGui::TextColored(colorVec, "Vmax:      %s", formatEngineering(r.vmax, "V").c_str());
            ImGui::TextColored(colorVec, "Vmin:      %s", formatEngineering(r.vmin, "V").c_str());
            ImGui::TextColored(colorVec, "Vavg:      %s", formatEngineering(r.vavg, "V").c_str());
            ImGui::TextColored(colorVec, "Vrms:      %s", formatEngineering(r.vrms, "V").c_str());
            ImGui::Separator();
            ImGui::TextColored(colorVec, "Rise Time: %s", formatEngineering(r.riseTime, "s").c_str());
            ImGui::TextColored(colorVec, "Fall Time: %s", formatEngineering(r.fallTime, "s").c_str());
            ImGui::TextColored(colorVec, "Duty:      %.1f%%", r.dutyCycle);
        } else {
            ImGui::TextDisabled("No signal data");
        }
    } else {
        ImGui::TextDisabled("Channel disabled");
    }

    ImGui::End();
}
