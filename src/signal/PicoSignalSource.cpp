#include "signal/PicoSignalSource.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <ps3000aApi.h>
#include <PicoStatus.h>
#include <cstring>
#include <cstdio>
#include <cstddef>
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

// ---- Rapid-block live mode tuning ----
// Segments armed per batch: the hardware re-arms in ~1 us between segments,
// so one batch captures up to this many back-to-back triggers.
static constexpr uint32_t kRapidSegments = 64;
// After arming, harvest as soon as all segments are full, or once this much
// time has passed AND at least one segment triggered — keeps the display
// responsive at slow trigger rates without cutting fast batches short.
static constexpr double kHarvestDeadlineMs = 25.0;
// Margin added on top of (sweep duration + auto-trigger delay) before a wait
// is abandoned: stop and recount even if the during-run capture count
// reported nothing (firmware fallback; also bounds Normal-mode waits).
static constexpr double kMaxWaitMs = 250.0;
// At most this many segments are animated per batch (one per UI frame);
// larger batches are skipped through evenly, always ending on the newest.
// Also bounds the capture dead time between batches (~one frame per segment).
static constexpr uint32_t kMaxAnimSegments = 6;

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

    auto apply = [&]() -> PICO_STATUS {
        return ps3000aSetSigGenBuiltIn(
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
    };

    PICO_STATUS status = apply();
    if (status == PICO_BUSY) {
        // A block capture is in flight — stop it and retry. The acquisition
        // state machine restarts cleanly from Idle on the next frame.
        ps3000aStop(m_handle);
        m_acqState = AcqState::Idle;
        status = apply();
    }

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
    auto apply = [&]() -> PICO_STATUS {
        return ps3000aSetSigGenBuiltIn(
            m_handle, 0, 0,
            static_cast<int16_t>(SigGenWave::DC),
            0, 0, 0, 0,
            PS3000A_UP, PS3000A_ES_OFF, 0, 0,
            PS3000A_SIGGEN_RISING, PS3000A_SIGGEN_NONE, 0
        );
    };
    PICO_STATUS status = apply();
    if (status == PICO_BUSY) {
        ps3000aStop(m_handle);
        m_acqState = AcqState::Idle;
        status = apply();
    }
    return status == PICO_OK;
}

void PicoSignalSource::close() {
    if (m_handle > 0) {
        stopRecording(); // flush + finalize the file if a stream is active
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
    // PicoScope 3406D MSO (1 GS/s family) timebase mapping, confirmed
    // against ps3000aGetTimebase2 on hardware (tb 33 -> 248 ns = (33-2)*8):
    //   timebase 0-2:  interval = 2^timebase ns        (1, 2, 4 ns)
    //   timebase >= 3: interval = (timebase - 2) * 8 ns (125 MHz base)
    // Note: configure() verifies the choice with ps3000aGetTimebase2 and the
    // renderer derives on-screen density from the *actual* interval, so a
    // residual mismatch here only changes how closely the record matches the
    // requested window — never the displayed time scale.

    if (desiredSampleIntervalNs <= 1.0f) return 0;      // 1 ns / 1 GSa/s
    if (desiredSampleIntervalNs <= 2.0f) return 1;      // 2 ns / 500 MSa/s
    if (desiredSampleIntervalNs <= 4.0f) return 2;      // 4 ns / 250 MSa/s

    // timebase >= 3: interval = (timebase - 2) * 8 ns, round to nearest
    uint32_t tb = static_cast<uint32_t>(desiredSampleIntervalNs / 8.0f + 0.5f) + 2;
    if (tb < 3) tb = 3;
    return tb;
}

bool PicoSignalSource::configChanged(const ScopeState& state) const {
    if (!m_configured) return true;
    if (state.recordAuto != m_lastConfig.recordAuto) return true;
    if (state.timePerDivIndex != m_lastConfig.timePerDivIndex) return true;
    if (state.effectiveRecordLength() != m_lastConfig.recordLength) return true;
    if (state.trigger.source != m_lastConfig.triggerSource) return true;
    if (state.trigger.level != m_lastConfig.triggerLevel) return true;
    if (static_cast<int>(state.trigger.edge) != m_lastConfig.triggerEdge) return true;
    if (static_cast<int>(state.trigger.mode) != m_lastConfig.triggerMode) return true;
    for (int i = 0; i < NUM_ANALOG_CHANNELS; i++) {
        if (state.analog[i].enabled != m_lastConfig.enabled[i]) return true;
        if (state.analog[i].voltsPerDivIndex != m_lastConfig.voltsPerDivIndex[i]) return true;
        if (static_cast<int>(state.analog[i].coupling) != m_lastConfig.coupling[i]) return true;
        if (state.analog[i].bandwidthLimit != m_lastConfig.bandwidthLimit[i]) return true;
        if (state.analog[i].probeAttenuation != m_lastConfig.probeAttenuation[i]) return true;
        // verticalOffset intentionally not compared — display-layer only.
    }
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++) {
        if (state.digital[i].enabled != m_lastConfig.digitalEnabled[i]) return true;
    }
    if (state.digitalThreshold[0] != m_lastConfig.digitalThreshold[0]) return true;
    if (state.digitalThreshold[1] != m_lastConfig.digitalThreshold[1]) return true;
    if (static_cast<int>(state.trigger.type) != m_lastConfig.triggerType) return true;
    if (state.trigger.digitalSource != m_lastConfig.triggerDigitalSource) return true;
    {
        const ChannelState& tc = state.analog[state.trigger.source];
        float scale = tc.probeAttenuation * (tc.invert ? -1.0f : 1.0f);
        if (scale != m_lastConfig.triggerUserScale) return true;
    }
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++) {
        if (state.trigger.digitalPattern[i] != m_lastConfig.triggerPattern[i]) return true;
    }
    return false;
}

