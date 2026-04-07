#pragma once

#include "core/Types.h"
#include "core/ScopeState.h"
#include "midi/MidiEngine.h"
#include <map>
#include <string>
#include <vector>

struct ParameterDescriptor {
    ParameterID id;
    const char* name;
    const char* group;
    float minValue;
    float maxValue;
    MappingCurve curve;
    // For stepped curves: index into a value table
    const float* stepValues = nullptr;
    int stepCount = 0;
};

struct MidiBinding {
    int midiChannel = 0;
    int ccNumber = 0;
    ParameterID parameter = ParameterID::COUNT;
    std::string label;
    MappingCurve curve = MappingCurve::Linear;
    bool invert = false;
    bool toggleMode = false;  // For momentary buttons: toggle on each press instead of hold
    int lastCCValue = 0;      // Track previous CC value for edge detection (not serialized)
};

// Key for looking up bindings: (channel, cc#)
struct MidiKey {
    int channel;
    int cc;
    bool operator<(const MidiKey& o) const {
        if (channel != o.channel) return channel < o.channel;
        return cc < o.cc;
    }
};

class MidiMapping {
public:
    MidiMapping();

    // Apply a MIDI CC message to the scope state
    void applyCC(const MidiMessage& msg, ScopeState& state);

    // Learn mode
    void startLearn(ParameterID target);
    void cancelLearn();
    bool isLearning() const { return m_learning; }
    ParameterID learnTarget() const { return m_learnTarget; }

    // Binding management
    void addBinding(const MidiBinding& binding);
    void removeBinding(int midiChannel, int ccNumber);
    void clearAllBindings();
    const std::map<MidiKey, MidiBinding>& bindings() const { return m_bindings; }

    // Parameter descriptors
    static const std::vector<ParameterDescriptor>& parameterDescriptors();
    static const ParameterDescriptor* findDescriptor(ParameterID id);

    // Get/set bindings for serialization
    std::vector<MidiBinding> getAllBindings() const;
    void setAllBindings(const std::vector<MidiBinding>& bindings);

private:
    float mapCCToValue(int ccValue, const ParameterDescriptor& desc, bool invert) const;
    void applyValue(ParameterID id, float value, ScopeState& state);
    void applyToggle(ParameterID id, ScopeState& state);

    std::map<MidiKey, MidiBinding> m_bindings;
    bool m_learning = false;
    ParameterID m_learnTarget = ParameterID::COUNT;
};
