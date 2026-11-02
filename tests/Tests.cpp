// Headless unit tests for the analysis core. Exit code 0 = all passed.
#include <juce_core/juce_core.h>
#include "core/Analyzer.h"
#include "core/LoudnessMeter.h"
#include "core/StereoFifo.h"
#include "core/Effects.h"
#include "core/KeyDetector.h"
#include "core/TempoTracker.h"
#include "core/TruePeak.h"

#include <cstdio>
#include <functional>

namespace
{
int failures = 0;

void check (bool ok, const char* name, double got, double expected)
{
    std::printf ("%s  %-52s got %10.4f  expected %10.4f\n", ok ? "PASS" : "FAIL", name, got, expected);
    if (! ok) ++failures;
}

std::vector<float> sine (double sr, double hz, double dbfs, double seconds, double phase = 0.0)
{
    const auto n = (size_t) (sr * seconds);
    const double amp = std::pow (10.0, dbfs / 20.0);
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i)
        v[i] = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sr + phase));
    return v;
}

void testLoudness (double sr)
{
    // EBU Tech 3341 case 1: stereo 1 kHz sine at -23 dBFS -> -23.0 LUFS (+-0.1)
    auto s = sine (sr, 1000.0, -23.0, 20.0);
    tp::LoudnessMeter m;
    m.prepare (sr);
    m.process (s.data(), s.data(), (int) s.size());

    const auto label = juce::String ("EBU 1 kHz -23 dBFS integrated @ ") + juce::String (sr, 0);
    check (std::abs (m.integrated() + 23.0f) < 0.1f, label.toRawUTF8(), m.integrated(), -23.0);
    check (std::abs (m.shortTerm() + 23.0f) < 0.1f, "EBU 1 kHz -23 dBFS short-term", m.shortTerm(), -23.0);

    // EBU Tech 3341 case 2: -33 dBFS -> -33.0 LUFS
    auto q = sine (sr, 1000.0, -33.0, 20.0);
    tp::LoudnessMeter m2;
    m2.prepare (sr);
    m2.process (q.data(), q.data(), (int) q.size());
    check (std::abs (m2.integrated() + 33.0f) < 0.1f, "EBU 1 kHz -33 dBFS integrated", m2.integrated(), -33.0);

    // EBU Tech 3342 case 1: 20 s at -20 dBFS then 20 s at -30 dBFS -> LRA 10 LU (+-1)
    if (sr == 48000.0)
    {
        auto a = sine (sr, 1000.0, -20.0, 20.0), b = sine (sr, 1000.0, -30.0, 20.0);
        a.insert (a.end(), b.begin(), b.end());
        tp::LoudnessMeter lra;
        lra.prepare (sr);
        lra.process (a.data(), a.data(), (int) a.size());
        check (std::abs (lra.range() - 10.0f) < 1.0f, "EBU 3342 loudness range (-20 / -30 dBFS)", lra.range(), 10.0);
        check (m.range() < 0.5f, "Steady sine has ~0 LU loudness range", m.range(), 0.0);
    }

    // Gating: 10 s of silence must not drag the integrated value down
    std::vector<float> silence ((size_t) (sr * 10.0), 0.0f);
    m.process (silence.data(), silence.data(), (int) silence.size());
    check (std::abs (m.integrated() + 23.0f) < 0.1f, "Integrated ignores gated silence", m.integrated(), -23.0);
}

void feed (tp::Analyzer& a, const std::vector<float>& l, const std::vector<float>& r, tp::AnalysisFrame& f)
{
    const int block = 512;
    for (size_t i = 0; i + block <= l.size(); i += block)
    {
        a.push (l.data() + i, r.data() + i, block);
        a.computeFrame (f, 1.0f / 60.0f);
    }
}

