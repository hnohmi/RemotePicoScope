#pragma once

#include "core/SignalBuffer.h"
#include <string>

struct MeasurementResult {
    float frequency = 0.0f;
    float period = 0.0f;
    float vpp = 0.0f;
    float vavg = 0.0f;
    float vrms = 0.0f;
    float vmax = 0.0f;
    float vmin = 0.0f;
    float riseTime = 0.0f;
    float fallTime = 0.0f;
    float dutyCycle = 0.0f;
    bool valid = false;
};

class Measurements {
public:
    static MeasurementResult compute(const AnalogBuffer& buffer, float sampleRate);
};
