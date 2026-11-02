#include "Visualizers.h"

namespace tp
{
namespace
{
using juce::Colour;
using juce::Rectangle;
using juce::Point;

constexpr float degToRad (float d) { return d * juce::MathConstants<float>::pi / 180.0f; }

juce::AffineTransform fitTransform (Rectangle<float> b, float vw, float vh)
{
    const float s = juce::jmin (b.getWidth() / vw, b.getHeight() / vh);
    return juce::AffineTransform::scale (s).translated (b.getCentreX() - vw * s * 0.5f, b.getCentreY() - vh * s * 0.5f);
}

/** Level of a log-frequency band, with a +4.5 dB/oct tilt so music looks balanced. */
float bandLevel (const AnalysisFrame& f, float lo, float hi)
{
    const float binHz = (float) (f.sampleRate / kFftSize);
    const float bLo = lo / binHz, bHi = hi / binHz;
    float level;

    if (bHi - bLo < 1.0f)
    {
        const float c = juce::jlimit (1.0f, (float) kNumBins - 2.0f, 0.5f * (bLo + bHi));
        const int i = (int) c;
        const float t = c - (float) i;
        level = f.spectrumDb[(size_t) i] * (1.0f - t) + f.spectrumDb[(size_t) i + 1] * t;
    }
    else
    {
        level = kSilenceDb;
        for (int b = juce::jmax (1, (int) bLo); b <= juce::jmin (kNumBins - 1, (int) bHi); ++b)
            level = juce::jmax (level, f.spectrumDb[(size_t) b]);
    }
    return level + 4.5f * std::log2 (std::sqrt (lo * hi) / 1000.0f);
}

float bandEdge (int i, int n, float lo = 20.0f, float hi = 20000.0f)
{
    return lo * std::pow (hi / lo, (float) i / (float) n);
}

juce::String formatDb (float v, int decimals = 1)
{
    if (v <= -99.0f) return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93\xe2\x88\x9e"));   // –∞
    return (v > 0.05f ? "+" : "") + juce::String (v, decimals);
}

void drawLabel (juce::Graphics& g, const juce::String& t, Rectangle<float> r, juce::Justification j = juce::Justification::centredLeft, Colour c = theme::label, float px = 10.0f)
{
    g.setColour (c);
    g.setFont (theme::font (px));
    g.drawText (t, r, j, false);
}

//==============================================================================
class VuMeter final : public Visualizer
{
public:
    VuMeter()
    {
        for (auto& n : needles) { n.position = n.target = -50.0f; n.stiffness = 90.0f; n.damping = 11.0f; }
    }

    void update (const AnalysisFrame& f, float dt) override
    {
        bool dirty = false;
        for (int c = 0; c < 2; ++c)
        {
            needles[c].target = vuToAngle (stereo() ? f.vu[c] : juce::jmax (f.vu[0], f.vu[1]));
            leds[c].target = (stereo() ? f.vu[c] : juce::jmax (f.vu[0], f.vu[1])) > 1.0f ? 1.0f : 0.0f;
            dirty |= needles[c].tick (dt);
            dirty |= leds[c].tick (dt, leds[c].target > leds[c].current ? 40.0f : 4.0f);
        }
        if (dirty) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (10.0f);
        if (stereo())
        {
            const float w = (area.getWidth() - 10.0f) * 0.5f;
            paintFace (g, area.removeFromLeft (w), 0, "L");
            area.removeFromLeft (10.0f);
            paintFace (g, area, 1, "R");
        }
        else paintFace (g, area, 0, nullptr);
    }

private:
    bool stereo() const { return getWidth() > getHeight() * 2.3f; }

    /** One meter face in a 300 x 190 design box: scale fully visible, pivot under a hub cap. */
    void paintFace (juce::Graphics& g, Rectangle<float> cell, int ch, const char* channel)
    {
        const auto t = fitTransform (cell, 300.0f, 190.0f);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (t);

        const auto face = Rectangle<float> (0, 0, 300, 190);
        juce::Path clip;
        clip.addRoundedRectangle (face, 8.0f);
        g.setColour (Colour (0xff5f5f5f));
        g.fillPath (clip);
        g.reduceClipRegion (clip);
        juce::ColourGradient shade (juce::Colours::black.withAlpha (0.18f), 0, 0, juce::Colours::transparentBlack, 0, 40, false);
        g.setGradientFill (shade);
        g.fillRect (face.withHeight (40));

        const float cx = 150.0f, cy = 222.0f, R = 150.0f;
        juce::Path arc;
        arc.addCentredArc (cx, cy, R, R, 0, degToRad (-48), degToRad (48), true);
        g.setColour (theme::scale);
        g.strokePath (arc, juce::PathStrokeType (1.6f));
        juce::Path red;
        red.addCentredArc (cx, cy, R + 3.0f, R + 3.0f, 0, degToRad (12), degToRad (48), true);
        g.setColour (Colour (0xff8e8e8e).interpolatedWith (theme::accent, 0.25f + leds[ch].current * 0.6f));
        g.strokePath (red, juce::PathStrokeType (5.0f));

        static const std::pair<float, const char*> db[] = { { -46, "20" }, { -37, "10" }, { -28, "7" }, { -20, "5" }, { -10, "3" }, { -3, "2" },
                                                            { 4, "1" }, { 12, "0" }, { 25, "+1" }, { 37, "+2" }, { 48, "+3" } };
        static const std::pair<float, const char*> pct[] = { { -48, "0" }, { -40, "20" }, { -30, "40" }, { -16, "60" }, { -2, "80" }, { 12, "100" } };
        for (auto& [deg, txt] : db)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.addTransform (juce::AffineTransform::rotation (degToRad (deg), cx, cy));
            g.setColour (theme::scale);
            g.drawLine (cx, cy - R, cx, cy - R - 9, 1.6f);
            g.setColour (deg > 11.0f ? Colour (0xfff0d4a8) : Colour (0xffdedede));
            g.setFont (juce::Font (juce::FontOptions (theme::sansName(), 15.0f, juce::Font::bold)));
            g.drawText (txt, Rectangle<float> (cx - 18, cy - R - 28, 36, 17), juce::Justification::centred, false);
        }
        for (auto& [deg, txt] : pct)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.addTransform (juce::AffineTransform::rotation (degToRad (deg), cx, cy));
            g.setColour (theme::scale);
            g.drawLine (cx, cy - R, cx, cy - R + 6, 1.2f);
            g.setColour (Colour (0xffc8c8c8));
            g.setFont (theme::font (8.5f));
            g.drawText (txt, Rectangle<float> (cx - 16, cy - R + 7, 32, 12), juce::Justification::centred, false);
        }

