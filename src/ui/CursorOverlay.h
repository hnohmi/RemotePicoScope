#pragma once

#include "core/ScopeState.h"
#include <imgui.h>

class CursorOverlay {
public:
    // Draw cursors on the waveform area. Call after waveform rendering.
    // pos/size should match the analog waveform area.
    void draw(ScopeState& state, ImVec2 waveformPos, ImVec2 waveformSize);
};
