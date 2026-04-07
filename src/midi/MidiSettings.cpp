#include "midi/MidiSettings.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

bool MidiSettings::load(const std::string& path) {
    try {
        std::ifstream f(path);
        if (!f.is_open()) return false;

        json j = json::parse(f);
        lastDeviceName = j.value("last_device", "");

        if (j.contains("device_profiles")) {
            for (auto& [key, val] : j["device_profiles"].items()) {
                deviceProfiles[key] = val.get<std::string>();
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool MidiSettings::save(const std::string& path) const {
    try {
        fs::path savePath(path);
        if (savePath.has_parent_path())
            fs::create_directories(savePath.parent_path());

        json j;
        j["last_device"] = lastDeviceName;

        json dp = json::object();
        for (const auto& [dev, prof] : deviceProfiles) {
            dp[dev] = prof;
        }
        j["device_profiles"] = dp;

        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << j.dump(2);
        return f.good();
    } catch (...) {
        return false;
    }
}

void MidiSettings::setDeviceProfile(const std::string& deviceName, const std::string& profilePath) {
    lastDeviceName = deviceName;
    deviceProfiles[deviceName] = profilePath;
}

std::string MidiSettings::getProfileForDevice(const std::string& deviceName) const {
    auto it = deviceProfiles.find(deviceName);
    return (it != deviceProfiles.end()) ? it->second : "";
}