        g.setColour (Colour (0xffcfcfcf));
        g.setFont (juce::Font (juce::FontOptions (theme::sansName(), 17.0f, juce::Font::bold)));
        g.drawText ("VU", Rectangle<float> (cx - 40, 112, 80, 20), juce::Justification::centred, false);
        if (channel != nullptr)
        {
            g.setFont (theme::font (11.0f, true));
            g.drawText (channel, Rectangle<float> (12, 160, 30, 18), juce::Justification::centredLeft, false);
        }

        // peak lamp
        const Point<float> lamp (268, 30);
        g.setColour (Colour (0xff474747));
        g.fillEllipse (Rectangle<float> (9, 9).withCentre (lamp));
        if (leds[ch].current > 0.01f)
        {
            g.setColour (theme::accent.withAlpha (0.25f * leds[ch].current));
            g.fillEllipse (Rectangle<float> (22, 22).withCentre (lamp));
            g.setColour (theme::accent.withAlpha (leds[ch].current));
            g.fillEllipse (Rectangle<float> (9, 9).withCentre (lamp));
        }
        g.setColour (Colour (0xffbdbdbd));
        g.setFont (theme::font (8.0f, true));
        g.drawText ("PEAK", Rectangle<float> (lamp.x - 40, lamp.y - 6, 32, 12), juce::Justification::centredRight, false);

        // needle, then the hub cap that hides the pivot
        const float a = degToRad (needles[ch].position);
        const Point<float> tip (cx + (R + 6.0f) * std::sin (a), cy - (R + 6.0f) * std::cos (a));
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.drawLine ({ { cx + 2, cy }, tip.translated (2, 3) }, 2.2f);
        g.setColour (Colour (0xfff2f2f2));
        g.drawLine ({ { cx, cy }, tip }, 1.8f);
        g.setColour (Colour (0xff3a3a3a));
        g.fillEllipse (Rectangle<float> (64, 64).withCentre ({ cx, 196 }));
        g.setColour (Colour (0xff4a4a4a));
        g.drawEllipse (Rectangle<float> (64, 64).withCentre ({ cx, 196 }), 1.0f);
    }

    static float vuToAngle (float vu)
    {
        static const float x[] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
        static const float y[] = { -46, -37, -28, -20, -10, -3, 4, 12, 25, 37, 48 };
        if (vu <= -20.0f) return juce::jmax (-52.0f, -46.0f + (vu + 20.0f) * 0.6f);
        if (vu >= 3.0f) return juce::jmin (52.0f, 48.0f + (vu - 3.0f) * 4.0f);
        for (int i = 0; i < 10; ++i)
            if (vu <= x[i + 1])
                return y[i] + (vu - x[i]) / (x[i + 1] - x[i]) * (y[i + 1] - y[i]);
        return 48.0f;
    }

    Spring needles[2];
    Smoothed leds[2];
};

//==============================================================================
class PolarField final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        bool moved = false;
        for (int i = 0; i < kPolarBins; ++i)
        {
            fill[(size_t) i].target = f.polar[(size_t) i];
            outline[(size_t) i].target = f.polarPeak[(size_t) i];
            moved |= fill[(size_t) i].tick (dt, 35.0f);
            moved |= outline[(size_t) i].tick (dt, 25.0f);
        }
        if (moved) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto t = fitTransform (getLocalBounds().toFloat(), 456.0f, 286.0f);
        g.saveState();
        g.addTransform (t);

        const float cx = 228.0f, cy = 262.0f, R = 220.0f;

        g.setColour (Colour (0xff9a9a9a));
        for (float r : { 220.0f, 185.0f, 140.0f, 95.0f, 50.0f })
        {
            juce::Path p;
            p.addCentredArc (cx, cy, r, r, 0, degToRad (-90), degToRad (90), true);
            g.strokePath (p, juce::PathStrokeType (1.2f));
        }
        g.drawLine (cx, cy, 0, 34, 1.2f);
        g.drawLine (cx, cy, 456, 34, 1.2f);
        g.drawLine (cx, cy, cx, 30, 1.2f);
        g.drawLine (6, cy, 450, cy, 1.2f);

        auto pointAt = [&] (int i, float radius)
        {
            const float a = degToRad (((float) i / (float) (kPolarBins - 1) - 0.5f) * 180.0f);
            return Point<float> (cx + radius * std::sin (a), cy - radius * std::cos (a));
        };

        juce::Path shape;
        shape.startNewSubPath (cx, cy);
        for (int i = 0; i < kPolarBins; ++i)
            shape.lineTo (pointAt (i, R * fill[(size_t) i].current));
        shape.closeSubPath();
        g.setColour (Colour (0xff9a9a9a).withAlpha (0.92f));
        g.fillPath (shape);

        juce::Path peak;
        for (int i = 0; i < kPolarBins; ++i)
        {
            const auto p = pointAt (i, R * outline[(size_t) i].current);
            if (i == 0) peak.startNewSubPath (p); else peak.lineTo (p);
        }
        g.setColour (Colour (0xff262626));
        g.strokePath (peak, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved));

        juce::Path hub;
        hub.addCentredArc (cx, cy, 24, 24, 0, degToRad (-90), degToRad (90), true);
        hub.closeSubPath();
        g.setColour (Colour (0xff7a7a7a));
        g.fillPath (hub);

        g.setColour (Colour (0xffc9c9c9));
        g.setFont (theme::font (10));
        static const std::pair<float, const char*> labels[] = { { 10, "0" }, { 43, "-20" }, { 88, "-40" }, { 133, "-60" }, { 228, "-80" },
                                                                { 323, "-60" }, { 368, "-40" }, { 413, "-20" }, { 446, "0" } };
        for (auto& [x, txt] : labels)
            g.drawText (txt, Rectangle<float> (x - 20, 268, 40, 14), juce::Justification::centred, false);

        g.restoreState();
        drawLabel (g, "POLAR STEREO FIELD  dBFS", getLocalBounds().toFloat().reduced (12, 8).removeFromTop (14), juce::Justification::centredLeft, theme::label, 10.0f);
    }

private:
    std::array<Smoothed, kPolarBins> fill {}, outline {};
};