void PicoSignalSource::snapshotConfig(const ScopeState& state) {
    m_lastConfig.recordAuto = state.recordAuto;
    m_lastConfig.timePerDivIndex = state.timePerDivIndex;
    m_lastConfig.recordLength = state.effectiveRecordLength();
    m_lastConfig.triggerSource = state.trigger.source;
    m_lastConfig.triggerLevel = state.trigger.level;
    m_lastConfig.triggerEdge = static_cast<int>(state.trigger.edge);
    m_lastConfig.triggerMode = static_cast<int>(state.trigger.mode);
    for (int i = 0; i < NUM_ANALOG_CHANNELS; i++) {
        m_lastConfig.enabled[i] = state.analog[i].enabled;
        m_lastConfig.voltsPerDivIndex[i] = state.analog[i].voltsPerDivIndex;
        m_lastConfig.coupling[i] = static_cast<int>(state.analog[i].coupling);
        m_lastConfig.bandwidthLimit[i] = state.analog[i].bandwidthLimit;
        m_lastConfig.probeAttenuation[i] = state.analog[i].probeAttenuation;
    }
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++) {
        m_lastConfig.digitalEnabled[i] = state.digital[i].enabled;
    }
    m_lastConfig.digitalThreshold[0] = state.digitalThreshold[0];
    m_lastConfig.digitalThreshold[1] = state.digitalThreshold[1];
    m_lastConfig.triggerType = static_cast<int>(state.trigger.type);
    m_lastConfig.triggerDigitalSource = state.trigger.digitalSource;
    {
        const ChannelState& tc = state.analog[state.trigger.source];
        m_lastConfig.triggerUserScale = tc.probeAttenuation * (tc.invert ? -1.0f : 1.0f);
    }
    for (int i = 0; i < NUM_DIGITAL_CHANNELS; i++) {
        m_lastConfig.triggerPattern[i] = state.trigger.digitalPattern[i];
    }
}

