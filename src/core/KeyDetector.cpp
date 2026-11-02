#include "KeyDetector.h"

#include <algorithm>
#include <cmath>

namespace tp
{
namespace
{
constexpr double kMajor[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
constexpr double kMinor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
const char* kNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

double correlate (const std::array<double, 12>& c, const double* profile, int rotation)
{
    double mc = 0, mp = 0;
    for (int i = 0; i < 12; ++i) { mc += c[(size_t) i]; mp += profile[i]; }
    mc /= 12; mp /= 12;
    double num = 0, dc = 0, dp = 0;
    for (int i = 0; i < 12; ++i)
    {
        const double a = c[(size_t) ((i + rotation) % 12)] - mc, b = profile[i] - mp;
        num += a * b; dc += a * a; dp += b * b;
    }
    return (dc > 0 && dp > 0) ? num / std::sqrt (dc * dp) : 0.0;
}
}

void KeyDetector::reset()
{
    accumulated.fill (0.0);
    current.fill (0.0f);
    bestKey = -1;
    bestConfidence = 0.0f;
    framesSinceDetect = 0;
}

void KeyDetector::addSpectrum (const float* mag, int numBins, double binHz, float weight)
{
    std::array<double, 12> frame {};
    const int lo = std::max (1, (int) (180.0 / binHz)), hi = std::min (numBins - 1, (int) (5000.0 / binHz));

    for (int b = lo; b <= hi; ++b)
    {
        const double f = b * binHz;
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        const double nearest = std::round (midi);
        const double w = std::max (0.0, 1.0 - 2.0 * std::abs (midi - nearest));   // favour bins on a semitone
        const int pc = ((int) nearest % 12 + 12) % 12;
        frame[(size_t) pc] += w * (double) mag[b];   // linear: square roots let overtones pull towards the fifth
    }

    double mx = 0.0;
    for (auto v : frame) mx = std::max (mx, v);
    if (mx <= 1.0e-9) return;

    for (int i = 0; i < 12; ++i)
    {
        const double v = frame[(size_t) i] / mx;
        accumulated[(size_t) i] += v * weight;
        current[(size_t) i] += ((float) v - current[(size_t) i]) * 0.25f;
    }

    if (++framesSinceDetect >= 15)
    {
        framesSinceDetect = 0;
        detect();
    }
}

void KeyDetector::detect()
{
    double best = -2.0, second = -2.0;
    int key = -1;
    for (int k = 0; k < 24; ++k)
    {
        const double r = correlate (accumulated, k < 12 ? kMajor : kMinor, k % 12);
        if (r > best) { second = best; best = r; key = k; }
        else if (r > second) second = r;
    }
    bestKey = key;
    bestConfidence = (float) std::max (0.0, std::min (1.0, (best - second) * 5.0 + best * 0.5));
}

std::string KeyDetector::keyName (int key)
{
    if (key < 0) return "--";
    return std::string (kNames[key % 12]) + (key >= 12 ? " minor" : " major");
}

std::string KeyDetector::camelot (int key)
{
    if (key < 0) return "--";
    const int majorPc = key >= 12 ? (key % 12 + 3) % 12 : key % 12;   // relative major for minor keys
    const int number = (majorPc * 7 % 12 + 7) % 12 + 1;
    return std::to_string (number) + (key >= 12 ? "A" : "B");
}
} // namespace tp