//==============================================================================
class Spectrum final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        const int n = numBars();
        bars.resize ((size_t) n);

        readoutTimer += dt;
        if (readoutTimer > 0.15f)
        {
            readoutTimer = 0.0f;
            int best = -1;
            const double binHz = f.sampleRate / kFftSize;
            for (int b = juce::jmax (1, (int) (25.0 / binHz)); b < juce::jmin (kNumBins, (int) (18000.0 / binHz)); ++b)
                if (best < 0 || f.spectrumDb[(size_t) b] > f.spectrumDb[(size_t) best]) best = b;
            if (best > 0 && f.spectrumDb[(size_t) best] > -80.0f)
            {
                const float hz = (float) (best * binHz);
                const int midi = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
                static const char* names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
                peakText = "peak " + (hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz")
                         + utf8 ("  \xc2\xb7  ") + names[((midi % 12) + 12) % 12] + juce::String (midi / 12 - 1)
                         + utf8 ("  \xc2\xb7  ") + juce::String (f.spectrumDb[(size_t) best], 1) + " dB";
            }
            else peakText = {};
        }
        bool moved = false;

        for (int i = 0; i < n; ++i)
        {
            auto& b = bars[(size_t) i];
            const float level = juce::jlimit (0.0f, 1.0f, (bandLevel (f, bandEdge (i, n), bandEdge (i + 1, n)) - floorDb) / -floorDb);
            b.value.target = level;
            moved |= b.value.tick (dt, level > b.value.current ? 45.0f : 14.0f);

            if (b.value.current >= b.peak) { b.peak = b.value.current; b.hold = 0.7f; b.fall = 0.0f; }
            else if ((b.hold -= dt) < 0.0f) { b.fall += dt * 1.6f; b.peak = juce::jmax (b.value.current, b.peak - b.fall * dt); moved = true; }
        }
        if (moved) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (10, 8);
        if (area.getHeight() > 90.0f)
        {
            auto head = area.removeFromTop (14);
            drawLabel (g, "SPECTRUM  dB / Hz", head, juce::Justification::centredLeft, theme::label, 10.0f);
            drawLabel (g, peakText, head, juce::Justification::centredRight, Colour (0xffd8d8d8), 10.0f);
            area.removeFromTop (4);
        }
        auto scaleCol = area.removeFromLeft (28);
        auto freqRow = area.removeFromBottom (14);
        area.removeFromBottom (4);

        const float dash[] = { 3.0f, 3.0f };
        g.setFont (theme::font (9.5f));
        for (int db = 0; db >= (int) floorDb; db -= (area.getHeight() < 110.0f ? 36 : 18))
        {
            const float y = area.getY() + area.getHeight() * ((float) db / floorDb);
            g.setColour (Colour (0xff666666));
            g.drawDashedLine ({ area.getX(), y, area.getRight(), y }, dash, 2, 1.0f);
            g.setColour (Colour (0xffcfcfcf));
            g.drawText (juce::String (db), scaleCol.withY (y - 7).withHeight (14).withTrimmedRight (4), juce::Justification::centredRight, false);
        }

        static const std::pair<float, const char*> freqs[] = { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" } };
        for (auto& [hz, txt] : freqs)
        {
            const float x = area.getX() + area.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f);
            g.setColour (Colour (0xff606060));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            g.setColour (Colour (0xffcfcfcf));
            g.drawText (txt, Rectangle<float> (x - 18, freqRow.getY(), 36, 14), juce::Justification::centred, false);
        }

        const int n = (int) bars.size();
        if (n == 0) return;
        const float w = area.getWidth() / (float) n;
        const float gap = juce::jmin (3.0f, w * 0.28f);

        for (int i = 0; i < n; ++i)
        {
            const auto& b = bars[(size_t) i];
            const float x = area.getX() + (float) i * w;
            const float h = area.getHeight() * b.value.current;
            if (h > 0.5f)
            {
                juce::ColourGradient grad (Colour (0xfff2f2f2), 0, area.getBottom() - h, Colour (0xffbdbdbd), 0, area.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRect (Rectangle<float> (x + gap * 0.5f, area.getBottom() - h, w - gap, h));
            }
            const float py = area.getBottom() - area.getHeight() * b.peak;
            g.setColour (theme::accent);
            g.fillRect (Rectangle<float> (x + gap * 0.5f, py - 2.0f, w - gap, 2.0f));
        }
    }

private:
    int numBars() const { return juce::jlimit (16, 96, (getWidth() - 50) / 9); }

    struct Bar { Smoothed value; float peak = 0, hold = 0, fall = 0; };
    std::vector<Bar> bars;
    juce::String peakText;
    float readoutTimer = 0.0f;
    const float floorDb = -90.0f;
};

//==============================================================================
class Loudness final : public Visualizer
{
public:
    Loudness() { setInterceptsMouseClicks (true, false); }

    bool isInteractive() const override { return true; }
    bool hitTest (int x, int y) override { return targetChip.contains ((float) x, (float) y); }
    void mouseUp (const juce::MouseEvent&) override { target = (target + 1) % 3; stateChanged(); repaint(); }

    juce::var getState() const override { auto* o = new juce::DynamicObject(); o->setProperty ("target", target); return juce::var (o); }
    void setState (const juce::var& s) override { target = juce::jlimit (0, 2, (int) s["target"]); }

