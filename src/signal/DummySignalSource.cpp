#include "signal/DummySignalSource.h"
#include <cmath>
#include <cstdlib>
#include <vector>
#include <array>
#include <utility>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
// Wrap x into [0, m).
double wrapPos(double x, double m) {
    double r = std::fmod(x, m);
    if (r < 0) r += m;
    return r;
}

// UART 8N1, LSB first, idle high. One entry per bit period; sends "Hi".
const std::vector<int>& uartPattern() {
    static const std::vector<int> p = [] {
        std::vector<int> v;
        v.push_back(1); v.push_back(1); // idle
        auto byte = [&](int by) {
            v.push_back(0); // start
            for (int b = 0; b < 8; b++) v.push_back((by >> b) & 1); // LSB first
            v.push_back(1); v.push_back(1); // stop + idle
        };
        byte(0x48); byte(0x69); // 'H', 'i'
        return v;
    }();
    return p;
}

// I2C (scl,sda) at half-bit tick: START, addr 0x50 W + ACK, data 0xA5 + ACK, STOP.
const std::vector<std::pair<int, int>>& i2cPattern() {
    static const std::vector<std::pair<int, int>> v = [] {
        std::vector<std::pair<int, int>> t;
        auto idle = [&](int n) { for (int k = 0; k < n; k++) t.push_back({ 1, 1 }); };
        auto cell = [&](int sda) { t.push_back({ 0, sda }); t.push_back({ 1, sda }); };
        auto byte = [&](int by, int ack) {
            for (int b = 7; b >= 0; b--) cell((by >> b) & 1); // MSB first
            cell(ack); // 0 = ACK
        };
        idle(2);
        t.push_back({ 1, 1 }); t.push_back({ 1, 0 }); // START
        byte(0xA0, 0); // (0x50 << 1) | W
        byte(0xA5, 0); // data
        t.push_back({ 0, 0 }); t.push_back({ 1, 0 }); t.push_back({ 1, 1 }); // STOP
        idle(2);
        return t;
    }();
    return v;
}

// SPI mode 0 (clk,mosi,cs) at half-clock tick, CS active low. Bytes 0x3C, 0xF0.
const std::vector<std::array<int, 3>>& spiPattern() {
    static const std::vector<std::array<int, 3>> v = [] {
        std::vector<std::array<int, 3>> t;
        auto idle = [&](int n) { for (int k = 0; k < n; k++) t.push_back({ 0, 0, 1 }); };
        auto byte = [&](int by) {
            for (int b = 7; b >= 0; b--) { // MSB first
                int m = (by >> b) & 1;
                t.push_back({ 0, m, 0 }); t.push_back({ 1, m, 0 });
            }
        };
        idle(2);
        byte(0x3C); byte(0xF0);
        t.push_back({ 0, 0, 1 }); // deassert CS
        idle(2);
        return t;
    }();
    return v;
}
} // namespace

void DummySignalSource::configure(const ScopeState& state) {
    m_timePerDiv = state.timePerDiv();
    m_visibleTime = m_timePerDiv * GRID_DIVISIONS_X;
    // Demo cap: the dummy source regenerates every sample each frame in
    // software, so large records stall the UI (sin() per sample per channel
    // per frame). Real hardware fills its own 512 MS buffer and is not
    // subject to this cap. Auto (live) mode uses a tighter cap since demo
    // fidelity doesn't need 1 M points.
    int n = state.effectiveRecordLength();
    int cap = state.recordAuto ? 400000 : 2000000;
    m_recordLength = (n > cap) ? cap : n;
    // The record covers the full acquisition span (trigger centered); the
    // horizontal offset is applied by the renderer's view mapping, NOT baked
    // into the generated data (that would double-pan).
    m_totalTime = state.acquisitionSpan();
    m_sampleRate = m_recordLength / m_totalTime;
    m_triggerLevel = state.trigger.level;
    m_triggerSource = state.trigger.source;
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        m_userScale[ch] = state.analog[ch].probeAttenuation *
                          (state.analog[ch].invert ? -1.0f : 1.0f);
    }
}

