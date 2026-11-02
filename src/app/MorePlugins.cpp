#include "Visualizers.h"
#include "core/KeyDetector.h"

namespace tp
{
namespace
{
using juce::Colour;
using juce::Rectangle;
using juce::Point;
using plug::drawLabel;
using plug::formatDb;

constexpr float kPi = juce::MathConstants<float>::pi;

const char* kNoteNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

//==============================================================================
class Tuner final : public Visualizer
{
public:
    Tuner() { cents.stiffness = 70.0f; cents.damping = 12.0f; }

    void update (const AnalysisFrame& f, float dt) override
    {
        const double binHz = f.sampleRate / kFftSize;
        const int lo = juce::jmax (2, (int) (60.0 / binHz)), hi = juce::jmin (kNumBins / 3 - 2, (int) (1500.0 / binHz));
        int best = -1;
        float bestScore = -1.0e9f;
        for (int b = lo; b <= hi; ++b)
        {
            const float s = f.spectrumDb[(size_t) b] + 0.6f * f.spectrumDb[(size_t) (2 * b)] + 0.4f * f.spectrumDb[(size_t) (3 * b)];
            if (s > bestScore) { bestScore = s; best = b; }
        }

        const bool voiced = best > 0 && f.spectrumDb[(size_t) best] > -55.0f;
        presence.target = voiced ? 1.0f : 0.0f;
        bool dirty = presence.tick (dt, voiced ? 12.0f : 3.0f);

        if (voiced)
        {
            const float y0 = f.spectrumDb[(size_t) best - 1], y1 = f.spectrumDb[(size_t) best], y2 = f.spectrumDb[(size_t) best + 1];
            const float d = y0 - 2.0f * y1 + y2;
            const float offset = std::abs (d) > 1.0e-6f ? 0.5f * (y0 - y2) / d : 0.0f;
            const float freq = (float) (((float) best + juce::jlimit (-0.5f, 0.5f, offset)) * binHz);
            const float midi = 69.0f + 12.0f * std::log2 (freq / 440.0f);
            const int nearest = juce::roundToInt (midi);
            cents.target = juce::jlimit (-50.0f, 50.0f, (midi - (float) nearest) * 100.0f);

            textTimer += dt;
            if (textTimer > 0.12f)
            {
                textTimer = 0.0f;
                note = juce::String (kNoteNames[((nearest % 12) + 12) % 12]) + juce::String (nearest / 12 - 1);
                hz = freq;
                dirty = true;
            }
        }
        dirty |= cents.tick (dt);
        if (dirty) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (14, 12);
        drawLabel (g, utf8 ("PITCH  \xc2\xb7  TUNER"), r.removeFromTop (14));
        const float a = 0.35f + 0.65f * presence.current;

        auto top = r.removeFromTop (r.getHeight() * 0.45f);
        const bool inTune = std::abs (cents.position) < 5.0f && presence.current > 0.5f;
        g.setColour ((inTune ? theme::accent : theme::text).withAlpha (a));
        g.setFont (theme::mono (juce::jlimit (20.0f, 44.0f, top.getHeight() * 0.7f)));
        g.drawText (presence.current > 0.05f ? note : "--", top, juce::Justification::centredLeft, false);
        g.setFont (theme::mono (12.0f, false));
        g.setColour (theme::label.withAlpha (a));
        g.drawText (presence.current > 0.05f ? juce::String (hz, 1) + " Hz" : juce::String(), top, juce::Justification::centredRight, false);

        // cents meter
        const auto c = Point<float> (r.getCentreX(), r.getBottom() - 4);
        const float radius = juce::jmin (r.getWidth() * 0.45f, r.getHeight() - 10.0f);
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, radius, radius, 0, -1.0f, 1.0f, true);
        g.setColour (Colour (0xff8a8a8a));
        g.strokePath (arc, juce::PathStrokeType (1.5f));
        for (int ct = -50; ct <= 50; ct += 10)
        {
            const float ang = (float) ct / 50.0f;
            const float len = ct == 0 ? 12.0f : 6.0f;
            g.setColour (ct == 0 ? theme::accent : Colour (0xffbdbdbd));
            g.drawLine (c.x + std::sin (ang) * radius, c.y - std::cos (ang) * radius,
                        c.x + std::sin (ang) * (radius - len), c.y - std::cos (ang) * (radius - len), ct == 0 ? 2.0f : 1.2f);
        }
        const float na = cents.position / 50.0f;
        g.setColour ((inTune ? theme::accent : Colour (0xffededed)).withAlpha (a));
        g.drawLine (c.x, c.y, c.x + std::sin (na) * (radius + 4), c.y - std::cos (na) * (radius + 4), 2.0f);
        g.fillEllipse (Rectangle<float> (8, 8).withCentre (c));
        drawLabel (g, (cents.position >= 0 ? "+" : "") + juce::String (juce::roundToInt (cents.position)) + " ct",
                   Rectangle<float> (c.x - 40, c.y - radius * 0.55f, 80, 14), juce::Justification::centred, theme::label.withAlpha (a), 10.5f);
    }

private:
    Spring cents;
    Smoothed presence;
    juce::String note { "--" };
    float hz = 0.0f, textTimer = 1.0f;
};

//==============================================================================
class KeyDisplay final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        bool dirty = false;
        for (int i = 0; i < 12; ++i)
        {
            bars[i].target = f.chroma[(size_t) i];
            dirty |= bars[i].tick (dt, 10.0f);
        }
        conf.target = f.keyConfidence;
        dirty |= conf.tick (dt, 4.0f);
        if (f.key != key) { key = f.key; fade.snap (0.0f); fade.target = 1.0f; dirty = true; }
        dirty |= fade.tick (dt, 6.0f);
        if (dirty) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (14, 12);
        drawLabel (g, "KEY", r.removeFromTop (14));
        r.removeFromTop (4);