    void update (const AnalysisFrame& f, float dt) override
    {
        auto norm = [] (float lufs) { return juce::jlimit (0.0f, 1.0f, (lufs + 48.0f) / 48.0f); };
        momentaryBar.target = norm (f.momentaryLufs);
        shortBar.target = norm (f.shortTermLufs);
        bool moved = momentaryBar.tick (dt, momentaryBar.target > momentaryBar.current ? 30.0f : 10.0f);
        moved |= shortBar.tick (dt, 10.0f);

        textTimer += dt;
        if (textTimer > 0.12f)
        {
            textTimer = 0.0f;
            integrated = f.integratedLufs; momentary = f.momentaryLufs; shortTerm = f.shortTermLufs;
            range = f.loudnessRange; truePeak = f.maxTruePeakDb;
            moved = true;
        }

        historyTimer += dt;
        if (historyTimer > 0.1f)
        {
            historyTimer = 0.0f;
            history.push_back (f.shortTermLufs);
            if (history.size() > 600) history.erase (history.begin());
            moved = true;
        }
        if (moved) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (14, 12);
        if (area.getHeight() < 130.0f) { paintCompact (g, area); return; }

        auto head = area.removeFromTop (16);
        drawLabel (g, "LOUDNESS", head, juce::Justification::centredLeft, theme::label, 10.0f);
        paintTargetChip (g, head);
        area.removeFromTop (8);

        const bool wide = area.getWidth() > area.getHeight() * 2.0f;
        Rectangle<float> readouts, graph;
        if (wide)
        {
            readouts = area.removeFromLeft (juce::jlimit (230.0f, 330.0f, area.getWidth() * 0.42f));
            area.removeFromLeft (12);
            graph = area;
        }
        else
        {
            graph = area.removeFromBottom (juce::jmax (40.0f, area.getHeight() * 0.32f));
            area.removeFromBottom (8);
            readouts = area;
        }
        paintReadouts (g, readouts);
        paintHistory (g, graph);
    }

private:
    struct Target { float lufs; const char* name; };
    static constexpr Target kTargets[3] = { { -14.0f, "Streaming" }, { -16.0f, "Apple Music" }, { -23.0f, "EBU R128" } };

    void paintTargetChip (juce::Graphics& g, Rectangle<float> row)
    {
        const auto txt = juce::String (kTargets[target].name) + "  " + juce::String (kTargets[target].lufs, 0) + " LUFS";
        g.setFont (theme::font (9.5f, true));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), txt) + 16.0f;
        targetChip = Rectangle<float> (w, 16.0f).withPosition (row.getRight() - w, row.getY());
        g.setColour (Colour (0xff474747));
        g.fillRoundedRectangle (targetChip, 8.0f);
        g.setColour (theme::accent);
        g.drawText (txt, targetChip, juce::Justification::centred, false);
    }

    void paintReadouts (juce::Graphics& g, Rectangle<float> r)
    {
        // momentary / short-term bars on the left
        auto bars = r.removeFromLeft (34).withTrimmedBottom (14);
        r.removeFromLeft (12);
        const float tgt = (kTargets[target].lufs + 48.0f) / 48.0f;
        for (int i = 0; i < 2; ++i)
        {
            auto bar = bars.removeFromLeft (14);
            bars.removeFromLeft (6);
            g.setColour (Colour (0xff444444));
            g.fillRect (bar);
            const float v = i == 0 ? momentaryBar.current : shortBar.current;
            auto fillR = bar.withTop (bar.getBottom() - bar.getHeight() * v);
            g.setColour (Colour (0xffe2e2e2));
            g.fillRect (fillR.withTop (juce::jmax (fillR.getY(), bar.getBottom() - bar.getHeight() * tgt)));
            if (v > tgt)
            {
                g.setColour (theme::accent);
                g.fillRect (fillR.withBottom (bar.getBottom() - bar.getHeight() * tgt));
            }
            g.setColour (juce::Colours::white);
            g.drawHorizontalLine ((int) (bar.getBottom() - bar.getHeight() * tgt), bar.getX() - 2, bar.getRight() + 2);
            drawLabel (g, i == 0 ? "M" : "S", bar.withY (bar.getBottom() + 1).withHeight (12), juce::Justification::centred, Colour (0xffcfcfcf), 9.5f);
        }

        auto big = r.removeFromTop (juce::jmin (62.0f, r.getHeight() * 0.48f));
        drawLabel (g, "Integrated", big.removeFromTop (13), juce::Justification::centredLeft, Colour (0xffcfcfcf), 10.0f);
        float diff = integrated > -70.0f ? integrated - kTargets[target].lufs : 0.0f;
        if (std::abs (diff) < 0.05f) diff = 0.0f;
        g.setColour (integrated > -70.0f && diff > 1.0f ? theme::accent : theme::text);
        g.setFont (theme::mono (juce::jlimit (18.0f, 30.0f, big.getHeight() * 0.7f)));
        const auto txt = formatDb (integrated);
        g.drawText (txt, big, juce::Justification::centredLeft, false);
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), txt);
        drawLabel (g, "LUFS", big.withTrimmedLeft (tw + 6).withTrimmedTop (big.getHeight() * 0.25f), juce::Justification::centredLeft, Colour (0xffd8d8d8), 11.0f);
        if (integrated > -70.0f)
            drawLabel (g, (diff > 0 ? "+" : "") + juce::String (diff, 1) + " LU vs target", big, juce::Justification::bottomRight, Colour (0xffbdbdbd), 9.5f);

        const std::pair<const char*, juce::String> cells[] = {
            { "Momentary", formatDb (momentary) }, { "Short-term", formatDb (shortTerm) },
            { "Range (LRA)", juce::String (range, 1) + " LU" }, { "True peak", formatDb (truePeak) + " dBTP" } };
        const float colW = r.getWidth() * 0.5f, rowH = juce::jmin (32.0f, r.getHeight() * 0.5f);
        for (int i = 0; i < 4; ++i)
        {
            auto c = Rectangle<float> (r.getX() + colW * (float) (i % 2), r.getY() + rowH * (float) (i / 2), colW, rowH);
            drawLabel (g, cells[i].first, c.removeFromTop (12), juce::Justification::centredLeft, Colour (0xffcfcfcf), 9.5f);
            g.setColour (i == 3 && truePeak > -1.0f ? theme::accent : theme::text);
            g.setFont (theme::mono (13.0f));
            g.drawText (cells[i].second, c, juce::Justification::centredLeft, false);
        }
    }

    void paintHistory (juce::Graphics& g, Rectangle<float> graph)
    {
        g.setColour (Colour (0xff4b4b4b));
        g.fillRect (graph);
        const float t = kTargets[target].lufs;
        const float lo = t - 18.0f, hi = t + 8.0f;
        auto toY = [&] (float v) { return graph.getBottom() - graph.getHeight() * juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo)); };

        g.setColour (theme::accent.withAlpha (0.10f));
        g.fillRect (Rectangle<float>::leftTopRightBottom (graph.getX(), toY (t + 1.0f), graph.getRight(), toY (t - 1.0f)));
        const float dash[] = { 3.0f, 3.0f };
        g.setColour (theme::accent.withAlpha (0.7f));
        g.drawDashedLine ({ graph.getX(), toY (t), graph.getRight(), toY (t) }, dash, 2, 1.0f);
        g.setFont (theme::font (9.0f));
        g.setColour (Colour (0xffcfcfcf));
        for (float v : { hi - 2.0f, t, lo + 2.0f })
            g.drawText (juce::String (v, 0), Rectangle<float> (graph.getRight() - 30, toY (v) - 12, 28, 11), juce::Justification::centredRight, false);
        drawLabel (g, "short-term, last 60 s", graph.reduced (6, 3).removeFromTop (12), juce::Justification::centredLeft, Colour (0xffbdbdbd), 9.0f);

        if (history.size() > 1)
        {
            juce::Path p;
            const float step = graph.getWidth() / 599.0f;
            const float x0 = graph.getRight() - step * (float) (history.size() - 1);
            bool started = false;
            for (size_t i = 0; i < history.size(); ++i)
            {
                if (history[i] < -70.0f) { started = false; continue; }
                const float x = x0 + step * (float) i, y = toY (history[i]);
                if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
            }
            g.setColour (Colour (0xffededed));
            g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
        }
    }

    void paintCompact (juce::Graphics& g, Rectangle<float> area)
    {
        auto bars = area.removeFromBottom (8);
        area.removeFromBottom (6);
        const float tgt = (kTargets[target].lufs + 48.0f) / 48.0f;
        g.setColour (Colour (0xff444444));
        g.fillRect (bars);
        g.setColour (Colour (0xffe2e2e2));
        g.fillRect (bars.withWidth (bars.getWidth() * juce::jmin (shortBar.current, tgt)));
        if (shortBar.current > tgt)
        {
            g.setColour (theme::accent);
            g.fillRect (Rectangle<float>::leftTopRightBottom (bars.getX() + bars.getWidth() * tgt, bars.getY(), bars.getX() + bars.getWidth() * shortBar.current, bars.getBottom()));
        }

        auto top = area.removeFromTop (16);
        drawLabel (g, "LUFS integrated", top, juce::Justification::centredLeft, Colour (0xffcfcfcf), 10.0f);
        paintTargetChip (g, top);
        drawLabel (g, "M " + formatDb (momentary) + "   S " + formatDb (shortTerm) + "   LRA " + juce::String (range, 1),
                   area.removeFromBottom (14), juce::Justification::centredLeft, Colour (0xffcfcfcf), 10.0f);
        g.setColour (theme::text);
        g.setFont (theme::mono (juce::jlimit (14.0f, 30.0f, area.getHeight() * 0.7f)));
        g.drawText (formatDb (integrated), area, juce::Justification::centredLeft, false);
    }

    Smoothed momentaryBar, shortBar;
    float integrated = kSilenceDb, momentary = kSilenceDb, shortTerm = kSilenceDb, range = 0.0f, truePeak = kSilenceDb;
    float textTimer = 1.0f, historyTimer = 0.0f;
    std::vector<float> history;
    int target = 0;
    Rectangle<float> targetChip;
};

