#include "midi/MidiMapping.h"
#include <cmath>
#include <algorithm>

static std::vector<ParameterDescriptor> s_descriptors;

static void initDescriptors() {
    if (!s_descriptors.empty()) return;

    // Analog channels
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        int base = ch * 3;
        char nameBuf[32], groupBuf[16];
        snprintf(groupBuf, sizeof(groupBuf), "Channel %d", ch + 1);

        snprintf(nameBuf, sizeof(nameBuf), "CH%d V/div", ch + 1);
        s_descriptors.push_back({
            static_cast<ParameterID>(base + 0), strdup(nameBuf), strdup(groupBuf),
            0, static_cast<float>(Sequence125::VOLTS_PER_DIV_COUNT - 1),
            MappingCurve::Stepped,
            Sequence125::VOLTS_PER_DIV, Sequence125::VOLTS_PER_DIV_COUNT
        });

        snprintf(nameBuf, sizeof(nameBuf), "CH%d Offset", ch + 1);
        s_descriptors.push_back({
            static_cast<ParameterID>(base + 1), strdup(nameBuf), strdup(groupBuf),
            -10.0f, 10.0f, MappingCurve::Linear
        });

        snprintf(nameBuf, sizeof(nameBuf), "CH%d Enable", ch + 1);
        s_descriptors.push_back({
            static_cast<ParameterID>(base + 2), strdup(nameBuf), strdup(groupBuf),
            0, 1, MappingCurve::Toggle
        });
    }

    // Horizontal
    s_descriptors.push_back({
        ParameterID::TimePerDiv, "Time/div", "Horizontal",
        0, static_cast<float>(Sequence125::TIME_PER_DIV_COUNT - 1),
        MappingCurve::Stepped,
        Sequence125::TIME_PER_DIV, Sequence125::TIME_PER_DIV_COUNT
    });
    s_descriptors.push_back({
        ParameterID::HorizontalOffset, "H Position", "Horizontal",
        -1.0f, 1.0f, MappingCurve::Linear
    });

    // Trigger
    s_descriptors.push_back({
        ParameterID::TriggerLevel, "Trigger Level", "Trigger",
        -5.0f, 5.0f, MappingCurve::Linear
    });

    // Run control
    s_descriptors.push_back({
        ParameterID::RunStop, "Run/Stop", "Control",
        0, 1, MappingCurve::Toggle
    });
    s_descriptors.push_back({
        ParameterID::SingleShot, "Single", "Control",
        0, 1, MappingCurve::Momentary
    });
}

MidiMapping::MidiMapping() {
    initDescriptors();
}

const std::vector<ParameterDescriptor>& MidiMapping::parameterDescriptors() {
    initDescriptors();
    return s_descriptors;
}

const ParameterDescriptor* MidiMapping::findDescriptor(ParameterID id) {
    initDescriptors();
    for (const auto& d : s_descriptors) {
        if (d.id == id) return &d;
    }
    return nullptr;
}

float MidiMapping::mapCCToValue(int ccValue, const ParameterDescriptor& desc, bool invert) const {
    float normalized = static_cast<float>(ccValue) / 127.0f;
    if (invert) normalized = 1.0f - normalized;

    switch (desc.curve) {
        case MappingCurve::Linear:
            return desc.minValue + normalized * (desc.maxValue - desc.minValue);

        case MappingCurve::Logarithmic: {
            float logMin = std::log(std::max(desc.minValue, 1e-10f));
            float logMax = std::log(std::max(desc.maxValue, 1e-10f));
            return std::exp(logMin + normalized * (logMax - logMin));
        }

        case MappingCurve::Stepped: {
            int stepIdx = static_cast<int>(normalized * (desc.stepCount - 1) + 0.5f);
            stepIdx = std::clamp(stepIdx, 0, desc.stepCount - 1);
            return static_cast<float>(stepIdx);
        }

        case MappingCurve::Toggle:
            return (ccValue >= 64) ? 1.0f : 0.0f;

        case MappingCurve::Momentary:
            return (ccValue > 0) ? 1.0f : 0.0f;
    }
    return 0.0f;
}

