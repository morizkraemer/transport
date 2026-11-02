#include "Effects.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace tp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }
}

//==============================================================================
void Biquad::set (Type type, double sr, double freq, double q, double gainDb) noexcept
{
    freq = std::clamp (freq, 10.0, sr * 0.49);
    q = std::max (0.1, q);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * pi * freq / sr, c = std::cos (w0), s = std::sin (w0);
    const double alpha = s / (2.0 * q), sqA2a = 2.0 * std::sqrt (A) * alpha;
    double a0 = 1;

    switch (type)
    {
        case Type::peak:
            b0 = 1 + alpha * A; b1 = -2 * c; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * c; a2 = 1 - alpha / A;
            break;
        case Type::lowShelf:
            b0 = A * ((A + 1) - (A - 1) * c + sqA2a); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sqA2a);
            a0 = (A + 1) + (A - 1) * c + sqA2a;       a1 = -2 * ((A - 1) + (A + 1) * c);    a2 = (A + 1) + (A - 1) * c - sqA2a;
            break;
        case Type::highShelf:
            b0 = A * ((A + 1) + (A - 1) * c + sqA2a); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sqA2a);
            a0 = (A + 1) - (A - 1) * c + sqA2a;       a1 = 2 * ((A - 1) - (A + 1) * c);      a2 = (A + 1) - (A - 1) * c - sqA2a;
            break;
        case Type::lowCut:
            b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2;
            a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
            break;
        case Type::highCut:
            b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2;
            a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
            break;
    }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
}

double Biquad::magnitudeDb (double freq, double sr) const noexcept
{
    const std::complex<double> z1 = std::polar (1.0, -2.0 * pi * freq / sr);
    const auto z2 = z1 * z1;
    const auto h = (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    return 20.0 * std::log10 (std::max (1.0e-9, std::abs (h)));
}

//==============================================================================
Equalizer::Equalizer() { resetBands(); }

void Equalizer::resetBands()
{
    const Biquad::Type types[kBands] = { Biquad::Type::lowShelf, Biquad::Type::peak, Biquad::Type::peak, Biquad::Type::peak, Biquad::Type::highShelf };
    const float freqs[kBands] = { 80.0f, 250.0f, 1000.0f, 4000.0f, 10000.0f };
    const float qs[kBands] = { 0.7f, 1.0f, 1.0f, 1.0f, 0.7f };
    for (int b = 0; b < kBands; ++b)
    {
        bands[(size_t) b].type = types[b];
        bands[(size_t) b].freq = freqs[b];
        bands[(size_t) b].q = qs[b];
        bands[(size_t) b].gainDb = 0.0f;
        bands[(size_t) b].enabled = true;
    }
}

void Equalizer::prepare (double sr)
{
    sampleRate = sr;
    for (int b = 0; b < kBands; ++b)
    {
        current[b][0] = bands[(size_t) b].freq;
        current[b][1] = bands[(size_t) b].gainDb;
        current[b][2] = bands[(size_t) b].q;
        filters[b].clear();
        filters[b].set (bands[(size_t) b].type, sampleRate, current[b][0], current[b][2], current[b][1]);
    }
}

void Equalizer::process (float* l, float* r, int n) noexcept
{
    const float amount = std::min (1.0f, (float) n / (float) (0.03 * sampleRate));

    for (int b = 0; b < kBands; ++b)
    {
        auto& band = bands[(size_t) b];
        const float target[3] = { band.freq.load(), band.enabled ? band.gainDb.load() : 0.0f, band.q.load() };
        bool changed = false;
        for (int k = 0; k < 3; ++k)
        {
            const float before = current[b][k];
            current[b][k] += (target[k] - current[b][k]) * amount;
            if (std::abs (target[k] - current[b][k]) < 1.0e-4f) current[b][k] = target[k];
            changed |= current[b][k] != before;
        }
        if (changed)
            filters[b].set (band.type, sampleRate, current[b][0], current[b][2], current[b][1]);

        if (std::abs (current[b][1]) < 0.01f)
            continue;   // flat band: skip, the shelf/peak is transparent at 0 dB

        for (int i = 0; i < n; ++i)
        {
            l[i] = filters[b].process (0, l[i]);
            if (r != l) r[i] = filters[b].process (1, r[i]);
        }
    }
}

float Equalizer::responseDb (float hz) const
{
    double total = 0.0;
    for (int b = 0; b < kBands; ++b)
    {
        const auto& band = bands[(size_t) b];
        if (! band.enabled || std::abs (band.gainDb.load()) < 0.01f) continue;
        Biquad f;
        f.set (band.type, 48000.0, band.freq, band.q, band.gainDb);
        total += f.magnitudeDb (hz, 48000.0);
    }
    return (float) total;
}

//==============================================================================
float Compressor::curve (float in, float t, float ratio, float knee)
{
    const float over = in - t;
    if (2.0f * over < -knee) return in;
    if (knee > 0.0f && 2.0f * std::abs (over) <= knee)
    {
        const float x = over + knee * 0.5f;
        return in + (1.0f / ratio - 1.0f) * x * x / (2.0f * knee);
    }
    return t + over / ratio;
}

void Compressor::prepare (double sr)
{
    sampleRate = sr;
    envelopeDb = 0.0f;
    makeup = dbToGain (makeupDb);
}

void Compressor::process (float* left, float* right, int n) noexcept
{
    const float t = thresholdDb.load(), r = std::max (1.0f, ratio.load()), k = kneeDb.load();
    const float att = std::exp (-1.0f / (std::max (0.05f, attackMs.load()) * 0.001f * (float) sampleRate));
    const float rel = std::exp (-1.0f / (std::max (5.0f, releaseMs.load()) * 0.001f * (float) sampleRate));
    const float targetMakeup = dbToGain (makeupDb);
    const float makeupStep = (targetMakeup - makeup) / (float) std::max (1, n);

    float maxGr = 0.0f, maxIn = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float peak = std::max (std::abs (left[i]), std::abs (right[i]));
        maxIn = std::max (maxIn, peak);
        const float inDb = gainToDb (peak);
        const float gr = std::max (0.0f, inDb - curve (inDb, t, r, k));
        envelopeDb = gr > envelopeDb ? gr + (envelopeDb - gr) * att : gr + (envelopeDb - gr) * rel;

        makeup += makeupStep;
        const float g = dbToGain (-envelopeDb) * makeup;
        left[i] *= g;
        if (right != left) right[i] *= g;
        maxGr = std::max (maxGr, envelopeDb);
    }
    makeup = targetMakeup;
    meterGainReductionDb = maxGr;
    meterInputDb = gainToDb (maxIn);
}

