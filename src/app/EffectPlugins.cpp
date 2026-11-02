#include "Visualizers.h"
#include "Knob.h"

namespace tp
{
namespace
{
using juce::Colour;
using juce::Rectangle;
using juce::Point;

juce::var objectWith (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* o = new juce::DynamicObject();
    for (auto& [k, v] : props) o->setProperty (k, v);
    return juce::var (o);
}

float prop (const juce::var& v, const char* key, float fallback)
{
    return v.hasProperty (key) ? (float) v[key] : fallback;
}

void dashedHLine (juce::Graphics& g, float y, float x0, float x1, Colour c)
{
    const float dash[] = { 3.0f, 3.0f };
    g.setColour (c);
    g.drawDashedLine ({ x0, y, x1, y }, dash, 2, 1.0f);
}

//==============================================================================
class EqPlugin final : public InteractivePlugin
{
public:
    EqPlugin() : eq (std::make_shared<Equalizer>()) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }

    std::shared_ptr<EffectProcessor> effect() const override { return eq; }

    juce::var getState() const override
    {
        juce::Array<juce::var> bands;
        for (auto& b : eq->bands)
            bands.add (objectWith ({ { "f", b.freq.load() }, { "g", b.gainDb.load() }, { "q", b.q.load() }, { "on", b.enabled.load() } }));
        return objectWith ({ { "bands", bands }, { "bypass", eq->bypass.load() } });
    }

    void setState (const juce::var& s) override
    {
        if (auto* arr = s["bands"].getArray())
            for (int i = 0; i < juce::jmin (arr->size(), Equalizer::kBands); ++i)
            {
                auto& b = eq->bands[(size_t) i];
                const auto& v = arr->getReference (i);
                b.freq = prop (v, "f", b.freq); b.gainDb = prop (v, "g", 0.0f); b.q = prop (v, "q", b.q);
                b.enabled = v.hasProperty ("on") ? (bool) v["on"] : true;
            }
        eq->bypass = (bool) s["bypass"];
        curveDirty = true;
    }

    void update (const AnalysisFrame& f, float dt) override
    {
        const auto g = graph();
        const int cols = juce::jmax (8, (int) (g.getWidth() / 3.0f));
        if ((int) spectrum.size() != cols) spectrum.assign ((size_t) cols, -90.0f);
        for (int i = 0; i < cols; ++i)
        {
            const float lo = xToFreq (g.getX() + g.getWidth() * (float) i / (float) cols);
            const float hi = xToFreq (g.getX() + g.getWidth() * (float) (i + 1) / (float) cols);
            const float target = plug::bandLevel (f, lo, hi);
            spectrum[(size_t) i] += (target - spectrum[(size_t) i]) * (1.0f - std::exp (-dt * 18.0f));
        }
        for (int i = 0; i < Equalizer::kBands; ++i)
        {
            nodeHover[i].target = (i == hovered || i == dragging) ? 1.0f : 0.0f;
            nodeHover[i].tick (dt, 18.0f);
        }
        bypassFade.target = eq->bypass ? 1.0f : 0.0f;
        bypassFade.tick (dt, 10.0f);
        repaint();
    }

    void resized() override { curveDirty = true; }

    void paint (juce::Graphics& g) override
    {
        const auto area = graph();

        // grid
        g.setFont (theme::font (9.5f));
        for (int db : { 12, 6, 0, -6, -12 })
        {
            const float y = gainToY ((float) db);
            g.setColour (db == 0 ? Colour (0xff787878) : Colour (0xff626262));
            g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
            g.setColour (Colour (0xffcfcfcf));
            g.drawText ((db > 0 ? "+" : "") + juce::String (db), Rectangle<float> (getLocalBounds().toFloat().getX() + 4, y - 7, 24, 14), juce::Justification::centredRight, false);
        }
        static const std::pair<float, const char*> freqs[] = { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" } };
        for (auto& [hz, txt] : freqs)
        {
            const float x = freqToX (hz);
            g.setColour (Colour (0xff5e5e5e));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            g.setColour (Colour (0xffcfcfcf));
            g.drawText (txt, Rectangle<float> (x - 18, area.getBottom() + 2, 36, 13), juce::Justification::centred, false);
        }

        // live spectrum behind the curve
        if (! spectrum.empty())
        {
            juce::Path p;
            const float step = area.getWidth() / (float) spectrum.size();
            p.startNewSubPath (area.getX(), area.getBottom());
            for (size_t i = 0; i < spectrum.size(); ++i)
            {
                const float y = area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, (spectrum[i] + 90.0f) / 84.0f);
                p.lineTo (area.getX() + step * ((float) i + 0.5f), y);
            }
            p.lineTo (area.getRight(), area.getBottom());
            p.closeSubPath();
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillPath (p);
        }

        // response curve
        if (curveDirty) rebuildCurve();
        const auto curveColour = theme::accent.interpolatedWith (Colour (0xff9a9a9a), bypassFade.current);
        juce::Path fill (curve);
        fill.lineTo (area.getRight(), gainToY (0.0f));
        fill.lineTo (area.getX(), gainToY (0.0f));
        fill.closeSubPath();
        g.setColour (curveColour.withAlpha (0.14f));
        g.fillPath (fill);
        g.setColour (curveColour);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));

