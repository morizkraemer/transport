#include "HeaderView.h"

namespace tp
{
using juce::Colour;
using juce::Rectangle;
using juce::Point;

//==============================================================================
class HeaderView::IconButton final : public juce::Component, public Animated
{
public:
    enum class Kind { prev, play, next };

    explicit IconButton (Kind k) : kind (k) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    std::function<void()> onClick;
    void setPlaying (bool p) { morph.target = p ? 1.0f : 0.0f; }

    void animate (float dt) override
    {
        const bool a = state.tick (dt);
        const bool b = morph.tick (dt, 16.0f);
        if (a || b) repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override { state.hover.target = 1.0f; }
    void mouseExit (const juce::MouseEvent&) override  { state.hover.target = 0.0f; state.press.target = 0.0f; }
    void mouseDown (const juce::MouseEvent&) override  { state.press.target = 1.0f; }
    void mouseUp (const juce::MouseEvent& e) override
    {
        state.press.target = 0.0f;
        if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
    }

    void paint (juce::Graphics& g) override
    {
        // glyphs are designed in a 34x34 (prev/next) or 46x50 (play) box and scaled to fit
        const float boxW = kind == Kind::play ? 46.0f : 34.0f, boxH = kind == Kind::play ? 50.0f : 34.0f;
        const auto b = getLocalBounds().toFloat();
        const float fit = juce::jmin (b.getWidth() / (boxW + 8.0f), b.getHeight() / (boxH + 8.0f));
        const float scale = fit * (1.0f + 0.06f * state.hover.current - 0.08f * state.press.current);
        const auto c = b.getCentre();
        g.addTransform (juce::AffineTransform::translation (-boxW * 0.5f, -boxH * 0.5f).scaled (scale).translated (c.x, c.y));

        g.setColour (theme::icon.interpolatedWith (theme::iconBright, juce::jmax (state.hover.current * 0.75f, kind == Kind::play ? morph.current * 0.7f : 0.0f)));
        juce::Path p;
        if (kind == Kind::prev)      { p.addRectangle (3, 5, 4, 24); p.addTriangle (31, 5, 9, 17, 31, 29); }
        else if (kind == Kind::next) { p.addTriangle (3, 5, 25, 17, 3, 29); p.addRectangle (27, 5, 4, 24); }
        else
        {
            const float t = easeInOutCubic (morph.current);
            auto lerp = [t] (Point<float> a, Point<float> bb) { return a + (bb - a) * t; };
            const Point<float> playL[] = { { 4, 2 }, { 24, 13.5f }, { 24, 36.5f }, { 4, 48 } };
            const Point<float> pauseL[] = { { 8, 4 }, { 18, 4 }, { 18, 46 }, { 8, 46 } };
            const Point<float> playR[] = { { 24, 13.5f }, { 44, 25 }, { 44, 25 }, { 24, 36.5f } };
            const Point<float> pauseR[] = { { 28, 4 }, { 38, 4 }, { 38, 46 }, { 28, 46 } };
            for (auto [a, bb] : { std::pair { playL, pauseL }, std::pair { playR, pauseR } })
            {
                p.startNewSubPath (lerp (a[0], bb[0]));
                for (int i = 1; i < 4; ++i) p.lineTo (lerp (a[i], bb[i]));
                p.closeSubPath();
            }
        }
        g.fillPath (p);
    }

private:
    Kind kind;
    HoverState state;
    Smoothed morph;
};

//==============================================================================
class HeaderView::VolumeSlider final : public juce::Component, public Animated
{
public:
    VolumeSlider() { setMouseCursor (juce::MouseCursor::PointingHandCursor); shown.snap (value); setTitle ("Volume"); }

    std::function<void (float)> onChange;
    bool iconOnly = false;

    void setValue (float v, bool notify)
    {
        value = juce::jlimit (0.0f, 1.0f, v);
        shown.target = value;
        if (notify && onChange) onChange (value);
    }