void DummySignalSource::acquire(SignalData& data) {
    data.resize(m_recordLength);
    data.sampleRate = m_sampleRate;

    float dt = 1.0f / m_sampleRate;

    // Advance phase for animation (simulate continuous acquisition)
    m_phase += 0.02f;
    if (m_phase > 2.0f * static_cast<float>(M_PI))
        m_phase -= 2.0f * static_cast<float>(M_PI);

    // Time origin: samples span [t_start, t_start + totalTime], with the
    // trigger point (t=0) at the record center. View panning happens in the
    // renderer, never here.
    float t_start = -m_totalTime * 0.5f;

    // Frequencies based on visible time window for consistent cycle count
    float baseTime = m_visibleTime;

    // CH1: Sine wave — ~2.5 cycles visible
    {
        float freq = 2.5f / baseTime;
        for (int i = 0; i < m_recordLength; i++) {
            float t = t_start + i * dt;
            data.analog[0].samples[i] = 2.0f * sinf(2.0f * static_cast<float>(M_PI) * freq * t + m_phase);
        }
    }

    // CH2: Square wave at slightly different frequency
    {
        float freq = 1.5f / baseTime;
        for (int i = 0; i < m_recordLength; i++) {
            float t = t_start + i * dt;
            float phase = fmodf(freq * t + m_phase * 0.7f, 1.0f);
            if (phase < 0.0f) phase += 1.0f;
            data.analog[1].samples[i] = (phase < 0.5f) ? 1.5f : -1.5f;
        }
    }

    // CH3: Triangle wave
    {
        float freq = 3.0f / baseTime;
        for (int i = 0; i < m_recordLength; i++) {
            float t = t_start + i * dt;
            float phase = fmodf(freq * t + m_phase * 0.5f, 1.0f);
            if (phase < 0.0f) phase += 1.0f;
            data.analog[2].samples[i] = 2.0f * (2.0f * fabsf(phase - 0.5f) - 0.5f);
        }
    }

    // CH4: Sine + noise (simulating a noisy signal)
    {
        float freq = 2.0f / baseTime;
        for (int i = 0; i < m_recordLength; i++) {
            float t = t_start + i * dt;
            float signal = 1.5f * sinf(2.0f * static_cast<float>(M_PI) * freq * t + m_phase * 1.3f);
            float noise = 0.3f * (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f);
            data.analog[3].samples[i] = signal + noise;
        }
    }

    // Apply display-layer scale (probe attenuation / invert). The buffer is
    // fully regenerated above, so this runs exactly once per acquisition.
    for (int ch = 0; ch < NUM_ANALOG_CHANNELS; ch++) {
        if (m_userScale[ch] == 1.0f) continue;
        float* s = data.analog[ch].samples.data();
        for (int i = 0; i < m_recordLength; i++)
            s[i] *= m_userScale[ch];
    }

    // Digital channels:
    //   D0        UART  (9600 8N1, "Hi")
    //   D1,D2     I2C   (SCL, SDA; 100 kHz; addr 0x50 W, data 0xA5)
    //   D3,D4,D5  SPI   (CLK, MOSI, CS; mode 0; bytes 0x3C, 0xF0)
    //   D6-D15    binary counter patterns (as visual filler)
    {
        const double UART_BAUD = 9600.0;
        const double I2C_TICK = 5.0e-6; // 100 kHz SCL (2 ticks/bit)
        const double SPI_TICK = 5.0e-6; // 100 kHz CLK (resolvable at typical Sa/s)

        const auto& up = uartPattern();
        const auto& ip = i2cPattern();
        const auto& sp = spiPattern();
        double uartBit = 1.0 / UART_BAUD;
        double uartPeriod = up.size() * uartBit;
        double i2cPeriod = ip.size() * I2C_TICK;
        double spiPeriod = sp.size() * SPI_TICK;

        float clockFreq = 5.0f / baseTime;
        for (int i = 0; i < m_recordLength; i++) {
            double t = t_start + i * dt;
            uint16_t bits = 0;

            // D0: UART
            int ui = static_cast<int>(wrapPos(t, uartPeriod) / uartBit);
            if (ui >= 0 && ui < static_cast<int>(up.size()) && up[ui]) bits |= (1 << 0);

            // D1/D2: I2C SCL/SDA
            int ii = static_cast<int>(wrapPos(t, i2cPeriod) / I2C_TICK);
            if (ii >= 0 && ii < static_cast<int>(ip.size())) {
                if (ip[ii].first)  bits |= (1 << 1);
                if (ip[ii].second) bits |= (1 << 2);
            }

            // D3/D4/D5: SPI CLK/MOSI/CS
            int si = static_cast<int>(wrapPos(t, spiPeriod) / SPI_TICK);
            if (si >= 0 && si < static_cast<int>(sp.size())) {
                if (sp[si][0]) bits |= (1 << 3);
                if (sp[si][1]) bits |= (1 << 4);
                if (sp[si][2]) bits |= (1 << 5);
            }

            // D6-D15: counter patterns (visual filler)
            double tt = (t < 0.0) ? -t : t;
            int counterVal = static_cast<int>(tt * clockFreq * 0.5f);
            bits |= static_cast<uint16_t>((counterVal & 0x03) << 6);        // D6-D7
            bits |= static_cast<uint16_t>(((counterVal >> 2) & 0x0F) << 8); // D8-D11
            int pseudo = (counterVal * 1103515245 + 12345) >> 16;
            bits |= static_cast<uint16_t>((pseudo & 0x0F) << 12);           // D12-D15

            data.digital.samples[i] = bits;
        }
    }
}
