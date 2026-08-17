#pragma once

#include "core/ScopeState.h"
#include "core/SignalBuffer.h"
#include "midi/MidiEngine.h"
#include <functional>

class StatusBar {
public:
    // onSettingsClicked is called when the settings button is pressed
    // signalSourceName: display name of the active signal source
    void draw(const ScopeState& state, const SignalData& data,
              const MidiEngine& midiEngine, std::function<void()> onSettingsClicked,
              const std::string& signalSourceName = "",
              std::function<void()> onAutoscale = nullptr);
};
