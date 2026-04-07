#include "ui/SigGenPanel.h"
#include <imgui.h>
#include <cmath>

static const char* kWaveNames[] = {
    "Sine", "Square", "Triangle", "Ramp Up", "Ramp Down",
    "Sinc", "Gaussian", "Half Sine", "DC"
};
static constexpr int kNumWaves = sizeof(kWaveNames) / sizeof(kWaveNames[0]);

void SigGenPanel::draw(PicoSignalSource& picoSource) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    if (!ImGui::Begin("Signal Generator", nullptr, flags)) {
        ImGui::End();
        return;
    }

    if (!picoSource.isOpen()) {
        ImGui::TextDisabled("Connect a PicoScope to use the signal generator.");
        ImGui::End();
        return;
    }

    // Enable/disable toggle
    if (ImGui::Checkbox("Enable Output", &m_enabled)) {
        if (m_enabled) {
            m_needsApply = true;
        } else {
            picoSource.sigGenDisable();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Waveform type
    ImGui::Text("Waveform");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##wave", &m_waveType, kWaveNames, kNumWaves)) {
        if (m_enabled) m_needsApply = true;
    }

    ImGui::Spacing();

    // Frequency — log slider for wide range
    ImGui::Text("Frequency");
    // Log-scale slider: 0.1 Hz to 20 MHz
    float logFreq = std::log10(m_frequencyHz);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##freq_log", &logFreq, -1.0f, 7.3f, "")) {
        m_frequencyHz = std::pow(10.0f, logFreq);
        if (m_enabled) m_needsApply = true;
    }
    // Show value and allow direct input
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputFloat("Hz##freq_input", &m_frequencyHz, 0, 0, "%.2f")) {
        if (m_frequencyHz < 0.03f) m_frequencyHz = 0.03f;
        if (m_frequencyHz > 20000000.0f) m_frequencyHz = 20000000.0f;
        if (m_enabled) m_needsApply = true;
    }

    ImGui::Spacing();

    // Amplitude (peak-to-peak)
    ImGui::Text("Amplitude (Pk-Pk)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##amp", &m_amplitudeMv, 0.0f, 4000.0f, "%.0f mV")) {
        if (m_enabled) m_needsApply = true;
    }

    ImGui::Spacing();

    // Offset
    ImGui::Text("Offset");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##offset", &m_offsetMv, -2000.0f, 2000.0f, "%.0f mV")) {
        if (m_enabled) m_needsApply = true;
    }

    ImGui::Spacing();

    // Apply button (also auto-applies on change)
    if (m_enabled && m_needsApply) {
        auto wave = static_cast<PicoSignalSource::SigGenWave>(m_waveType);
        picoSource.sigGenEnable(wave, m_frequencyHz, m_amplitudeMv, m_offsetMv);
        m_needsApply = false;
    }

    // Manual apply
    if (m_enabled) {
        if (ImGui::Button("Apply", ImVec2(-1, 0))) {
            auto wave = static_cast<PicoSignalSource::SigGenWave>(m_waveType);
            if (!picoSource.sigGenEnable(wave, m_frequencyHz, m_amplitudeMv, m_offsetMv)) {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Error: %s",
                                   picoSource.lastError().c_str());
            }
        }
    }

    // Show summary
    if (m_enabled) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "Output: %s %.2f Hz %.0f mVpp",
                           kWaveNames[m_waveType], m_frequencyHz, m_amplitudeMv);
    }

    ImGui::End();
}