//==============================================================================
class Goniometer final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        const auto area = plotArea();
        const int w = juce::jmax (1, (int) area.getWidth()), h = juce::jmax (1, (int) area.getHeight());
        if (phosphor.getWidth() != w || phosphor.getHeight() != h)
            phosphor = juce::Image (juce::Image::ARGB, w, h, true);

        phosphor.multiplyAllAlphas (std::exp (-dt * 9.0f));

        float peak = 1.0e-4f;
        for (int i = kScopeSize - 1024; i < kScopeSize; ++i)
            peak = juce::jmax (peak, std::abs (f.left[(size_t) i]), std::abs (f.right[(size_t) i]));
        gain.target = juce::jlimit (1.0f, 30.0f, 0.75f / peak);
        gain.tick (dt, gain.target < gain.current ? 12.0f : 1.5f);

        if (f.receivedAudio)
        {
            juce::Graphics g (phosphor);
            const float cx = (float) w * 0.5f, cy = (float) h * 0.5f, R = (float) juce::jmin (w, h) * 0.5f;
            g.setColour (Colour (0xfff4f4f4).withAlpha (0.45f));
            for (int i = kScopeSize - 1024; i < kScopeSize; ++i)
            {
                const float l = f.left[(size_t) i], r = f.right[(size_t) i];
                const float x = (l - r) * 0.7071f * gain.current, y = (l + r) * 0.7071f * gain.current;
                g.fillRect (cx + x * R, cy - y * R, 1.6f, 1.6f);
            }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = plotArea();
        const auto c = area.getCentre();
        const float R = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;

        juce::Path diamond;
        diamond.startNewSubPath (c.x, c.y - R);
        diamond.lineTo (c.x + R, c.y); diamond.lineTo (c.x, c.y + R); diamond.lineTo (c.x - R, c.y);
        diamond.closeSubPath();
        g.setColour (Colour (0xff7c7c7c));
        g.strokePath (diamond, juce::PathStrokeType (1.0f));

        const float dash[] = { 3.0f, 3.0f };
        g.setColour (Colour (0xff6e6e6e));
        g.drawDashedLine ({ c.x, c.y - R, c.x, c.y + R }, dash, 2, 1.0f);
        g.drawDashedLine ({ c.x - R, c.y, c.x + R, c.y }, dash, 2, 1.0f);
        g.drawDashedLine ({ c.x - R * 0.5f, c.y - R * 0.5f, c.x + R * 0.5f, c.y + R * 0.5f }, dash, 2, 1.0f);
        g.drawDashedLine ({ c.x + R * 0.5f, c.y - R * 0.5f, c.x - R * 0.5f, c.y + R * 0.5f }, dash, 2, 1.0f);

        g.drawImage (phosphor, area);
        drawLabel (g, "GONIOMETER", getLocalBounds().toFloat().reduced (12, 8).removeFromTop (14), juce::Justification::centredLeft, theme::label, 10.0f);

        g.setColour (Colour (0xffd8d8d8));
        g.setFont (theme::font (10));
        g.drawText ("M", Rectangle<float> (c.x - 8, c.y - R - 15, 16, 12), juce::Justification::centred, false);
        g.drawText ("L", Rectangle<float> (c.x - R * 0.5f - 18, c.y - R * 0.5f - 18, 16, 12), juce::Justification::centred, false);
        g.drawText ("R", Rectangle<float> (c.x + R * 0.5f + 2, c.y - R * 0.5f - 18, 16, 12), juce::Justification::centred, false);
        g.drawText ("+S", Rectangle<float> (c.x - R - 22, c.y - 6, 20, 12), juce::Justification::centredRight, false);
        g.drawText ("-S", Rectangle<float> (c.x + R + 2, c.y - 6, 20, 12), juce::Justification::centredLeft, false);
    }

