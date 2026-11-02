#pragma once

#include <array>
#include <string>

namespace tp
{
/** Musical key from a chromagram accumulated over the track, matched against
    Krumhansl-Kessler major/minor profiles. Keys 0-11 = C..B major, 12-23 = C..B minor. */
class KeyDetector
{
public:
    void reset();

    /** magnitudes: linear FFT magnitudes for bins 0..numBins-1. */
    void addSpectrum (const float* magnitudes, int numBins, double binHz, float weight);

    const std::array<float, 12>& chroma() const noexcept { return current; }   // 0..1, smoothed
    int key() const noexcept           { return bestKey; }
    float confidence() const noexcept  { return bestConfidence; }

    static std::string keyName (int key);   // "A minor"
    static std::string camelot (int key);   // "8A"
    static int tonic (int key) noexcept { return key % 12; }
    static bool isMinor (int key) noexcept { return key >= 12; }

private:
    void detect();

    std::array<double, 12> accumulated {};
    std::array<float, 12> current {};
    int bestKey = -1;
    float bestConfidence = 0.0f;
    int framesSinceDetect = 0;
};
} // namespace tp