void PicoSignalSource::configure(const ScopeState& state) {
    if (m_handle <= 0) return;
    if (m_recording) return; // device is owned by the streaming recorder

    // Display-layer scaling (probe attenuation / invert) is applied at data
    // retrieval, not in hardware — update it before the early-return so it
    // takes effect without a hardware reconfigure.
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        m_userScale[ch] = state.analog[ch].probeAttenuation *
                          (state.analog[ch].invert ? -1.0f : 1.0f);
    }

    // Skip reconfiguration if nothing changed — avoids stopping active captures
    if (!configChanged(state)) return;

    // If a block is running or mid-retrieval, stop and abandon it before
    // reconfiguring (a partial retrieval must never reach the live buffer).
    if (m_acqState != AcqState::Idle) {
        ps3000aStop(m_handle);
        m_acqState = AcqState::Idle;
    }

    // Configure analog channels
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        PS3000A_CHANNEL psChannel = static_cast<PS3000A_CHANNEL>(PS3000A_CHANNEL_A + ch);
        bool enabled = state.analog[ch].enabled;

        // V/div is a signal-layer value in probe-tip volts; the hardware
        // input range is BNC-side, so divide by the probe attenuation.
        // This keeps full ADC resolution with a 10x probe (matches
        // PicoScope 6 behavior). retrieveData converts back via
        // rangeToVolts(range) * m_userScale, so the round trip is exact.
        float probe = state.analog[ch].probeAttenuation;
        if (probe <= 0.0f) probe = 1.0f;
        PS3000A_RANGE range = voltsDivToRange(state.analog[ch].voltsPerDiv() / probe);
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

    // Per-port logic threshold: convert volts to ADC counts (full scale +/-5V).
    auto thresholdCounts = [](float volts) -> int16_t {
        float clamped = volts;
        if (clamped > 5.0f) clamped = 5.0f;
        if (clamped < -5.0f) clamped = -5.0f;
        return static_cast<int16_t>(clamped / 5.0f * 32767.0f);
    };
    ps3000aSetDigitalPort(m_handle, PS3000A_DIGITAL_PORT0,
                          m_port0Enabled ? 1 : 0, thresholdCounts(state.digitalThreshold[0]));
    ps3000aSetDigitalPort(m_handle, PS3000A_DIGITAL_PORT1,
                          m_port1Enabled ? 1 : 0, thresholdCounts(state.digitalThreshold[1]));

    // Compute timebase: the record covers the full acquisition span
    // (multiple display windows, trigger centered) so the view can pan
    // through captured data.
    int numSamples = state.effectiveRecordLength();
    float sampleIntervalSec = state.acquisitionSpan() / numSamples;
    float sampleIntervalNs = sampleIntervalSec * 1.0e9f;
    m_timebase = computeTimebase(sampleIntervalNs);
    m_numSamples = numSamples;

    // Memory segmentation. Auto record mode uses rapid block: split capture
    // memory into segments so one RunBlock collects a whole batch of
    // triggered sweeps back-to-back. Fixed (deep) record mode keeps a single
    // segment and the classic one-block-at-a-time path.
    m_rapid = state.recordAuto;
    uint32_t nSeg = 1;
    if (m_rapid) {
        int activeBufs = 0;
        for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++)
            if (m_currentEnabled[ch]) activeBufs++;
        if (m_port0Enabled) activeBufs++;
        if (m_port1Enabled) activeBufs++;
        if (activeBufs < 1) activeBufs = 1;

        uint32_t maxSeg = 0;
        if (ps3000aGetMaxSegments(m_handle, &maxSeg) != PICO_OK || maxSeg == 0)
            maxSeg = 1;
        nSeg = (kRapidSegments < maxSeg) ? kRapidSegments : maxSeg;

        // Shrink until each segment holds the full record. nMaxSamples is
        // shared across active channels/ports, so budget conservatively.
        int64_t need = static_cast<int64_t>(m_numSamples) * activeBufs;
        while (nSeg > 1) {
            int32_t segSamples = 0;
            if (ps3000aMemorySegments(m_handle, nSeg, &segSamples) == PICO_OK &&
                segSamples >= need)
                break;
            nSeg /= 2;
        }
    }
    if (nSeg <= 1) {
        nSeg = 1;
        int32_t segSamples = 0;
        ps3000aMemorySegments(m_handle, 1, &segSamples);
    }
    ps3000aSetNoOfCaptures(m_handle, nSeg);
    m_nSegments = nSeg;

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

    // Wall-clock duration of one sweep (the record fills in real time on
    // both sides of the trigger). The rapid-mode harvest timeout scales
    // with this — see acquireRapid. Fall back to the timebase formula if
    // the driver never returned an interval.
    if (actualIntervalNs <= 0.0f)
        actualIntervalNs = (m_timebase <= 2)
            ? static_cast<float>(1u << m_timebase)
            : static_cast<float>(m_timebase - 2) * 8.0f;
    m_sweepSec = static_cast<float>(m_numSamples) * actualIntervalNs * 1.0e-9f;

    // Set trigger
    int16_t autoTrigMs = (state.trigger.mode == TriggerMode::Auto) ? 100 : 0;
    m_autoTrigMs = autoTrigMs;

    if (state.trigger.type == TriggerType::Edge) {
        PS3000A_CHANNEL trigSource = static_cast<PS3000A_CHANNEL>(
            PS3000A_CHANNEL_A + state.trigger.source);

        // trigger.level is a signal-layer value in probe-tip volts. The ADC
        // compares BNC-side volts, so convert through the channel's display
        // scale (probe attenuation / invert). An inverted channel flips both
        // the threshold sign and the edge direction.
        float userScale = m_userScale[state.trigger.source];
        if (userScale == 0.0f) userScale = 1.0f;
        float bncLevel = state.trigger.level / userScale;

        bool rising = (state.trigger.edge == TriggerEdge::Rising);
        if (userScale < 0.0f) rising = !rising;
        PS3000A_THRESHOLD_DIRECTION trigDir = rising ? PS3000A_RISING : PS3000A_FALLING;

        // No hardware analog offset is applied, so the BNC-level maps
        // directly to ADC counts via the channel range.
        float trigRange = rangeToVolts(m_currentRange[state.trigger.source]);
        int16_t trigThreshold = static_cast<int16_t>(
            (bncLevel / trigRange) * m_maxADC);

        PICO_STATUS trigStatus = ps3000aSetSimpleTrigger(
            m_handle, 1, trigSource, trigThreshold, trigDir, 0, autoTrigMs);
        if (trigStatus != PICO_OK) {
            char buf[128];
            snprintf(buf, sizeof(buf), "SetSimpleTrigger failed: 0x%08X",
                     static_cast<unsigned>(trigStatus));
            m_lastError = buf;
        }
    } else {
        // Digital-edge or pattern trigger (MSO). Built from the SDK headers;
        // verify on hardware. Clear any analog simple trigger first.
        ps3000aSetSimpleTrigger(m_handle, 0, PS3000A_CHANNEL_A, 0, PS3000A_RISING, 0, autoTrigMs);

        PS3000A_DIGITAL_CHANNEL_DIRECTIONS dirs[NUM_DIGITAL_CHANNELS];
        int16_t nd = 0;
        if (state.trigger.type == TriggerType::Digital) {
            dirs[nd].channel = static_cast<PS3000A_DIGITAL_CHANNEL>(
                PS3000A_DIGITAL_CHANNEL_0 + state.trigger.digitalSource);
            dirs[nd].direction = (state.trigger.edge == TriggerEdge::Rising)
                ? PS3000A_DIGITAL_DIRECTION_RISING : PS3000A_DIGITAL_DIRECTION_FALLING;
            nd++;
        } else { // Pattern
            for (int lane = 0; lane < NUM_DIGITAL_CHANNELS; lane++) {
                int cond = state.trigger.digitalPattern[lane];
                if (cond == 0) continue; // don't care
                dirs[nd].channel = static_cast<PS3000A_DIGITAL_CHANNEL>(
                    PS3000A_DIGITAL_CHANNEL_0 + lane);
                dirs[nd].direction = (cond == 1)
                    ? PS3000A_DIGITAL_DIRECTION_HIGH : PS3000A_DIGITAL_DIRECTION_LOW;
                nd++;
            }
        }

        if (nd > 0) {
            ps3000aSetTriggerDigitalPortProperties(m_handle, dirs, nd);
            PS3000A_TRIGGER_CONDITIONS_V2 cond{};
            cond.channelA = cond.channelB = cond.channelC = cond.channelD =
                PS3000A_CONDITION_DONT_CARE;
            cond.external = cond.aux = cond.pulseWidthQualifier = PS3000A_CONDITION_DONT_CARE;
            cond.digital = PS3000A_CONDITION_TRUE;
            ps3000aSetTriggerChannelConditionsV2(m_handle, &cond, 1);
        }
    }

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