void testSpectrum()
{
    const double sr = 48000.0;
    tp::Analyzer a;
    a.prepare (sr);
    tp::AnalysisFrame f;
    auto s = sine (sr, 1000.0, -6.0, 1.0);
    feed (a, s, s, f);

    int best = 1;
    for (int b = 1; b < tp::kNumBins; ++b)
        if (f.spectrumDb[(size_t) b] > f.spectrumDb[(size_t) best]) best = b;

    const double binHz = sr / tp::kFftSize;
    check (std::abs (f.binToHz (best) - 1000.0) <= binHz, "FFT peak bin at 1 kHz", f.binToHz (best), 1000.0);
    check (std::abs (f.spectrumDb[(size_t) best] + 6.0f) < 1.6f, "FFT peak level of -6 dBFS sine", f.spectrumDb[(size_t) best], -6.0);
    check (std::abs (f.vu[0] - 4.0f) < 0.6f, "-6 dBFS sine reads +4 VU (0 VU = -10 dBFS)", f.vu[0], 4.0);
}

void testStereo()
{
    const double sr = 44100.0;
    tp::AnalysisFrame f;

    {
        tp::Analyzer a; a.prepare (sr);
        auto s = sine (sr, 440.0, -12.0, 0.5);
        feed (a, s, s, f);
        check (f.correlation > 0.99f, "Identical channels -> correlation +1", f.correlation, 1.0);
        check (std::abs (f.balanceDb) < 0.05f, "Identical channels -> balance 0 dB", f.balanceDb, 0.0);

        // mono content must land in the centre polar bin
        int best = 0;
        for (int i = 0; i < tp::kPolarBins; ++i)
            if (f.polarPeak[(size_t) i] > f.polarPeak[(size_t) best]) best = i;
        check (best == tp::kPolarBins / 2, "Mono signal -> centre polar bin", best, tp::kPolarBins / 2);
    }
    {
        tp::Analyzer a; a.prepare (sr);
        auto l = sine (sr, 440.0, -12.0, 0.5);
        auto r = l;
        for (auto& x : r) x = -x;
        feed (a, l, r, f);
        check (f.correlation < -0.99f, "Inverted channel -> correlation -1", f.correlation, -1.0);
    }
    {
        tp::Analyzer a; a.prepare (sr);
        auto l = sine (sr, 440.0, -12.0, 0.5);
        auto r = sine (sr, 440.0, -18.0, 0.5);
        feed (a, l, r, f);
        check (std::abs (f.balanceDb - 6.0f) < 0.1f, "L 6 dB louder -> balance +6 dB", f.balanceDb, 6.0);
    }
}

void testFifo()
{
    tp::StereoFifo fifo (1024);
    std::vector<float> l (700), r (700), ol (1024), orr (1024);
    for (int i = 0; i < 700; ++i) { l[(size_t) i] = (float) i; r[(size_t) i] = (float) -i; }

    fifo.push (l.data(), r.data(), 700);
    int got = fifo.pop (ol.data(), orr.data(), 1024);
    fifo.push (l.data(), r.data(), 700);   // wraps around
    got = fifo.pop (ol.data(), orr.data(), 1024);
    bool same = got == 700;
    for (int i = 0; i < 700 && same; ++i)
        same = ol[(size_t) i] == (float) i && orr[(size_t) i] == (float) -i;
    check (same, "FIFO wrap-around keeps samples intact", got, 700);
}

std::vector<float> beatLoop (double sr, double bpm, double seconds)
{
    const auto n = (size_t) (sr * seconds);
    std::vector<float> v (n);
    const double beat = 60.0 / bpm, twoPi = juce::MathConstants<double>::twoPi;
    double phase = 0.0;
    juce::Random rng (7);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr, inBeat = std::fmod (t, beat), off = std::fmod (t + beat * 0.5, beat);
        phase += twoPi * (45.0 + 95.0 * std::exp (-inBeat * 28.0)) / sr;
        const double kick = 0.8 * std::sin (phase) * std::exp (-inBeat * 7.0);
        const double hat = (rng.nextDouble() * 2.0 - 1.0) * 0.12 * std::exp (-off * 45.0);
        const double pad = 0.05 * std::sin (twoPi * 220.0 * t);
        v[i] = (float) (kick + hat + pad);
    }
    return v;
}