    void animate (float dt) override
    {
        const bool a = shown.tick (dt, 22.0f), b = hover.tick (dt, 16.0f), c = drag.tick (dt, 20.0f);
        if (a || b || c) repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override { hover.target = 1.0f; }
    void mouseExit (const juce::MouseEvent&) override  { hover.target = 0.0f; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag.target = 1.0f;
        if (iconOnly || e.x < trackArea().getX() - 4) { toggleMute(); return; }
        setFromX ((float) e.x);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! iconOnly && e.getMouseDownX() >= trackArea().getX() - 4) setFromX ((float) e.x); }
    void mouseUp (const juce::MouseEvent&) override    { drag.target = 0.0f; }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { setValue (value + (w.deltaY + w.deltaX) * 0.25f, true); }

    void paint (juce::Graphics& g) override
    {
        const float cy = trackY();

        juce::Path spk;
        spk.startNewSubPath (2, cy - 3.5f); spk.lineTo (6, cy - 3.5f); spk.lineTo (12, cy - 8);
        spk.lineTo (12, cy + 8); spk.lineTo (6, cy + 3.5f); spk.lineTo (2, cy + 3.5f); spk.closeSubPath();
        g.setColour (theme::icon.interpolatedWith (theme::iconBright, hover.current * 0.5f));
        g.fillPath (spk);
        for (int i = 0; i < 2; ++i)
        {
            const float a = juce::jlimit (0.0f, 1.0f, shown.current * 2.5f - (float) i * 0.9f);
            if (a <= 0.0f) continue;
            juce::Path wave;
            const float r = 4.5f + (float) i * 4.5f;
            wave.addCentredArc (12, cy, r, r, 0, 0.75f, 2.39f, true);
            g.setColour (theme::icon.withAlpha (a));
            g.strokePath (wave, juce::PathStrokeType (1.6f));
        }
        if (iconOnly) return;

        const auto track = trackArea();
        const float th = 3.0f + hover.current * 1.0f;
        const auto line = Rectangle<float> (track.getX(), cy - th * 0.5f, track.getWidth(), th);
        g.setColour (Colour (0xff595959));
        g.fillRoundedRectangle (line, th * 0.5f);
        g.setColour (Colour (0xffdedede));
        g.fillRoundedRectangle (line.withWidth (line.getWidth() * shown.current), th * 0.5f);

        const float r = 5.5f + hover.current * 1.5f + drag.current;
        const Point<float> thumb (line.getX() + line.getWidth() * shown.current, cy);
        if (drag.current > 0.01f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f * drag.current));
            g.fillEllipse (Rectangle<float> (r * 3.2f, r * 3.2f).withCentre (thumb));
        }
        g.setColour (juce::Colours::white);
        g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (thumb));

        // value label sits fully inside the component, above the track
        const float a = juce::jmax (hover.current, drag.current);
        if (a > 0.01f)
        {
            g.setColour (theme::label.withAlpha (a));
            g.setFont (theme::mono (10.0f, false));
            g.drawText (juce::String (juce::roundToInt (value * 100.0f)) + "%",
                        Rectangle<float> (juce::jlimit (0.0f, (float) getWidth() - 40.0f, thumb.x - 20.0f), 0.0f, 40.0f, cy - 9.0f),
                        juce::Justification::centredBottom, false);
        }
    }

private:
    float trackY() const { return (float) getHeight() * 0.62f; }
    Rectangle<float> trackArea() const { return getLocalBounds().toFloat().withTrimmedLeft (28).withTrimmedRight (8); }
    void setFromX (float x) { const auto t = trackArea(); setValue ((x - t.getX()) / t.getWidth(), true); }
    void toggleMute()
    {
        if (value > 0.0f) { unmuted = value; setValue (0.0f, true); }
        else setValue (unmuted > 0.0f ? unmuted : 0.6f, true);
    }

    float value = 0.8f, unmuted = 0.8f;
    Smoothed shown, hover, drag;
};

