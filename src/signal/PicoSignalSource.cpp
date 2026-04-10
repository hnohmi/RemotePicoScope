#include "signal/PicoSignalSource.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <ps3000aApi.h>
#include <PicoStatus.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cmath>

// Range table: maps PS3000A_RANGE enum to full-scale voltage
static constexpr float kRangeVolts[] = {
    0.010f,  // PS3000A_10MV
    0.020f,  // PS3000A_20MV
    0.050f,  // PS3000A_50MV
    0.100f,  // PS3000A_100MV
    0.200f,  // PS3000A_200MV
    0.500f,  // PS3000A_500MV
    1.000f,  // PS3000A_1V
    2.000f,  // PS3000A_2V
    5.000f,  // PS3000A_5V
    10.00f,  // PS3000A_10V
    20.00f,  // PS3000A_20V
    50.00f,  // PS3000A_50V
};
static constexpr int kNumRanges = sizeof(kRangeVolts) / sizeof(float);

PicoSignalSource::PicoSignalSource() {
    m_currentRange.fill(static_cast<PS3000A_RANGE>(0));
    m_currentEnabled.fill(false);
}

PicoSignalSource::~PicoSignalSource() {
    close();
}

std::vector<PicoDeviceInfo> PicoSignalSource::enumerateDevices() {
    std::vector<PicoDeviceInfo> result;

    int16_t count = 0;
    int8_t serials[1024] = {};
    int16_t serialsLen = sizeof(serials);

    PICO_STATUS status = ps3000aEnumerateUnits(&count, serials, &serialsLen);
    if (status != PICO_OK || count == 0)
        return result;

    // serials is a comma-separated list of serial numbers
    std::string serialStr(reinterpret_cast<char*>(serials));
    size_t pos = 0;
    while (pos < serialStr.size()) {
        size_t comma = serialStr.find(',', pos);
        if (comma == std::string::npos) comma = serialStr.size();
        std::string serial = serialStr.substr(pos, comma - pos);
        pos = comma + 1;

        if (serial.empty()) continue;

        // Open briefly to get variant info, then close
        PicoDeviceInfo info;
        info.serial = serial;

        int16_t tempHandle = 0;
        PICO_STATUS openStatus = ps3000aOpenUnit(
            &tempHandle, reinterpret_cast<int8_t*>(serial.data()));

        if (openStatus == PICO_POWER_SUPPLY_NOT_CONNECTED ||
            openStatus == PICO_USB3_0_DEVICE_NON_USB3_0_PORT) {
            openStatus = ps3000aChangePowerSource(tempHandle, openStatus);
        }

        if (openStatus == PICO_OK && tempHandle > 0) {
            int8_t infoStr[64] = {};
            int16_t reqSize = 0;
            ps3000aGetUnitInfo(tempHandle, infoStr, sizeof(infoStr), &reqSize, PICO_VARIANT_INFO);
            info.description = "PicoScope " + std::string(reinterpret_cast<char*>(infoStr));
            ps3000aCloseUnit(tempHandle);
        } else {
            info.description = "PicoScope 3000A (S/N: " + serial + ")";
        }

        result.push_back(std::move(info));
    }

    return result;
}

bool PicoSignalSource::open(const std::string& serial) {
    if (m_handle > 0)
        return true;

    int8_t* serialArg = nullptr;
    std::string serialCopy = serial;
    if (!serial.empty())
        serialArg = reinterpret_cast<int8_t*>(serialCopy.data());

    PICO_STATUS status = ps3000aOpenUnit(&m_handle, serialArg);

    // Handle power supply prompts for USB-powered scopes
    if (status == PICO_POWER_SUPPLY_NOT_CONNECTED ||
        status == PICO_USB3_0_DEVICE_NON_USB3_0_PORT) {
        status = ps3000aChangePowerSource(m_handle, status);
    }

    if (status != PICO_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "ps3000aOpenUnit failed: 0x%08X", static_cast<unsigned>(status));
        m_lastError = buf;
        m_handle = 0;
        return false;
    }

    // Get max ADC value
    ps3000aMaximumValue(m_handle, &m_maxADC);

    // Read device info (variant string)
    int8_t infoStr[64] = {};
    int16_t reqSize = 0;
    ps3000aGetUnitInfo(m_handle, infoStr, sizeof(infoStr), &reqSize, PICO_VARIANT_INFO);
    m_deviceInfo = "PicoScope " + std::string(reinterpret_cast<char*>(infoStr));

    // Read serial
    int8_t serialStr[64] = {};
    ps3000aGetUnitInfo(m_handle, serialStr, sizeof(serialStr), &reqSize, PICO_BATCH_AND_SERIAL);
    m_serial = std::string(reinterpret_cast<char*>(serialStr));

    m_lastError.clear();
    m_configured = false;
    m_acqState = AcqState::Idle;
    m_hasData = false;
    return true;
}

