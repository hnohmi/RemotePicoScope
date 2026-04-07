#include "core/Measurements.h"
#include <cmath>
#include <algorithm>
#include <vector>

MeasurementResult Measurements::compute(const AnalogBuffer& buffer, float sampleRate) {
    MeasurementResult r;
    if (buffer.count < 10) return r;

    r.valid = true;
    float dt = 1.0f / sampleRate;

    // Vmin, Vmax, Vpp, Vavg
    r.vmin = buffer.samples[0];
    r.vmax = buffer.samples[0];
    double sum = 0.0;
    double sumSq = 0.0;

    for (int i = 0; i < buffer.count; i++) {
        float v = buffer.samples[i];
        if (v < r.vmin) r.vmin = v;
        if (v > r.vmax) r.vmax = v;
        sum += v;
        sumSq += static_cast<double>(v) * v;
    }

    r.vpp = r.vmax - r.vmin;
    r.vavg = static_cast<float>(sum / buffer.count);
    r.vrms = static_cast<float>(std::sqrt(sumSq / buffer.count));

    // Frequency via zero-crossing detection (using mean as threshold)
    float threshold = r.vavg;
    std::vector<int> risingCrossings;
    for (int i = 1; i < buffer.count; i++) {
        if (buffer.samples[i - 1] < threshold && buffer.samples[i] >= threshold) {
            risingCrossings.push_back(i);
        }
    }

    if (risingCrossings.size() >= 2) {
        // Average period from multiple crossings
        double totalSamples = 0;
        int numPeriods = static_cast<int>(risingCrossings.size()) - 1;
        for (int i = 0; i < numPeriods; i++) {
            totalSamples += risingCrossings[i + 1] - risingCrossings[i];
        }
        double avgPeriodSamples = totalSamples / numPeriods;
        r.period = static_cast<float>(avgPeriodSamples * dt);
        r.frequency = 1.0f / r.period;
    }

    // Duty cycle: fraction of time above threshold
    int aboveCount = 0;
    for (int i = 0; i < buffer.count; i++) {
        if (buffer.samples[i] > threshold) aboveCount++;
    }
    r.dutyCycle = static_cast<float>(aboveCount) / buffer.count * 100.0f;

    // Rise time: 10% to 90% of Vpp
    float v10 = r.vmin + 0.1f * r.vpp;
    float v90 = r.vmin + 0.9f * r.vpp;

    // Find first rising edge
    for (int i = 1; i < buffer.count; i++) {
        if (buffer.samples[i - 1] < v10 && buffer.samples[i] >= v10) {
            // Found 10% crossing, look for 90%
            for (int j = i; j < buffer.count; j++) {
                if (buffer.samples[j] >= v90) {
                    r.riseTime = (j - i) * dt;
                    break;
                }
                // If signal goes back down, this isn't a clean rising edge
                if (buffer.samples[j] < buffer.samples[j - 1] && buffer.samples[j] < v10) break;
            }
            if (r.riseTime > 0) break;
        }
    }

    // Fall time: 90% to 10% of Vpp
    for (int i = 1; i < buffer.count; i++) {
        if (buffer.samples[i - 1] > v90 && buffer.samples[i] <= v90) {
            for (int j = i; j < buffer.count; j++) {
                if (buffer.samples[j] <= v10) {
                    r.fallTime = (j - i) * dt;
                    break;
                }
                if (buffer.samples[j] > buffer.samples[j - 1] && buffer.samples[j] > v90) break;
            }
            if (r.fallTime > 0) break;
        }
    }

    return r;
}
