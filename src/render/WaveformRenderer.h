#pragma once

#include "core/Types.h"
#include "core/ScopeState.h"
#include "core/SignalBuffer.h"

#include <imgui.h>

class WaveformRenderer {
public:
    // Draw the oscilloscope grid
    void drawGrid(ImDrawList* dl, ImVec2 pos, ImVec2 size) const;

    // Draw analog waveform for a single channel. sampleRate is the actual
    // acquisition rate — screen density derives from it, never from
    // stretching the record to fit the window.
    void drawAnalogChannel(ImDrawList* dl, ImVec2 pos, ImVec2 size,
                           const AnalogBuffer& buffer, float sampleRate,
                           const ChannelState& ch, const ScopeState& state,
                           uint32_t color) const;

    // Draw all 16 digital channels
    void drawDigitalChannels(ImDrawList* dl, ImVec2 pos, ImVec2 size,
                             const DigitalBuffer& buffer, float sampleRate,
                             const ScopeState& state) const;

    // Draw trigger level indicator
    void drawTriggerIndicator(ImDrawList* dl, ImVec2 pos, ImVec2 size,
                              const ScopeState& state) const;

private:
    static constexpr int MAX_POLYLINE_POINTS = 4096;
};