        auto top = r.removeFromTop (juce::jmin (52.0f, r.getHeight() * 0.42f));
        auto badge = top.removeFromRight (56).withSizeKeepingCentre (52, 30);
        const float t = easeOutCubic (fade.current);
        g.setColour (theme::text.withAlpha (t));
        g.setFont (theme::font (juce::jlimit (16.0f, 26.0f, top.getHeight() * 0.55f), true));
        g.drawText (juce::String (KeyDetector::keyName (key)), top.translated (0, 6.0f * (1.0f - t)), juce::Justification::centredLeft, true);
        g.setColour (Colour (0xff3a3a3a));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (theme::accent.withAlpha (t));
        g.drawRoundedRectangle (badge, 4.0f, 1.2f);
        g.setFont (theme::mono (14.0f));
        g.drawText (juce::String (KeyDetector::camelot (key)), badge, juce::Justification::centred, false);

        auto confRow = r.removeFromTop (14);
        drawLabel (g, key < 0 ? utf8 ("listening\xe2\x80\xa6") : (conf.current > 0.45f ? "confident" : "tentative"), confRow, juce::Justification::centredLeft, Colour (0xffcfcfcf), 9.5f);
        auto confBar = confRow.removeFromRight (80).withSizeKeepingCentre (80, 4);
        g.setColour (Colour (0xff444444)); g.fillRect (confBar);
        g.setColour (theme::label); g.fillRect (confBar.withWidth (confBar.getWidth() * juce::jlimit (0.0f, 1.0f, conf.current)));
        r.removeFromTop (8);

        // chroma bars, scale notes lit
        const int tonic = key >= 0 ? key % 12 : -1;
        static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 }, minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
        auto inScale = [&] (int pc)
        {
            if (tonic < 0) return false;
            for (int s : (key >= 12 ? minorScale : majorScale)) if ((tonic + s) % 12 == pc) return true;
            return false;
        };
        auto labels = r.removeFromBottom (12);
        const float w = r.getWidth() / 12.0f;
        for (int i = 0; i < 12; ++i)
        {
            const auto col = Rectangle<float> (r.getX() + w * (float) i + 2, r.getY(), w - 4, r.getHeight());
            g.setColour (Colour (0xff4b4b4b));
            g.fillRect (col);
            const float h = col.getHeight() * juce::jlimit (0.0f, 1.0f, bars[i].current);
            g.setColour (i == tonic ? theme::accent : (inScale (i) ? Colour (0xffe8e8e8) : Colour (0xff8a8a8a)));
            g.fillRect (col.withTop (col.getBottom() - h));
            drawLabel (g, kNoteNames[i], Rectangle<float> (col.getX() - 4, labels.getY(), col.getWidth() + 8, 12), juce::Justification::centred,
                       inScale (i) ? theme::text : Colour (0xffb8b8b8), 9.0f);
        }
    }

private:
    Smoothed bars[12], conf, fade;
    int key = -2;
};

//==============================================================================
class Tempo final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        if (f.bpm > 0.0f && prevPhase > 0.75f && f.beatPhase < 0.25f)
        {
            beat = (beat + 1) % 4;
            flash[beat].snap (1.0f);
            pulse.snap (1.0f);
        }
        prevPhase = f.beatPhase;
        for (auto& fl : flash) { fl.target = 0.0f; fl.tick (dt, 5.0f); }
        pulse.target = 0.0f;
        pulse.tick (dt, 7.0f);
        textTimer += dt;
        if (textTimer > 0.25f) { textTimer = 0.0f; bpm = f.bpm; confidence = f.bpmConfidence; }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (14, 12);
        drawLabel (g, "TEMPO", r.removeFromTop (14));

        auto ringArea = r.removeFromLeft (juce::jmin (r.getHeight(), r.getWidth() * 0.4f));
        const auto c = ringArea.getCentre();
        const float rad = juce::jmin (ringArea.getWidth(), ringArea.getHeight()) * 0.36f;
        g.setColour (theme::accent.withAlpha (0.18f * pulse.current));
        g.fillEllipse (Rectangle<float> (rad * 2.0f, rad * 2.0f).expanded (rad * 0.5f * pulse.current).withCentre (c));
        g.setColour (Colour (0xff6a6a6a).interpolatedWith (theme::accent, pulse.current));
        g.drawEllipse (Rectangle<float> (rad * 2, rad * 2).withCentre (c), 2.0f + 2.0f * pulse.current);
        for (int i = 0; i < 4; ++i)
        {
            const float ang = (float) i / 4.0f * 2.0f * kPi;
            const auto p = Point<float> (c.x + std::sin (ang) * rad, c.y - std::cos (ang) * rad);
            g.setColour (Colour (0xff8a8a8a).interpolatedWith (i == 0 ? theme::accent : juce::Colours::white, flash[i].current));
            g.fillEllipse (Rectangle<float> (9, 9).expanded (3.0f * flash[i].current).withCentre (p));
        }

        r.removeFromLeft (12);
        auto big = r.removeFromTop (r.getHeight() * 0.6f);
        g.setColour (theme::text);
        g.setFont (theme::mono (juce::jlimit (22.0f, 46.0f, big.getHeight() * 0.7f)));
        const auto txt = bpm > 0.0f ? juce::String (bpm, 1) : juce::String ("--");
        g.drawText (txt, big, juce::Justification::bottomLeft, false);
        drawLabel (g, "BPM", r.removeFromTop (14), juce::Justification::centredLeft, theme::label, 11.0f);
        if (bpm > 0.0f)
            drawLabel (g, utf8 ("\xc2\xbd ") + juce::String (bpm * 0.5f, 1) + utf8 ("  \xc2\xb7  2\xc3\x97 ") + juce::String (bpm * 2.0f, 1)
                       + (confidence < 0.2f ? utf8 ("  \xc2\xb7  unsure") : juce::String()),
                       r.removeFromTop (14), juce::Justification::centredLeft, Colour (0xffbdbdbd), 9.5f);
    }

