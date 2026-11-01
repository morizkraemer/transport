#include "Analyzer.h"

namespace tp
{
namespace
{
// 0 VU = -10 dBFS sine: mastered music sits around 0 VU instead of pinning the needle
constexpr float kVuOffsetDb = 10.0f;

inline float toDb (float gain) noexcept
{
    return gain > 1.0e-6f ? 20.0f * std::log10 (gain) : kSilenceDb;
}
}

Analyzer::Analyzer()
{
    historyL.assign (kFftSize, 0.0f);
    historyR.assign (kFftSize, 0.0f);
    fftData.assign (kFftSize * 2, 0.0f);
    window.resize (kFftSize);

    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kFftSize - 1));

    spectrumSmooth.fill (kSilenceDb);
    prepare (44100.0);
}

void Analyzer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    // one-pole on x^2 with a 300 ms time constant (VU / RMS integration)
    vuCoeff = (float) std::exp (-1.0 / (0.3 * sampleRate));
    loudness.prepare (sampleRate);
    resetTrack();
}

void Analyzer::resetTrack()
{
    loudness.reset();
    maxPeak = 0.0f;
    meanSquare[0] = meanSquare[1] = 0.0;
}

void Analyzer::push (const float* left, const float* right, int numSamples)
{
    const float half = 0.5f;
    constexpr float pi = juce::MathConstants<float>::pi;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];

        historyL[(size_t) historyPos] = l;
        historyR[(size_t) historyPos] = r;
        historyPos = (historyPos + 1) & (kFftSize - 1);

        meanSquare[0] = vuCoeff * meanSquare[0] + (1.0 - vuCoeff) * (double) (l * l);
        meanSquare[1] = vuCoeff * meanSquare[1] + (1.0 - vuCoeff) * (double) (r * r);

        tickPeak[0] = std::max (tickPeak[0], std::abs (l));
        tickPeak[1] = std::max (tickPeak[1], std::abs (r));

        // Polar stereo field: angle from mid/side, radius from level
        const float m = (l + r) * half, s = (l - r) * half;
        const float amp = std::sqrt (l * l + r * r) * 0.70710678f;

        if (amp > 1.0e-5f)
        {
            const float angle = std::atan2 (s, std::abs (m));                       // -pi/2 .. pi/2
            const int bin = juce::jlimit (0, kPolarBins - 1, (int) std::lround ((angle / pi + 0.5f) * (kPolarBins - 1)));
            const float radius = juce::jlimit (0.0f, 1.0f, (toDb (amp) + 80.0f) / 80.0f);
            polarTick[(size_t) bin] = std::max (polarTick[(size_t) bin], radius);
        }
    }

    loudness.process (left, right, numSamples);
    samplesSinceFrame += numSamples;
}

void Analyzer::computeFrame (AnalysisFrame& f, float dt)
{
    f.sampleRate = sampleRate;
    f.receivedAudio = samplesSinceFrame > 0;
    f.frameIndex = ++frameCounter;

    // --- scope: last kScopeSize samples, oldest first
    for (int i = 0; i < kScopeSize; ++i)
    {
        const int idx = (historyPos - kScopeSize + i + kFftSize) & (kFftSize - 1);
        f.left[(size_t) i]  = historyL[(size_t) idx];
        f.right[(size_t) i] = historyR[(size_t) idx];
    }

    // --- spectrum (Hann window, normalised so a 0 dBFS sine reads ~0 dB)
    for (int i = 0; i < kFftSize; ++i)
    {
        const int idx = (historyPos + i) & (kFftSize - 1);
        fftData[(size_t) i] = 0.5f * (historyL[(size_t) idx] + historyR[(size_t) idx]) * window[(size_t) i];
    }
    std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const float norm = 4.0f / (float) kFftSize;          // 2/N * 1/(window coherent gain 0.5)
    const float release = 1.0f - std::exp (-dt * 9.0f);

    for (int b = 0; b < kNumBins; ++b)
    {
        const float db = toDb (fftData[(size_t) b] * norm);
        float& s = spectrumSmooth[(size_t) b];
        s = db > s ? s + (db - s) * 0.85f : s + (db - s) * release;
        f.spectrumDb[(size_t) b] = s;
    }

    // --- levels
    for (int c = 0; c < 2; ++c)
    {
        if (samplesSinceFrame > 0)
            lastPeakDb[c] = toDb (tickPeak[c]);

        f.peakDb[c] = lastPeakDb[c];
        maxPeak = std::max (maxPeak, tickPeak[c]);

        const float rms = (float) std::sqrt (meanSquare[c]);
        f.rmsDb[c] = toDb (rms);
        f.vu[c] = toDb (rms * 1.41421356f) + kVuOffsetDb;
        tickPeak[c] = 0.0f;
    }
    f.maxPeakDb = toDb (maxPeak);

    // --- correlation & balance over the scope window
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int i = 0; i < kScopeSize; ++i)
    {
        const double l = f.left[(size_t) i], r = f.right[(size_t) i];
        lr += l * r; ll += l * l; rr += r * r;
    }
    f.correlation = (ll > 1.0e-9 && rr > 1.0e-9) ? (float) (lr / std::sqrt (ll * rr)) : 0.0f;
    f.balanceDb = (ll > 1.0e-9 && rr > 1.0e-9) ? (float) (10.0 * std::log10 (ll / rr)) : 0.0f;

    // --- polar field with decay
    for (int i = 0; i < kPolarBins; ++i)
    {
        polarHist[(size_t) i] = std::max (polarTick[(size_t) i], polarHist[(size_t) i] - dt * 1.4f);
        polarPeak[(size_t) i] = std::max (polarHist[(size_t) i], polarPeak[(size_t) i] - dt * 0.18f);
        polarTick[(size_t) i] = 0.0f;
    }
    f.polar = polarHist;
    f.polarPeak = polarPeak;

    // --- loudness
    f.momentaryLufs  = loudness.momentary();
    f.shortTermLufs  = loudness.shortTerm();
    f.integratedLufs = loudness.integrated();

    samplesSinceFrame = 0;
}
} // namespace tp
