#pragma once

#include "core/Types.h"
#include "core/SignalBuffer.h"
#include <vector>

struct FFTResult {
    std::vector<float> magnitudeDB;  // dBVrms per bin
    float freqResolution = 0.0f;     // Hz per bin
    float maxFrequency = 0.0f;       // Nyquist frequency
    int binCount = 0;
    bool valid = false;
};

class FFTEngine {
public:
    FFTResult compute(const AnalogBuffer& input, float sampleRate,
                      FFTWindowType window = FFTWindowType::Hanning);

private:
    void applyWindow(std::vector<float>& data, FFTWindowType window);
    std::vector<float> m_workBuffer;
};