        // nodes
        for (int i = 0; i < Equalizer::kBands; ++i)
        {
            const auto p = nodePos (i);
            const float h = nodeHover[i].current;
            const bool active = std::abs (eq->bands[(size_t) i].gainDb.load()) > 0.05f;
            const float r = 6.0f + 2.5f * h;
            if (h > 0.01f)
            {
                g.setColour (curveColour.withAlpha (0.25f * h));
                g.fillEllipse (Rectangle<float> (r * 3.2f, r * 3.2f).withCentre (p));
            }
            g.setColour (active ? curveColour : Colour (0xffd8d8d8));
            g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (p));
            g.setColour (theme::titleBlock);
            g.setFont (theme::font (8.5f, true));
            g.drawText (juce::String (i + 1), Rectangle<float> (r * 2, r * 2).withCentre (p), juce::Justification::centred, false);

            if (h > 0.05f)
            {
                const auto& b = eq->bands[(size_t) i];
                const juce::String info = fmtHz (b.freq) + "   " + fmtDb (b.gainDb) + "   Q " + juce::String (b.q.load(), 2);
                g.setFont (theme::mono (10.0f, false));
                const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), info) + 14.0f;
                auto bubble = Rectangle<float> (w, 18).withCentre (p.translated (0, p.y > area.getCentreY() ? -22.0f : 22.0f));
                bubble.setX (juce::jlimit (area.getX(), area.getRight() - w, bubble.getX()));
                g.setColour (theme::field.withAlpha (0.92f * h));
                g.fillRoundedRectangle (bubble, 3.0f);
                g.setColour (juce::Colours::white.withAlpha (h));
                g.drawText (info, bubble, juce::Justification::centred, false);
            }
        }

        if (bypassFade.current > 0.01f)
            plug::drawLabel (g, "BYPASSED", area.reduced (8).removeFromTop (16), juce::Justification::centredRight, theme::label.withAlpha (bypassFade.current), 10.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override { hovered = nodeAt (e.position); setMouseCursor (hovered >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor); }
    void mouseExit (const juce::MouseEvent&) override  { hovered = -1; }
    void mouseDown (const juce::MouseEvent& e) override { dragging = nodeAt (e.position); }
    void mouseUp (const juce::MouseEvent&) override    { if (dragging >= 0) stateChanged(); dragging = -1; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        auto& b = eq->bands[(size_t) dragging];
        b.freq = juce::jlimit (20.0f, 20000.0f, xToFreq (e.position.x));
        b.gainDb = juce::jlimit (-18.0f, 18.0f, yToGain (e.position.y));
        if (std::abs (b.gainDb.load()) < 0.3f) b.gainDb = 0.0f;   // gentle snap to flat
        curveDirty = true;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int n = nodeAt (e.position);
        if (n >= 0) eq->bands[(size_t) n].gainDb = 0.0f;
        else for (auto& b : eq->bands) b.gainDb = 0.0f;
        curveDirty = true;
        stateChanged();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int n = nodeAt (e.position);
        if (n < 0) return;
        auto& b = eq->bands[(size_t) n];
        b.q = juce::jlimit (0.2f, 8.0f, b.q.load() * std::pow (1.25f, w.deltaY * 4.0f));
        curveDirty = true;
        stateChanged();
    }

private:
    Rectangle<float> graph() const { return getLocalBounds().toFloat().reduced (10, 10).withTrimmedLeft (22).withTrimmedTop (16).withTrimmedBottom (14); }
    float freqToX (float hz) const { const auto a = graph(); return a.getX() + a.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); }
    float xToFreq (float x) const  { const auto a = graph(); return 20.0f * std::pow (1000.0f, (x - a.getX()) / a.getWidth()); }
    float gainToY (float db) const { const auto a = graph(); return a.getCentreY() - db / 18.0f * a.getHeight() * 0.5f; }
    float yToGain (float y) const  { const auto a = graph(); return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * 18.0f; }
    Point<float> nodePos (int i) const { return { freqToX (eq->bands[(size_t) i].freq), gainToY (eq->bands[(size_t) i].gainDb) }; }

    int nodeAt (Point<float> p) const
    {
        int best = -1;
        float bestD = 14.0f;
        for (int i = 0; i < Equalizer::kBands; ++i)
            if (const float d = nodePos (i).getDistanceFrom (p); d < bestD) { bestD = d; best = i; }
        return best;
    }

    void rebuildCurve()
    {
        const auto a = graph();
        curve.clear();
        for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
        {
            const float y = gainToY (juce::jlimit (-20.0f, 20.0f, eq->responseDb (xToFreq (x))));
            if (x == a.getX()) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        curveDirty = false;
    }

    std::shared_ptr<Equalizer> eq;
    std::vector<float> spectrum;
    juce::Path curve;
    bool curveDirty = true;
    int hovered = -1, dragging = -1;
    Smoothed nodeHover[Equalizer::kBands], bypassFade;
};

