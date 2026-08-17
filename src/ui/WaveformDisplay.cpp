#include "ui/WaveformDisplay.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

void WaveformDisplay::draw(ScopeState& state, const SignalData& data,
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
    for (const auto& b : state.buses)
        if (b.enabled && !b.lanes.empty()) { hasDigital = true; break; }

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
                data.analog[ch], data.sampleRate, state.analog[ch], state,
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
            *mathBuffer, data.sampleRate, mathCh, state, ChannelColors::Math);
    }

    // Trigger indicator
    m_renderer.drawTriggerIndicator(dl, analogPos, analogSize, state);

    // Measurement cursors (display-plane objects; readouts go through the
    // cursor source channel's view transform)
    m_cursorOverlay.draw(state, analogPos, analogSize);

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
            const ChannelState& cs = state.analog[ch];
            std::string name = cs.label.empty() ? ("CH" + std::to_string(ch + 1)) : cs.label;
            std::string label = name + ": " + formatEngineering(cs.voltsPerDiv(), "V/div");
            if (cs.probeAttenuation != 1.0f)
                label += " " + std::to_string(static_cast<int>(cs.probeAttenuation)) + "x";
            if (cs.invert) label += " INV";
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

    // --- XY control over the analog area (display layer) ---
    // Horizontal drag pans the timebase window; vertical drag moves the
    // vertical offset of the trace grabbed at drag start. Offsets are pure
    // draw-time Y translations — the sample buffer is never modified.
    if (analogSize.x > 1.0f && analogSize.y > 1.0f) {
        ImGui::InvisibleButton("##waveform_xy", analogSize);

        float pixelsPerDivX = analogSize.x / GRID_DIVISIONS_X;
        float pixelsPerDivY = analogSize.y / GRID_DIVISIONS_Y;
        float centerY = analogPos.y + analogSize.y * 0.5f;

        if (ImGui::IsItemActivated()) {
            // Grab the enabled trace nearest the cursor (within 40 px).
            m_dragChannel = -1;
            ImVec2 mouse = ImGui::GetIO().MousePos;
            float best = 40.0f;
            // Same time-aware mapping as the renderer (record center anchored
            // at view center, density from the actual sample rate).
            double viewSpan = static_cast<double>(state.timePerDiv()) * GRID_DIVISIONS_X;
            for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
                const ChannelState& cs = state.analog[ch];
                if (!cs.enabled || data.analog[ch].count < 1) continue;
                int count = data.analog[ch].count;
                double dt = (data.sampleRate > 0) ? 1.0 / data.sampleRate : viewSpan / count;
                double spw = viewSpan / dt;
                double first = (count - 1) * 0.5 - spw * 0.5
                               + static_cast<double>(state.horizontalOffset) / dt;
                double s = first + (mouse.x - analogPos.x) / analogSize.x * spw;
                if (s < 0.0 || s > count - 1) continue; // outside the record
                int idx = static_cast<int>(s);
                float v = data.analog[ch].samples[idx];
                float y = centerY - ((v + cs.verticalOffset) / cs.voltsPerDiv()) * pixelsPerDivY;
                float d = fabsf(mouse.y - y);
                if (d < best) { best = d; m_dragChannel = ch; }
            }
        }

        if (ImGui::IsItemActive()) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;

            // X: pan the time window (drag right = waveform moves right)
            if (delta.x != 0.0f) {
                float maxHOff = state.maxHorizontalOffset();
                state.horizontalOffset = std::clamp(
                    state.horizontalOffset - delta.x * state.timePerDiv() / pixelsPerDivX,
                    -maxHOff, maxHOff);
            }

            // Y: move the grabbed channel's offset (drag = trace follows mouse)
            if (delta.y != 0.0f && m_dragChannel >= 0) {
                ChannelState& cs = state.analog[m_dragChannel];
                float maxVOff = cs.voltsPerDiv() * GRID_DIVISIONS_Y * 0.5f;
                cs.verticalOffset = std::clamp(
                    cs.verticalOffset - delta.y * cs.voltsPerDiv() / pixelsPerDivY,
                    -maxVOff, maxVOff);
            }
        } else if (ImGui::IsItemDeactivated()) {
            m_dragChannel = -1;
        }

        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else {
        ImGui::Dummy(analogSize);
    }

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
            data.digital, data.sampleRate, state);

        ImGui::Dummy(digitalSize);
    }

    ImGui::End();
}
