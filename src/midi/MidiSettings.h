#pragma once

#include <string>
#include <map>

// Persists MIDI device/profile associations to a JSON file.
// Tracks which device was last used, and which profile was last used per device.
struct MidiSettings {
    std::string lastDeviceName;   // most recently used device
    // Map device name -> last used profile file path
    std::map<std::string, std::string> deviceProfiles;

    bool load(const std::string& path);
    bool save(const std::string& path) const;

    void setDeviceProfile(const std::string& deviceName, const std::string& profilePath);
    std::string getProfileForDevice(const std::string& deviceName) const;
};
