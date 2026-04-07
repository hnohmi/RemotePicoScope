#include "ui/MidiConfigPanel.h"
#include "signal/PicoSignalSource.h"
#include <imgui.h>
#include <cstdio>
#include <filesystem>

void MidiConfigPanel::draw(MidiEngine& engine, MidiMapping& mapping,
                           MidiProfileInfo& currentProfile, const std::string& profileDir,
                           MidiSettings& midiSettings, const std::string& settingsPath,
                           ScopeState* state, PicoSignalSource* picoSource)
{
    // Fade status message
    if (m_statusTimer > 0.0f) {
        m_statusTimer -= ImGui::GetIO().DeltaTime;
        if (m_statusTimer <= 0.0f) m_statusMessage.clear();
    }

    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &m_open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("SettingsTabs")) {
        if (state && ImGui::BeginTabItem("Signal Source")) {
            drawSignalSourceSection(*state, picoSource);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("MIDI Device")) {
            drawDeviceSection(engine, mapping, currentProfile, profileDir,
                              midiSettings, settingsPath);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controller Profiles")) {
            drawProfileSection(mapping, currentProfile, profileDir,
                               engine, midiSettings, settingsPath);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("MIDI Learn")) {
            drawLearnSection(mapping);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Bindings")) {
            drawBindingsSection(mapping);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // Status message at bottom
    if (!m_statusMessage.empty()) {
        ImGui::Separator();
        float alpha = (m_statusTimer > 0.5f) ? 1.0f : m_statusTimer * 2.0f;
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, alpha), "%s", m_statusMessage.c_str());
    }

    ImGui::End();
}

