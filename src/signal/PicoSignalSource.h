#pragma once

#include "signal/ISignalSource.h"
#include <array>
#include <vector>
#include <string>
#include <cstdint>

// Forward-declare PicoScope types to avoid pulling the full header into every TU
// The actual ps3000aApi.h is included only in the .cpp
enum enPS3000ARange : int;
typedef enum enPS3000ARange PS3000A_RANGE;

struct PicoDeviceInfo {
    std::string serial;
    std::string description; // e.g. "PicoScope 3406D MSO"
};

class PicoSignalSource : public ISignalSource {
public:
    PicoSignalSource();
    ~PicoSignalSource() override;

    // ISignalSource interface
    void configure(const ScopeState& state) override;
    void acquire(SignalData& data) override;
    std::string name() const override;

    // Device enumeration — returns list of connected PicoScope 3000A devices
    static std::vector<PicoDeviceInfo> enumerateDevices();

    // Device lifecycle
    bool open(const std::string& serial = ""); // Open by serial, or first available if empty
    void close();
    bool isOpen() const { return m_handle > 0; }

    // Status / error info
    const std::string& lastError() const { return m_lastError; }
    const std::string& deviceInfo() const { return m_deviceInfo; }
    const std::string& serial() const { return m_serial; }

    // Built-in signal generator
    enum class SigGenWave : int {
        Sine = 0, Square, Triangle, RampUp, RampDown, Sinc, Gaussian, HalfSine, DC
    };
    // Enable the signal generator. frequency in Hz, amplitudeMv is peak-to-peak in mV, offsetMv in mV.
    bool sigGenEnable(SigGenWave wave, float frequencyHz, float amplitudeMv, float offsetMv = 0);
    // Disable the signal generator
    bool sigGenDisable();

private:
    // Map our V/div index to the best PicoScope range enum
    static PS3000A_RANGE voltsDivToRange(float voltsPerDiv);
    // Full-scale voltage for a given range enum
    static float rangeToVolts(PS3000A_RANGE range);
    // Compute the best timebase index for a desired sample interval
    uint32_t computeTimebase(float desiredSampleIntervalNs) const;
    // Retrieve data from completed block and fill SignalData
    void retrieveData(SignalData& data);

    int16_t m_handle = 0;
    int16_t m_maxADC = 32767;
    std::string m_lastError;
    std::string m_deviceInfo;
    std::string m_serial;

    // Per-channel raw ADC buffers (registered with the driver)
    std::array<std::vector<int16_t>, NUM_ANALOG_CHANNELS> m_adcBuffers;
    // Digital port buffers (port0 = D0-7, port1 = D8-15)
    std::array<std::vector<int16_t>, 2> m_digitalBuffers;

    // Current hardware configuration
    std::array<PS3000A_RANGE, NUM_ANALOG_CHANNELS> m_currentRange;
    std::array<bool, NUM_ANALOG_CHANNELS> m_currentEnabled;
    bool m_port0Enabled = false;
    bool m_port1Enabled = false;
    uint32_t m_timebase = 0;
    int32_t m_numSamples = 10000;
    bool m_configured = false;

    // Non-blocking acquisition state machine
    enum class AcqState { Idle, Running, Ready };
    AcqState m_acqState = AcqState::Idle;
    bool m_hasData = false; // true once we have at least one valid capture

    // Change detection — only reconfigure hardware when settings differ
    struct ConfigSnapshot {
        std::array<bool, NUM_ANALOG_CHANNELS> enabled{};
        std::array<int, NUM_ANALOG_CHANNELS> voltsPerDivIndex{};
        std::array<int, NUM_ANALOG_CHANNELS> coupling{};
        std::array<bool, NUM_ANALOG_CHANNELS> bandwidthLimit{};
        std::array<float, NUM_ANALOG_CHANNELS> verticalOffset{};
        int timePerDivIndex = -1;
        int recordLength = -1;
        int triggerSource = -1;
        float triggerLevel = 0;
        int triggerEdge = -1;
        int triggerMode = -1;
        bool digitalEnabled[NUM_DIGITAL_CHANNELS]{};
    };
    ConfigSnapshot m_lastConfig;
    bool configChanged(const ScopeState& state) const;
    void snapshotConfig(const ScopeState& state);
};