void testTempo()
{
    for (double bpm : { 124.0, 92.0, 140.0 })
    {
        const double sr = 44100.0;
        auto s = beatLoop (sr, bpm, 14.0);
        tp::TempoTracker t;
        t.prepare (sr);
        t.process (s.data(), s.data(), (int) s.size());
        const auto label = "Tempo of a " + juce::String (bpm, 0) + " BPM loop";
        check (std::abs (t.bpm() - bpm) < 1.0, label.toRawUTF8(), t.bpm(), bpm);
    }
}

void chords (std::vector<float>& out, double sr, std::initializer_list<std::initializer_list<int>> progression, double secondsEach)
{
    const double twoPi = juce::MathConstants<double>::twoPi;
    for (auto& chord : progression)
    {
        const auto n = (size_t) (sr * secondsEach);
        const auto start = out.size();
        out.resize (start + n, 0.0f);
        for (int midi : chord)
        {
            const double f = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
            for (size_t i = 0; i < n; ++i)
                for (int h = 1; h <= 4; ++h)
                    out[start + i] += (float) (0.08 / h * std::sin (twoPi * f * h * (double) i / sr));
        }
    }
}

void testKey()
{
    const double sr = 44100.0;
    struct Case { const char* name; int expected; std::initializer_list<std::initializer_list<int>> prog; };
    // C major: C F G C (with a bass note)   A minor: Am Dm E Am
    const Case cases[] = {
        { "Key of C-F-G-C progression is C major", 0,  { { 48, 60, 64, 67 }, { 53, 60, 65, 69 }, { 55, 59, 62, 67 }, { 48, 60, 64, 67 } } },
        { "Key of Am-Dm-E-Am progression is A minor", 21, { { 45, 57, 60, 64 }, { 50, 57, 62, 65 }, { 52, 56, 59, 64 }, { 45, 57, 60, 64 } } },
        { "Key of G-C-D-G progression is G major", 7, { { 43, 59, 62, 67 }, { 48, 60, 64, 67 }, { 50, 57, 62, 66 }, { 43, 59, 62, 67 } } },
        { "Key of Em-Am-B-Em progression is E minor", 16, { { 40, 55, 59, 64 }, { 45, 57, 60, 64 }, { 47, 54, 59, 63 }, { 40, 55, 59, 64 } } },
    };

    for (auto& c : cases)
    {
        std::vector<float> s;
        chords (s, sr, c.prog, 2.0);
        tp::Analyzer a; a.prepare (sr);
        tp::AnalysisFrame f;
        feed (a, s, s, f);
        check (f.key == c.expected, (juce::String (c.name) + " (got " + tp::KeyDetector::keyName (f.key) + ")").toRawUTF8(), f.key, c.expected);
    }
    check (tp::KeyDetector::camelot (21) == "8A" && tp::KeyDetector::camelot (7) == "9B", "Camelot codes (Am = 8A, G = 9B)", 1, 1);
}

void testTruePeak()
{
    // fs/4 sine at 0 dBFS, phase 45 deg: samples only reach -3 dB, the true peak is 0 dB
    const double sr = 48000.0;
    auto s = sine (sr, sr / 4.0, 0.0, 0.2, juce::MathConstants<double>::pi / 4.0);
    tp::TruePeak tpk;
    float samplePeak = 0.0f, truePeak = 0.0f;
    for (auto x : s) { samplePeak = std::max (samplePeak, std::abs (x)); truePeak = std::max (truePeak, tpk.process (x)); }
    check (std::abs (20.0f * std::log10 (truePeak)) < 0.6f, "True peak finds the inter-sample peak (0 dB)", 20.0 * std::log10 (truePeak), 0.0);
    check (20.0f * std::log10 (samplePeak) < -2.5f, "...while the sample peak reads about -3 dB", 20.0 * std::log10 (samplePeak), -3.0);
}

