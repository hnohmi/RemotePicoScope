#include "ui/MathPanel.h"
#include "ui/Widgets.h"
#include <imgui.h>

void MathPanel::draw(ScopeState& state) {
    ImGui::Begin("Math");

    auto& mc = state.mathChannel;

    ImGui::Checkbox("Enable##math", &mc.enabled);

    if (!mc.enabled) {
        ImGui::TextDisabled("Math channel disabled");
        ImGui::End();
        return;
    }

    // Operator
    const char* opLabels[] = { "+", "-", "*", "/", "FFT", "d/dt", "Integral", "Sqrt" };
    int opIdx = static_cast<int>(mc.op);
    ImGui::SetNextItemWidth(-1);
    if (Widgets::Combo("Operator", &opIdx, opLabels, 8))
        mc.op = static_cast<MathOp>(opIdx);

    // Source 1
    const char* chLabels[] = { "CH1", "CH2", "CH3", "CH4" };
    ImGui::SetNextItemWidth(-1);
    Widgets::Combo("Source 1", &mc.source1, chLabels, NUM_ANALOG_CHANNELS);

    // Source 2 (only for binary ops)
    if (!isMathOpUnary(mc.op)) {
        ImGui::SetNextItemWidth(-1);
        Widgets::Combo("Source 2", &mc.source2, chLabels, NUM_ANALOG_CHANNELS);
    }

    ImGui::Separator();

    // Scale (V/div equivalent)
    ImGui::Text("Scale: %.3f", mc.scale);
    ImGui::SetNextItemWidth(-1);
    Widgets::SliderFloat("##mathscale", &mc.scale, 0.001f, 100.0f, "%.3f", 0.0f);

    // Offset
    ImGui::SetNextItemWidth(-1);
    Widgets::SliderFloat("Offset##math", &mc.verticalOffset, -10.0f, 10.0f, "%.2f");

    // FFT window (only when FFT selected)
    if (mc.op == MathOp::FFT) {
        ImGui::Separator();
        const char* windowLabels[] = {
            "Rectangular", "Hanning", "Hamming", "Blackman-Harris", "Flat Top"
        };
        int winIdx = static_cast<int>(mc.fftWindow);
        ImGui::SetNextItemWidth(-1);
        if (Widgets::Combo("Window", &winIdx, windowLabels, 5))
            mc.fftWindow = static_cast<FFTWindowType>(winIdx);
    }

    // Color indicator
    ImVec4 mathColor = ImGui::ColorConvertU32ToFloat4(ChannelColors::Math);
    ImGui::ColorButton("##mathcolor", mathColor, ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
    ImGui::SameLine();
    ImGui::Text("Math");

    ImGui::End();
}
