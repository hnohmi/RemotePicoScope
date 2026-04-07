#pragma once

#include "midi/MidiEngine.h"
#include "midi/MidiMapping.h"
#include "midi/MidiProfile.h"
#include "midi/MidiSettings.h"
#include "core/ScopeState.h"
#include <string>
#include <vector>
#include <functional>

class PicoSignalSource;
struct PicoDeviceInfo;

class MidiConfigPanel {
public:
    // Call from main loop — draws settings button and popup window
    // picoSource can be null if PicoScope is not available
    void draw(MidiEngine& engine, MidiMapping& mapping,
              MidiProfileInfo& currentProfile, const std::string& profileDir,
              MidiSettings& midiSettings, const std::string& settingsPath,
              ScopeState* state = nullptr, PicoSignalSource* picoSource = nullptr);

    bool isOpen() const { return m_open; }
    void open() { m_open = true; }
    void close() { m_open = false; }
    void toggle() { m_open = !m_open; }

private:
    void drawDeviceSection(MidiEngine& engine, MidiMapping& mapping,
                           MidiProfileInfo& currentProfile, const std::string& profileDir,
                           MidiSettings& midiSettings, const std::string& settingsPath);
    void drawProfileSection(MidiMapping& mapping, MidiProfileInfo& currentProfile,
                            const std::string& profileDir, MidiEngine& engine,
                            MidiSettings& midiSettings, const std::string& settingsPath);
    void drawLearnSection(MidiMapping& mapping);
    void drawBindingsSection(MidiMapping& mapping);
    void drawSignalSourceSection(ScopeState& state, PicoSignalSource* picoSource);

    bool m_open = false;
    float m_refreshTimer = 0.0f;
    std::vector<MidiProfileInfo> m_cachedProfiles;
    float m_profileRefreshTimer = 0.0f;
    std::string m_saveAsName;
    std::string m_statusMessage;
    float m_statusTimer = 0.0f;

    // PicoScope device enumeration cache
    std::vector<PicoDeviceInfo> m_cachedPicoDevices;
    float m_picoRefreshTimer = 10.0f; // start at threshold to trigger first scan
    bool m_picoScanning = false;
};