void testWidth()
{
    const double sr = 44100.0;
    tp::AnalysisFrame f;
    {
        tp::Analyzer a; a.prepare (sr);
        auto s = sine (sr, 1000.0, -12.0, 0.5);
        feed (a, s, s, f);
        check (f.bandWidth[2] < 0.02f, "Mono 1 kHz -> mid band width 0", f.bandWidth[2], 0.0);
    }
    {
        tp::Analyzer a; a.prepare (sr);
        auto s = sine (sr, 1000.0, -12.0, 0.5);
        std::vector<float> silent (s.size(), 0.0f);
        feed (a, s, silent, f);
        check (std::abs (f.bandWidth[2] - 0.5f) < 0.03f, "Left-only 1 kHz -> mid band width 0.5", f.bandWidth[2], 0.5);
    }
}

float peakDbOf (const std::vector<float>& v, size_t from)
{
    float p = 0.0f;
    for (size_t i = from; i < v.size(); ++i) p = std::max (p, std::abs (v[i]));
    return 20.0f * std::log10 (std::max (p, 1.0e-9f));
}

void runEffect (tp::EffectProcessor& fx, std::vector<float>& l, std::vector<float>& r)
{
    for (size_t i = 0; i < l.size(); i += 512)
    {
        const int n = (int) std::min<size_t> (512, l.size() - i);
        fx.process (l.data() + i, r.data() + i, n);
    }
}

void testEffects()
{
    const double sr = 48000.0;
    {
        tp::Equalizer eq;
        eq.bands[2].freq = 1000.0f; eq.bands[2].gainDb = 6.0f; eq.bands[2].q = 1.0f;
        eq.prepare (sr);
        auto l = sine (sr, 1000.0, -12.0, 1.0), r = l;
        runEffect (eq, l, r);
        check (std::abs (peakDbOf (l, l.size() / 2) + 6.0f) < 0.3f, "EQ +6 dB at 1 kHz boosts a 1 kHz sine by 6 dB", peakDbOf (l, l.size() / 2), -6.0);
        auto l2 = sine (sr, 100.0, -12.0, 1.0), r2 = l2;
        runEffect (eq, l2, r2);
        check (std::abs (peakDbOf (l2, l2.size() / 2) + 12.0f) < 0.3f, "...and leaves 100 Hz alone", peakDbOf (l2, l2.size() / 2), -12.0);
        check (std::abs (eq.responseDb (1000.0f) - 6.0f) < 0.1f, "EQ curve reads +6 dB at 1 kHz", eq.responseDb (1000.0f), 6.0);
    }
    {
        tp::Compressor c;
        c.thresholdDb = -20.0f; c.ratio = 4.0f; c.kneeDb = 0.0f; c.attackMs = 1.0f; c.releaseMs = 100.0f;
        c.prepare (sr);
        auto l = sine (sr, 1000.0, -6.0, 1.0), r = l;
        runEffect (c, l, r);
        // -6 dB peak, 14 dB over the threshold at 4:1 -> -20 + 3.5 = -16.5 dB
        check (std::abs (peakDbOf (l, l.size() / 2) + 16.5f) < 0.8f, "Compressor 4:1 at -20 dB: -6 dB in -> -16.5 dB out", peakDbOf (l, l.size() / 2), -16.5);
    }
    {
        tp::Limiter lim;
        lim.driveDb = 9.0f; lim.ceilingDb = -1.0f; lim.releaseMs = 50.0f;
        lim.prepare (sr);
        auto l = beatLoop (sr, 124.0, 4.0), r = l;
        runEffect (lim, l, r);
        const float out = peakDbOf (l, 0);
        check (out <= -0.999f && out > -1.6f, "Limiter +9 dB drive never exceeds the -1 dB ceiling", out, -1.0);
    }
}
} // namespace

int main()
{
    testLoudness (44100.0);
    testLoudness (48000.0);
    testSpectrum();
    testStereo();
    testFifo();
    testTempo();
    testKey();
    testTruePeak();
    testWidth();
    testEffects();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
