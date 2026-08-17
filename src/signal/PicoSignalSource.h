#pragma once

#include "signal/ISignalSource.h"
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <chrono>

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

    // ---- Streaming recorder (gapless capture to disk) ----
    // Uses ps3000aRunStreaming with autoStop=0: runs until stopped, limited
    // only by USB throughput and disk space. Block acquisition is suspended
    // while recording; a rolling window of the stream feeds the live display.
    struct RecordingStatus {
        bool active = false;
        uint64_t samples = 0;   // per channel
        uint64_t bytes = 0;     // file size written so far
        double seconds = 0.0;
        float actualRateHz = 0.0f;
        int overflowEvents = 0;
        std::string file;
    };
    // channelMask bit i = record analog channel i (0 = all enabled channels).
    // viewWindowSec sizes the rolling live-display buffer.
    bool startRecording(const std::string& path, float requestedRateHz,
                        uint16_t channelMask, float viewWindowSec,
                        std::string& errOut);
    void stopRecording();
    bool isRecording() const { return m_recording; }
    RecordingStatus recordingStatus() const;
    // Drain streamed data; call once per frame (no-op unless recording).
    void serviceRecording(SignalData& data);

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

    // Chunked block retrieval: a completed block is pulled from the driver in
    // slices (one per frame) into a staging buffer, then swapped into the
    // live SignalData atomically. Bounds per-frame cost for huge records and
    // guarantees CLI/display never observe a torn capture.
    bool beginRetrieve();                 // false on driver error
    bool retrieveChunk(SignalData& data); // true when retrieval finished
    void convertRange(int offset, int count);  // ADC -> volts into staging
    void publishStaging(SignalData& data);     // atomic swap into live data
    void registerBuffers(uint32_t segmentIndex);

    // Rapid-block live mode (auto record length, not recording): the scope
    // captures a batch of triggered segments back-to-back in hardware
    // (~1 us re-arm between segments), then the batch is animated one
    // segment per UI frame. Skips segments when a batch is larger than the
    // animation budget; capture is paused only while a batch is drained.
    void armRapid();                      // SetNoOfCaptures + RunBlock
    void acquireRapid(SignalData& data);  // per-frame rapid state machine
    bool retrieveSegment(uint32_t segmentIndex, SignalData& data);

    int16_t m_handle = 0;
    int16_t m_maxADC = 32767;
    std::string m_lastError;
    std::string m_deviceInfo;
    std::string m_serial;

    // Per-channel display-layer scale = probeAttenuation * (invert ? -1 : 1).
    // Applied exactly once, at ADC->volts conversion in retrieveData(). Kept
    // out of ConfigSnapshot: changing it must not stop/restart acquisition.
    std::array<float, NUM_ANALOG_CHANNELS> m_userScale{ 1.0f, 1.0f, 1.0f, 1.0f };

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
    enum class AcqState { Idle, Running, Retrieving, RapidRunning, RapidRetrieving };
    AcqState m_acqState = AcqState::Idle;
    bool m_hasData = false; // true once we have at least one valid capture

    // Chunked retrieval state
    int m_retrieveOffset = 0;        // samples pulled so far
    SignalData m_staging;            // conversion target until complete
    float m_pendingSampleRate = 0.0f;

    // Rapid-block state
    bool m_rapid = false;            // rapid live mode active (auto record)
    float m_sweepSec = 0.0f;         // wall-clock duration of one sweep
    int16_t m_autoTrigMs = 0;        // auto-trigger delay set on the hardware
    uint32_t m_nSegments = 1;        // memory segments / captures per batch
    std::vector<uint32_t> m_rapidPlan; // segment indices to animate, oldest->newest
    size_t m_rapidPlanPos = 0;
    std::chrono::steady_clock::time_point m_armTime{};

    // Recording state
    bool m_recording = false;
    std::string m_recPath;
    void* m_recFile = nullptr;       // FILE*, opaque to keep cstdio out of the header
    uint16_t m_recMask = 0;
    int m_recNumCh = 0;
    int m_recChannels[NUM_ANALOG_CHANNELS]{};
    uint64_t m_recSamples = 0;
    int m_recOverflows = 0;
    float m_recActualRate = 0.0f;
    float m_recScale[NUM_ANALOG_CHANNELS]{};
    std::vector<int16_t> m_recScratch; // interleave buffer for disk writes
    // Rolling live-view ring (converted volts) per recorded channel
    std::array<std::vector<float>, NUM_ANALOG_CHANNELS> m_rollBuf;
    int m_rollSize = 0;
    int m_rollPos = 0;
    bool m_rollFull = false;
    bool m_rollDirty = false;

    static void __stdcall streamingCallback(int16_t handle, int32_t noOfSamples,
                                            uint32_t startIndex, int16_t overflow,
                                            uint32_t triggerAt, int16_t triggered,
                                            int16_t autoStop, void* pParameter);
    void onStreamingData(int32_t noOfSamples, uint32_t startIndex, int16_t overflow);

    // Change detection — only reconfigure hardware when settings differ
    struct ConfigSnapshot {
        // NOTE: verticalOffset and invert are deliberately absent — they are
        // display-layer settings that must not trigger a hardware
        // stop/reconfigure/restart cycle. probeAttenuation IS tracked: the
        // BNC-side input range and trigger threshold both depend on it.
        bool recordAuto = true; // selects rapid live vs deep single-block mode
        std::array<bool, NUM_ANALOG_CHANNELS> enabled{};
        std::array<int, NUM_ANALOG_CHANNELS> voltsPerDivIndex{};
        std::array<int, NUM_ANALOG_CHANNELS> coupling{};
        std::array<bool, NUM_ANALOG_CHANNELS> bandwidthLimit{};
        std::array<float, NUM_ANALOG_CHANNELS> probeAttenuation{};
        int timePerDivIndex = -1;
        int recordLength = -1;
        int triggerSource = -1;
        float triggerLevel = 0;
        int triggerEdge = -1;
        int triggerMode = -1;
        int triggerType = -1;
        int triggerDigitalSource = -1;
        // Probe/invert of the *trigger source* channel: the hardware trigger
        // threshold is expressed in BNC volts, so it must be recomputed when
        // this scale changes (the only probe/invert-driven reconfigure).
        float triggerUserScale = 1.0f;
        int triggerPattern[NUM_DIGITAL_CHANNELS]{};
        bool digitalEnabled[NUM_DIGITAL_CHANNELS]{};
        float digitalThreshold[2]{ 0, 0 };
    };
    ConfigSnapshot m_lastConfig;
    bool configChanged(const ScopeState& state) const;
    void snapshotConfig(const ScopeState& state);
};