void PicoSignalSource::registerBuffers(uint32_t segmentIndex) {
    // (Re-)register data buffers — the driver may invalidate buffer
    // registrations after Stop on some firmware versions, and in rapid mode
    // the registration selects which memory segment GetValues reads from.
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        if (m_currentEnabled[ch]) {
            ps3000aSetDataBuffer(m_handle,
                static_cast<PS3000A_CHANNEL>(PS3000A_CHANNEL_A + ch),
                m_adcBuffers[ch].data(), m_numSamples, segmentIndex,
                PS3000A_RATIO_MODE_NONE);
        }
    }
    if (m_port0Enabled) {
        ps3000aSetDataBuffer(m_handle,
            static_cast<PS3000A_CHANNEL>(PS3000A_DIGITAL_PORT0),
            m_digitalBuffers[0].data(), m_numSamples, segmentIndex,
            PS3000A_RATIO_MODE_NONE);
    }
    if (m_port1Enabled) {
        ps3000aSetDataBuffer(m_handle,
            static_cast<PS3000A_CHANNEL>(PS3000A_DIGITAL_PORT1),
            m_digitalBuffers[1].data(), m_numSamples, segmentIndex,
            PS3000A_RATIO_MODE_NONE);
    }
}

void PicoSignalSource::acquire(SignalData& data) {
    if (m_handle <= 0 || !m_configured) return;
    if (m_recording) return; // streaming recorder owns the device

    if (m_rapid) {
        acquireRapid(data);
        return;
    }

    // Classic single-block path (fixed/deep record lengths).
    // Non-blocking state machine:
    // Idle -> stop (safety) -> re-register buffers -> RunBlock -> Running
    // Running -> poll IsReady -> if ready -> GetValues -> Idle
    // If not ready, return immediately (UI shows previous data)

    if (m_acqState == AcqState::Idle) {
        // Ensure device is stopped before starting a new block
        ps3000aStop(m_handle);

        registerBuffers(0);

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
            m_acqState = beginRetrieve() ? AcqState::Retrieving : AcqState::Idle;
        }
    }

    if (m_acqState == AcqState::Retrieving) {
        // One bounded slice per frame — the UI stays responsive while a
        // large block streams in over USB.
        if (retrieveChunk(data))
            m_acqState = AcqState::Idle;
    }
}

bool PicoSignalSource::beginRetrieve() {
    // Actual sample rate from the driver (ground truth for the display).
    float intervalNs = 0;
    ps3000aGetTimebase2(m_handle, m_timebase, m_numSamples, &intervalNs, 0, nullptr, 0);
    m_pendingSampleRate = (intervalNs > 0) ? (1.0e9f / intervalNs) : 0.0f;

    // Size the staging buffers (enabled channels + digital only).
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        if (m_currentEnabled[ch]) {
            m_staging.analog[ch].samples.resize(m_numSamples);
            m_staging.analog[ch].count = m_numSamples;
        } else {
            m_staging.analog[ch].samples.clear();
            m_staging.analog[ch].count = 0;
        }
    }
    if (m_port0Enabled || m_port1Enabled) {
        m_staging.digital.samples.resize(m_numSamples);
        m_staging.digital.count = m_numSamples;
    } else {
        m_staging.digital.samples.clear();
        m_staging.digital.count = 0;
    }

    m_retrieveOffset = 0;
    return true;
}

