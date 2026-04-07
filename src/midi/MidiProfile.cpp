#include "midi/MidiProfile.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string MidiProfile::parameterIdToString(ParameterID id) {
    switch (id) {
        case ParameterID::CH1_VoltsPerDiv: return "CH1_VoltsPerDiv";
        case ParameterID::CH1_Offset:      return "CH1_Offset";
        case ParameterID::CH1_Enable:      return "CH1_Enable";
        case ParameterID::CH2_VoltsPerDiv: return "CH2_VoltsPerDiv";
        case ParameterID::CH2_Offset:      return "CH2_Offset";
        case ParameterID::CH2_Enable:      return "CH2_Enable";
        case ParameterID::CH3_VoltsPerDiv: return "CH3_VoltsPerDiv";
        case ParameterID::CH3_Offset:      return "CH3_Offset";
        case ParameterID::CH3_Enable:      return "CH3_Enable";
        case ParameterID::CH4_VoltsPerDiv: return "CH4_VoltsPerDiv";
        case ParameterID::CH4_Offset:      return "CH4_Offset";
        case ParameterID::CH4_Enable:      return "CH4_Enable";
        case ParameterID::TimePerDiv:      return "TimePerDiv";
        case ParameterID::HorizontalOffset:return "HorizontalOffset";
        case ParameterID::TriggerLevel:    return "TriggerLevel";
        case ParameterID::TriggerSource:   return "TriggerSource";
        case ParameterID::TriggerEdge:     return "TriggerEdge";
        case ParameterID::RunStop:         return "RunStop";
        case ParameterID::SingleShot:      return "SingleShot";
        case ParameterID::CursorX1:        return "CursorX1";
        case ParameterID::CursorX2:        return "CursorX2";
        case ParameterID::CursorY1:        return "CursorY1";
        case ParameterID::CursorY2:        return "CursorY2";
        default:                           return "Unknown";
    }
}

ParameterID MidiProfile::stringToParameterId(const std::string& str) {
    static const std::map<std::string, ParameterID> lookup = {
        {"CH1_VoltsPerDiv", ParameterID::CH1_VoltsPerDiv},
        {"CH1_Offset",      ParameterID::CH1_Offset},
        {"CH1_Enable",      ParameterID::CH1_Enable},
        {"CH2_VoltsPerDiv", ParameterID::CH2_VoltsPerDiv},
        {"CH2_Offset",      ParameterID::CH2_Offset},
        {"CH2_Enable",      ParameterID::CH2_Enable},
        {"CH3_VoltsPerDiv", ParameterID::CH3_VoltsPerDiv},
        {"CH3_Offset",      ParameterID::CH3_Offset},
        {"CH3_Enable",      ParameterID::CH3_Enable},
        {"CH4_VoltsPerDiv", ParameterID::CH4_VoltsPerDiv},
        {"CH4_Offset",      ParameterID::CH4_Offset},
        {"CH4_Enable",      ParameterID::CH4_Enable},
        {"TimePerDiv",      ParameterID::TimePerDiv},
        {"HorizontalOffset",ParameterID::HorizontalOffset},
        {"TriggerLevel",    ParameterID::TriggerLevel},
        {"TriggerSource",   ParameterID::TriggerSource},
        {"TriggerEdge",     ParameterID::TriggerEdge},
        {"RunStop",         ParameterID::RunStop},
        {"SingleShot",      ParameterID::SingleShot},
        {"CursorX1",        ParameterID::CursorX1},
        {"CursorX2",        ParameterID::CursorX2},
        {"CursorY1",        ParameterID::CursorY1},
        {"CursorY2",        ParameterID::CursorY2},
    };
    auto it = lookup.find(str);
    return (it != lookup.end()) ? it->second : ParameterID::COUNT;
}