bool PicoSignalSource::sigGenEnable(SigGenWave wave, float frequencyHz,
                                     float amplitudeMv, float offsetMv) {
    if (m_handle <= 0) return false;

    int32_t offsetUv = static_cast<int32_t>(offsetMv * 1000.0f);
    uint32_t pkToPkUv = static_cast<uint32_t>(amplitudeMv * 1000.0f);

    PICO_STATUS status = ps3000aSetSigGenBuiltIn(
        m_handle,
        offsetUv,
        pkToPkUv,
        static_cast<int16_t>(wave), // PS3000A_WAVE_TYPE matches our enum order
        frequencyHz,
        frequencyHz,    // stopFrequency = startFrequency (no sweep)
        0,              // increment
        0,              // dwellTime
        PS3000A_UP,     // sweepType (unused for fixed freq)
        PS3000A_ES_OFF, // no extra operations
        0,              // shots = 0 -> continuous
        0,              // sweeps
        PS3000A_SIGGEN_RISING,
        PS3000A_SIGGEN_NONE, // free-running
        0               // extInThreshold
    );

    if (status != PICO_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "SigGen failed: 0x%08X", static_cast<unsigned>(status));
        m_lastError = buf;
        return false;
    }
    return true;
}

bool PicoSignalSource::sigGenDisable() {
    if (m_handle <= 0) return false;

    // Set to DC 0V to effectively disable
    PICO_STATUS status = ps3000aSetSigGenBuiltIn(
        m_handle, 0, 0,
        static_cast<int16_t>(SigGenWave::DC),
        0, 0, 0, 0,
        PS3000A_UP, PS3000A_ES_OFF, 0, 0,
        PS3000A_SIGGEN_RISING, PS3000A_SIGGEN_NONE, 0
    );
    return status == PICO_OK;
}

void PicoSignalSource::close() {
    if (m_handle > 0) {
        sigGenDisable();
        ps3000aStop(m_handle);
        ps3000aCloseUnit(m_handle);
        m_handle = 0;
    }
    m_configured = false;
    m_acqState = AcqState::Idle;
    m_hasData = false;
    m_deviceInfo.clear();
    m_serial.clear();
}

std::string PicoSignalSource::name() const {
    if (!m_deviceInfo.empty())
        return m_deviceInfo;
    return "PicoScope 3000A";
}

PS3000A_RANGE PicoSignalSource::voltsDivToRange(float voltsPerDiv) {
    // The scope range should cover the full display (4 divisions above and below center)
    float needed = voltsPerDiv * static_cast<float>(GRID_DIVISIONS_Y) / 2.0f;

    for (int i = 0; i < kNumRanges; i++) {
        if (kRangeVolts[i] >= needed)
            return static_cast<PS3000A_RANGE>(i);
    }
    return static_cast<PS3000A_RANGE>(kNumRanges - 1); // PS3000A_50V
}

float PicoSignalSource::rangeToVolts(PS3000A_RANGE range) {
    int idx = static_cast<int>(range);
    if (idx < 0 || idx >= kNumRanges)
        return 50.0f;
    return kRangeVolts[idx];
}

uint32_t PicoSignalSource::computeTimebase(float desiredSampleIntervalNs) const {
    // PicoScope 3406D MSO timebase formula:
    // For timebase 0-2: sample interval = 2^timebase / 500MHz  (i.e. 2ns, 4ns, 8ns)
    // For timebase >= 3: sample interval = (timebase - 2) / 62.5MHz  (i.e. 16ns, 32ns, ...)

    if (desiredSampleIntervalNs <= 2.0f) return 0;      // 2 ns / 500 MSa/s
    if (desiredSampleIntervalNs <= 4.0f) return 1;      // 4 ns / 250 MSa/s
    if (desiredSampleIntervalNs <= 8.0f) return 2;      // 8 ns / 125 MSa/s

    // For timebase >= 3: interval = (timebase - 2) * 16 ns
    uint32_t tb = static_cast<uint32_t>(desiredSampleIntervalNs / 16.0f) + 2;
    if (tb < 3) tb = 3;
    return tb;
}

