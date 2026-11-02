#include "TempoTracker.h"

#include <algorithm>
#include <cmath>

namespace tp
{
namespace
{
constexpr float kMinBpm = 70.0f, kMaxBpm = 180.0f;
}

void TempoTracker::prepare (double sr)
{
    sampleRate = sr;
    hopRate = sampleRate / kHop;
    lowCoeff = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * 150.0 / sampleRate));
    odf.assign ((size_t) (hopRate * 10.0), 0.0f);
    acfAverage.clear();
    reset();
}

void TempoTracker::reset()
{
    low1 = low2 = 0.0f;
    energyLow = energyAll = 0.0;
    hopFill = 0;
    hasPrev = false;
    std::fill (odf.begin(), odf.end(), 0.0f);
    odfWrite = odfCount = 0;
    acfAverage.clear();
    hopsSinceEstimate = 0;
    runningMax = 1.0e-3f;
    currentBpm = currentConfidence = phase = onsetLevel = periodHops = 0.0f;
}

void TempoTracker::process (const float* left, const float* right, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (left[i] + right[i]);
        low1 += lowCoeff * (m - low1);
        low2 += lowCoeff * (low1 - low2);
        energyLow += (double) (low2 * low2);
        energyAll += (double) (m * m);

        if (++hopFill >= kHop)
            endHop();
    }
}

void TempoTracker::endHop()
{
    const float eLow = 10.0f * std::log10 (1.0e-10f + (float) (energyLow / kHop));
    const float eAll = 10.0f * std::log10 (1.0e-10f + (float) (energyAll / kHop));
    energyLow = energyAll = 0.0;
    hopFill = 0;

    float flux = 0.0f;
    if (hasPrev)
        flux = std::max (0.0f, eLow - prevLow) + 0.5f * std::max (0.0f, eAll - prevAll);
    flux = std::min (flux, 30.0f);
    prevLow = eLow;
    prevAll = eAll;
    hasPrev = true;

    odf[(size_t) odfWrite] = flux;
    odfWrite = (odfWrite + 1) % (int) odf.size();
    odfCount = std::min (odfCount + 1, (int) odf.size());

    runningMax = std::max (runningMax * 0.995f, flux);
    onsetLevel = std::min (1.0f, flux / std::max (runningMax, 1.0e-3f));

    if (periodHops > 1.0f)
    {
        phase += 1.0f / periodHops;
        phase -= std::floor (phase);
    }

    if (++hopsSinceEstimate >= 43)
    {
        hopsSinceEstimate = 0;
        estimate();
    }
}

float TempoTracker::odfAt (int hopsAgo) const
{
    if (hopsAgo < 0 || hopsAgo >= odfCount) return 0.0f;
    const int n = (int) odf.size();
    return odf[(size_t) ((odfWrite - 1 - hopsAgo + n * 2) % n)];
}

void TempoTracker::estimate()
{
    if (odfCount < (int) (hopRate * 4.0))
        return;

    const int L = odfCount;
    std::vector<float> x ((size_t) L);
    double mean = 0.0;
    for (int i = 0; i < L; ++i) { x[(size_t) i] = odfAt (L - 1 - i); mean += x[(size_t) i]; }
    mean /= L;
    for (auto& v : x) v -= (float) mean;

    const int lagMin = (int) std::floor (60.0 * hopRate / kMaxBpm);
    const int lagMax = (int) std::ceil (60.0 * hopRate / kMinBpm);
    const int maxLag = std::min (L - 1, lagMax * 2 + 3);

    std::vector<float> acf ((size_t) maxLag + 1, 0.0f);
    double zero = 0.0;
    for (int i = 0; i < L; ++i) zero += (double) x[(size_t) i] * x[(size_t) i];
    if (zero <= 1.0e-9)
        return;

    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < L; ++i) s += (double) x[(size_t) i] * x[(size_t) (i + lag)];
        acf[(size_t) lag] = (float) ((s / (L - lag)) / (zero / L));
    }

    if (acfAverage.size() != acf.size()) acfAverage = acf;
    else for (size_t i = 0; i < acf.size(); ++i) acfAverage[i] = 0.7f * acfAverage[i] + 0.3f * acf[i];
    const auto& a = acfAverage;

    int best = -1;
    float bestScore = -1.0e9f;
    for (int lag = std::max (2, lagMin); lag <= std::min (lagMax, maxLag - 1); ++lag)
    {
        const float b = (float) (60.0 * hopRate / lag);
        const float prior = std::exp (-0.5f * std::pow (std::log2 (b / 120.0f) / 0.8f, 2.0f));
        float score = a[(size_t) lag];
        if (2 * lag <= maxLag) score += 0.5f * a[(size_t) (2 * lag)];
        score *= prior;
        if (score > bestScore) { bestScore = score; best = lag; }
    }
    if (best < 0) return;

    auto refine = [&] (int i)
    {
        if (i < 1 || i + 1 > maxLag) return (float) i;
        const float y0 = a[(size_t) i - 1], y1 = a[(size_t) i], y2 = a[(size_t) i + 1];
        const float d = y0 - 2.0f * y1 + y2;
        return std::abs (d) > 1.0e-9f ? (float) i + 0.5f * (y0 - y2) / d : (float) i;
    };

    float lag = refine (best);

    // the peak at twice the period gives a finer estimate
    if (best * 2 + 2 <= maxLag)
    {
        int b2 = best * 2;
        for (int j = best * 2 - 2; j <= best * 2 + 2; ++j)
            if (a[(size_t) j] > a[(size_t) b2]) b2 = j;
        const float lag2 = refine (b2) * 0.5f;
        if (std::abs (lag2 - lag) < 1.0f) lag = lag2;
    }

    const float newBpm = (float) (60.0 * hopRate / lag);
    currentBpm = (currentBpm > 0.0f && std::abs (newBpm - currentBpm) < 2.0f) ? 0.8f * currentBpm + 0.2f * newBpm : newBpm;
    currentConfidence = std::max (0.0f, std::min (1.0f, a[(size_t) best]));

    // beat phase: the offset whose comb of past beats lines up with the most onset energy
    periodHops = (float) (60.0 * hopRate / currentBpm);
    int bestOffset = 0;
    float bestSum = -1.0f;
    for (int o = 0; o < (int) periodHops; ++o)
    {
        float s = 0.0f;
        for (int k = 0; k < 8; ++k) s += odfAt (o + (int) std::lround (k * periodHops));
        if (s > bestSum) { bestSum = s; bestOffset = o; }
    }
    phase = (float) bestOffset / periodHops;
}
} // namespace tp