private:
    Smoothed flash[4], pulse;
    int beat = 0;
    float prevPhase = 0.0f, bpm = 0.0f, confidence = 0.0f, textTimer = 1.0f;
};

//==============================================================================
class Dynamics final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        // peak held over ~1 s against the 300 ms RMS (an instantaneous frame peak underestimates the crest)
        peakHold = juce::jmax (juce::jmax (f.peakDb[0], f.peakDb[1]), peakHold - dt * 6.0f);
        if (f.receivedAudio && f.rmsDb[0] > -70.0f)
        {
            const float crestNow = peakHold - juce::jmax (f.rmsDb[0], f.rmsDb[1]);
            crest.target = juce::jlimit (0.0f, 30.0f, crestNow);
        }
        crest.tick (dt, 1.5f);
        plr = f.integratedLufs > -70.0f ? f.maxPeakDb - f.integratedLufs : 0.0f;

        histTimer += dt;
        if (histTimer > 0.1f) { histTimer = 0.0f; history.push_back (crest.current); if (history.size() > 150) history.erase (history.begin()); }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (14, 12);
        drawLabel (g, "DYNAMICS", r.removeFromTop (14));
        r.removeFromTop (4);

        const float c = crest.current;
        const char* verdict = c < 6.0f ? "Squashed" : (c < 9.0f ? "Dense" : (c < 13.0f ? "Punchy" : "Dynamic"));
        auto top = r.removeFromTop (juce::jmin (46.0f, r.getHeight() * 0.35f));
        g.setColour (theme::text);
        g.setFont (theme::mono (juce::jlimit (18.0f, 32.0f, top.getHeight() * 0.75f)));
        g.drawText (juce::String (c, 1) + " dB", top, juce::Justification::centredLeft, false);
        drawLabel (g, verdict, top, juce::Justification::centredRight, c < 6.0f ? theme::accent : theme::text, 13.0f);
        drawLabel (g, "crest factor (peak - RMS)", r.removeFromTop (14), juce::Justification::centredLeft, Colour (0xffcfcfcf), 9.5f);
        r.removeFromTop (4);

        // scale with zones
        auto bar = r.removeFromTop (10);
        const float zones[] = { 0, 6, 9, 13, 20 };
        const Colour zc[] = { Colour (0xff6a4a2a), Colour (0xff5a5a5a), Colour (0xff6a6a6a), Colour (0xff7a7a7a) };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (zc[i]);
            g.fillRect (Rectangle<float>::leftTopRightBottom (bar.getX() + bar.getWidth() * zones[i] / 20.0f, bar.getY(), bar.getX() + bar.getWidth() * zones[i + 1] / 20.0f, bar.getBottom()));
        }
        const float x = bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, c / 20.0f);
        g.setColour (juce::Colours::white);
        g.fillRect (Rectangle<float> (x - 1.5f, bar.getY() - 3, 3.0f, bar.getHeight() + 6));
        r.removeFromTop (8);

        drawLabel (g, "PLR  " + (plr > 0.0f ? juce::String (plr, 1) + " dB" : juce::String ("--")), r.removeFromTop (14), juce::Justification::centredLeft, theme::label, 10.5f);
        r.removeFromTop (6);

        if (r.getHeight() > 20.0f && history.size() > 1)
        {
            g.setColour (Colour (0xff4b4b4b));
            g.fillRect (r);
            juce::Path p;
            const float step = r.getWidth() / 149.0f;
            const float x0 = r.getRight() - step * (float) (history.size() - 1);
            for (size_t i = 0; i < history.size(); ++i)
            {
                const float y = r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, history[i] / 20.0f);
                if (i == 0) p.startNewSubPath (x0, y); else p.lineTo (x0 + step * (float) i, y);
            }
            g.setColour (Colour (0xffededed));
            g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
        }
    }

private:
    Smoothed crest { 10.0f, 10.0f };
    float plr = 0.0f, histTimer = 0.0f, peakHold = kSilenceDb;
    std::vector<float> history;
};