private:
    Rectangle<float> plotArea() const
    {
        auto b = getLocalBounds().toFloat().reduced (30, 22);
        const float s = juce::jmin (b.getWidth(), b.getHeight());
        return b.withSizeKeepingCentre (s, s);
    }

    juce::Image phosphor;
    Smoothed gain { 4.0f, 4.0f };
};

//==============================================================================
class PhaseMeter final : public Visualizer
{
public:
    PhaseMeter() { corr.stiffness = 60.0f; corr.damping = 12.0f; }

    void update (const AnalysisFrame& f, float dt) override
    {
        corr.target = f.receivedAudio || std::abs (f.correlation) > 0.0f ? f.correlation : corr.target;
        bal.target = juce::jlimit (-12.0f, 12.0f, f.balanceDb);
        bool moved = corr.tick (dt);
        moved |= bal.tick (dt, 6.0f);
        textTimer += dt;
        if (textTimer > 0.15f) { textTimer = 0.0f; shown = corr.position; moved = true; }
        if (moved) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (14, 12);
        drawLabel (g, utf8 ("PHASE CORRELATION  \xc2\xb7  BALANCE"), area.removeFromTop (14));
        area.removeFromTop (6);

        g.setColour (shown < 0.0f ? theme::accent : theme::text);
        g.setFont (theme::mono (juce::jlimit (20.0f, 42.0f, area.getHeight() * 0.28f)));
        g.drawText ((shown >= 0.0f ? "+" : "") + juce::String (shown, 2), area.removeFromTop (juce::jlimit (28.0f, 56.0f, area.getHeight() * 0.36f)), juce::Justification::centredLeft, false);

        auto drawBar = [&] (Rectangle<float> r, float pos01, bool fillFromCentre, const juce::String& a, const juce::String& b, const juce::String& c)
        {
            g.setColour (Colour (0xff444444));
            g.fillRect (r);
            g.setColour (Colour (0xff8a8a8a));
            g.drawVerticalLine ((int) r.getCentreX(), r.getY(), r.getBottom());
            const float x = r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, pos01);
            if (fillFromCentre)
            {
                g.setColour (Colour (0xffe2e2e2));
                g.fillRect (Rectangle<float>::leftTopRightBottom (juce::jmin (x, r.getCentreX()), r.getY() + 4, juce::jmax (x, r.getCentreX()), r.getBottom() - 4));
            }
            g.setColour (fillFromCentre ? theme::accent : Colour (0xffe2e2e2));
            g.fillRect (Rectangle<float> (x - 1.5f, r.getY() - 3, 3.0f, r.getHeight() + 6));
            auto labels = Rectangle<float> (r.getX(), r.getBottom() + 3, r.getWidth(), 12);
            drawLabel (g, a, labels, juce::Justification::centredLeft, Colour (0xffcfcfcf), 9.5f);
            drawLabel (g, b, labels, juce::Justification::centred, Colour (0xffcfcfcf), 9.5f);
            drawLabel (g, c, labels, juce::Justification::centredRight, Colour (0xffcfcfcf), 9.5f);
        };

        area.removeFromTop (6);
        drawBar (area.removeFromTop (18), (corr.position + 1.0f) * 0.5f, true, "-1", "0", "+1");
        area.removeFromTop (26);
        if (area.getHeight() > 24)
            drawBar (area.removeFromTop (10), 0.5f - bal.current / 24.0f, false, "L", formatDb (bal.current, 1) + " dB", "R");
    }

private:
    Spring corr;
    Smoothed bal;
    float shown = 0.0f, textTimer = 1.0f;
};

//==============================================================================
class Spectrogram final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        const auto area = plotArea();
        const int w = juce::jmax (1, (int) area.getWidth()), h = juce::jmax (1, (int) area.getHeight());
        if (image.getWidth() != w || image.getHeight() != h)
        {
            if (image.isValid())
            {
                image = image.rescaled (w, h, juce::Graphics::mediumResamplingQuality);   // keep history while slots animate
            }
            else
            {
                image = juce::Image (juce::Image::RGB, w, h, true);
                juce::Graphics (image).fillAll (Colour (0xff464646));
            }
        }

        scroll += dt * 60.0f;
        const int cols = juce::jmin (w, (int) scroll);
        if (cols <= 0) return;
        scroll -= (float) cols;

        image.moveImageSection (0, 0, cols, 0, w - cols, h);
        juce::Image::BitmapData bd (image, w - cols, 0, cols, h, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
        {
            const float t0 = 1.0f - (float) (y + 1) / (float) h, t1 = 1.0f - (float) y / (float) h;
            const float lo = 20.0f * std::pow (1000.0f, t0), hi = 20.0f * std::pow (1000.0f, t1);
            const auto c = colourFor ((bandLevel (f, lo, hi) + 84.0f) / 72.0f);
            for (int x = 0; x < cols; ++x)
                bd.setPixelColour (x, y, c);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto full = getLocalBounds().toFloat().reduced (10, 8);
        const auto area = plotArea();
        g.drawImage (image, area);
        {
            auto tag = Rectangle<float> (100, 16).withPosition (area.getX() + 6, area.getY() + 6);
            g.setColour (theme::titleBlock.withAlpha (0.6f));
            g.fillRoundedRectangle (tag, 3.0f);
            drawLabel (g, "SPECTROGRAM", tag, juce::Justification::centred, theme::label, 9.5f);
        }

        g.setFont (theme::font (9.5f));
        g.setColour (Colour (0xffcfcfcf));
        static const std::pair<float, const char*> marks[] = { { 10000, "10k" }, { 1000, "1k" }, { 100, "100" } };
        for (auto& [hz, txt] : marks)
        {
            const float y = area.getBottom() - area.getHeight() * std::log (hz / 20.0f) / std::log (1000.0f);
            g.drawText (txt, Rectangle<float> (full.getX(), y - 7, 24, 14), juce::Justification::centredRight, false);
        }
    }

private:
    Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (10, 8).withTrimmedLeft (30); }

    static Colour colourFor (float v)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        const Colour stops[] = { Colour (0xff464646), Colour (0xff8a8a8a), theme::accent, Colour (0xfffff4de) };
        const float pos[] = { 0.0f, 0.45f, 0.78f, 1.0f };
        for (int i = 1; i < 4; ++i)
            if (v <= pos[i])
                return stops[i - 1].interpolatedWith (stops[i], (v - pos[i - 1]) / (pos[i] - pos[i - 1]));
        return stops[3];
    }

    juce::Image image;
    float scroll = 0.0f;
};