bool PicoSignalSource::retrieveChunk(SignalData& data) {
    // Per-frame slice size: bounds USB transfer + conversion cost so the UI
    // stays responsive regardless of total record length.
    constexpr int kChunk = 4000000;

    int remaining = m_numSamples - m_retrieveOffset;
    int chunk = (remaining < kChunk) ? remaining : kChunk;

    if (chunk > 0) {
        uint32_t n = static_cast<uint32_t>(chunk);
        int16_t overflow = 0;
        // startIndex selects the range within the (full-record) registered
        // buffers; the driver fills the same offsets in our ADC buffers.
        PICO_STATUS status = ps3000aGetValues(
            m_handle, static_cast<uint32_t>(m_retrieveOffset), &n, 1,
            PS3000A_RATIO_MODE_NONE, 0, &overflow);
        if (status != PICO_OK) {
            char buf[128];
            snprintf(buf, sizeof(buf), "ps3000aGetValues failed: 0x%08X",
                     static_cast<unsigned>(status));
            m_lastError = buf;
            return true; // abort retrieval; keep previous live data intact
        }
        int got = static_cast<int>(n);
        if (got <= 0) return true;
        if (m_retrieveOffset + got > m_numSamples)
            got = m_numSamples - m_retrieveOffset;

        convertRange(m_retrieveOffset, got);

        m_retrieveOffset += got;
        if (m_retrieveOffset < m_numSamples)
            return false; // more slices to go; live data still shows the
                          // previous complete capture
    }

    publishStaging(data);
    return true;
}

void PicoSignalSource::convertRange(int offset, int count) {
    // Convert a slice: ADC counts -> probe-tip volts. Hardware analog
    // offset is not used (see configure()); probe attenuation and invert
    // are folded in here — exactly once per acquisition.
    float maxADCf = static_cast<float>(m_maxADC);
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        if (!m_currentEnabled[ch]) continue;
        float scale = (rangeToVolts(m_currentRange[ch]) / maxADCf) * m_userScale[ch];
        const int16_t* raw = m_adcBuffers[ch].data() + offset;
        float* out = m_staging.analog[ch].samples.data() + offset;
        for (int i = 0; i < count; i++)
            out[i] = static_cast<float>(raw[i]) * scale;
    }
    if (m_staging.digital.count > 0) {
        uint16_t* dst = m_staging.digital.samples.data() + offset;
        for (int i = 0; i < count; i++) {
            uint16_t bits = 0;
            int idx = offset + i;
            if (m_port0Enabled)
                bits |= static_cast<uint16_t>(m_digitalBuffers[0][idx] & 0xFF);
            if (m_port1Enabled)
                bits |= static_cast<uint16_t>((m_digitalBuffers[1][idx] & 0xFF) << 8);
            dst[i] = bits;
        }
    }
}

void PicoSignalSource::publishStaging(SignalData& data) {
    // Publish atomically by swapping staging into the live data.
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        if (m_currentEnabled[ch]) {
            data.analog[ch].samples.swap(m_staging.analog[ch].samples);
            data.analog[ch].count = m_numSamples;
        } else {
            data.analog[ch].samples.clear();
            data.analog[ch].count = 0;
        }
    }
    if (m_staging.digital.count > 0) {
        data.digital.samples.swap(m_staging.digital.samples);
        data.digital.count = m_numSamples;
    } else {
        data.digital.samples.clear();
        data.digital.count = 0;
    }
    data.sampleRate = m_pendingSampleRate;

    m_hasData = true;
    m_lastError.clear();
}

// ---------------------------------------------------------------------------
// Rapid-block live mode
// ---------------------------------------------------------------------------
// One RunBlock arms m_nSegments triggered captures; the hardware re-arms in
// ~1 us between segments, so a batch catches essentially every trigger while
// it runs. Each UI frame then animates one captured segment onto the screen
// (skipping evenly when a batch outruns the animation budget), and the next
// batch is armed as soon as the current one is drained.

void PicoSignalSource::armRapid() {
    ps3000aStop(m_handle);
    ps3000aSetNoOfCaptures(m_handle, m_nSegments);

    int32_t preTrig = m_numSamples / 2;   // trigger at record center: one
    int32_t postTrig = m_numSamples - preTrig; // full window each side of the
                                          // active window (3-window span)
    int32_t timeIndisposed = 0;
    PICO_STATUS status = ps3000aRunBlock(m_handle, preTrig, postTrig,
                                          m_timebase, 0, &timeIndisposed,
                                          0, nullptr, nullptr);
    if (status != PICO_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "ps3000aRunBlock (rapid) failed: 0x%08X",
                 static_cast<unsigned>(status));
        m_lastError = buf;
        if (status == PICO_INVALID_HANDLE || status == PICO_NOT_RESPONDING) {
            m_handle = 0;
            m_configured = false;
        }
        m_acqState = AcqState::Idle;
        return;
    }
    m_armTime = std::chrono::steady_clock::now();
    m_acqState = AcqState::RapidRunning;
}