//==============================================================================
class HeaderView::QuickButton final : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    QuickButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    QuickAction action = QuickAction::playlist;
    std::function<void()> onClick, onRightClick;

    void setActive (bool a) { active.target = a ? 1.0f : 0.0f; }
    void setAction (QuickAction a) { action = a; setTooltip (utf8 (infoFor (a).name) + utf8 (" \xe2\x80\x94 right-click to change")); repaint(); }

    void animate (float dt) override
    {
        const bool a = state.tick (dt), b = active.tick (dt, 14.0f);
        if (a || b) repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override { state.hover.target = 1.0f; }
    void mouseExit (const juce::MouseEvent&) override  { state.hover.target = 0.0f; state.press.target = 0.0f; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        state.press.target = 1.0f;
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (state.press.target > 0.5f && getLocalBounds().contains (e.getPosition()) && onClick) onClick();
        state.press.target = 0.0f;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float s = 1.0f - 0.04f * state.press.current;
        r = r.withSizeKeepingCentre (r.getWidth() * s, r.getHeight() * s);

        g.setColour (Colour (0xff424242).interpolatedWith (Colour (0xff4d4d4d), state.hover.current));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f + 0.04f * state.hover.current));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        if (active.current > 0.01f)
        {
            g.setColour (theme::accent.withAlpha (active.current));
            g.fillRoundedRectangle (Rectangle<float> (r.getWidth() * 0.4f, 2.0f).withCentre ({ r.getCentreX(), r.getBottom() - 4.0f }), 1.0f);
        }

        // icons are drawn for a ~60px tile; scale them to this tile
        const float k = juce::jmin (r.getWidth(), r.getHeight()) / 62.0f;
        const auto iconColour = Colour (0xff9e9e9e).interpolatedWith (juce::Colours::white, state.hover.current * 0.5f)
                                                   .interpolatedWith (theme::accent, active.current);
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::scale (k * 0.82f, k * 0.82f, r.getCentreX(), r.getCentreY()));
        drawQuickActionIcon (g, action, r, iconColour);
    }

private:
    HoverState state;
    Smoothed active;
};

//==============================================================================
HeaderView::HeaderView()
{
    prevButton = std::make_unique<IconButton> (IconButton::Kind::prev);
    playButton = std::make_unique<IconButton> (IconButton::Kind::play);
    nextButton = std::make_unique<IconButton> (IconButton::Kind::next);
    prevButton->onClick = [this] { if (onPrev) onPrev(); };
    playButton->onClick = [this] { if (onPlayPause) onPlayPause(); };
    nextButton->onClick = [this] { if (onNext) onNext(); };
    prevButton->setTitle ("Previous track");
    playButton->setTitle ("Play / pause");
    nextButton->setTitle ("Next track");

    volume = std::make_unique<VolumeSlider>();
    volume->onChange = [this] (float v) { if (onVolumeChanged) onVolumeChanged (v); };

    for (auto* c : std::initializer_list<juce::Component*> { prevButton.get(), playButton.get(), nextButton.get(), volume.get() })
        addAndMakeVisible (c);

    artFade.snap (1.0f);
    textFade.snap (1.0f);
    title = "Drop a track here";
    artist = "or click the cover to open one";
}

HeaderView::~HeaderView() = default;

juce::Rectangle<int> HeaderView::artArea() const
{
    const int size = getHeight() - 2 * kPad;
    return { kPad, kPad, size, size };
}

void HeaderView::setTrack (const juce::Image& newCover, const juce::String& newTitle, const juce::String& newArtist, const juce::StringArray& newMeta)
{
    previousCover = cover;
    cover = newCover;
    artFade.snap (0.0f);
    artFade.target = 1.0f;

    previousTitle = title;
    previousArtist = artist;
    previousMeta = meta;
    title = newTitle;
    artist = newArtist;
    meta = newMeta;
    textFade.snap (0.0f);
    textFade.target = 1.0f;
    repaint();
}

void HeaderView::setCover (const juce::Image& newCover)
{
    previousCover = cover;
    cover = newCover;
    artFade.snap (0.0f);
    artFade.target = 1.0f;
    repaint();
}

void HeaderView::setPlaying (bool p)
{
    playing = p;
    playButton->setPlaying (p);
}

void HeaderView::setVolume (float v) { volume->setValue (v, false); }

void HeaderView::setTimeLine (const juce::String& line)
{
    if (line != timeLine) { timeLine = line; repaint (textArea()); }
}