void MidiConfigPanel::drawDeviceSection(MidiEngine& engine, MidiMapping& mapping,
                                        MidiProfileInfo& currentProfile,
                                        const std::string& profileDir,
                                        MidiSettings& midiSettings,
                                        const std::string& settingsPath)
{
    ImGui::Spacing();

    // Refresh ports periodically
    m_refreshTimer += ImGui::GetIO().DeltaTime;
    if (m_refreshTimer > 2.0f) {
        engine.refreshPorts();
        m_refreshTimer = 0.0f;
    }

    ImGui::Text("MIDI Input Device");
    ImGui::Spacing();

    auto ports = engine.listInputPorts();

    if (ports.empty()) {
        ImGui::TextDisabled("No MIDI devices detected.");
        ImGui::TextDisabled("Connect a MIDI controller and it will appear here.");
    } else {
        const char* currentName = engine.isOpen() ? engine.currentPortName().c_str() : "-- Select Device --";
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##midiport", currentName)) {
            for (int i = 0; i < static_cast<int>(ports.size()); i++) {
                bool selected = (engine.currentPortIndex() == i);
                if (ImGui::Selectable(ports[i].c_str(), selected)) {
                    if (engine.openPort(i)) {
                        std::string devName = ports[i];
                        m_statusMessage = "Connected: " + devName;
                        m_statusTimer = 3.0f;

                        // Auto-load profile for this device
                        midiSettings.lastDeviceName = devName;
                        std::string savedProfile = midiSettings.getProfileForDevice(devName);
                        bool loaded = false;

                        if (!savedProfile.empty() && std::filesystem::exists(savedProfile)) {
                            loaded = MidiProfile::load(savedProfile, mapping, currentProfile);
                        }

                        // Fall back to device_hint match
                        if (!loaded) {
                            if (m_cachedProfiles.empty())
                                m_cachedProfiles = MidiProfile::listProfiles(profileDir);
                            for (const auto& prof : m_cachedProfiles) {
                                if (!prof.deviceHint.empty() &&
                                    devName.find(prof.deviceHint) != std::string::npos)
                                {
                                    loaded = MidiProfile::load(prof.filePath, mapping, currentProfile);
                                    if (loaded) {
                                        midiSettings.setDeviceProfile(devName, prof.filePath);
                                    }
                                    break;
                                }
                            }
                        }

                        if (loaded) {
                            m_statusMessage += " (profile: " + currentProfile.name + ")";
                        }

                        midiSettings.save(settingsPath);
                    }
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();

    if (engine.isOpen()) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Status: Connected");
        ImGui::Text("Device: %s", engine.currentPortName().c_str());
        if (!currentProfile.name.empty()) {
            ImGui::Text("Profile: %s", currentProfile.name.c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Disconnect")) {
            engine.closePort();
            m_statusMessage = "Disconnected";
            m_statusTimer = 2.0f;
        }
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Status: Not connected");
    }
}

void MidiConfigPanel::drawProfileSection(MidiMapping& mapping, MidiProfileInfo& currentProfile,
                                          const std::string& profileDir, MidiEngine& engine,
                                          MidiSettings& midiSettings, const std::string& settingsPath)
{
    ImGui::Spacing();

    // Refresh profile list periodically
    m_profileRefreshTimer += ImGui::GetIO().DeltaTime;
    if (m_profileRefreshTimer > 5.0f || m_cachedProfiles.empty()) {
        m_cachedProfiles = MidiProfile::listProfiles(profileDir);
        m_profileRefreshTimer = 0.0f;
    }

    // Current profile
    ImGui::Text("Current Profile:");
    ImGui::SameLine();
    if (currentProfile.name.empty()) {
        ImGui::TextDisabled("None");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", currentProfile.name.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Load profile
    ImGui::Text("Load Profile");
    if (m_cachedProfiles.empty()) {
        ImGui::TextDisabled("No profiles found in: %s", profileDir.c_str());
    } else {
        for (const auto& prof : m_cachedProfiles) {
            ImGui::PushID(prof.filePath.c_str());
            bool isCurrent = (prof.filePath == currentProfile.filePath);

            if (isCurrent) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.4f, 1.0f));
            }

            if (ImGui::Button(prof.name.c_str(), ImVec2(-1, 0))) {
                if (MidiProfile::load(prof.filePath, mapping, currentProfile)) {
                    m_statusMessage = "Loaded: " + prof.name;
                    m_statusTimer = 3.0f;

                    // Save device-profile association
                    if (engine.isOpen()) {
                        midiSettings.setDeviceProfile(engine.currentPortName(), prof.filePath);
                        midiSettings.save(settingsPath);
                    }
                } else {
                    m_statusMessage = "Failed to load profile";
                    m_statusTimer = 3.0f;
                }
            }
            if (ImGui::IsItemHovered() && !prof.description.empty()) {
                ImGui::SetTooltip("%s\nDevice hint: %s",
                    prof.description.c_str(),
                    prof.deviceHint.empty() ? "(any)" : prof.deviceHint.c_str());
            }

            if (isCurrent) ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Save current mapping as profile
    ImGui::Text("Save Current Bindings as Profile");

    static char saveNameBuf[128] = "";
    if (saveNameBuf[0] == '\0' && !currentProfile.name.empty()) {
        snprintf(saveNameBuf, sizeof(saveNameBuf), "%s", currentProfile.name.c_str());
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80);
    ImGui::InputText("##savename", saveNameBuf, sizeof(saveNameBuf));
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(-1, 0))) {
        if (saveNameBuf[0] != '\0') {
            MidiProfileInfo info;
            info.name = saveNameBuf;
            info.author = "User";
            info.version = 1;

            // Auto-set device_hint from connected device
            if (engine.isOpen()) {
                info.deviceHint = engine.currentPortName();
            }

            // Generate filename from name
            std::string filename = saveNameBuf;
            for (char& c : filename) {
                if (c == ' ') c = '_';
                else if (!isalnum(c) && c != '_' && c != '-') c = '_';
            }
            std::string savePath = profileDir + "/" + filename + ".json";

            if (MidiProfile::save(savePath, mapping, info)) {
                currentProfile = info;
                currentProfile.filePath = savePath;
                m_statusMessage = "Saved: " + info.name;
                m_statusTimer = 3.0f;
                m_cachedProfiles.clear(); // force refresh

                // Save device-profile association
                if (engine.isOpen()) {
                    midiSettings.setDeviceProfile(engine.currentPortName(), savePath);
                    midiSettings.save(settingsPath);
                }
            } else {
                m_statusMessage = "Failed to save profile";
                m_statusTimer = 3.0f;
            }
        }
    }
}

void MidiConfigPanel::drawLearnSection(MidiMapping& mapping) {
    ImGui::Spacing();

    if (mapping.isLearning()) {
        const auto* desc = MidiMapping::findDescriptor(mapping.learnTarget());
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
            "MIDI Learn Active");
        ImGui::Text("Move a knob/slider on your MIDI controller to assign it to:");
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
            "  %s", desc ? desc->name : "?");
        ImGui::Spacing();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            mapping.cancelLearn();
        }
    } else {
        ImGui::Text("Click a parameter below, then move a MIDI knob/slider to bind it.");
        ImGui::Spacing();

        const auto& descriptors = MidiMapping::parameterDescriptors();
        const char* lastGroup = nullptr;

        for (const auto& desc : descriptors) {
            // Group header
            if (!lastGroup || strcmp(lastGroup, desc.group) != 0) {
                if (lastGroup) ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.8f, 1.0f), "%s", desc.group);
                ImGui::Separator();
                lastGroup = desc.group;
            }

            ImGui::PushID(static_cast<int>(desc.id));
            if (ImGui::Button("Learn", ImVec2(60, 0))) {
                mapping.startLearn(desc.id);
            }

            // Show Unbind button if this parameter has a binding
            MidiKey boundKey{-1, -1};
            bool hasBound = false;
            for (const auto& [key, binding] : mapping.bindings()) {
                if (binding.parameter == desc.id) {
                    boundKey = key;
                    hasBound = true;
                    break;
                }
            }

            if (hasBound) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("Unbind", ImVec2(55, 0))) {
                    mapping.removeBinding(boundKey.channel, boundKey.cc);
                    hasBound = false;
                }
                ImGui::PopStyleColor();
            }

            ImGui::SameLine();
            ImGui::Text("%s", desc.name);

            // Show current binding info and toggle mode checkbox
            if (hasBound) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f),
                    "[Ch%d CC%d]", boundKey.channel + 1, boundKey.cc);

                // Show toggle mode checkbox for boolean-style parameters
                if (desc.curve == MappingCurve::Toggle || desc.curve == MappingCurve::Momentary) {
                    auto it = mapping.bindings().find(boundKey);
                    if (it != mapping.bindings().end()) {
                        ImGui::SameLine();
                        bool toggle = it->second.toggleMode;
                        if (ImGui::Checkbox("Toggle##tgl", &toggle)) {
                            // Need non-const access
                            const_cast<MidiBinding&>(it->second).toggleMode = toggle;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Toggle on each button press\n(instead of hold on/off)");
                    }
                }
            }

            ImGui::PopID();
        }
    }
}

