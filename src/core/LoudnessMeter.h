#pragma once

#include <vector>

namespace tp
{
/** EBU R128 / ITU-R BS.1770 loudness for a stereo signal.
    Momentary (400 ms), short-term (3 s) and gated integrated loudness. */
class LoudnessMeter
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (const float* left, const float* right, int numSamples);

    float momentary() const noexcept  { return momentaryLufs; }
    float shortTerm() const noexcept  { return shortTermLufs; }
    float integrated() const noexcept { return integratedLufs; }
    /** Loudness range (EBU Tech 3342), LU. */
    float range();

    static float energyToLufs (double energy) noexcept;

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1 = z2 = 0; }
    };

    void finishBlock();
    void updateIntegrated();

    double sampleRate = 44100.0;
    Biquad shelf[2], highPass[2];

    int samplesPerBlock = 4410;     // 100 ms
    int blockFill = 0;
    double blockEnergy = 0.0;

    std::vector<double> recentBlocks;   // ring of the last 30 100-ms block energies
    int recentWrite = 0, recentCount = 0;

    std::vector<double> gatingBlocks;   // 400 ms energies, one every 100 ms (75 % overlap)
    std::vector<double> shortTermBlocks; // 3 s energies, one every 100 ms
    float cachedRange = 0.0f;
    size_t rangeComputedAt = 0;

    float momentaryLufs = -120.0f, shortTermLufs = -120.0f, integratedLufs = -120.0f;
};
} // namespace tp
