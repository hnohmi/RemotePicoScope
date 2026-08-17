#pragma once

#include "core/ScopeState.h"
#include <imgui.h>

// ============================================================================
// Data / display layering
// ============================================================================
//
//   [hardware / demo source]
//        |  raw ADC counts
//        v
//   Signal layer   — volts at the probe tip (probe attenuation & invert are
//                    folded in at ADC->volts conversion, exactly once).
//                    Trigger level, measurements, math, and every CLI/remote
//                    output live HERE and are never touched by display
//                    settings. Internal hardware control converts back to
//                    API units (BNC volts / ADC counts) at the driver call.
//        |
//        v  ChannelView (this file) — the ONLY volts -> screen mapping
//   Display layer  — screen XY. Per-channel display offset is added at this
//                    stage, so it applies uniformly to everything drawn for
//                    that channel: the waveform, the trigger level line (via
//                    the trigger source's view), the 0 V ground marker, and
//                    cursor readouts. Offset of channel 1 can never leak into
//                    channel 2, and never into signal-layer data.
//
// Conversion is deliberately two-stage:
//   Stage 1 (data -> plane):    divY = volts / voltsPerDiv      (offset-free)
//   Stage 2 (plane -> screen):  y = centerY - (divY + offsetDiv) * pxPerDivY
// ============================================================================

struct ChannelView {
    float centerY = 0.0f;     // screen y of the grid center line
    float pxPerDivY = 1.0f;   // pixels per vertical division
    float voltsPerDiv = 1.0f; // channel vertical scale
    float offsetDiv = 0.0f;   // display offset, in divisions (stage 2 only)

    static ChannelView make(float voltsPerDiv, float offsetVolts,
                            ImVec2 areaPos, ImVec2 areaSize) {
        ChannelView v;
        v.centerY = areaPos.y + areaSize.y * 0.5f;
        v.pxPerDivY = areaSize.y / GRID_DIVISIONS_Y;
        v.voltsPerDiv = (voltsPerDiv != 0.0f) ? voltsPerDiv : 1.0f;
        v.offsetDiv = offsetVolts / v.voltsPerDiv;
        return v;
    }

    static ChannelView forChannel(const ChannelState& ch,
                                  ImVec2 areaPos, ImVec2 areaSize) {
        return make(ch.voltsPerDiv(), ch.verticalOffset, areaPos, areaSize);
    }

    // Stage 1: data domain (volts) -> plane (divisions), no offset.
    float divFromVolts(float volts) const { return volts / voltsPerDiv; }

    // Stage 2: plane (divisions) -> screen pixels, display offset added here.
    float yFromDiv(float divY) const {
        return centerY - (divY + offsetDiv) * pxPerDivY;
    }

    float yFromVolts(float volts) const { return yFromDiv(divFromVolts(volts)); }

    // Inverse: screen y -> volts (for mouse interaction / cursor readouts).
    float voltsFromY(float y) const {
        return ((centerY - y) / pxPerDivY - offsetDiv) * voltsPerDiv;
    }

    // Inverse from a display-plane position given in divisions from the grid
    // center (how cursors are stored): the voltage this channel shows there.
    float voltsFromScreenDiv(float d) const {
        return (d - offsetDiv) * voltsPerDiv;
    }
};