void PicoSignalSource::acquireRapid(SignalData& data) {
    if (m_acqState != AcqState::RapidRunning &&
        m_acqState != AcqState::RapidRetrieving) {
        armRapid(); // freshly armed (or driver error); poll next frame
        return;
    }

    if (m_acqState == AcqState::RapidRunning) {
        int16_t ready = 0;
        PICO_STATUS status = ps3000aIsReady(m_handle, &ready);
        if (status != PICO_OK) {
            char buf[128];
            snprintf(buf, sizeof(buf), "ps3000aIsReady failed: 0x%08X",
                     static_cast<unsigned>(status));
            m_lastError = buf;
            m_acqState = AcqState::Idle;
            return;
        }

        double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - m_armTime).count();

        // A segment cannot complete faster than one full sweep, preceded by
        // up to the auto-trigger delay, so the hard cap must scale with the
        // sweep duration. A fixed cap starves slow timebases: the batch is
        // stopped before any segment can fill, recounted as 0 captures, and
        // re-armed forever — no data is ever published.
        double maxWaitMs = kMaxWaitMs + m_autoTrigMs + m_sweepSec * 1000.0;

        uint32_t captured = 0;
        if (ready) {
            captured = m_nSegments; // full batch
        } else if (elapsedMs < maxWaitMs) {
            if (elapsedMs < kHarvestDeadlineMs)
                return; // give the batch time to accumulate triggers
            // Completed-so-far count (callable during a run). Nothing yet ->
            // keep waiting: stopping now would also reset the auto-trigger
            // timer and starve the display in Auto mode with no signal.
            if (ps3000aGetNoOfCaptures(m_handle, &captured) != PICO_OK)
                captured = 0;
            if (captured == 0)
                return;
        }
        // else: hard timeout — stop and recount below (robust even if the
        // during-run count is unsupported; bounds Normal-mode waits).

        ps3000aStop(m_handle);
        if (!ready) {
            uint32_t confirmed = 0;
            if (ps3000aGetNoOfCaptures(m_handle, &confirmed) == PICO_OK)
                captured = confirmed;
        }
        if (captured == 0) {
            armRapid(); // timed out with nothing triggered — re-arm
            return;
        }
        if (captured > m_nSegments) captured = m_nSegments;

        // Animation plan: at most kMaxAnimSegments per batch, evenly spaced
        // through the batch, always ending on the newest capture.
        uint32_t k = (captured < kMaxAnimSegments) ? captured : kMaxAnimSegments;
        m_rapidPlan.clear();
        for (uint32_t i = 0; i < k; i++)
            m_rapidPlan.push_back((i + 1) * captured / k - 1);
        m_rapidPlanPos = 0;
        m_acqState = AcqState::RapidRetrieving;
        // Fall through: show the first segment this same frame.
    }

    if (m_acqState == AcqState::RapidRetrieving) {
        if (!retrieveSegment(m_rapidPlan[m_rapidPlanPos], data)) {
            armRapid(); // driver error: drop the batch, keep previous display
            return;
        }
        if (++m_rapidPlanPos >= m_rapidPlan.size())
            armRapid(); // batch drained — start capturing the next one
    }
}

bool PicoSignalSource::retrieveSegment(uint32_t segmentIndex, SignalData& data) {
    registerBuffers(segmentIndex);

    uint32_t n = static_cast<uint32_t>(m_numSamples);
    int16_t overflow = 0;
    PICO_STATUS status = ps3000aGetValues(m_handle, 0, &n, 1,
                                          PS3000A_RATIO_MODE_NONE,
                                          segmentIndex, &overflow);
    if (status != PICO_OK || n == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "ps3000aGetValues (seg %u) failed: 0x%08X",
                 segmentIndex, static_cast<unsigned>(status));
        m_lastError = buf;
        return false;
    }

    // Records in rapid mode are small (<= 300 k samples), so one segment is
    // converted and published whole within a single frame.
    beginRetrieve(); // size staging + refresh actual sample rate
    int got = (static_cast<int>(n) < m_numSamples) ? static_cast<int>(n)
                                                   : m_numSamples;
    convertRange(0, got);
    publishStaging(data);
    return true;
}

// ---------------------------------------------------------------------------
// Streaming recorder
// ---------------------------------------------------------------------------

// Binary file layout: 64-byte header, then interleaved int16 ADC frames
// (one sample per recorded channel, in ascending channel order). Multiply by
// scale[ch] to get probe-tip volts. A .json sidecar with the same metadata is
// written on stop.
#pragma pack(push, 1)
struct RecFileHeader {
    char magic[8];       // "PSRECv1\0"
    uint32_t version;    // 1
    uint32_t numChannels;
    uint32_t channelMask;
    float sampleRateHz;  // actual
    float scale[4];      // volts per LSB per recorded channel (file order)
    uint64_t sampleCount; // per channel; patched on stop
    uint8_t reserved[16];
};
#pragma pack(pop)
static_assert(sizeof(RecFileHeader) == 64, "recording header must be 64 bytes");

