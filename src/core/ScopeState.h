#pragma once

#include "core/Types.h"
#include <array>

struct ChannelState {
    bool enabled = false;
    int voltsPerDivIndex = Sequence125::VOLTS_PER_DIV_DEFAULT;
    float verticalOffset = 0.0f; // in volts
    Coupling coupling = Coupling::DC;
    bool bandwidthLimit = false;

    float voltsPerDiv() const { return Sequence125::VOLTS_PER_DIV[voltsPerDivIndex]; }
};

struct DigitalChannelState {
    bool enabled = false;
};

struct TriggerConfig {
    int source = 0;          // 0-3 for CH1-CH4
    float level = 0.0f;      // trigger level in volts
    TriggerEdge edge = TriggerEdge::Rising;
    TriggerMode mode = TriggerMode::Auto;
};

struct MathChannelConfig {
    bool enabled = false;
    MathOp op = MathOp::Add;
    int source1 = 0;  // 0-3 for CH1-CH4
    int source2 = 1;  // 0-3 for CH1-CH4 (unused for unary ops)
    float scale = 1.0f;         // V/div equivalent for display
    float verticalOffset = 0.0f;
    FFTWindowType fftWindow = FFTWindowType::Hanning;
};

struct CursorState {
    bool enabled = false;
    float x1 = -2.0f; // in divisions from center
    float x2 = 2.0f;
    float y1 = -1.0f; // in divisions from center
    float y2 = 1.0f;
};

struct ScopeState {
    // Analog channels
    std::array<ChannelState, NUM_ANALOG_CHANNELS> analog;

    // Digital channels
    std::array<DigitalChannelState, NUM_DIGITAL_CHANNELS> digital;

    // Horizontal
    int timePerDivIndex = Sequence125::TIME_PER_DIV_DEFAULT;
    float horizontalOffset = 0.0f; // in seconds

    // Trigger
    TriggerConfig trigger;
    TriggerStatus triggerStatus = TriggerStatus::Auto;

    // Run mode
    RunMode runMode = RunMode::Run;

    // Math channel
    MathChannelConfig mathChannel;

    // Cursors
    CursorState cursors;

    // Signal source
    SignalSourceType signalSource = SignalSourceType::Dummy;

    // Acquisition
    float sampleRate = 1.0e6f;  // 1 MSa/s default
    int recordLength = 10000;

    float timePerDiv() const { return Sequence125::TIME_PER_DIV[timePerDivIndex]; }

    // Initialize with sensible defaults
    void initDefaults() {
        // CH1 on, rest off
        analog[0].enabled = true;
        analog[0].voltsPerDivIndex = Sequence125::VOLTS_PER_DIV_DEFAULT; // 1V/div
        analog[0].coupling = Coupling::DC;

        for (int i = 1; i < NUM_ANALOG_CHANNELS; i++) {
            analog[i].enabled = false;
            analog[i].voltsPerDivIndex = Sequence125::VOLTS_PER_DIV_DEFAULT;
            analog[i].coupling = Coupling::DC;
        }

        // Enable first 8 digital channels
        for (int i = 0; i < 8; i++)
            digital[i].enabled = true;
        for (int i = 8; i < NUM_DIGITAL_CHANNELS; i++)
            digital[i].enabled = false;

        // Timebase: 1ms/div
        timePerDivIndex = Sequence125::TIME_PER_DIV_DEFAULT;
        horizontalOffset = 0.0f;

        // Trigger: CH1, rising edge, 0V, auto
        trigger.source = 0;
        trigger.level = 0.0f;
        trigger.edge = TriggerEdge::Rising;
        trigger.mode = TriggerMode::Auto;
        triggerStatus = TriggerStatus::Auto;

        runMode = RunMode::Run;
    }
};
