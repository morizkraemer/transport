// Headless unit tests for the analysis core. Exit code 0 = all passed.
#include <juce_core/juce_core.h>
#include "core/Analyzer.h"
#include "core/LoudnessMeter.h"
#include "core/StereoFifo.h"

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
} // namespace

int main()
{
    testLoudness (44100.0);
    testLoudness (48000.0);
    testSpectrum();
    testStereo();
    testFifo();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