//==============================================================================
class TruePeakMeter final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        now.target = juce::jlimit (0.0f, 1.0f, (f.truePeakDb + 24.0f) / 27.0f);
        now.tick (dt, now.target > now.current ? 60.0f : 6.0f);
        if (f.clippedSamples + f.intersampleOvers > lastCount) lamp.snap (1.0f);
        lastCount = f.clippedSamples + f.intersampleOvers;
        lamp.target = 0.0f;
        lamp.tick (dt, 2.5f);
        maxTp = f.maxTruePeakDb;
        clipped = f.clippedSamples;
        overs = f.intersampleOvers;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (14, 12);
        auto head = r.removeFromTop (14);
        drawLabel (g, utf8 ("TRUE PEAK  \xc2\xb7  CLIPS"), head);
        const auto lampR = Rectangle<float> (10, 10).withCentre ({ head.getRight() - 6, head.getCentreY() });
        g.setColour (Colour (0xff474747).interpolatedWith (theme::accent, lamp.current));
        g.fillEllipse (lampR);
        r.removeFromTop (4);

        auto big = r.removeFromTop (juce::jmin (48.0f, r.getHeight() * 0.38f));
        g.setColour (maxTp > -1.0f ? theme::accent : theme::text);
        g.setFont (theme::mono (juce::jlimit (18.0f, 34.0f, big.getHeight() * 0.75f)));
        g.drawText (formatDb (maxTp), big, juce::Justification::centredLeft, false);
        drawLabel (g, "dBTP max", big, juce::Justification::centredRight, theme::label, 11.0f);

        auto bar = r.removeFromTop (12);
        g.setColour (Colour (0xff444444));
        g.fillRect (bar);
        const float zeroX = bar.getX() + bar.getWidth() * (24.0f / 27.0f);
        const float x = bar.getX() + bar.getWidth() * now.current;
        g.setColour (Colour (0xffe2e2e2));
        g.fillRect (bar.withWidth (juce::jmin (x, zeroX) - bar.getX()));
        if (x > zeroX) { g.setColour (theme::accent); g.fillRect (Rectangle<float>::leftTopRightBottom (zeroX, bar.getY(), x, bar.getBottom())); }
        g.setColour (juce::Colours::white);
        g.drawVerticalLine ((int) zeroX, bar.getY() - 3, bar.getBottom() + 3);
        auto scale = r.removeFromTop (13);
        drawLabel (g, "-24", scale, juce::Justification::centredLeft, Colour (0xffcfcfcf), 9.0f);
        drawLabel (g, "0", scale.withX (zeroX - 10).withWidth (20), juce::Justification::centred, Colour (0xffcfcfcf), 9.0f);
        r.removeFromTop (8);

        const float colW = r.getWidth() * 0.5f;
        auto cell = [&] (Rectangle<float> c, const char* name, int count)
        {
            drawLabel (g, name, c.removeFromTop (13), juce::Justification::centredLeft, Colour (0xffcfcfcf), 9.5f);
            g.setColour (count > 0 ? theme::accent : theme::text);
            g.setFont (theme::mono (16.0f));
            g.drawText (juce::String (count), c.removeFromTop (22), juce::Justification::centredLeft, false);
        };
        cell (r.withWidth (colW), "Clipped samples", clipped);
        cell (r.withTrimmedLeft (colW), "Inter-sample overs", overs);
    }

private:
    Smoothed now, lamp;
    float maxTp = kSilenceDb;
    int clipped = 0, overs = 0, lastCount = 0;
};

//==============================================================================
class Imager final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        bool dirty = false;
        for (int i = 0; i < AnalysisFrame::kWidthBands; ++i)
        {
            w[i].target = f.bandWidth[(size_t) i];
            dirty |= w[i].tick (dt, 6.0f);
        }
        if (dirty) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (14, 12);
        drawLabel (g, "STEREO IMAGER", r.removeFromTop (14));
        auto warn = r.removeFromBottom (14);
        r.removeFromTop (6);

        static const char* names[] = { "SUB", "LOW", "MID", "HIGH", "AIR" };
        static const char* ranges[] = { "20-120", "120-500", "0.5-2k", "2k-6k", "6k-20k" };
        const float rowH = r.getHeight() / (float) AnalysisFrame::kWidthBands;
        for (int i = AnalysisFrame::kWidthBands - 1; i >= 0; --i)
        {
            auto row = r.removeFromTop (rowH).reduced (0, juce::jmin (5.0f, rowH * 0.15f));
            auto labelArea = row.removeFromLeft (64);
            const int band = AnalysisFrame::kWidthBands - 1 - i;   // top row = air
            drawLabel (g, names[band], labelArea.removeFromTop (labelArea.getHeight() * 0.55f), juce::Justification::bottomLeft, theme::text, 10.0f);
            drawLabel (g, ranges[band], labelArea, juce::Justification::topLeft, Colour (0xffb8b8b8), 8.5f);

            auto pct = row.removeFromRight (42);
            g.setColour (Colour (0xff4b4b4b));
            g.fillRect (row);
            g.setColour (Colour (0xff7a7a7a));
            g.drawVerticalLine ((int) row.getCentreX(), row.getY(), row.getBottom());

            const float v = w[band].current;
            const float half = row.getWidth() * 0.5f * juce::jlimit (0.0f, 1.0f, v * 2.0f);
            const bool outOfPhase = v > 0.55f, wideBass = band == 0 && v > 0.25f;
            g.setColour (outOfPhase || wideBass ? theme::accent : Colour (0xffe2e2e2));
            g.fillRect (Rectangle<float> (row.getCentreX() - half, row.getY() + 2, half * 2.0f, row.getHeight() - 4));
            drawLabel (g, juce::String (juce::roundToInt (juce::jmin (200.0f, v * 200.0f))) + "%", pct, juce::Justification::centredRight, theme::text, 10.0f);
        }

        if (w[0].current > 0.25f)
            drawLabel (g, utf8 ("Sub is wide \xe2\x80\x94 check it in mono"), warn, juce::Justification::centredLeft, theme::accent, 9.5f);
        else if (w[2].current > 0.55f || w[3].current > 0.55f)
            drawLabel (g, "Out-of-phase content in the mids", warn, juce::Justification::centredLeft, theme::accent, 9.5f);
    }

private:
    Smoothed w[AnalysisFrame::kWidthBands];
};

