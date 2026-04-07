#include "core/MathEngine.h"
#include <cmath>
#include <algorithm>

void MathEngine::compute(const MathChannelConfig& config,
                         const SignalData& input,
                         AnalogBuffer& output)
{
    if (!config.enabled || config.op == MathOp::FFT)
        return;

    const auto& src1 = input.analog[config.source1];
    const auto& src2 = input.analog[config.source2];
    int n = src1.count;
    if (n <= 0) return;

    output.resize(n);

    float dt = (input.sampleRate > 0.0f) ? 1.0f / input.sampleRate : 1.0f;

    switch (config.op) {
    case MathOp::Add:
        for (int i = 0; i < n; i++)
            output.samples[i] = src1.samples[i] + src2.samples[i];
        break;

    case MathOp::Subtract:
        for (int i = 0; i < n; i++)
            output.samples[i] = src1.samples[i] - src2.samples[i];
        break;

    case MathOp::Multiply:
        for (int i = 0; i < n; i++)
            output.samples[i] = src1.samples[i] * src2.samples[i];
        break;

    case MathOp::Divide:
        for (int i = 0; i < n; i++) {
            float denom = src2.samples[i];
            output.samples[i] = (std::abs(denom) > 1e-12f)
                ? src1.samples[i] / denom
                : 0.0f;
        }
        break;

    case MathOp::Derivative:
        // Central difference: f'(i) = (f(i+1) - f(i-1)) / (2*dt)
        output.samples[0] = (n > 1) ? (src1.samples[1] - src1.samples[0]) / dt : 0.0f;
        for (int i = 1; i < n - 1; i++)
            output.samples[i] = (src1.samples[i + 1] - src1.samples[i - 1]) / (2.0f * dt);
        if (n > 1)
            output.samples[n - 1] = (src1.samples[n - 1] - src1.samples[n - 2]) / dt;
        break;

    case MathOp::Integral: {
        // Cumulative trapezoidal integration
        double sum = 0.0;
        output.samples[0] = 0.0f;
        for (int i = 1; i < n; i++) {
            sum += 0.5 * (src1.samples[i] + src1.samples[i - 1]) * dt;
            output.samples[i] = static_cast<float>(sum);
        }
        break;
    }

    case MathOp::Sqrt:
        for (int i = 0; i < n; i++) {
            float v = src1.samples[i];
            output.samples[i] = (v >= 0.0f) ? std::sqrt(v) : -std::sqrt(-v);
        }
        break;

    default:
        break;
    }
}