bool PicoSignalSource::startRecording(const std::string& path, float requestedRateHz,
                                      uint16_t channelMask, float viewWindowSec,
                                      std::string& errOut) {
    if (m_handle <= 0) { errOut = "no PicoScope connected"; return false; }
    if (m_recording) { errOut = "already recording"; return false; }
    if (requestedRateHz <= 0.0f) { errOut = "invalid sample rate"; return false; }

    // Resolve channels: mask 0 = all currently enabled channels.
    m_recNumCh = 0;
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        bool want = channelMask ? ((channelMask >> ch) & 1) : m_currentEnabled[ch];
        if (!want) continue;
        if (!m_currentEnabled[ch]) {
            errOut = std::string("channel ") + static_cast<char>('A' + ch) +
                     " not enabled";
            return false;
        }
        m_recChannels[m_recNumCh++] = ch;
    }
    if (m_recNumCh == 0) { errOut = "no channels to record"; return false; }

    // Take the device from block mode. Undo any rapid-block segmentation so
    // streaming gets the whole capture memory as its FIFO (stopRecording
    // forces a full reconfigure, which restores segments on resume).
    ps3000aStop(m_handle);
    m_acqState = AcqState::Idle;
    int32_t segSamples = 0;
    ps3000aMemorySegments(m_handle, 1, &segSamples);
    ps3000aSetNoOfCaptures(m_handle, 1);

    // Register streaming buffers (driver ring segments).
    constexpr int kStreamBuf = 1000000;
    for (int k = 0; k < m_recNumCh; k++) {
        int ch = m_recChannels[k];
        m_adcBuffers[ch].resize(kStreamBuf);
        ps3000aSetDataBuffer(m_handle,
            static_cast<PS3000A_CHANNEL>(PS3000A_CHANNEL_A + ch),
            m_adcBuffers[ch].data(), kStreamBuf, 0, PS3000A_RATIO_MODE_NONE);
    }
    // Digital ports are not recorded in v1: disable during the stream.
    ps3000aSetDigitalPort(m_handle, PS3000A_DIGITAL_PORT0, 0, 0);
    ps3000aSetDigitalPort(m_handle, PS3000A_DIGITAL_PORT1, 0, 0);

    // Sample interval (driver adjusts to what it can honor).
    uint32_t interval;
    PS3000A_TIME_UNITS units;
    if (requestedRateHz >= 1.0e6f) {
        interval = static_cast<uint32_t>(1.0e9 / requestedRateHz + 0.5);
        units = PS3000A_NS;
    } else {
        interval = static_cast<uint32_t>(1.0e6 / requestedRateHz + 0.5);
        units = PS3000A_US;
    }

    PICO_STATUS status = ps3000aRunStreaming(m_handle, &interval, units,
                                             0, kStreamBuf, 0 /* no autoStop */,
                                             1, PS3000A_RATIO_MODE_NONE, kStreamBuf);
    if (status != PICO_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "ps3000aRunStreaming failed: 0x%08X",
                 static_cast<unsigned>(status));
        errOut = buf;
        m_configured = false; // force full block-mode reconfigure on resume
        return false;
    }
    double intervalSec = (units == PS3000A_NS) ? interval * 1.0e-9 : interval * 1.0e-6;
    m_recActualRate = static_cast<float>(1.0 / intervalSec);

    // Per-channel volts-per-LSB (probe-tip volts, consistent with capture).
    for (int k = 0; k < m_recNumCh; k++) {
        int ch = m_recChannels[k];
        m_recScale[k] = (rangeToVolts(m_currentRange[ch]) / m_maxADC) * m_userScale[ch];
    }

    // Open the output file with a placeholder header.
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "wb");
    if (!f) {
        ps3000aStop(m_handle);
        m_configured = false;
        errOut = "cannot open file: " + path;
        return false;
    }
    setvbuf(f, nullptr, _IOFBF, 1 << 20);
    RecFileHeader hdr{};
    memcpy(hdr.magic, "PSRECv1", 8);
    hdr.version = 1;
    hdr.numChannels = static_cast<uint32_t>(m_recNumCh);
    uint32_t mask = 0;
    for (int k = 0; k < m_recNumCh; k++) mask |= (1u << m_recChannels[k]);
    hdr.channelMask = mask;
    hdr.sampleRateHz = m_recActualRate;
    for (int k = 0; k < m_recNumCh && k < 4; k++) hdr.scale[k] = m_recScale[k];
    hdr.sampleCount = 0;
    fwrite(&hdr, sizeof(hdr), 1, f);

    // Rolling live-view ring sized to the current display window.
    double rollN = static_cast<double>(viewWindowSec) * m_recActualRate;
    m_rollSize = static_cast<int>(std::clamp(rollN, 1000.0, 2000000.0));
    for (int k = 0; k < m_recNumCh; k++)
        m_rollBuf[m_recChannels[k]].assign(m_rollSize, 0.0f);
    m_rollPos = 0;
    m_rollFull = false;
    m_rollDirty = false;

    m_recFile = f;
    m_recPath = path;
    m_recMask = static_cast<uint16_t>(mask);
    m_recSamples = 0;
    m_recOverflows = 0;
    m_recording = true;
    m_lastError.clear();
    return true;
}

void __stdcall PicoSignalSource::streamingCallback(int16_t, int32_t noOfSamples,
                                                   uint32_t startIndex, int16_t overflow,
                                                   uint32_t, int16_t, int16_t,
                                                   void* pParameter) {
    static_cast<PicoSignalSource*>(pParameter)
        ->onStreamingData(noOfSamples, startIndex, overflow);
}

