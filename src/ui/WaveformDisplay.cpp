#include "ui/WaveformDisplay.h"
#include <imgui.h>

void WaveformDisplay::draw(const ScopeState& state, const SignalData& data,
                           const AnalogBuffer* mathBuffer,
                           const FFTResult* fftResult)
{
    ImGui::Begin("Waveform", nullptr,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 avail = ImGui::GetContentRegionAvail();

    // Determine layout splits
    bool hasDigital = false;
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++) {
        if (state.digital[i].enabled) { hasDigital = true; break; }
    }

    bool showFFT = (state.mathChannel.enabled &&
                    state.mathChannel.op == MathOp::FFT &&
                    fftResult && fftResult->valid);

    // Layout ratios
    float analogRatio = 1.0f;
    float fftRatio = 0.0f;
    float digitalRatio = 0.0f;

    if (showFFT && hasDigital) {
        analogRatio = 0.40f;
        fftRatio = 0.35f;
        digitalRatio = 0.25f;
    } else if (showFFT) {
        analogRatio = 0.50f;
        fftRatio = 0.50f;
    } else if (hasDigital) {
        analogRatio = 0.70f;
        digitalRatio = 0.30f;
    }

    float analogH = avail.y * analogRatio;
    float fftH = avail.y * fftRatio;
    float digitalH = avail.y * digitalRatio;

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // --- Analog section ---
    ImVec2 analogPos = cursorPos;
    ImVec2 analogSize(avail.x, analogH);

    // Background
    dl->AddRectFilled(analogPos,
        ImVec2(analogPos.x + analogSize.x, analogPos.y + analogSize.y),
        IM_COL32(10, 10, 14, 255));

    // Grid
    m_renderer.drawGrid(dl, analogPos, analogSize);

    // Analog channels
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        if (state.analog[ch].enabled) {
            m_renderer.drawAnalogChannel(dl, analogPos, analogSize,
                data.analog[ch], state.analog[ch], state,
                ChannelColors::analog(ch));
        }
    }

    // Math channel (non-FFT ops)
    if (state.mathChannel.enabled && state.mathChannel.op != MathOp::FFT
        && mathBuffer && mathBuffer->count > 0)
    {
        // Create a temporary ChannelState for the math channel
        ChannelState mathCh;
        mathCh.enabled = true;
        mathCh.verticalOffset = state.mathChannel.verticalOffset;
        // Find the closest V/div index for the math scale
        mathCh.voltsPerDivIndex = Sequence125::findClosestIndex(
            Sequence125::VOLTS_PER_DIV, Sequence125::VOLTS_PER_DIV_COUNT,
            state.mathChannel.scale);

        m_renderer.drawAnalogChannel(dl, analogPos, analogSize,
            *mathBuffer, mathCh, state, ChannelColors::Math);
    }

    // Trigger indicator
    m_renderer.drawTriggerIndicator(dl, analogPos, analogSize, state);

    // Time/div label
    {
        std::string timeLabel = formatEngineering(state.timePerDiv(), "s/div");
        ImVec2 textSize = ImGui::CalcTextSize(timeLabel.c_str());
        dl->AddText(
            ImVec2(analogPos.x + analogSize.x - textSize.x - 8,
                   analogPos.y + analogSize.y - textSize.y - 4),
            IM_COL32(200, 200, 210, 200), timeLabel.c_str());
    }

    // Channel V/div labels
    {
        float labelY = analogPos.y + 4;
        for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
            if (!state.analog[ch].enabled) continue;
            std::string label = "CH" + std::to_string(ch + 1) + ": " +
                formatEngineering(state.analog[ch].voltsPerDiv(), "V/div");
            dl->AddText(ImVec2(analogPos.x + 8, labelY),
                ChannelColors::analog(ch), label.c_str());
            labelY += ImGui::GetTextLineHeight() + 2;
        }
        // Math channel label
        if (state.mathChannel.enabled && state.mathChannel.op != MathOp::FFT) {
            std::string label = std::string("Math: ") + mathOpName(state.mathChannel.op);
            dl->AddText(ImVec2(analogPos.x + 8, labelY),
                ChannelColors::Math, label.c_str());
        }
    }

    // Advance cursor past analog area
    ImGui::Dummy(analogSize);

    // --- FFT section ---
    if (showFFT && fftH > 20.0f) {
        ImVec2 fftPos(cursorPos.x, cursorPos.y + analogH + 2);
        ImVec2 fftSize(avail.x, fftH - 2);

        dl->AddRectFilled(fftPos,
            ImVec2(fftPos.x + fftSize.x, fftPos.y + fftSize.y),
            IM_COL32(8, 8, 14, 255));

        m_fftDisplay.draw(dl, fftPos, fftSize, *fftResult, ChannelColors::Math);

        ImGui::Dummy(fftSize);
    }

    // --- Digital section ---
    if (hasDigital && digitalH > 20.0f) {
        float digitalY = cursorPos.y + analogH + (showFFT ? fftH + 2 : 0) + 2;
        ImVec2 digitalPos(cursorPos.x, digitalY);
        ImVec2 digitalSize(avail.x, digitalH - 2);

        dl->AddRectFilled(digitalPos,
            ImVec2(digitalPos.x + digitalSize.x, digitalPos.y + digitalSize.y),
            IM_COL32(8, 12, 8, 255));

        m_renderer.drawDigitalChannels(dl, digitalPos, digitalSize,
            data.digital, state);

        ImGui::Dummy(digitalSize);
    }

    ImGui::End();
}