void MidiConfigPanel::drawBindingsSection(MidiMapping& mapping) {
    ImGui::Spacing();

    auto bindings = mapping.getAllBindings();
    if (bindings.empty()) {
        ImGui::TextDisabled("No MIDI bindings configured.");
        ImGui::TextDisabled("Use the MIDI Learn tab or load a controller profile.");
        return;
    }

    ImGui::Text("%d active binding(s)", static_cast<int>(bindings.size()));
    ImGui::Spacing();

    // Table view
    if (ImGui::BeginTable("BindingsTable", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, ImGui::GetContentRegionAvail().y - 40)))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupColumn("MIDI", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Curve", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Toggle", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableHeadersRow();

        int removeIdx = -1;
        for (int i = 0; i < static_cast<int>(bindings.size()); i++) {
            const auto& b = bindings[i];
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (ImGui::SmallButton("X")) removeIdx = i;
            ImGui::PopID();

            ImGui::TableNextColumn();
            ImGui::Text("Ch%d CC%d", b.midiChannel + 1, b.ccNumber);

            ImGui::TableNextColumn();
            const auto* desc = MidiMapping::findDescriptor(b.parameter);
            ImGui::Text("%s", desc ? desc->name : "?");

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", MidiProfile::curveToString(b.curve).c_str());

            ImGui::TableNextColumn();
            if (b.toggleMode)
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Yes");
        }

        if (removeIdx >= 0 && removeIdx < static_cast<int>(bindings.size())) {
            const auto& b = bindings[removeIdx];
            mapping.removeBinding(b.midiChannel, b.ccNumber);
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("Clear All Bindings")) {
        mapping.clearAllBindings();
        m_statusMessage = "All bindings cleared";
        m_statusTimer = 2.0f;
    }
}