void PicoSignalSource::onStreamingData(int32_t noOfSamples, uint32_t startIndex,
                                       int16_t overflow) {
    if (noOfSamples <= 0 || !m_recFile) return;
    if (overflow) m_recOverflows++;

    // Interleave the new samples and append to disk.
    size_t frames = static_cast<size_t>(noOfSamples);
    m_recScratch.resize(frames * m_recNumCh);
    for (int k = 0; k < m_recNumCh; k++) {
        const int16_t* src = m_adcBuffers[m_recChannels[k]].data() + startIndex;
        int16_t* dst = m_recScratch.data() + k;
        for (size_t i = 0; i < frames; i++)
            dst[i * m_recNumCh] = src[i];
    }
    fwrite(m_recScratch.data(), sizeof(int16_t), m_recScratch.size(),
           static_cast<FILE*>(m_recFile));
    m_recSamples += frames;

    // Feed the rolling live-view ring (converted to volts).
    for (int k = 0; k < m_recNumCh; k++) {
        int ch = m_recChannels[k];
        const int16_t* src = m_adcBuffers[ch].data() + startIndex;
        float scale = m_recScale[k];
        std::vector<float>& ring = m_rollBuf[ch];
        int pos = m_rollPos;
        for (size_t i = 0; i < frames; i++) {
            ring[pos] = src[i] * scale;
            if (++pos >= m_rollSize) { pos = 0; m_rollFull = true; }
        }
        if (k == m_recNumCh - 1) m_rollPos = pos;
    }
    m_rollDirty = true;
}

void PicoSignalSource::serviceRecording(SignalData& data) {
    if (!m_recording || m_handle <= 0) return;

    PICO_STATUS status = ps3000aGetStreamingLatestValues(
        m_handle, &PicoSignalSource::streamingCallback, this);
    if (status != PICO_OK && status != PICO_BUSY) {
        char buf[128];
        snprintf(buf, sizeof(buf), "GetStreamingLatestValues failed: 0x%08X",
                 static_cast<unsigned>(status));
        m_lastError = buf;
    }

    // Publish the rolling view to the live display (oldest -> newest).
    if (m_rollDirty) {
        int count = m_rollFull ? m_rollSize : m_rollPos;
        for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
            bool recorded = (m_recMask >> ch) & 1;
            if (!recorded || count < 2) {
                data.analog[ch].samples.clear();
                data.analog[ch].count = 0;
                continue;
            }
            std::vector<float>& ring = m_rollBuf[ch];
            AnalogBuffer& out = data.analog[ch];
            out.samples.resize(count);
            if (m_rollFull) {
                int tail = m_rollSize - m_rollPos;
                memcpy(out.samples.data(), ring.data() + m_rollPos,
                       tail * sizeof(float));
                memcpy(out.samples.data() + tail, ring.data(),
                       m_rollPos * sizeof(float));
            } else {
                memcpy(out.samples.data(), ring.data(), count * sizeof(float));
            }
            out.count = count;
        }
        data.digital.samples.clear();
        data.digital.count = 0;
        data.sampleRate = m_recActualRate;
        m_rollDirty = false;
    }
}

void PicoSignalSource::stopRecording() {
    if (!m_recording) return;

    ps3000aStop(m_handle);

    FILE* f = static_cast<FILE*>(m_recFile);
    if (f) {
        // Patch the per-channel sample count into the header.
        fflush(f);
        fseek(f, offsetof(RecFileHeader, sampleCount), SEEK_SET);
        fwrite(&m_recSamples, sizeof(m_recSamples), 1, f);
        fclose(f);
    }
    m_recFile = nullptr;

    // Metadata sidecar for easy tooling.
    FILE* j = nullptr;
    fopen_s(&j, (m_recPath + ".json").c_str(), "w");
    if (j) {
        fprintf(j, "{\"file\":\"%s\",\"format\":\"int16 interleaved, 64-byte header\","
                   "\"channels\":[", m_recPath.c_str());
        for (int k = 0; k < m_recNumCh; k++)
            fprintf(j, "%s\"%c\"", k ? "," : "", 'A' + m_recChannels[k]);
        fprintf(j, "],\"sample_rate_hz\":%.6g,\"scale_v_per_lsb\":[", m_recActualRate);
        for (int k = 0; k < m_recNumCh; k++)
            fprintf(j, "%s%.9g", k ? "," : "", m_recScale[k]);
        fprintf(j, "],\"samples_per_channel\":%llu,\"overflow_events\":%d}\n",
                static_cast<unsigned long long>(m_recSamples), m_recOverflows);
        fclose(j);
    }

    m_recording = false;
    m_configured = false; // force a full block-mode reconfigure on resume
    m_acqState = AcqState::Idle;
}

PicoSignalSource::RecordingStatus PicoSignalSource::recordingStatus() const {
    RecordingStatus s;
    s.active = m_recording;
    s.samples = m_recSamples;
    s.bytes = sizeof(RecFileHeader) + m_recSamples * 2ull * m_recNumCh;
    s.seconds = (m_recActualRate > 0) ? m_recSamples / static_cast<double>(m_recActualRate) : 0.0;
    s.actualRateHz = m_recActualRate;
    s.overflowEvents = m_recOverflows;
    s.file = m_recPath;
    return s;
}