bool PicoSignalSource::configChanged(const ScopeState& state) const {
    if (!m_configured) return true;
    if (state.timePerDivIndex != m_lastConfig.timePerDivIndex) return true;
    if (state.recordLength != m_lastConfig.recordLength) return true;
    if (state.trigger.source != m_lastConfig.triggerSource) return true;
    if (state.trigger.level != m_lastConfig.triggerLevel) return true;
    if (static_cast<int>(state.trigger.edge) != m_lastConfig.triggerEdge) return true;
    if (static_cast<int>(state.trigger.mode) != m_lastConfig.triggerMode) return true;
    for (int i = 0; i < NUM_ANALOG_CHANNELS; i++) {
        if (state.analog[i].enabled != m_lastConfig.enabled[i]) return true;
        if (state.analog[i].voltsPerDivIndex != m_lastConfig.voltsPerDivIndex[i]) return true;
        if (static_cast<int>(state.analog[i].coupling) != m_lastConfig.coupling[i]) return true;
        if (state.analog[i].bandwidthLimit != m_lastConfig.bandwidthLimit[i]) return true;
        if (state.analog[i].verticalOffset != m_lastConfig.verticalOffset[i]) return true;
    }
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++) {
        if (state.digital[i].enabled != m_lastConfig.digitalEnabled[i]) return true;
    }
    return false;
}

void PicoSignalSource::snapshotConfig(const ScopeState& state) {
    m_lastConfig.timePerDivIndex = state.timePerDivIndex;
    m_lastConfig.recordLength = state.recordLength;
    m_lastConfig.triggerSource = state.trigger.source;
    m_lastConfig.triggerLevel = state.trigger.level;
    m_lastConfig.triggerEdge = static_cast<int>(state.trigger.edge);
    m_lastConfig.triggerMode = static_cast<int>(state.trigger.mode);
    for (int i = 0; i < NUM_ANALOG_CHANNELS; i++) {
        m_lastConfig.enabled[i] = state.analog[i].enabled;
        m_lastConfig.voltsPerDivIndex[i] = state.analog[i].voltsPerDivIndex;
        m_lastConfig.coupling[i] = static_cast<int>(state.analog[i].coupling);
        m_lastConfig.bandwidthLimit[i] = state.analog[i].bandwidthLimit;
        m_lastConfig.verticalOffset[i] = state.analog[i].verticalOffset;
    }
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++) {
        m_lastConfig.digitalEnabled[i] = state.digital[i].enabled;
    }
}