//==============================================================================
class TonalBalance final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        static const float edges[5] = { 20.0f, 250.0f, 2000.0f, 6000.0f, 20000.0f };
        const double binHz = f.sampleRate / kFftSize;
        double energy[4] {}, total = 0.0;
        for (int band = 0; band < 4; ++band)
            for (int b = juce::jmax (1, (int) (edges[band] / binHz)); b < juce::jmin (kNumBins, (int) (edges[band + 1] / binHz)); ++b)
            {
                const double e = std::pow (10.0, f.spectrumDb[(size_t) b] / 10.0);
                energy[band] += e;
                total += e;
            }
        bool dirty = false;
        for (int band = 0; band < 4; ++band)
        {
            level[band].target = total > 1.0e-12 ? juce::jlimit (-36.0f, 0.0f, (float) (10.0 * std::log10 (energy[band] / total + 1.0e-12))) : -36.0f;
            dirty |= level[band].tick (dt, 3.0f);
        }
        if (dirty) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (14, 12);
        drawLabel (g, "TONAL BALANCE", r.removeFromTop (14));
        drawLabel (g, "vs. modern pop / electronic", r.removeFromBottom (12), juce::Justification::centredLeft, Colour (0xffb8b8b8), 9.0f);
        r.removeFromTop (6);
        r.removeFromBottom (4);
        auto labels = r.removeFromBottom (14);

        // reference corridors: share of total energy per band (approximate)
        static const float lo[4] = { -6.0f, -11.0f, -20.0f, -28.0f }, hi[4] = { -1.0f, -4.0f, -11.0f, -17.0f };
        static const char* names[4] = { "LOW", "LOW-MID", "HIGH-MID", "HIGH" };
        auto toY = [&] (float db) { return r.getBottom() - r.getHeight() * (db + 36.0f) / 36.0f; };
        const float colW = r.getWidth() / 4.0f;
        for (int b = 0; b < 4; ++b)
        {
            auto col = Rectangle<float> (r.getX() + colW * (float) b, r.getY(), colW, r.getHeight()).reduced (colW * 0.22f, 0);
            g.setColour (Colour (0xff4b4b4b));
            g.fillRoundedRectangle (col, 3.0f);
            const auto corridor = Rectangle<float>::leftTopRightBottom (col.getX(), toY (hi[b]), col.getRight(), toY (lo[b]));
            g.setColour (Colour (0xff6e6e6e));
            g.fillRoundedRectangle (corridor, 3.0f);

            const float v = level[b].current;
            const bool outside = v > hi[b] || v < lo[b];
            const float y = toY (v);
            g.setColour (outside ? theme::accent : juce::Colours::white);
            g.fillRoundedRectangle (Rectangle<float> (col.getX() - 4, y - 2.5f, col.getWidth() + 8, 5.0f), 2.5f);
            drawLabel (g, names[b], Rectangle<float> (r.getX() + colW * (float) b, labels.getY(), colW, 14), juce::Justification::centred, theme::text, 9.5f);
        }
    }

private:
    Smoothed level[4] { { -36.0f, -36.0f }, { -36.0f, -36.0f }, { -36.0f, -36.0f }, { -36.0f, -36.0f } };
};

//==============================================================================
class Waterfall final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        timer += dt;
        if (timer < 1.0f / 24.0f) return;
        timer = 0.0f;
        std::vector<float> row (kPoints);
        for (int i = 0; i < kPoints; ++i)
            row[(size_t) i] = juce::jlimit (0.0f, 1.0f, (plug::bandLevel (f, plug::bandEdge (i, kPoints, 30.0f, 16000.0f), plug::bandEdge (i + 1, kPoints, 30.0f, 16000.0f)) + 80.0f) / 72.0f);
        rows.push_back (std::move (row));
        if ((int) rows.size() > kRows) rows.erase (rows.begin());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds().toFloat().reduced (12, 12);
        const int n = (int) rows.size();
        for (int k = 0; k < n; ++k)
        {
            const float d = n > 1 ? (float) (n - 1 - k) / (float) (kRows - 1) : 0.0f;   // 0 = newest/front
            const float baseY = a.getBottom() - d * a.getHeight() * 0.55f;
            const float x0 = a.getX() + d * a.getWidth() * 0.2f;
            const float w = a.getWidth() * (1.0f - d * 0.32f);
            const float h = a.getHeight() * 0.48f * (1.0f - d * 0.35f);

            juce::Path p;
            p.startNewSubPath (x0, baseY);
            for (int i = 0; i < kPoints; ++i)
                p.lineTo (x0 + w * (float) i / (float) (kPoints - 1), baseY - h * rows[(size_t) k][(size_t) i]);
            p.lineTo (x0 + w, baseY);
            juce::Path closed (p);
            closed.closeSubPath();
            g.setColour (theme::panel);
            g.fillPath (closed);
            g.setColour ((k == n - 1 ? theme::accent : juce::Colours::white).withAlpha (k == n - 1 ? 1.0f : 0.12f + 0.75f * (1.0f - d)));
            g.strokePath (p, juce::PathStrokeType (k == n - 1 ? 1.6f : 1.0f));
        }
    }

private:
    static constexpr int kRows = 36, kPoints = 80;
    std::vector<std::vector<float>> rows;
    float timer = 0.0f;
};

