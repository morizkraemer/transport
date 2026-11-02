#pragma once

#include <array>
#include <algorithm>
#include <cmath>

namespace tp
{
/** 4x oversampled peak detector (BS.1770 style true peak) for one channel. */
class TruePeak
{
public:
    TruePeak()
    {
        // windowed-sinc interpolation filter, 48 taps split into 4 phases of 12
        constexpr int taps = kPhases * kTapsPerPhase;
        constexpr double pi = 3.14159265358979323846;
        double h[taps];
        for (int i = 0; i < taps; ++i)
        {
            const double x = (i - (taps - 1) * 0.5) / kPhases;
            const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double w = 0.5 - 0.5 * std::cos (2.0 * pi * (i + 0.5) / taps);
            h[i] = sinc * w;
        }
        for (int p = 0; p < kPhases; ++p)
        {
            double sum = 0.0;
            for (int k = 0; k < kTapsPerPhase; ++k) sum += h[p + k * kPhases];
            for (int k = 0; k < kTapsPerPhase; ++k) coeffs[p][k] = (float) (h[p + k * kPhases] / sum);
        }
        reset();
    }

    void reset() { history.fill (0.0f); pos = 0; }

    /** Feeds one sample, returns the highest absolute value among the 4 interpolated points. */
    float process (float x) noexcept
    {
        history[(size_t) pos] = x;
        float peak = 0.0f;
        for (int p = 0; p < kPhases; ++p)
        {
            float y = 0.0f;
            for (int k = 0; k < kTapsPerPhase; ++k)
                y += coeffs[p][k] * history[(size_t) ((pos - k + kTapsPerPhase) % kTapsPerPhase)];
            peak = std::max (peak, std::abs (y));
        }
        pos = (pos + 1) % kTapsPerPhase;
        return peak;
    }

private:
    static constexpr int kPhases = 4, kTapsPerPhase = 12;
    float coeffs[kPhases][kTapsPerPhase] {};
    std::array<float, kTapsPerPhase> history {};
    int pos = 0;
};
} // namespace tp
