#pragma once

#include "midi/MidiMapping.h"
#include <string>
#include <vector>

struct MidiProfileInfo {
    std::string name;
    std::string author;
    int version = 1;
    std::string deviceHint;
    std::string description;
    std::string filePath;
};

class MidiProfile {
public:
    // Load a profile from JSON file and apply bindings to mapping
    static bool load(const std::string& path, MidiMapping& mapping, MidiProfileInfo& info);

    // Save current mapping to a JSON file
    static bool save(const std::string& path, const MidiMapping& mapping, const MidiProfileInfo& info);

    // List available profiles in a directory
    static std::vector<MidiProfileInfo> listProfiles(const std::string& directory);

    // Convert ParameterID to/from string
    static std::string parameterIdToString(ParameterID id);
    static ParameterID stringToParameterId(const std::string& str);

    // Convert MappingCurve to/from string
    static std::string curveToString(MappingCurve curve);
    static MappingCurve stringToCurve(const std::string& str);
};