//==============================================================================
class Radial final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        for (int i = 0; i < kBands; ++i)
        {
            const float v = juce::jlimit (0.0f, 1.0f, (plug::bandLevel (f, plug::bandEdge (i, kBands, 30.0f, 16000.0f), plug::bandEdge (i + 1, kBands, 30.0f, 16000.0f)) + 78.0f) / 70.0f);
            bands[i].target = v;
            bands[i].tick (dt, v > bands[i].current ? 40.0f : 8.0f);
        }
        if (f.onset > 0.6f) pulse.snap (juce::jmax (pulse.current, f.onset));
        pulse.target = 0.0f;
        pulse.tick (dt, 6.0f);
        rotation += dt * 0.12f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto c = b.getCentre();
        const float m = juce::jmin (b.getWidth(), b.getHeight());
        const float r0 = m * 0.17f * (1.0f + 0.15f * pulse.current);
        const float len = m * 0.3f;

        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < kBands; ++i)
            {
                const float t = ((float) i + 0.5f) / (float) kBands;
                const float ang = rotation + (side == 0 ? t : -t) * kPi;
                const float v = bands[i].current;
                const Point<float> p0 (c.x + std::sin (ang) * r0, c.y - std::cos (ang) * r0);
                const Point<float> p1 (c.x + std::sin (ang) * (r0 + 3.0f + v * len), c.y - std::cos (ang) * (r0 + 3.0f + v * len));
                g.setColour (Colour (0xffd8d8d8).interpolatedWith (theme::accent, juce::jlimit (0.0f, 1.0f, v * 1.3f - 0.3f)));
                g.drawLine ({ p0, p1 }, juce::jmax (1.5f, m * 0.012f));
            }

        g.setColour (Colour (0xff2e2e2e));
        g.fillEllipse (Rectangle<float> (r0 * 1.8f, r0 * 1.8f).withCentre (c));
        g.setColour (theme::accent.withAlpha (0.3f + 0.7f * pulse.current));
        g.drawEllipse (Rectangle<float> (r0 * 1.8f, r0 * 1.8f).withCentre (c), 1.5f + 2.0f * pulse.current);
    }

private:
    static constexpr int kBands = 48;
    Smoothed bands[kBands], pulse;
    float rotation = 0.0f;
};

//==============================================================================
class Ambient final : public Visualizer
{
public:
    Ambient() { trackChanged ({}); }

    void trackChanged (const juce::Image& cover) override
    {
        palette.clear();
        if (cover.isValid())
        {
            auto small = cover.rescaled (12, 12, juce::Graphics::mediumResamplingQuality);
            std::vector<std::pair<float, Colour>> candidates;
            for (int y = 0; y < 12; ++y)
                for (int x = 0; x < 12; ++x)
                {
                    const auto c = small.getPixelAt (x, y);
                    candidates.push_back ({ c.getSaturation() * 0.7f + c.getBrightness() * 0.3f, c });
                }
            std::sort (candidates.begin(), candidates.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            for (auto& [score, c] : candidates)
            {
                bool distinct = true;
                for (auto& p : palette)
                    if (std::abs (p.getHue() - c.getHue()) < 0.07f && std::abs (p.getBrightness() - c.getBrightness()) < 0.2f) distinct = false;
                if (distinct) palette.push_back (c.withBrightness (juce::jmax (0.45f, c.getBrightness())));
                if (palette.size() == 4) break;
            }
        }
        const Colour fallback[] = { theme::accent, Colour (0xffc0763a), Colour (0xff9a9a9a), Colour (0xfff2d2a0) };
        for (int i = (int) palette.size(); i < 4; ++i) palette.push_back (fallback[i]);
    }

    void update (const AnalysisFrame& f, float dt) override
    {
        time += dt;
        bass.target = juce::jlimit (0.0f, 1.0f, (plug::bandLevel (f, 30.0f, 160.0f) + 50.0f) / 40.0f);
        bass.tick (dt, 8.0f);
        if (f.onset > 0.6f) pulse.snap (juce::jmax (pulse.current, f.onset));
        pulse.target = 0.0f;
        pulse.tick (dt, 4.0f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (Colour (0xff262626));
        g.fillRect (b);
        const float m = juce::jmax (b.getWidth(), b.getHeight());
        const float speeds[4][2] = { { 0.21f, 0.17f }, { 0.13f, 0.23f }, { 0.19f, 0.11f }, { 0.09f, 0.15f } };
        for (int i = 0; i < 4; ++i)
        {
            const Point<float> p (b.getCentreX() + std::sin (time * speeds[i][0] * 2.0f + (float) i * 1.7f) * b.getWidth() * 0.32f,
                                  b.getCentreY() + std::cos (time * speeds[i][1] * 2.0f + (float) i * 2.3f) * b.getHeight() * 0.3f);
            const float radius = m * (0.32f + 0.12f * bass.current + 0.08f * pulse.current);
            const float alpha = 0.35f + 0.35f * bass.current + 0.2f * pulse.current;
            juce::ColourGradient grad (palette[(size_t) i].withAlpha (alpha), p.x, p.y, palette[(size_t) i].withAlpha (0.0f), p.x + radius, p.y, true);
            g.setGradientFill (grad);
            g.fillEllipse (Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));
        }
    }

private:
    std::vector<Colour> palette;
    Smoothed bass, pulse;
    float time = 0.0f;
};

//==============================================================================
class Vinyl final : public Visualizer
{
public:
    void trackChanged (const juce::Image& cover) override { label = cover; }