void MidiMapping::applyValue(ParameterID id, float value, ScopeState& state) {
    switch (id) {
        case ParameterID::CH1_VoltsPerDiv: state.analog[0].voltsPerDivIndex = static_cast<int>(value); break;
        case ParameterID::CH1_Offset:      state.analog[0].verticalOffset = value; break;
        case ParameterID::CH1_Enable:      state.analog[0].enabled = (value > 0.5f); break;
        case ParameterID::CH2_VoltsPerDiv: state.analog[1].voltsPerDivIndex = static_cast<int>(value); break;
        case ParameterID::CH2_Offset:      state.analog[1].verticalOffset = value; break;
        case ParameterID::CH2_Enable:      state.analog[1].enabled = (value > 0.5f); break;
        case ParameterID::CH3_VoltsPerDiv: state.analog[2].voltsPerDivIndex = static_cast<int>(value); break;
        case ParameterID::CH3_Offset:      state.analog[2].verticalOffset = value; break;
        case ParameterID::CH3_Enable:      state.analog[2].enabled = (value > 0.5f); break;
        case ParameterID::CH4_VoltsPerDiv: state.analog[3].voltsPerDivIndex = static_cast<int>(value); break;
        case ParameterID::CH4_Offset:      state.analog[3].verticalOffset = value; break;
        case ParameterID::CH4_Enable:      state.analog[3].enabled = (value > 0.5f); break;

        case ParameterID::TimePerDiv:
            state.timePerDivIndex = static_cast<int>(value);
            break;
        case ParameterID::HorizontalOffset: {
            float maxOff = state.timePerDiv() * GRID_DIVISIONS_X * 0.5f;
            state.horizontalOffset = value * maxOff;
            break;
        }

        case ParameterID::TriggerLevel:
            state.trigger.level = value;
            break;

        case ParameterID::RunStop:
            if (value > 0.5f)
                state.runMode = (state.runMode == RunMode::Run) ? RunMode::Stop : RunMode::Run;
            break;
        case ParameterID::SingleShot:
            if (value > 0.5f)
                state.runMode = RunMode::Single;
            break;

        default:
            break;
    }
}

void MidiMapping::applyToggle(ParameterID id, ScopeState& state) {
    switch (id) {
        case ParameterID::CH1_Enable: state.analog[0].enabled = !state.analog[0].enabled; break;
        case ParameterID::CH2_Enable: state.analog[1].enabled = !state.analog[1].enabled; break;
        case ParameterID::CH3_Enable: state.analog[2].enabled = !state.analog[2].enabled; break;
        case ParameterID::CH4_Enable: state.analog[3].enabled = !state.analog[3].enabled; break;
        case ParameterID::RunStop:
            state.runMode = (state.runMode == RunMode::Run) ? RunMode::Stop : RunMode::Run;
            break;
        case ParameterID::SingleShot:
            state.runMode = RunMode::Single;
            break;
        default:
            break;
    }
}

void MidiMapping::applyCC(const MidiMessage& msg, ScopeState& state) {
    if (msg.type != MidiMessage::ControlChange)
        return;

    // Learn mode: bind this CC to the pending parameter
    if (m_learning) {
        MidiBinding binding;
        binding.midiChannel = msg.channel;
        binding.ccNumber = msg.data1;
        binding.parameter = m_learnTarget;

        const auto* desc = findDescriptor(m_learnTarget);
        if (desc) {
            binding.label = std::string("CC") + std::to_string(msg.data1) + " -> " + desc->name;
            binding.curve = desc->curve;
        }

        addBinding(binding);
        m_learning = false;
        m_learnTarget = ParameterID::COUNT;
        return;
    }

    // Normal mode: look up binding and apply
    MidiKey key{ msg.channel, msg.data1 };
    auto it = m_bindings.find(key);
    if (it == m_bindings.end()) return;

    auto& binding = it->second;
    const auto* desc = findDescriptor(binding.parameter);
    if (!desc) return;

    // Toggle mode: detect rising edge (off->on) and toggle the boolean state
    if (binding.toggleMode) {
        bool wasPressed = binding.lastCCValue >= 64;
        bool isPressed = msg.data2 >= 64;
        binding.lastCCValue = msg.data2;

        if (isPressed && !wasPressed) {
            // Rising edge — toggle
            applyToggle(binding.parameter, state);
        }
        return;
    }

    float value = mapCCToValue(msg.data2, *desc, binding.invert);
    applyValue(binding.parameter, value, state);
}

void MidiMapping::startLearn(ParameterID target) {
    m_learning = true;
    m_learnTarget = target;
}

void MidiMapping::cancelLearn() {
    m_learning = false;
    m_learnTarget = ParameterID::COUNT;
}

void MidiMapping::addBinding(const MidiBinding& binding) {
    MidiKey key{ binding.midiChannel, binding.ccNumber };
    m_bindings[key] = binding;
}

void MidiMapping::removeBinding(int midiChannel, int ccNumber) {
    m_bindings.erase(MidiKey{ midiChannel, ccNumber });
}

void MidiMapping::clearAllBindings() {
    m_bindings.clear();
}

std::vector<MidiBinding> MidiMapping::getAllBindings() const {
    std::vector<MidiBinding> result;
    for (const auto& [key, binding] : m_bindings)
        result.push_back(binding);
    return result;
}

void MidiMapping::setAllBindings(const std::vector<MidiBinding>& bindings) {
    m_bindings.clear();
    for (const auto& b : bindings)
        addBinding(b);
}
