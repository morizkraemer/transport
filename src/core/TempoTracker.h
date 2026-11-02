#pragma once

#include <vector>

namespace tp
{
/** Tempo from an onset-detection function (bass + broadband spectral-energy flux) and its
    autocorrelation, with a tempo prior around 120 BPM. Also tracks beat phase. */
class TempoTracker
{
public:
    static constexpr int kHop = 512;

    void prepare (double sampleRate);
    void reset();
    void process (const float* left, const float* right, int numSamples);

    float bpm() const noexcept        { return currentBpm; }
    float confidence() const noexcept { return currentConfidence; }
    float beatPhase() const noexcept  { return phase; }      // 0 at a beat, rises to 1
    float onset() const noexcept      { return onsetLevel; } // 0..1, latest onset strength

private:
    void endHop();
    void estimate();
    float odfAt (int hopsAgo) const;

    double sampleRate = 44100.0, hopRate = 86.13;
    float lowCoeff = 0.02f, low1 = 0.0f, low2 = 0.0f;
    double energyLow = 0.0, energyAll = 0.0;
    int hopFill = 0;
    float prevLow = 0.0f, prevAll = 0.0f;
    bool hasPrev = false;

    std::vector<float> odf;   // ring buffer of onset strength per hop
    int odfWrite = 0, odfCount = 0;
    std::vector<float> acfAverage;
    int hopsSinceEstimate = 0;
    float runningMax = 1.0e-3f;

    float currentBpm = 0.0f, currentConfidence = 0.0f, phase = 0.0f, onsetLevel = 0.0f;
    float periodHops = 0.0f;
};
} // namespace tp