void HeaderView::setQuickActions (const std::vector<QuickAction>& actions, int columns)
{
    quickColumns = columns;
    while (quick.size() > actions.size()) quick.pop_back();
    while (quick.size() < actions.size())
    {
        auto b = std::make_unique<QuickButton>();
        const int index = (int) quick.size();
        b->onClick = [this, index] { if (onQuickAction) onQuickAction (index, quick[(size_t) index]->action); };
        b->onRightClick = [this, index] { if (onQuickRightClick) onQuickRightClick (index); };
        addAndMakeVisible (*b);
        quick.push_back (std::move (b));
    }
    for (size_t i = 0; i < actions.size(); ++i)
        quick[i]->setAction (actions[i]);
    resized();
}

void HeaderView::setQuickActive (QuickAction a, bool active)
{
    for (auto& q : quick)
        if (q->action == a)
            q->setActive (active);
}

void HeaderView::animate (float dt)
{
    for (auto* a : std::initializer_list<Animated*> { prevButton.get(), playButton.get(), nextButton.get(), volume.get() })
        a->animate (dt);
    for (auto& q : quick)
        q->animate (dt);

    bool dirtyArt = artFade.tick (dt, 7.0f);
    if (! cover.isValid() && playing)
    {
        vinylAngle += dt * 3.5f;
        dirtyArt = true;
    }
    if (dirtyArt) repaint (artArea());
    if (textFade.tick (dt, 9.0f)) repaint (textArea());
}

void HeaderView::paintArt (juce::Graphics& g, Rectangle<float> r, const juce::Image& img, float alpha)
{
    if (alpha <= 0.0f) return;
    g.setOpacity (alpha);
    if (img.isValid())
    {
        g.drawImage (img, r, juce::RectanglePlacement::fillDestination);
        return;
    }

    const float s = r.getWidth() / 136.0f;
    g.setColour (Colour (0xff262626).withMultipliedAlpha (alpha));
    g.fillRect (r);
    const auto c = r.getCentre();
    g.setColour (Colour (0xff1c1c1c).withMultipliedAlpha (alpha));
    g.fillEllipse (Rectangle<float> (112 * s, 112 * s).withCentre (c));
    g.setColour (Colour (0xff2f2f2f).withMultipliedAlpha (alpha));
    for (float rad : { 48.0f, 41.0f, 34.0f, 27.0f })
        g.drawEllipse (Rectangle<float> (rad * 2 * s, rad * 2 * s).withCentre (c), 1.0f);

    juce::Path sheen;
    sheen.addPieSegment (Rectangle<float> (108 * s, 108 * s).withCentre (c), vinylAngle, vinylAngle + 0.5f, 0.32f);
    sheen.addPieSegment (Rectangle<float> (108 * s, 108 * s).withCentre (c), vinylAngle + juce::MathConstants<float>::pi,
                         vinylAngle + juce::MathConstants<float>::pi + 0.5f, 0.32f);
    g.setColour (juce::Colours::white.withAlpha (0.05f * alpha));
    g.fillPath (sheen);

    g.setColour (theme::accent.withMultipliedAlpha (alpha));
    g.fillEllipse (Rectangle<float> (34 * s, 34 * s).withCentre (c));
    g.setColour (Colour (0xff1c1c1c).withMultipliedAlpha (alpha));
    g.fillEllipse (Rectangle<float> (5 * s, 5 * s).withCentre (c));
}