    void update (const AnalysisFrame& f, float dt) override
    {
        speed.target = f.playing ? 2.0f * kPi * 33.333f / 60.0f : 0.0f;
        speed.tick (dt, f.playing ? 2.5f : 1.2f);
        angle += speed.current * dt;
        const float progress = f.lengthSeconds > 0.0 ? (float) (f.positionSeconds / f.lengthSeconds) : 0.0f;
        arm.target = (f.playing || progress > 0.001f) ? 0.42f + 0.38f * juce::jlimit (0.0f, 1.0f, progress) : 0.05f;
        arm.tick (dt, 3.0f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (10);
        const float R = juce::jmin (b.getHeight() * 0.48f, b.getWidth() * 0.36f);
        const Point<float> c (b.getX() + R + b.getWidth() * 0.06f, b.getCentreY());

        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillEllipse (Rectangle<float> (R * 2, R * 2).withCentre (c.translated (2, 4)));
        g.setColour (Colour (0xff141414));
        g.fillEllipse (Rectangle<float> (R * 2, R * 2).withCentre (c));
        for (float rr = R * 0.96f; rr > R * 0.42f; rr -= R * 0.035f)
        {
            g.setColour (Colour (0xff222222));
            g.drawEllipse (Rectangle<float> (rr * 2, rr * 2).withCentre (c), 0.8f);
        }
        juce::Path sheen;
        sheen.addPieSegment (Rectangle<float> (R * 2, R * 2).withCentre (c), -0.9f, -0.4f, 0.42f);
        sheen.addPieSegment (Rectangle<float> (R * 2, R * 2).withCentre (c), kPi - 0.9f, kPi - 0.4f, 0.42f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillPath (sheen);

        // label turns with the record
        const float lr = R * 0.38f;
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip;
            clip.addEllipse (Rectangle<float> (lr * 2, lr * 2).withCentre (c));
            g.reduceClipRegion (clip);
            g.addTransform (juce::AffineTransform::rotation (angle, c.x, c.y));
            if (label.isValid())
                g.drawImage (label, Rectangle<float> (lr * 2, lr * 2).withCentre (c), juce::RectanglePlacement::fillDestination);
            else
            {
                g.setColour (theme::accent);
                g.fillEllipse (Rectangle<float> (lr * 2, lr * 2).withCentre (c));
                g.setColour (theme::titleBlock);
                g.setFont (theme::font (juce::jmax (7.0f, lr * 0.2f), true));
                g.drawText ("TRANSPORT", Rectangle<float> (lr * 2, lr * 0.6f).withCentre (c.translated (0, -lr * 0.45f)), juce::Justification::centred, false);
            }
        }
        g.setColour (Colour (0xffd8d8d8));
        g.fillEllipse (Rectangle<float> (6, 6).withCentre (c));

        // tonearm
        const Point<float> pivot (c.x + R * 1.25f, c.y - R * 0.8f);
        const float L = R * 1.45f;
        const float a = kPi + arm.current;   // pointing down-left
        const Point<float> tip (pivot.x + std::sin (a) * L, pivot.y - std::cos (a) * L);
        g.setColour (Colour (0xff3a3a3a));
        g.fillEllipse (Rectangle<float> (R * 0.22f, R * 0.22f).withCentre (pivot));
        g.setColour (Colour (0xffcfcfcf));
        g.drawLine ({ pivot, tip }, juce::jmax (2.5f, R * 0.035f));
        g.setColour (Colour (0xffe8e8e8));
        g.fillRect (Rectangle<float> (R * 0.12f, R * 0.08f).withCentre (tip));
    }

private:
    juce::Image label;
    Smoothed speed, arm { 0.05f, 0.05f };
    float angle = 0.0f;
};

//==============================================================================
class Reels final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        run.target = f.playing ? 1.0f : 0.0f;
        run.tick (dt, f.playing ? 3.0f : 2.0f);
        progress = f.lengthSeconds > 0.0 ? (float) juce::jlimit (0.0, 1.0, f.positionSeconds / f.lengthSeconds) : 0.0f;
        const auto [rl, rr] = radii();
        const float v = reelSize() * 0.9f * run.current;   // tape speed, constant
        angleL += v / rl * dt;
        angleR += v / rr * dt;
        seconds = f.positionSeconds;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float R = reelSize();
        const Point<float> cl (b.getX() + b.getWidth() * 0.29f, b.getY() + b.getHeight() * 0.44f);
        const Point<float> cr (b.getX() + b.getWidth() * 0.71f, cl.y);
        const auto [rl, rr] = radii();

        // tape path under the reels
        const Point<float> head (b.getCentreX(), b.getBottom() - b.getHeight() * 0.12f);
        g.setColour (Colour (0xff2b2724));
        g.drawLine ({ cl.translated (-rl * 0.2f, rl * 0.98f), head.translated (-R * 0.4f, 0) }, 2.0f);
        g.drawLine ({ head.translated (-R * 0.4f, 0), head.translated (R * 0.4f, 0) }, 2.0f);
        g.drawLine ({ head.translated (R * 0.4f, 0), cr.translated (rr * 0.2f, rr * 0.98f) }, 2.0f);
        g.setColour (Colour (0xff8a8a8a));
        g.fillRoundedRectangle (Rectangle<float> (R * 0.5f, R * 0.22f).withCentre (head.translated (0, R * 0.08f)), 2.0f);

        auto reel = [&] (Point<float> c, float tape, float ang)
        {
            g.setColour (Colour (0xff2b2724));
            g.fillEllipse (Rectangle<float> (tape * 2, tape * 2).withCentre (c));
            g.setColour (Colour (0xff3d3833));
            g.drawEllipse (Rectangle<float> (tape * 2, tape * 2).withCentre (c), 1.0f);

            g.setColour (Colour (0xffb8b8b8).withAlpha (0.28f));
            g.fillEllipse (Rectangle<float> (R * 2, R * 2).withCentre (c));
            g.setColour (Colour (0xffd0d0d0));
            g.drawEllipse (Rectangle<float> (R * 2, R * 2).withCentre (c), 1.5f);
            for (int i = 0; i < 3; ++i)
            {
                const float a = ang + (float) i * 2.0f * kPi / 3.0f;
                const Point<float> w (c.x + std::sin (a) * R * 0.62f, c.y - std::cos (a) * R * 0.62f);
                g.setColour (theme::panel.withAlpha (0.85f));
                g.fillEllipse (Rectangle<float> (R * 0.5f, R * 0.5f).withCentre (w));
            }
            g.setColour (Colour (0xffe0e0e0));
            g.fillEllipse (Rectangle<float> (R * 0.34f, R * 0.34f).withCentre (c));
            for (int i = 0; i < 3; ++i)
            {
                const float a = ang + (float) i * 2.0f * kPi / 3.0f + kPi / 3.0f;
                g.setColour (Colour (0xff6a6a6a));
                g.drawLine (c.x, c.y, c.x + std::sin (a) * R * 0.17f, c.y - std::cos (a) * R * 0.17f, 2.0f);
            }
        };
        reel (cl, rl, angleL);
        reel (cr, rr, angleR);