void PicoSignalSource::configure(const ScopeState& state) {
    if (m_handle <= 0) return;

    // Skip reconfiguration if nothing changed — avoids stopping active captures
    if (!configChanged(state)) return;

    // If a block is still running, stop it before reconfiguring
    if (m_acqState == AcqState::Running) {
        ps3000aStop(m_handle);
        m_acqState = AcqState::Idle;
    }

    // Configure analog channels
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        PS3000A_CHANNEL psChannel = static_cast<PS3000A_CHANNEL>(PS3000A_CHANNEL_A + ch);
        bool enabled = state.analog[ch].enabled;
        PS3000A_RANGE range = voltsDivToRange(state.analog[ch].voltsPerDiv());
        PS3000A_COUPLING coupling = (state.analog[ch].coupling == Coupling::AC)
            ? PS3000A_AC : PS3000A_DC;
        PS3000A_BANDWIDTH_LIMITER bw = state.analog[ch].bandwidthLimit
            ? PS3000A_BW_20MHZ : PS3000A_BW_FULL;

        // Do not use the PicoScope hardware analog offset — offset is
        // applied purely in the display layer. This keeps the sample
        // buffer holding true probe voltage and ensures the waveform and
        // the trigger level line stay in lockstep when offset changes.
        ps3000aSetChannel(m_handle, psChannel, enabled ? 1 : 0,
                          coupling, range, 0.0f);
        ps3000aSetBandwidthFilter(m_handle, psChannel, bw);

        m_currentEnabled[ch] = enabled;
        m_currentRange[ch] = range;
    }

    // Configure digital ports (MSO model)
    m_port0Enabled = false;
    m_port1Enabled = false;
    for (int d = 0; d < 8; d++) {
        if (state.digital[d].enabled) m_port0Enabled = true;
    }
    for (int d = 8; d < 16; d++) {
        if (state.digital[d].enabled) m_port1Enabled = true;
    }

    // Logic level threshold ~1.5V (TTL)
    int16_t logicLevel = 9830;
    ps3000aSetDigitalPort(m_handle, PS3000A_DIGITAL_PORT0,
                          m_port0Enabled ? 1 : 0, logicLevel);
    ps3000aSetDigitalPort(m_handle, PS3000A_DIGITAL_PORT1,
                          m_port1Enabled ? 1 : 0, logicLevel);

    // Compute timebase from time/div
    float visibleTime = state.timePerDiv() * GRID_DIVISIONS_X;
    int numSamples = state.recordLength;
    float sampleIntervalSec = visibleTime / numSamples;
    float sampleIntervalNs = sampleIntervalSec * 1.0e9f;
    m_timebase = computeTimebase(sampleIntervalNs);
    m_numSamples = numSamples;

    // Verify timebase with the driver
    float actualIntervalNs = 0;
    int32_t maxSamples = 0;
    PICO_STATUS tbStatus = ps3000aGetTimebase2(m_handle, m_timebase, m_numSamples,
                                                &actualIntervalNs, 0, &maxSamples, 0);
    if (tbStatus != PICO_OK) {
        for (uint32_t tb = m_timebase + 1; tb < m_timebase + 100; tb++) {
            tbStatus = ps3000aGetTimebase2(m_handle, tb, m_numSamples,
                                           &actualIntervalNs, 0, &maxSamples, 0);
            if (tbStatus == PICO_OK) {
                m_timebase = tb;
                break;
            }
        }
    }

    if (maxSamples > 0 && m_numSamples > maxSamples) {
        m_numSamples = maxSamples;
    }

    // Set trigger
    PS3000A_CHANNEL trigSource = static_cast<PS3000A_CHANNEL>(
        PS3000A_CHANNEL_A + state.trigger.source);
    PS3000A_THRESHOLD_DIRECTION trigDir =
        (state.trigger.edge == TriggerEdge::Rising) ? PS3000A_RISING : PS3000A_FALLING;

    // No hardware analog offset is applied, so trigger.level maps directly
    // to ADC counts via the channel range.
    float trigRange = rangeToVolts(m_currentRange[state.trigger.source]);
    int16_t trigThreshold = static_cast<int16_t>(
        (state.trigger.level / trigRange) * m_maxADC);

    int16_t autoTrigMs = (state.trigger.mode == TriggerMode::Auto) ? 100 : 0;

    ps3000aSetSimpleTrigger(m_handle, 1, trigSource, trigThreshold,
                            trigDir, 0, autoTrigMs);

    // Allocate ADC buffers and register with driver
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        m_adcBuffers[ch].resize(m_numSamples);
        if (m_currentEnabled[ch]) {
            ps3000aSetDataBuffer(m_handle,
                static_cast<PS3000A_CHANNEL>(PS3000A_CHANNEL_A + ch),
                m_adcBuffers[ch].data(), m_numSamples, 0, PS3000A_RATIO_MODE_NONE);
        }
    }

    if (m_port0Enabled) {
        m_digitalBuffers[0].resize(m_numSamples);
        ps3000aSetDataBuffer(m_handle,
            static_cast<PS3000A_CHANNEL>(PS3000A_DIGITAL_PORT0),
            m_digitalBuffers[0].data(), m_numSamples, 0, PS3000A_RATIO_MODE_NONE);
    }
    if (m_port1Enabled) {
        m_digitalBuffers[1].resize(m_numSamples);
        ps3000aSetDataBuffer(m_handle,
            static_cast<PS3000A_CHANNEL>(PS3000A_DIGITAL_PORT1),
            m_digitalBuffers[1].data(), m_numSamples, 0, PS3000A_RATIO_MODE_NONE);
    }

    m_configured = true;
    snapshotConfig(state);
}

