#include "LoudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace tp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int kRecentBlocks = 30;   // 3 s of 100 ms blocks
}

float LoudnessMeter::energyToLufs (double energy) noexcept
{
    if (energy <= 1.0e-12)
        return -120.0f;
    return (float) (-0.691 + 10.0 * std::log10 (energy));
}

void LoudnessMeter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    // K-weighting coefficients for an arbitrary sample rate (BS.1770, as derived in libebur128).
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K  = std::tan (pi * f0 / sampleRate);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        for (auto& s : shelf)
        {
            s.b0 = (Vh + Vb * K / Q + K * K) / a0;
            s.b1 = 2.0 * (K * K - Vh) / a0;
            s.b2 = (Vh - Vb * K / Q + K * K) / a0;
            s.a1 = 2.0 * (K * K - 1.0) / a0;
            s.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K  = std::tan (pi * f0 / sampleRate);
        const double d  = 1.0 + K / Q + K * K;

        for (auto& h : highPass)
        {
            h.b0 = 1.0; h.b1 = -2.0; h.b2 = 1.0;
            h.a1 = 2.0 * (K * K - 1.0) / d;
            h.a2 = (1.0 - K / Q + K * K) / d;
        }
    }

    samplesPerBlock = std::max (1, (int) std::lround (sampleRate * 0.1));
    reset();
}

void LoudnessMeter::reset()
{
    for (auto& s : shelf) s.clear();
    for (auto& h : highPass) h.clear();

    blockFill = 0;
    blockEnergy = 0.0;
    recentBlocks.assign (kRecentBlocks, 0.0);
    recentWrite = recentCount = 0;
    gatingBlocks.clear();
    gatingBlocks.reserve (36000);
    momentaryLufs = shortTermLufs = integratedLufs = -120.0f;
}

void LoudnessMeter::process (const float* left, const float* right, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        const double l = highPass[0].process (shelf[0].process (left[i]));
        const double r = highPass[1].process (shelf[1].process (right[i]));
        blockEnergy += l * l + r * r;

        if (++blockFill >= samplesPerBlock)
            finishBlock();
    }
}

void LoudnessMeter::finishBlock()
{
    recentBlocks[(size_t) recentWrite] = blockEnergy / (double) samplesPerBlock;
    recentWrite = (recentWrite + 1) % kRecentBlocks;
    recentCount = std::min (recentCount + 1, kRecentBlocks);
    blockFill = 0;
    blockEnergy = 0.0;

    auto meanOfLast = [this] (int n)
    {
        n = std::min (n, recentCount);
        double sum = 0.0;
        for (int i = 1; i <= n; ++i)
            sum += recentBlocks[(size_t) ((recentWrite - i + kRecentBlocks) % kRecentBlocks)];
        return n > 0 ? sum / n : 0.0;
    };

    if (recentCount >= 4)
    {
        const double m = meanOfLast (4);
        momentaryLufs = energyToLufs (m);
        gatingBlocks.push_back (m);
        updateIntegrated();
    }

    shortTermLufs = recentCount >= 30 ? energyToLufs (meanOfLast (30))
                                      : (recentCount >= 4 ? energyToLufs (meanOfLast (recentCount)) : -120.0f);
}

void LoudnessMeter::updateIntegrated()
{
    // Absolute gate at -70 LUFS
    const double absGate = std::pow (10.0, (-70.0 + 0.691) / 10.0);
    double sum = 0.0;
    int n = 0;

    for (auto e : gatingBlocks)
        if (e > absGate) { sum += e; ++n; }

    if (n == 0) { integratedLufs = -120.0f; return; }

    // Relative gate 10 LU below the absolute-gated loudness
    const double relGate = (sum / n) * std::pow (10.0, -10.0 / 10.0);
    double sum2 = 0.0;
    int n2 = 0;

    for (auto e : gatingBlocks)
        if (e > absGate && e > relGate) { sum2 += e; ++n2; }

    integratedLufs = n2 > 0 ? energyToLufs (sum2 / n2) : -120.0f;
}
} // namespace tp
