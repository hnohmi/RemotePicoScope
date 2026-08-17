#pragma once

#include "core/Types.h"
#include <array>
#include <vector>

struct ChannelState {
    bool enabled = false;
    int voltsPerDivIndex = Sequence125::VOLTS_PER_DIV_DEFAULT;
    float verticalOffset = 0.0f; // in volts
    Coupling coupling = Coupling::DC;
    bool bandwidthLimit = false;
    float probeAttenuation = 1.0f; // 1x / 10x / 100x; scales measured voltage
    bool invert = false;           // negate the trace
    std::string label;             // optional user label (empty = default "CHn")

    float voltsPerDiv() const { return Sequence125::VOLTS_PER_DIV[voltsPerDivIndex]; }
};

struct DigitalChannelState {
    bool enabled = false;
};

struct TriggerConfig {
    TriggerType type = TriggerType::Edge;
    int source = 0;          // 0-3 for CH1-CH4 (Edge)
    float level = 0.0f;      // trigger level in volts (Edge)
    TriggerEdge edge = TriggerEdge::Rising;
    TriggerMode mode = TriggerMode::Auto;

    // Digital-edge trigger
    int digitalSource = 0;   // lane 0-15

    // Pattern trigger: per-lane condition, 0 = don't care, 1 = high, 2 = low
    std::array<int, NUM_DIGITAL_CHANNELS> digitalPattern{};
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
    float x1 = -2.0f; // in divisions from center (display plane)
    float x2 = 2.0f;
    float y1 = -1.0f; // in divisions from center (display plane)
    float y2 = 1.0f;
    int source = 0;   // channel the Y cursors read out against (0-3)
};

// A named group of digital lanes displayed/read as a single multi-bit value.
struct BusConfig {
    bool enabled = false;
    std::string name;
    std::vector<int> lanes; // digital lane indices, LSB first
    int display = 0;        // 0 = hex, 1 = binary, 2 = decimal

    // Extract this bus's value from a 16-bit digital sample.
    uint32_t value(uint16_t sample) const {
        uint32_t v = 0;
        for (size_t k = 0; k < lanes.size(); k++)
            if ((sample >> lanes[k]) & 1) v |= (1u << k);
        return v;
    }
};

// Serial protocol decode configuration (lane assignments + parameters).
// The decode itself is computed on demand from the current capture.
struct DecodeConfig {
    bool enabled = false;
    int protocol = 0;      // 0 = UART, 1 = I2C, 2 = SPI
    // UART
    int uartLane = 0;
    float baud = 9600.0f;
    // I2C
    int i2cScl = 1;
    int i2cSda = 2;
    // SPI
    int spiClk = 3;
    int spiMosi = 4;
    int spiCs = 5;
    int spiCpol = 0;
    int spiCpha = 0;
};

// Mirror of the last-applied signal generator settings, kept so the state
// can be reported over the remote/CLI interface. The generator itself lives
// in PicoSignalSource; this is a reporting shadow updated when it changes.
struct SigGenState {
    bool enabled = false;
    std::string wave = "sine";
    float frequency = 1000.0f;   // Hz
    float amplitudeMv = 2000.0f; // peak-to-peak, mV
    float offsetMv = 0.0f;       // mV
};

struct ScopeState {
    // Analog channels
    std::array<ChannelState, NUM_ANALOG_CHANNELS> analog;

    // Digital channels
    std::array<DigitalChannelState, NUM_DIGITAL_CHANNELS> digital;

    // Per-port logic threshold in volts. Port 0 = D0-D7, Port 1 = D8-D15.
    // Default 1.5 V (TTL). Clamped to +/-5 V by the hardware.
    std::array<float, 2> digitalThreshold = { 1.5f, 1.5f };

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

    // Signal generator (reporting shadow)
    SigGenState sigGen;

    // Serial protocol decode
    DecodeConfig decode;

    // Digital buses (up to 2)
    std::array<BusConfig, 2> buses;

    // Signal source
    SignalSourceType signalSource = SignalSourceType::Dummy;

    // Acquisition
    float sampleRate = 1.0e6f;  // 1 MSa/s default
    int recordLength = 10000;   // fixed-mode record length (per sweep)
    bool recordAuto = true;     // auto: size the sweep for live display

    float timePerDiv() const { return Sequence125::TIME_PER_DIV[timePerDivIndex]; }

    // Total time each sweep captures (trigger centered), capped in absolute
    // time at slow timebases. Auto record mode uses the short 3-window live
    // span (active window + one window either side for timeshift); fixed
    // record mode keeps the deep ACQ_WINDOW_MULT-window span.
    float acquisitionSpan() const {
        float window = timePerDiv() * GRID_DIVISIONS_X;
        float span = window * (recordAuto ? ACQ_FAST_WINDOW_MULT : ACQ_WINDOW_MULT);
        if (span > ACQ_MAX_SPAN_SEC) span = ACQ_MAX_SPAN_SEC;
        if (span < window) span = window;
        return span;
    }

    // How far the view can pan from the trigger while staying inside the
    // captured record (at least half a window either way).
    float maxHorizontalOffset() const {
        float window = timePerDiv() * GRID_DIVISIONS_X;
        float headroom = (acquisitionSpan() - window) * 0.5f;
        return (headroom > window * 0.5f) ? headroom : window * 0.5f;
    }

    // Effective per-sweep record length. In auto mode (default, real-scope
    // behavior) the sweep is sized for responsive live display — the update
    // rate then depends on time/div (window duration), never on a deep
    // memory setting. Fixed mode uses the configured recordLength (deep
    // captures; update rate degrades accordingly).
    int effectiveRecordLength() const {
        if (!recordAuto) return recordLength;
        double n = static_cast<double>(acquisitionSpan()) / 1.0e-9;
        if (n > 300000.0) n = 300000.0; // live cap: 100 k points per window
                                        // (3-window span; same density as the
                                        // old 1 M cap over 10 windows)
        if (n < 1000.0) n = 1000.0;
        return static_cast<int>(n);
    }

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