void HeaderView::paint (juce::Graphics& g)
{
    g.fillAll (theme::header);

    const auto art = artArea().toFloat();
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (artArea());
        paintArt (g, art, previousCover, 1.0f - artFade.current);
        paintArt (g, art, cover, artFade.current);
        g.setOpacity (1.0f);
    }

    const auto text = textArea().toFloat();
    if (text.getWidth() < 40.0f) return;
    const bool roomy = text.getHeight() >= 90.0f;
    const float titlePx = roomy ? 17.0f : 15.0f;

    auto drawText = [&] (const juce::String& t, const juce::String& a, const juce::StringArray& lines, float alpha, float yOffset, bool withTime)
    {
        if (alpha <= 0.01f) return;
        auto r = text.translated (0, yOffset);
        g.setColour (theme::text.withAlpha (alpha));
        g.setFont (theme::font (titlePx, true));
        g.drawText (t, r.withHeight (titlePx * 1.45f), juce::Justification::topLeft, true);
        if (a.isNotEmpty())
        {
            g.setColour (Colour (0xffc8c8c8).withAlpha (alpha));
            g.setFont (theme::font (titlePx * 0.78f));
            g.drawText (a, r.withTrimmedTop (titlePx * 1.45f).withHeight (titlePx * 1.2f), juce::Justification::topLeft, true);
        }

        g.setFont (theme::font (11.0f));
        g.setColour (Colour (0xffb8b8b8).withAlpha (alpha));
        juce::StringArray all;
        if (roomy)
        {
            all = lines;
            if (withTime && timeLine.isNotEmpty()) all.add (timeLine);
        }
        else
        {
            juce::String one = lines.joinIntoString (utf8 ("  \xc2\xb7  "));
            if (withTime && timeLine.isNotEmpty()) one << utf8 ("  \xc2\xb7  ") << timeLine;
            all.add (one);
        }
        float y = r.getBottom() - 15.0f * (float) all.size();
        for (auto& l : all)
        {
            g.drawText (l, Rectangle<float> (r.getX(), y, r.getWidth(), 15.0f), juce::Justification::centredLeft, true);
            y += 15.0f;
        }
    };

    const float t = easeOutCubic (textFade.current);
    drawText (previousTitle, previousArtist, previousMeta, 1.0f - t, -8.0f * t, false);
    drawText (title, artist, meta, t, 8.0f * (1.0f - t), true);
}

void HeaderView::resized()
{
    const int w = getWidth(), h = getHeight();
    const int inner = h - 2 * kPad;
    const int gap = 6;

    // quick-access grid: two rows when there is room, otherwise one row of the first buttons
    const int rows = inner >= 92 ? 2 : 1;
    const int tile = juce::jlimit (28, 48, (inner - gap * (rows - 1)) / rows);
    const int cols = juce::jmax (1, quickColumns);
    const int visible = juce::jmin ((int) quick.size(), cols * rows);
    const int qw = cols * tile + (cols - 1) * gap;
    const int qh = rows * tile + (rows - 1) * gap;
    const int qx = w - kPad - qw;
    for (size_t i = 0; i < quick.size(); ++i)
    {
        const bool show = (int) i < visible;
        quick[i]->setVisible (show);
        if (show)
            quick[i]->setBounds (qx + ((int) i % cols) * (tile + gap), (h - qh) / 2 + ((int) i / cols) * (tile + gap), tile, tile);
    }

    // transport block (fixed size, scales a little with height)
    const int playSize = juce::jlimit (34, 52, inner / 2 + 8);
    const int sideSize = juce::roundToInt ((float) playSize * 0.72f);
    const int transportW = sideSize * 2 + playSize + 2 * 14;

    // from the right: quick grid, volume, transport; the text takes what's left, never less than 120
    const int textX = artArea().getRight() + 12;
    int available = qx - 24 - textX;
    int volW = juce::jlimit (0, 200, available - transportW - 140 - 40);
    volume->iconOnly = volW < 90;
    if (volume->iconOnly) volW = 28;
    const int volX = qx - 20 - volW;
    volume->setBounds (volX, h / 2 - 18, volW, 36);

    const int textMax = juce::jmin (420, volX - 24 - transportW - 24 - textX);
    const int textW = juce::jmax (120, textMax);
    textBounds = { textX, kPad + 2, juce::jmax (0, juce::jmin (textW, volX - 24 - transportW - 12 - textX)), inner - 4 };

    const int transportLeft = textBounds.getRight() + 12;
    const int transportRight = volX - 12;
    const int transportX = juce::jmax (transportLeft, (transportLeft + transportRight - transportW) / 2);
    prevButton->setBounds (transportX, (h - sideSize) / 2, sideSize, sideSize);
    playButton->setBounds (prevButton->getRight() + 14, (h - playSize) / 2, playSize, playSize);
    nextButton->setBounds (playButton->getRight() + 14, (h - sideSize) / 2, sideSize, sideSize);

    if (onLayoutChanged) onLayoutChanged();
}

void HeaderView::mouseUp (const juce::MouseEvent& e)
{
    if (artArea().contains (e.getPosition()) && onOpen)
        onOpen();
}
} // namespace tp
