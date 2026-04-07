#include "core/FFTEngine.h"

#include <kissfft/kiss_fft.h>
#include <kissfft/kiss_fftr.h>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void FFTEngine::applyWindow(std::vector<float>& data, FFTWindowType window) {
    int N = static_cast<int>(data.size());
    if (N <= 1 || window == FFTWindowType::Rectangular)
        return;

    for (int i = 0; i < N; i++) {
        double w = 0.0;
        double n = static_cast<double>(i) / (N - 1);

        switch (window) {
        case FFTWindowType::Hanning:
            w = 0.5 * (1.0 - std::cos(2.0 * M_PI * n));
            break;
        case FFTWindowType::Hamming:
            w = 0.54 - 0.46 * std::cos(2.0 * M_PI * n);
            break;
        case FFTWindowType::BlackmanHarris:
            w = 0.35875
              - 0.48829 * std::cos(2.0 * M_PI * n)
              + 0.14128 * std::cos(4.0 * M_PI * n)
              - 0.01168 * std::cos(6.0 * M_PI * n);
            break;
        case FFTWindowType::FlatTop:
            w = 0.21557895
              - 0.41663158 * std::cos(2.0 * M_PI * n)
              + 0.27726316 * std::cos(4.0 * M_PI * n)
              - 0.08357895 * std::cos(6.0 * M_PI * n)
              + 0.00694737 * std::cos(8.0 * M_PI * n);
            break;
        default:
            w = 1.0;
            break;
        }
        data[i] *= static_cast<float>(w);
    }
}

FFTResult FFTEngine::compute(const AnalogBuffer& input, float sampleRate,
                             FFTWindowType window)
{
    FFTResult result;
    if (input.count < 4 || sampleRate <= 0.0f) return result;

    // Round down to power of 2
    int N = 1;
    while (N * 2 <= input.count) N *= 2;

    // Copy and window
    m_workBuffer.resize(N);
    for (int i = 0; i < N; i++)
        m_workBuffer[i] = input.samples[i];
    applyWindow(m_workBuffer, window);

    // Allocate KissFFT
    int nFreqBins = N / 2 + 1;
    kiss_fftr_cfg cfg = kiss_fftr_alloc(N, 0, nullptr, nullptr);
    if (!cfg) return result;

    std::vector<kiss_fft_cpx> freqData(nFreqBins);
    kiss_fftr(cfg, m_workBuffer.data(), freqData.data());

    // Convert to dBVrms
    result.magnitudeDB.resize(nFreqBins);
    result.binCount = nFreqBins;
    result.freqResolution = sampleRate / N;
    result.maxFrequency = sampleRate / 2.0f;

    float scale = 2.0f / N; // factor of 2 for single-sided spectrum
    constexpr float DB_FLOOR = -160.0f;

    for (int i = 0; i < nFreqBins; i++) {
        float re = freqData[i].r;
        float im = freqData[i].i;
        float mag = std::sqrt(re * re + im * im) * scale;

        // DC and Nyquist bins don't need the x2 factor
        if (i == 0 || i == nFreqBins - 1)
            mag *= 0.5f;

        // Convert to RMS: divide by sqrt(2) for sinusoidal signals
        float rms = mag / std::sqrt(2.0f);

        if (rms > 0.0f)
            result.magnitudeDB[i] = 20.0f * std::log10(rms);
        else
            result.magnitudeDB[i] = DB_FLOOR;

        result.magnitudeDB[i] = std::max(result.magnitudeDB[i], DB_FLOOR);
    }

    kiss_fftr_free(cfg);
    result.valid = true;
    return result;
}
