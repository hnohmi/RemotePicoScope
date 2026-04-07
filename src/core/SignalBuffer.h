#pragma once

#include "core/Types.h"
#include <vector>
#include <array>
#include <cstdint>

struct AnalogBuffer {
    std::vector<float> samples;
    int count = 0;

    void resize(int n) {
        samples.resize(n, 0.0f);
        count = n;
    }

    void clear() {
        std::fill(samples.begin(), samples.end(), 0.0f);
    }
};

struct DigitalBuffer {
    // Each sample is a bitmask of 16 digital channels
    std::vector<uint16_t> samples;
    int count = 0;

    void resize(int n) {
        samples.resize(n, 0);
        count = n;
    }

    void clear() {
        std::fill(samples.begin(), samples.end(), static_cast<uint16_t>(0));
    }
};

struct SignalData {
    std::array<AnalogBuffer, NUM_ANALOG_CHANNELS> analog;
    DigitalBuffer digital;
    float sampleRate = 1.0e6f;

    void resize(int n) {
        for (auto& ch : analog)
            ch.resize(n);
        digital.resize(n);
    }
};
