#include "ui/DeviceSelectPopup.h"
#include <imgui.h>

static const char* kPopupId = "Select Signal Source";

void DeviceSelectPopup::show() {
    m_visible = true;
    m_needsOpen = true;
    m_scanned = false;
    m_error.clear();
}

bool DeviceSelectPopup::draw(ScopeState& state, PicoSignalSource& picoSource) {
    if (!m_visible) return false;

    // Scan on first draw after show()
    if (!m_scanned) {
        m_devices = PicoSignalSource::enumerateDevices();
        m_scanned = true;
    }

    // Deferred open (must happen outside BeginPopupModal)
    if (m_needsOpen) {
        ImGui::OpenPopup(kPopupId);
        m_needsOpen = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 0), ImVec2(400, FLT_MAX));

    bool open = true;
    if (!ImGui::BeginPopupModal(kPopupId, &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (!open) m_visible = false;
        return m_visible;
    }

    ImGui::Text("Choose a signal source to get started.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // PicoScope devices
    if (m_devices.empty()) {
        ImGui::TextDisabled("No PicoScope devices detected.");
    } else {
        ImGui::Text("PicoScope Hardware:");
        ImGui::Spacing();
        for (const auto& dev : m_devices) {
            ImGui::PushID(dev.serial.c_str());
            std::string label = dev.description + "  [" + dev.serial + "]";
            if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) {
                if (picoSource.open(dev.serial)) {
                    state.signalSource = SignalSourceType::PicoScope;
                    m_visible = false;
                    ImGui::CloseCurrentPopup();
                    ImGui::PopID();
                    ImGui::EndPopup();
                    return false;
                } else {
                    m_error = picoSource.lastError();
                }
            }
            ImGui::PopID();
        }
    }

    ImGui::Spacing();

    // Rescan button
    if (ImGui::SmallButton("Rescan")) {
        m_devices = PicoSignalSource::enumerateDevices();
    }

    // Error display
    if (!m_error.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "Error: %s", m_error.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Demo mode
    ImGui::Text("Or run without hardware:");
    ImGui::Spacing();
    if (ImGui::Button("Demo Mode (Simulated Signals)", ImVec2(-1, 0))) {
        state.signalSource = SignalSourceType::Dummy;
        m_visible = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return false;
    }

    ImGui::Spacing();
    ImGui::EndPopup();

    if (!open) m_visible = false;
    return m_visible;
}