//==============================================================================
class Oscilloscope final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        constexpr int window = 1024;
        int start = kScopeSize - window - 1;
        for (int i = kScopeSize - window - 1; i > 1; --i)
        {
            const float a = f.left[(size_t) i - 1] + f.right[(size_t) i - 1], b = f.left[(size_t) i] + f.right[(size_t) i];
            if (a < 0.0f && b >= 0.0f) { start = i; break; }
        }

        float peak = 1.0e-4f;
        samples.resize (window);
        for (int i = 0; i < window; ++i)
        {
            samples[(size_t) i] = 0.5f * (f.left[(size_t) (start + i)] + f.right[(size_t) (start + i)]);
            peak = juce::jmax (peak, std::abs (samples[(size_t) i]));
        }
        gain.target = juce::jlimit (1.0f, 40.0f, 0.8f / peak);
        gain.tick (dt, gain.target < gain.current ? 10.0f : 1.2f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawLabel (g, "OSCILLOSCOPE", getLocalBounds().toFloat().reduced (12, 8).removeFromTop (14), juce::Justification::centredLeft, theme::label, 10.0f);
        auto area = getLocalBounds().toFloat().reduced (12, 14).withTrimmedTop (10);
        g.setColour (Colour (0xff666666));
        const float dash[] = { 3.0f, 3.0f };
        g.drawDashedLine ({ area.getX(), area.getCentreY(), area.getRight(), area.getCentreY() }, dash, 2, 1.0f);
        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 8.0f), area.getY(), area.getBottom());

        if (samples.empty()) return;

        juce::Path p;
        const float half = area.getHeight() * 0.5f;
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (samples.size() - 1);
            const float y = area.getCentreY() - juce::jlimit (-1.0f, 1.0f, samples[i] * gain.current) * half;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (Colour (0xffffffff).withAlpha (0.12f));
        g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Colour (0xfff0f0f0));
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    std::vector<float> samples;
    Smoothed gain { 4.0f, 4.0f };
};

//==============================================================================
class PeakRms final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        bool moved = false;
        for (int c = 0; c < 2; ++c)
        {
            auto& m = meters[c];
            const float peak = norm (f.peakDb[c]), rms = norm (f.rmsDb[c]);
            m.peak.target = peak;
            m.rms.target = rms;
            moved |= m.peak.tick (dt, peak > m.peak.current ? 80.0f : 5.0f);
            moved |= m.rms.tick (dt, 12.0f);
            if (peak >= m.hold) { m.hold = peak; m.holdTime = 1.5f; }
            else if ((m.holdTime -= dt) < 0.0f) { m.hold = juce::jmax (m.peak.current, m.hold - dt * 0.35f); moved = true; }
            m.clip = juce::jmax (m.clip - dt * 0.5f, f.peakDb[c] > -0.1f ? 1.0f : 0.0f);
        }
        if (moved) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (14, 12);
        drawLabel (g, utf8 ("PEAK  \xc2\xb7  RMS  dBFS"), area.removeFromTop (14));
        area.removeFromTop (8);
        auto readout = area.removeFromBottom (18);

        auto scaleCol = area.removeFromRight (30);
        g.setFont (theme::font (9.5f));
        for (int db : { 0, -6, -12, -18, -24, -36, -48 })
        {
            const float y = area.getBottom() - area.getHeight() * norm ((float) db);
            g.setColour (Colour (0xff6a6a6a));
            g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
            g.setColour (Colour (0xffcfcfcf));
            g.drawText (juce::String (db), scaleCol.withY (y - 7).withHeight (14), juce::Justification::centredRight, false);
        }

        const float barW = juce::jmin (46.0f, (area.getWidth() - 12.0f) / 2.0f);
        for (int c = 0; c < 2; ++c)
        {
            const auto& m = meters[c];
            auto bar = Rectangle<float> (area.getX() + (float) c * (barW + 12.0f), area.getY() + 10, barW, area.getHeight() - 10);
            auto clipLamp = Rectangle<float> (bar.getX(), area.getY(), barW, 6);
            g.setColour (Colour (0xff474747).interpolatedWith (theme::accent, m.clip));
            g.fillRect (clipLamp);

            g.setColour (Colour (0xff444444));
            g.fillRect (bar);
            g.setColour (Colour (0xff9a9a9a));
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * m.peak.current));
            g.setColour (Colour (0xffe8e8e8));
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * m.rms.current).reduced (4, 0));
            g.setColour (theme::accent);
            g.fillRect (Rectangle<float> (bar.getX(), bar.getBottom() - bar.getHeight() * m.hold - 1.0f, barW, 2.0f));

            g.setColour (theme::text);
            g.setFont (theme::mono (11));
            g.drawText ((c == 0 ? "L " : "R ") + formatDb (m.hold * 63.0f - 60.0f, 1), Rectangle<float> (bar.getX() - 6, readout.getY(), barW + 12, readout.getHeight()), juce::Justification::centred, false);
        }
    }

private:
    static float norm (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f); }
    struct Meter { Smoothed peak, rms; float hold = 0, holdTime = 0, clip = 0; };
    Meter meters[2];
};