//==============================================================================
class CompressorPlugin final : public InteractivePlugin
{
public:
    CompressorPlugin() : comp (std::make_shared<Compressor>())
    {
        knobs.push_back (std::make_unique<Knob> ("Threshold", -60.0f, 0.0f, -18.0f, fmtDb));
        knobs.push_back (std::make_unique<Knob> ("Ratio", 1.0f, 20.0f, 3.0f, fmtRatio, true));
        knobs.push_back (std::make_unique<Knob> ("Attack", 0.1f, 100.0f, 10.0f, fmtMs, true));
        knobs.push_back (std::make_unique<Knob> ("Release", 10.0f, 1000.0f, 150.0f, fmtMs, true));
        knobs.push_back (std::make_unique<Knob> ("Makeup", 0.0f, 24.0f, 0.0f, fmtDb));
        std::atomic<float>* targets[] = { &comp->thresholdDb, &comp->ratio, &comp->attackMs, &comp->releaseMs, &comp->makeupDb };
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto* t = targets[i];
            knobs[i]->onChange = [this, t] (float v) { *t = v; stateChanged(); };
            addAndMakeVisible (*knobs[i]);
        }
    }

    std::shared_ptr<EffectProcessor> effect() const override { return comp; }

    juce::var getState() const override
    {
        return objectWith ({ { "threshold", comp->thresholdDb.load() }, { "ratio", comp->ratio.load() }, { "attack", comp->attackMs.load() },
                             { "release", comp->releaseMs.load() }, { "makeup", comp->makeupDb.load() }, { "bypass", comp->bypass.load() } });
    }

    void setState (const juce::var& s) override
    {
        const char* keys[] = { "threshold", "ratio", "attack", "release", "makeup" };
        for (size_t i = 0; i < knobs.size(); ++i)
            if (s.hasProperty (keys[i])) knobs[i]->setValue ((float) s[keys[i]], true);
        comp->bypass = (bool) s["bypass"];
    }

    void update (const AnalysisFrame&, float dt) override
    {
        for (auto& k : knobs) k->animate (dt);
        const float gr = comp->bypass ? 0.0f : comp->meterGainReductionDb.load();
        grShown.target = gr;
        grShown.tick (dt, gr > grShown.current ? 40.0f : 8.0f);
        inShown.target = juce::jmax (-60.0f, comp->meterInputDb.load());
        inShown.tick (dt, 20.0f);
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto row = r.removeFromBottom (juce::jlimit (48, 68, getHeight() / 3));
        const int w = row.getWidth() / (int) knobs.size();
        for (auto& k : knobs) k->setBounds (row.removeFromLeft (w).reduced (2, 0));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (10, 8).withTrimmedTop (18);
        r.removeFromBottom ((float) juce::jlimit (48, 68, getHeight() / 3) + 6.0f);

        auto meter = r.removeFromRight (38);
        r.removeFromRight (10);
        const float s = juce::jmin (r.getWidth(), r.getHeight());
        const auto graph = Rectangle<float> (s, s).withCentre (r.getCentre()).withX (r.getX() + 22);

        // transfer curve
        auto toX = [&] (float db) { return graph.getX() + graph.getWidth() * (db + 60.0f) / 60.0f; };
        auto toY = [&] (float db) { return graph.getBottom() - graph.getHeight() * (db + 60.0f) / 60.0f; };
        g.setColour (Colour (0xff4b4b4b));
        g.fillRect (graph);
        g.setFont (theme::font (9.0f));
        for (int db : { -12, -24, -36, -48 })
        {
            g.setColour (Colour (0xff5c5c5c));
            g.drawVerticalLine ((int) toX ((float) db), graph.getY(), graph.getBottom());
            g.drawHorizontalLine ((int) toY ((float) db), graph.getX(), graph.getRight());
            g.setColour (Colour (0xffcfcfcf));
            g.drawText (juce::String (db), Rectangle<float> (graph.getX() - 24, toY ((float) db) - 6, 20, 12), juce::Justification::centredRight, false);
        }
        const float dash[] = { 3.0f, 3.0f };
        g.setColour (Colour (0xff7a7a7a));
        g.drawDashedLine ({ graph.getX(), graph.getBottom(), graph.getRight(), graph.getY() }, dash, 2, 1.0f);

        const float t = comp->thresholdDb, ratio = comp->ratio, knee = comp->kneeDb, makeup = comp->makeupDb;
        juce::Path p;
        for (float in = -60.0f; in <= 0.0f; in += 0.5f)
        {
            const float out = juce::jlimit (-60.0f, 0.0f, Compressor::curve (in, t, ratio, knee) + makeup);
            if (in == -60.0f) p.startNewSubPath (toX (in), toY (out)); else p.lineTo (toX (in), toY (out));
        }
        const auto col = comp->bypass ? Colour (0xff9a9a9a) : theme::accent;
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));

        dashedHLine (g, toY (t), graph.getX(), graph.getRight(), col.withAlpha (0.35f));

        // where the music is right now
        const float in = inShown.current;
        const Point<float> dot (toX (in), toY (juce::jlimit (-60.0f, 0.0f, Compressor::curve (in, t, ratio, knee) + makeup)));
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillEllipse (Rectangle<float> (16, 16).withCentre (dot));
        g.setColour (juce::Colours::white);
        g.fillEllipse (Rectangle<float> (7, 7).withCentre (dot));

        // gain reduction meter
        auto bar = meter.withTrimmedBottom (16);
        g.setColour (Colour (0xff444444));
        g.fillRect (bar);
        const float frac = juce::jlimit (0.0f, 1.0f, grShown.current / 24.0f);
        g.setColour (theme::accent);
        g.fillRect (bar.withHeight (bar.getHeight() * frac));
        g.setColour (juce::Colours::white);
        g.setFont (theme::mono (10.0f));
        g.drawText ("-" + juce::String (grShown.current, 1), meter.withTop (bar.getBottom() + 2), juce::Justification::centred, false);
        plug::drawLabel (g, "GR", bar.withHeight (14).translated (0, -15), juce::Justification::centred, theme::label, 9.5f);
    }

