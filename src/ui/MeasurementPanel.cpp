#include "ui/MeasurementPanel.h"
#include "ui/Widgets.h"
#include <imgui.h>
#include <algorithm>

// Cursor controls + readouts. Cursors are display-plane objects (divisions);
// voltage readouts resolve through the source channel's V/div and display
// offset — identical math to CLI get-cursors and the waveform overlay.
static void drawCursorSection(ScopeState& state) {
    if (!ImGui::CollapsingHeader("Cursors", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    CursorState& c = state.cursors;
    ImGui::Checkbox("Enable##cursors", &c.enabled);

    ImGui::SameLine();
    const char* srcItems[] = { "CH1", "CH2", "CH3", "CH4" };
    ImGui::SetNextItemWidth(70);
    Widgets::Combo("Source##cursors", &c.source, srcItems, NUM_ANALOG_CHANNELS);

    if (!c.enabled) return;

    float halfX = GRID_DIVISIONS_X * 0.5f; // +/-5 div
    float halfY = GRID_DIVISIONS_Y * 0.5f; // +/-4 div
    ImGui::SetNextItemWidth(-1);
    Widgets::SliderFloat("X1 (div)", &c.x1, -halfX, halfX, "%.2f");
    ImGui::SetNextItemWidth(-1);
    Widgets::SliderFloat("X2 (div)", &c.x2, -halfX, halfX, "%.2f");
    ImGui::SetNextItemWidth(-1);
    Widgets::SliderFloat("Y1 (div)", &c.y1, -halfY, halfY, "%.2f");
    ImGui::SetNextItemWidth(-1);
    Widgets::SliderFloat("Y2 (div)", &c.y2, -halfY, halfY, "%.2f");

    // Readouts
    int src = std::clamp(c.source, 0, NUM_ANALOG_CHANNELS - 1);
    const ChannelState& ch = state.analog[src];
    float offDiv = ch.verticalOffset / ch.voltsPerDiv();
    float v1 = (c.y1 - offDiv) * ch.voltsPerDiv();
    float v2 = (c.y2 - offDiv) * ch.voltsPerDiv();
    float dt = (c.x2 - c.x1) * state.timePerDiv();

    ImGui::Separator();
    ImGui::Text("dT: %s   1/dT: %s",
        formatEngineering(dt, "s").c_str(),
        (dt != 0.0f) ? formatEngineering(1.0f / dt, "Hz").c_str() : "---");
    ImGui::Text("Y1: %s   Y2: %s   dV: %s",
        formatEngineering(v1, "V").c_str(),
        formatEngineering(v2, "V").c_str(),
        formatEngineering(v2 - v1, "V").c_str());
}

void MeasurementPanel::draw(ScopeState& state, const SignalData& data) {
    ImGui::Begin("Measurements");

    // Channel selector
    const char* items[] = { "CH1", "CH2", "CH3", "CH4" };
    ImGui::SetNextItemWidth(80);
    Widgets::Combo("Channel", &m_selectedChannel, items, NUM_ANALOG_CHANNELS);

    // Compute measurements for selected channel (throttled + decimated:
    // full-buffer scans every frame freeze the UI at large record lengths)
    if (m_selectedChannel >= 0 && m_selectedChannel < NUM_ANALOG_CHANNELS &&
        state.analog[m_selectedChannel].enabled)
    {
        const AnalogBuffer& buf = data.analog[m_selectedChannel];
        double now = ImGui::GetTime();
        if (m_selectedChannel != m_cachedChannel || now - m_lastCompute > 0.5) {
            m_cachedChannel = m_selectedChannel;
            m_lastCompute = now;
            constexpr int kMaxPoints = 1000000;
            if (buf.count > kMaxPoints) {
                int stride = buf.count / kMaxPoints + 1;
                int n = buf.count / stride;
                m_scratch.resize(n);
                for (int i = 0; i < n; i++)
                    m_scratch.samples[i] = buf.samples[static_cast<size_t>(i) * stride];
                m_scratch.count = n;
                m_cached = Measurements::compute(m_scratch, data.sampleRate / stride);
            } else {
                m_cached = Measurements::compute(buf, data.sampleRate);
            }
        }
        MeasurementResult r = m_cached;

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

    ImGui::Separator();
    drawCursorSection(state);

    ImGui::End();
}