//==============================================================================
void Limiter::prepare (double sr)
{
    sampleRate = sr;
    lookahead = std::clamp ((int) std::lround (0.0015 * sr), 8, kMaxLookahead - 1);
    writePos = 0;
    std::fill (std::begin (delayL), std::end (delayL), 0.0f);
    std::fill (std::begin (delayR), std::end (delayR), 0.0f);
    std::fill (std::begin (needed), std::end (needed), 1.0f);
    gain = 1.0f;
    drive = dbToGain (driveDb);
}

void Limiter::process (float* l, float* r, int n) noexcept
{
    const float ceiling = dbToGain (std::min (0.0f, ceilingDb.load()));
    const float attackCoeff = 1.0f - std::exp (-5.0f / (float) lookahead);
    const float releaseCoeff = 1.0f - std::exp (-1.0f / (std::max (1.0f, releaseMs.load()) * 0.001f * (float) sampleRate));
    const float targetDrive = dbToGain (driveDb);
    const float driveStep = (targetDrive - drive) / (float) std::max (1, n);

    float minGain = 1.0f, maxOut = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        drive += driveStep;
        const float xl = l[i] * drive, xr = (r != l ? r[i] : l[i]) * drive;
        const float peak = std::max (std::abs (xl), std::abs (xr));

        delayL[writePos] = xl;
        delayR[writePos] = xr;
        needed[writePos] = peak > ceiling ? ceiling / peak : 1.0f;

        float target = 1.0f;
        for (int k = 0; k < lookahead; ++k) target = std::min (target, needed[k]);

        gain += (target - gain) * (target < gain ? attackCoeff : releaseCoeff);
        const float g = gain;   // the look-ahead lets the smoothed gain arrive before the peak; the clamp below is the safety net

        const int read = (writePos + 1) % lookahead;
        const float ol = std::clamp (delayL[read] * g, -ceiling, ceiling);
        const float orr = std::clamp (delayR[read] * g, -ceiling, ceiling);
        l[i] = ol;
        if (r != l) r[i] = orr;
        writePos = read;

        minGain = std::min (minGain, g);
        maxOut = std::max (maxOut, std::max (std::abs (ol), std::abs (orr)));
    }
    drive = targetDrive;
    meterGainReductionDb = -gainToDb (minGain);
    meterOutputDb = gainToDb (maxOut);
}
} // namespace tp