        const int t = (int) seconds;
        g.setColour (theme::label);
        g.setFont (theme::mono (11.0f, false));
        g.drawText (juce::String::formatted ("%02d:%02d", t / 60, t % 60), Rectangle<float> (head.x - 30, b.getBottom() - 16, 60, 14), juce::Justification::centred, false);
    }

private:
    float reelSize() const { return juce::jmin ((float) getWidth() * 0.19f, (float) getHeight() * 0.36f); }
    std::pair<float, float> radii() const
    {
        const float R = reelSize(), hub = R * 0.24f, full = R * 0.93f;
        const float left = std::sqrt (hub * hub + (1.0f - progress) * (full * full - hub * hub));
        const float right = std::sqrt (hub * hub + progress * (full * full - hub * hub));
        return { juce::jmax (hub, left), juce::jmax (hub, right) };
    }

    Smoothed run;
    float progress = 0.0f, angleL = 0.0f, angleR = 0.0f;
    double seconds = 0.0;
};

//==============================================================================
class Particles final : public Visualizer
{
public:
    Particles() { pool.reserve (900); }

    void update (const AnalysisFrame& f, float dt) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        const float m = (float) juce::jmin (getWidth(), getHeight());
        cooldown -= dt;

        if (f.onset > 0.55f && cooldown <= 0.0f && f.playing)
        {
            cooldown = 0.09f;
            const int count = (int) (20.0f + 60.0f * f.onset);
            for (int i = 0; i < count && pool.size() < 900; ++i)
            {
                const float a = rng.nextFloat() * 2.0f * kPi;
                const float s = m * (0.6f + rng.nextFloat() * 1.6f) * f.onset;
                pool.push_back ({ c.x, c.y, std::sin (a) * s, -std::cos (a) * s, 0.0f, 0.6f + rng.nextFloat() * 1.2f,
                                  1.2f + rng.nextFloat() * 2.4f, rng.nextFloat() < 0.35f });
            }
            flash.snap (f.onset);
        }

        // gentle drift proportional to level
        const float level = juce::jlimit (0.0f, 1.0f, (juce::jmax (f.rmsDb[0], f.rmsDb[1]) + 40.0f) / 40.0f);
        spawn += dt * 30.0f * level;
        while (spawn > 1.0f && pool.size() < 900)
        {
            spawn -= 1.0f;
            const float a = rng.nextFloat() * 2.0f * kPi;
            pool.push_back ({ c.x + std::sin (a) * m * 0.1f, c.y - std::cos (a) * m * 0.1f, std::sin (a) * m * 0.15f, -std::cos (a) * m * 0.15f,
                              0.0f, 1.5f + rng.nextFloat(), 1.0f, false });
        }

        const float drag = std::exp (-dt * 1.6f);
        for (auto& p : pool)
        {
            p.vx *= drag; p.vy = p.vy * drag + 25.0f * dt;
            p.x += p.vx * dt; p.y += p.vy * dt;
            p.age += dt;
        }
        pool.erase (std::remove_if (pool.begin(), pool.end(), [] (const P& p) { return p.age >= p.life; }), pool.end());

        flash.target = 0.0f;
        flash.tick (dt, 6.0f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        const float m = (float) juce::jmin (getWidth(), getHeight());
        juce::ColourGradient glow (theme::accent.withAlpha (0.25f * flash.current), c.x, c.y, theme::accent.withAlpha (0.0f), c.x + m * 0.45f, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (Rectangle<float> (m * 0.9f, m * 0.9f).withCentre (c));

        for (auto& p : pool)
        {
            const float a = 1.0f - p.age / p.life;
            g.setColour ((p.accent ? theme::accent : juce::Colours::white).withAlpha (a * 0.9f));
            g.fillEllipse (p.x - p.size * 0.5f, p.y - p.size * 0.5f, p.size, p.size);
        }
    }

private:
    struct P { float x, y, vx, vy, age, life, size; bool accent; };
    std::vector<P> pool;
    juce::Random rng;
    Smoothed flash;
    float cooldown = 0.0f, spawn = 0.0f;
};
} // namespace

std::unique_ptr<Visualizer> makeTuner()        { return std::make_unique<Tuner>(); }
std::unique_ptr<Visualizer> makeKeyDisplay()   { return std::make_unique<KeyDisplay>(); }
std::unique_ptr<Visualizer> makeTempo()        { return std::make_unique<Tempo>(); }
std::unique_ptr<Visualizer> makeDynamics()     { return std::make_unique<Dynamics>(); }
std::unique_ptr<Visualizer> makeTruePeak()     { return std::make_unique<TruePeakMeter>(); }
std::unique_ptr<Visualizer> makeImager()       { return std::make_unique<Imager>(); }
std::unique_ptr<Visualizer> makeTonalBalance() { return std::make_unique<TonalBalance>(); }
std::unique_ptr<Visualizer> makeWaterfall()    { return std::make_unique<Waterfall>(); }
std::unique_ptr<Visualizer> makeRadial()       { return std::make_unique<Radial>(); }
std::unique_ptr<Visualizer> makeAmbient()      { return std::make_unique<Ambient>(); }
std::unique_ptr<Visualizer> makeVinyl()        { return std::make_unique<Vinyl>(); }
std::unique_ptr<Visualizer> makeReels()        { return std::make_unique<Reels>(); }
std::unique_ptr<Visualizer> makeParticles()    { return std::make_unique<Particles>(); }
} // namespace tp
