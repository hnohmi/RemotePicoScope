#include "signal/DummySignalSource.h"
#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void DummySignalSource::configure(const ScopeState& state) {
    m_timePerDiv = state.timePerDiv();
    m_visibleTime = m_timePerDiv * GRID_DIVISIONS_X;
    m_recordLength = state.recordLength;
    m_horizontalOffset = state.horizontalOffset;
    // Sample rate based on record length covering the visible window
    m_sampleRate = m_recordLength / m_visibleTime;
    // Total acquisition time: wider than visible to allow panning
    m_totalTime = m_recordLength / m_sampleRate;
    m_triggerLevel = state.trigger.level;
    m_triggerSource = state.trigger.source;
}

void DummySignalSource::acquire(SignalData& data) {
    data.resize(m_recordLength);
    data.sampleRate = m_sampleRate;

    float dt = 1.0f / m_sampleRate;

    // Advance phase for animation (simulate continuous acquisition)
    m_phase += 0.02f;
    if (m_phase > 2.0f * static_cast<float>(M_PI))
        m_phase -= 2.0f * static_cast<float>(M_PI);

    // Time origin: samples span [t_start, t_start + totalTime]
    // t=0 is trigger point (center of visible window when offset=0)
    // horizontalOffset shifts the visible window
    float t_start = -m_visibleTime * 0.5f + m_horizontalOffset;

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

    // Digital channels: various patterns
    {
        float clockFreq = 5.0f / baseTime; // fast clock
        for (int i = 0; i < m_recordLength; i++) {
            float t = t_start + i * dt;
            if (t < 0.0f) t = -t; // mirror for negative time
            uint16_t bits = 0;

            // D0: Clock (fastest)
            int clockCount = static_cast<int>(clockFreq * t * 2.0f);
            if (clockCount % 2) bits |= (1 << 0);

            // D1-D3: Clock dividers
            if ((clockCount / 2) % 2) bits |= (1 << 1);
            if ((clockCount / 4) % 2) bits |= (1 << 2);
            if ((clockCount / 8) % 2) bits |= (1 << 3);

            // D4-D7: Binary counter (slower)
            int counterVal = static_cast<int>(t * clockFreq * 0.5f);
            bits |= static_cast<uint16_t>((counterVal & 0x0F) << 4);

            // D8-D11: Shifted version of D4-D7
            bits |= static_cast<uint16_t>(((counterVal + 3) & 0x0F) << 8);

            // D12-D15: Random-ish pattern based on counter
            int pseudo = (counterVal * 1103515245 + 12345) >> 16;
            bits |= static_cast<uint16_t>((pseudo & 0x0F) << 12);

            data.digital.samples[i] = bits;
        }
    }
}