//==============================================================================
class RetroLed final : public Visualizer
{
public:
    void update (const AnalysisFrame& f, float dt) override
    {
        bool moved = false;
        for (int i = 0; i < kBands; ++i)
        {
            auto& b = bands[(size_t) i];
            b.level.target = juce::jlimit (0.0f, 1.0f, (bandLevel (f, bandEdge (i, kBands, 30.0f, 16000.0f), bandEdge (i + 1, kBands, 30.0f, 16000.0f)) + 78.0f) / 70.0f);
            moved |= b.level.tick (dt, b.level.target > b.level.current ? 50.0f : 9.0f);
            if (b.level.current >= b.peak) { b.peak = b.level.current; b.velocity = 0.0f; }
            else { b.velocity += dt * 1.8f; b.peak = juce::jmax (b.level.current, b.peak - b.velocity * dt); moved = true; }
        }
        if (moved) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (14, 14);
        const int segs = juce::jlimit (8, 24, (int) (area.getHeight() / 9.0f));
        const float bw = area.getWidth() / (float) kBands, sh = area.getHeight() / (float) segs;

        for (int i = 0; i < kBands; ++i)
        {
            const auto& b = bands[(size_t) i];
            const int lit = (int) std::round (b.level.current * (float) segs);
            const int peakSeg = juce::jlimit (0, segs - 1, (int) std::round (b.peak * (float) segs) - 1);

            for (int s = 0; s < segs; ++s)
            {
                const auto r = Rectangle<float> (area.getX() + (float) i * bw + 2.0f, area.getBottom() - (float) (s + 1) * sh + 1.5f, bw - 4.0f, sh - 3.0f);
                const float pos = (float) s / (float) (segs - 1);
                const Colour on = pos > 0.88f ? Colour (0xfffff4de) : (pos > 0.68f ? theme::accent : Colour (0xffd8d8d8));
                if (s < lit)                      g.setColour (on);
                else if (s == peakSeg && b.peak > 0.02f) g.setColour (on.withAlpha (0.85f));
                else                              g.setColour (Colour (0xff4c4c4c));
                g.fillRoundedRectangle (r, 1.5f);
            }
        }
    }

private:
    static constexpr int kBands = 20;
    struct Band { Smoothed level; float peak = 0, velocity = 0; };
    std::array<Band, kBands> bands {};
};

template <typename T> std::unique_ptr<Visualizer> make() { return std::make_unique<T>(); }
} // namespace

namespace plug
{
float bandLevel (const AnalysisFrame& f, float lo, float hi) { return tp::bandLevel (f, lo, hi); }
float bandEdge (int i, int n, float lo, float hi)            { return tp::bandEdge (i, n, lo, hi); }
juce::String formatDb (float v, int decimals)               { return tp::formatDb (v, decimals); }
void drawLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c, float px)
{
    tp::drawLabel (g, t, r, j, c, px);
}
}

const std::vector<VisualizerInfo>& visualizerRegistry()
{
    static const std::vector<VisualizerInfo> registry {
        // meters
        { "vu",          "VU Meter",            "Meters",    "VU",   "Classic needle meter with real ballistics and a peak lamp.", make<VuMeter> },
        { "peakrms",     "Peak / RMS",          "Meters",    "PPM",  "Fast peak and RMS bars per channel, peak hold and clip lamps.", make<PeakRms> },
        { "loudness",    "Loudness (EBU R128)", "Meters",    "LUFS", "Momentary, short-term and integrated LUFS with a history graph.", make<Loudness> },
        { "truepeak",    "True Peak & Clips",   "Meters",    "dBTP", "4x oversampled true peak, clipped samples and inter-sample overs.", makeTruePeak },
        { "dynamics",    "Dynamics",            "Meters",    "DR",   "Crest factor and peak-to-loudness ratio: how squashed is it?", makeDynamics },
        // analyzers
        { "spectrum",    "Spectrum Analyzer",   "Analyzers", "FFT",  "Real-time FFT on a log scale with falling peak caps.", make<Spectrum> },
        { "spectrogram", "Spectrogram",         "Analyzers", "SPG",  "Scrolling time x frequency heat map.", make<Spectrogram> },
        { "waterfall",   "Waterfall 3D",        "Analyzers", "3D",   "Spectrum history as a receding landscape.", makeWaterfall },
        { "tonal",       "Tonal Balance",       "Analyzers", "TBC",  "Four bands against a reference range for modern mixes.", makeTonalBalance },
        { "scope",       "Oscilloscope",        "Analyzers", "OSC",  "Triggered waveform with automatic gain.", make<Oscilloscope> },
        // stereo
        { "polar",       "Polar Stereo Field",  "Stereo",    "POL",  "Level by stereo angle with a decaying peak outline.", make<PolarField> },
        { "goniometer",  "Goniometer",          "Stereo",    "GON",  "Phosphor vectorscope: width, balance and phase at a glance.", make<Goniometer> },
        { "phase",       "Phase Correlation",   "Stereo",    utf8 ("\xc2\xb1" "1"), "Correlation from -1 to +1 plus L/R balance.", make<PhaseMeter> },
        { "imager",      "Stereo Imager",       "Stereo",    "IMG",  "Stereo width per band, from sub to air.", makeImager },
        // musical
        { "tempo",       "Tempo (BPM)",         "Musical",   "BPM",  "Detected tempo with a beat display.", makeTempo },
        { "key",         "Key",                 "Musical",   "KEY",  "Detected key with Camelot code and a live chromagram.", makeKeyDisplay },
        { "tuner",       "Pitch / Tuner",       "Musical",   "HZ",   "Dominant note and how many cents it is off.", makeTuner },
        // effects
        { "eq",          "Equalizer",           "Effects",   "EQ",   "5-band EQ: drag the nodes over the live spectrum, scroll for Q.", makeEqualizer },
        { "compressor",  "Compressor",          "Effects",   "CMP",  "Threshold, ratio, attack, release, makeup, with transfer curve.", makeCompressor },
        { "limiter",     "Maximizer",           "Effects",   "MAX",  "Look-ahead limiter: drive it into a ceiling, watch the reduction.", makeLimiter },
        // fun
        { "led",         "Retro LED Bars",      "Fun",       "LED",  "80s hi-fi spectrum with falling peak segments.", make<RetroLed> },
        { "radial",      "Radial Spectrum",     "Fun",       "RAD",  "Spectrum around a circle that breathes with the beat.", makeRadial },
        { "particles",   "Particles",           "Fun",       "PRT",  "Bursts of particles on every hit.", makeParticles },
        { "ambient",     "Cover Glow",          "Fun",       "AMB",  "Soft light in the album art's colours, pulsing with the bass.", makeAmbient },
        { "vinyl",       "Vinyl Deck",          "Fun",       "VNL",  "Spinning record with the cover as label; the arm follows the song.", makeVinyl },
        { "reels",       "Tape Reels",          "Fun",       "REEL", "Reel-to-reel that winds from one spool to the other.", makeReels },
    };
    return registry;
}

const VisualizerInfo* findVisualizer (const juce::String& id)
{
    for (auto& v : visualizerRegistry())
        if (v.id == id)
            return &v;
    return nullptr;
}

juce::StringArray visualizerCategories()
{
    juce::StringArray cats;
    for (auto& v : visualizerRegistry())
        cats.addIfNotAlreadyThere (v.category);
    return cats;
}
} // namespace tp