std::string MidiProfile::curveToString(MappingCurve curve) {
    switch (curve) {
        case MappingCurve::Linear:      return "linear";
        case MappingCurve::Logarithmic: return "logarithmic";
        case MappingCurve::Stepped:     return "stepped";
        case MappingCurve::Toggle:      return "toggle";
        case MappingCurve::Momentary:   return "momentary";
    }
    return "linear";
}

MappingCurve MidiProfile::stringToCurve(const std::string& str) {
    if (str == "linear")      return MappingCurve::Linear;
    if (str == "logarithmic") return MappingCurve::Logarithmic;
    if (str == "stepped")     return MappingCurve::Stepped;
    if (str == "toggle")      return MappingCurve::Toggle;
    if (str == "momentary")   return MappingCurve::Momentary;
    return MappingCurve::Linear;
}

bool MidiProfile::load(const std::string& path, MidiMapping& mapping, MidiProfileInfo& info) {
    try {
        std::ifstream f(path);
        if (!f.is_open()) return false;

        json j = json::parse(f);

        // Read profile info
        if (j.contains("profile")) {
            auto& p = j["profile"];
            info.name = p.value("name", "Unknown");
            info.author = p.value("author", "");
            info.version = p.value("version", 1);
            info.deviceHint = p.value("device_hint", "");
            info.description = p.value("description", "");
        }
        info.filePath = path;

        // Read mappings
        int defaultChannel = j.value("midi_channel", 0);
        std::vector<MidiBinding> bindings;

        if (j.contains("mappings")) {
            for (const auto& m : j["mappings"]) {
                MidiBinding b;
                b.midiChannel = m.value("midi_channel", defaultChannel);
                b.ccNumber = m.value("cc", 0);
                b.parameter = stringToParameterId(m.value("parameter", ""));
                b.label = m.value("label", "");
                b.curve = stringToCurve(m.value("curve", "linear"));
                b.invert = m.value("invert", false);
                b.toggleMode = m.value("toggle_mode", false);

                if (b.parameter != ParameterID::COUNT)
                    bindings.push_back(b);
            }
        }

        mapping.setAllBindings(bindings);
        return true;
    } catch (...) {
        return false;
    }
}

bool MidiProfile::save(const std::string& path, const MidiMapping& mapping, const MidiProfileInfo& info) {
    try {
        json j;

        j["profile"] = {
            {"name", info.name},
            {"author", info.author},
            {"version", info.version},
            {"device_hint", info.deviceHint},
            {"description", info.description}
        };

        j["midi_channel"] = 0;

        json mappingsArr = json::array();
        for (const auto& b : mapping.getAllBindings()) {
            json m;
            m["cc"] = b.ccNumber;
            m["midi_channel"] = b.midiChannel;
            m["parameter"] = parameterIdToString(b.parameter);
            m["label"] = b.label;
            m["curve"] = curveToString(b.curve);
            m["invert"] = b.invert;
            m["toggle_mode"] = b.toggleMode;
            mappingsArr.push_back(m);
        }
        j["mappings"] = mappingsArr;

        // Ensure parent directory exists
        fs::path savePath(path);
        if (savePath.has_parent_path())
            fs::create_directories(savePath.parent_path());

        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << j.dump(2);
        return f.good();
    } catch (...) {
        return false;
    }
}

std::vector<MidiProfileInfo> MidiProfile::listProfiles(const std::string& directory) {
    std::vector<MidiProfileInfo> profiles;

    try {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.path().extension() == ".json") {
                try {
                    std::ifstream f(entry.path());
                    json j = json::parse(f);

                    MidiProfileInfo info;
                    if (j.contains("profile")) {
                        auto& p = j["profile"];
                        info.name = p.value("name", entry.path().stem().string());
                        info.deviceHint = p.value("device_hint", "");
                        info.description = p.value("description", "");
                    } else {
                        info.name = entry.path().stem().string();
                    }
                    info.filePath = entry.path().string();
                    profiles.push_back(info);
                } catch (...) {
                    // Skip invalid JSON files
                }
            }
        }
    } catch (...) {
        // Directory doesn't exist
    }

    return profiles;
}