void MidiConfigPanel::drawSignalSourceSection(ScopeState& state, PicoSignalSource* picoSource) {
    ImGui::Spacing();

    // Show current connection status at top
    if (picoSource && picoSource->isOpen()) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Connected: %s",
                           picoSource->name().c_str());
        ImGui::Text("Serial: %s", picoSource->serial().c_str());
        ImGui::Spacing();
        if (ImGui::Button("Disconnect")) {
            picoSource->close();
            state.signalSource = SignalSourceType::Dummy;
            m_statusMessage = "PicoScope disconnected";
            m_statusTimer = 2.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Use Demo Mode")) {
            state.signalSource = SignalSourceType::Dummy;
        }
    } else {
        if (state.signalSource == SignalSourceType::PicoScope) {
            // Was PicoScope but device gone
            state.signalSource = SignalSourceType::Dummy;
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.4f, 1.0f), "Demo Mode (no hardware)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Device list
    ImGui::Text("Available PicoScope Devices");
    ImGui::Spacing();

    // Periodic scan for devices (but not if already connected — enumeration
    // opens/closes units which would conflict with an active handle)
    if (picoSource && !picoSource->isOpen()) {
        m_picoRefreshTimer += ImGui::GetIO().DeltaTime;
        if (m_picoRefreshTimer > 5.0f) {
            m_cachedPicoDevices = PicoSignalSource::enumerateDevices();
            m_picoRefreshTimer = 0.0f;
        }
    }

    if (ImGui::Button("Refresh")) {
        if (picoSource && !picoSource->isOpen()) {
            m_cachedPicoDevices = PicoSignalSource::enumerateDevices();
            m_picoRefreshTimer = 0.0f;
        }
    }

    ImGui::Spacing();

    if (m_cachedPicoDevices.empty() && !(picoSource && picoSource->isOpen())) {
        ImGui::TextDisabled("No PicoScope devices detected.");
        ImGui::TextDisabled("Connect a PicoScope via USB and click Refresh.");
    } else if (picoSource && picoSource->isOpen()) {
        // Already connected — show that device
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.15f, 1.0f));
        ImGui::Button(picoSource->name().c_str(), ImVec2(-1, 0));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Currently connected\nSerial: %s", picoSource->serial().c_str());
        }
    } else {
        for (const auto& dev : m_cachedPicoDevices) {
            ImGui::PushID(dev.serial.c_str());
            std::string label = dev.description + "  [" + dev.serial + "]";
            if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) {
                if (picoSource) {
                    if (picoSource->open(dev.serial)) {
                        state.signalSource = SignalSourceType::PicoScope;
                        m_statusMessage = "Connected: " + picoSource->name();
                        m_statusTimer = 3.0f;
                        m_cachedPicoDevices.clear(); // stop scanning
                    } else {
                        m_statusMessage = "Failed: " + picoSource->lastError();
                        m_statusTimer = 4.0f;
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to connect to %s", dev.description.c_str());
            }
            ImGui::PopID();
        }
    }

    // Show error if any
    if (picoSource && !picoSource->lastError().empty() && !picoSource->isOpen()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "Last error: %s",
                           picoSource->lastError().c_str());
    }
}
