#pragma once

#include "signal/PicoSignalSource.h"
#include "core/ScopeState.h"
#include <vector>
#include <string>

class DeviceSelectPopup {
public:
    // Call once to trigger the popup (e.g. on first frame or from menu)
    void show();

    // Call every frame from the main loop. Returns true while the popup is open.
    // When the user selects a device, it opens picoSource and sets state.signalSource.
    bool draw(ScopeState& state, PicoSignalSource& picoSource);

    bool isVisible() const { return m_visible; }

private:
    bool m_visible = false;
    bool m_needsOpen = false;        // deferred ImGui::OpenPopup
    bool m_scanned = false;
    std::vector<PicoDeviceInfo> m_devices;
    std::string m_error;
    bool m_scanning = false;
};