void PicoSignalSource::acquire(SignalData& data) {
    if (m_handle <= 0 || !m_configured) return;

    // Non-blocking state machine:
    // Idle -> stop (safety) -> re-register buffers -> RunBlock -> Running
    // Running -> poll IsReady -> if ready -> GetValues -> Idle
    // If not ready, return immediately (UI shows previous data)

    if (m_acqState == AcqState::Idle) {
        // Ensure device is stopped before starting a new block
        ps3000aStop(m_handle);

        // Re-register data buffers each cycle — the driver may invalidate
        // buffer registrations after Stop on some firmware versions
        for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
            if (m_currentEnabled[ch]) {
                ps3000aSetDataBuffer(m_handle,
                    static_cast<PS3000A_CHANNEL>(PS3000A_CHANNEL_A + ch),
                    m_adcBuffers[ch].data(), m_numSamples, 0, PS3000A_RATIO_MODE_NONE);
            }
        }
        if (m_port0Enabled) {
            ps3000aSetDataBuffer(m_handle,
                static_cast<PS3000A_CHANNEL>(PS3000A_DIGITAL_PORT0),
                m_digitalBuffers[0].data(), m_numSamples, 0, PS3000A_RATIO_MODE_NONE);
        }
        if (m_port1Enabled) {
            ps3000aSetDataBuffer(m_handle,
                static_cast<PS3000A_CHANNEL>(PS3000A_DIGITAL_PORT1),
                m_digitalBuffers[1].data(), m_numSamples, 0, PS3000A_RATIO_MODE_NONE);
        }

        // Start block acquisition
        int32_t preTrig = m_numSamples / 2;
        int32_t postTrig = m_numSamples - preTrig;
        int32_t timeIndisposed = 0;

        PICO_STATUS status = ps3000aRunBlock(m_handle, preTrig, postTrig,
                                              m_timebase, 0, &timeIndisposed,
                                              0, nullptr, nullptr);
        if (status != PICO_OK) {
            char buf[128];
            snprintf(buf, sizeof(buf), "ps3000aRunBlock failed: 0x%08X",
                     static_cast<unsigned>(status));
            m_lastError = buf;
            // Check if device is still connected
            if (status == PICO_INVALID_HANDLE || status == PICO_NOT_RESPONDING) {
                m_handle = 0;
                m_configured = false;
            }
            return;
        }
        m_acqState = AcqState::Running;
    }

    if (m_acqState == AcqState::Running) {
        int16_t ready = 0;
        PICO_STATUS status = ps3000aIsReady(m_handle, &ready);

        if (status != PICO_OK) {
            // Device error — abort
            char buf[128];
            snprintf(buf, sizeof(buf), "ps3000aIsReady failed: 0x%08X",
                     static_cast<unsigned>(status));
            m_lastError = buf;
            m_acqState = AcqState::Idle;
            return;
        }

        if (ready) {
            retrieveData(data);
            m_acqState = AcqState::Idle;
        }
    }
}

void PicoSignalSource::retrieveData(SignalData& data) {
    uint32_t numSamples = static_cast<uint32_t>(m_numSamples);
    int16_t overflow = 0;
    PICO_STATUS status = ps3000aGetValues(m_handle, 0, &numSamples, 1,
                                           PS3000A_RATIO_MODE_NONE, 0, &overflow);
    if (status != PICO_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "ps3000aGetValues failed: 0x%08X",
                 static_cast<unsigned>(status));
        m_lastError = buf;
        return;
    }

    // Clamp to buffer size to prevent out-of-bounds reads
    int n = static_cast<int>(numSamples);
    int bufSize = static_cast<int>(m_adcBuffers[0].size());
    if (bufSize > 0 && n > bufSize)
        n = bufSize;

    data.resize(n);

    // Compute actual sample rate from timebase
    float intervalNs = 0;
    ps3000aGetTimebase2(m_handle, m_timebase, n, &intervalNs, 0, nullptr, 0);
    if (intervalNs > 0)
        data.sampleRate = 1.0e9f / intervalNs;

    // Convert ADC counts to volts
    float maxADCf = static_cast<float>(m_maxADC);
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        if (!m_currentEnabled[ch]) {
            data.analog[ch].clear();
            continue;
        }

        int chBufSize = static_cast<int>(m_adcBuffers[ch].size());
        int count = (n < chBufSize) ? n : chBufSize;
        float rangeV = rangeToVolts(m_currentRange[ch]);
        const int16_t* raw = m_adcBuffers[ch].data();
        float* out = data.analog[ch].samples.data();

        // Hardware analog offset is not used (see configure()), so the
        // samples already represent true probe voltage.
        for (int i = 0; i < count; i++) {
            out[i] = (static_cast<float>(raw[i]) / maxADCf) * rangeV;
        }
    }

    // Convert digital port data to 16-bit bitmask
    int digi0Size = static_cast<int>(m_digitalBuffers[0].size());
    int digi1Size = static_cast<int>(m_digitalBuffers[1].size());
    for (int i = 0; i < n; i++) {
        uint16_t bits = 0;
        if (m_port0Enabled && i < digi0Size) {
            bits |= static_cast<uint16_t>(m_digitalBuffers[0][i] & 0xFF);
        }
        if (m_port1Enabled && i < digi1Size) {
            bits |= static_cast<uint16_t>((m_digitalBuffers[1][i] & 0xFF) << 8);
        }
        data.digital.samples[i] = bits;
    }

    m_hasData = true;
    m_lastError.clear();
}