private:
    std::shared_ptr<Compressor> comp;
    std::vector<std::unique_ptr<Knob>> knobs;
    Smoothed grShown, inShown { -60.0f, -60.0f };
};

//==============================================================================
class LimiterPlugin final : public InteractivePlugin
{
public:
    LimiterPlugin() : lim (std::make_shared<Limiter>())
    {
        knobs.push_back (std::make_unique<Knob> ("Drive", 0.0f, 18.0f, 0.0f, fmtDb));
        knobs.push_back (std::make_unique<Knob> ("Ceiling", -6.0f, 0.0f, -1.0f, fmtDb));
        knobs.push_back (std::make_unique<Knob> ("Release", 1.0f, 500.0f, 80.0f, fmtMs, true));
        std::atomic<float>* targets[] = { &lim->driveDb, &lim->ceilingDb, &lim->releaseMs };
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto* t = targets[i];
            knobs[i]->onChange = [this, t] (float v) { *t = v; stateChanged(); };
            addAndMakeVisible (*knobs[i]);
        }
        history.assign (kHistory, { 0.0f, -60.0f });
    }

    std::shared_ptr<EffectProcessor> effect() const override { return lim; }

    juce::var getState() const override
    {
        return objectWith ({ { "drive", lim->driveDb.load() }, { "ceiling", lim->ceilingDb.load() }, { "release", lim->releaseMs.load() }, { "bypass", lim->bypass.load() } });
    }

    void setState (const juce::var& s) override
    {
        const char* keys[] = { "drive", "ceiling", "release" };
        for (size_t i = 0; i < knobs.size(); ++i)
            if (s.hasProperty (keys[i])) knobs[i]->setValue ((float) s[keys[i]], true);
        lim->bypass = (bool) s["bypass"];
    }

    void update (const AnalysisFrame& f, float dt) override
    {
        for (auto& k : knobs) k->animate (dt);
        accumulator += dt;
        const float gr = lim->bypass ? 0.0f : lim->meterGainReductionDb.load();
        const float out = f.peakDb[0] > f.peakDb[1] ? f.peakDb[0] : f.peakDb[1];
        while (accumulator > 1.0f / 50.0f)
        {
            accumulator -= 1.0f / 50.0f;
            history[(size_t) writePos] = { gr, out };
            writePos = (writePos + 1) % kHistory;
        }
        grShown.target = gr;
        grShown.tick (dt, gr > grShown.current ? 40.0f : 6.0f);
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto row = r.removeFromBottom (juce::jlimit (48, 68, getHeight() / 3));
        const int w = juce::jmin (90, row.getWidth() / 4);
        for (auto& k : knobs) k->setBounds (row.removeFromLeft (w).reduced (2, 0));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (10, 8).withTrimmedTop (18);
        r.removeFromBottom ((float) juce::jlimit (48, 68, getHeight() / 3) + 6.0f);

        g.setColour (Colour (0xff4b4b4b));
        g.fillRect (r);

        const float ceiling = lim->ceilingDb;
        auto levelY = [&] (float db) { return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 36.0f) / 36.0f); };
        const float w = r.getWidth() / (float) kHistory;
        for (int i = 0; i < kHistory; ++i)
        {
            const auto& h = history[(size_t) ((writePos + i) % kHistory)];
            const float x = r.getX() + (float) i * w;
            const float y = levelY (h.second);
            g.setColour (Colour (0xffbdbdbd).withAlpha (0.55f));
            g.fillRect (Rectangle<float> (x, y, w + 0.5f, r.getBottom() - y));
            const float grH = r.getHeight() * juce::jlimit (0.0f, 1.0f, h.first / 12.0f);
            if (grH > 0.5f)
            {
                g.setColour (theme::accent.withAlpha (0.9f));
                g.fillRect (Rectangle<float> (x, r.getY(), w + 0.5f, grH));
            }
        }
        dashedHLine (g, levelY (ceiling), r.getX(), r.getRight(), juce::Colours::white.withAlpha (0.7f));
        plug::drawLabel (g, "ceiling " + plug::formatDb (ceiling) + " dB", Rectangle<float> (r.getX() + 6, levelY (ceiling) - 15, 160, 13), juce::Justification::centredLeft, juce::Colours::white.withAlpha (0.8f), 9.5f);

        // readouts next to the knobs
        auto info = getLocalBounds().toFloat().reduced (10, 8).removeFromBottom ((float) juce::jlimit (48, 68, getHeight() / 3));
        info = info.withTrimmedLeft (juce::jmin (90.0f, info.getWidth() / 4.0f) * 3.0f + 10.0f);
        plug::drawLabel (g, "Gain reduction", info.removeFromTop (14), juce::Justification::centredLeft, Colour (0xffcfcfcf), 9.5f);
        g.setColour (grShown.current > 0.1f ? theme::accent : theme::text);
        g.setFont (theme::mono (juce::jlimit (14.0f, 24.0f, info.getHeight() * 0.6f)));
        g.drawText ("-" + juce::String (grShown.current, 1) + " dB", info, juce::Justification::centredLeft, false);
    }

private:
    static constexpr int kHistory = 200;
    std::shared_ptr<Limiter> lim;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::pair<float, float>> history;
    int writePos = 0;
    float accumulator = 0.0f;
    Smoothed grShown;
};
} // namespace

std::unique_ptr<Visualizer> makeEqualizer()  { return std::make_unique<EqPlugin>(); }
std::unique_ptr<Visualizer> makeCompressor() { return std::make_unique<CompressorPlugin>(); }
std::unique_ptr<Visualizer> makeLimiter()    { return std::make_unique<LimiterPlugin>(); }
} // namespace tp
