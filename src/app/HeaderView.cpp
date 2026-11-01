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

    explicit IconButton (Kind k) : kind (k)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (false);
    }

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
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }

    void paint (juce::Graphics& g) override
    {
        const float scale = 1.0f + 0.07f * state.hover.current - 0.1f * state.press.current;
        const auto c = getLocalBounds().toFloat().getCentre();
        const float boxW = kind == Kind::play ? 46.0f : 34.0f, boxH = kind == Kind::play ? 50.0f : 34.0f;
        g.addTransform (juce::AffineTransform::translation (-boxW * 0.5f, -boxH * 0.5f).scaled (scale).translated (c.x, c.y));

        auto colour = theme::icon.interpolatedWith (theme::iconBright, juce::jmax (state.hover.current * 0.75f, kind == Kind::play ? morph.current * 0.8f : 0.0f));
        g.setColour (colour);

        juce::Path p;
        if (kind == Kind::prev)
        {
            p.addRectangle (3, 5, 4, 24);
            p.addTriangle (31, 5, 9, 17, 31, 29);
        }
        else if (kind == Kind::next)
        {
            p.addTriangle (3, 5, 25, 17, 3, 29);
            p.addRectangle (27, 5, 4, 24);
        }
        else
        {
            const float t = easeInOutCubic (morph.current);
            auto lerp = [t] (Point<float> a, Point<float> b) { return a + (b - a) * t; };
            const Point<float> playL[] = { { 4, 2 }, { 24, 13.5f }, { 24, 36.5f }, { 4, 48 } };
            const Point<float> pauseL[] = { { 8, 4 }, { 18, 4 }, { 18, 46 }, { 8, 46 } };
            const Point<float> playR[] = { { 24, 13.5f }, { 44, 25 }, { 44, 25 }, { 24, 36.5f } };
            const Point<float> pauseR[] = { { 28, 4 }, { 38, 4 }, { 38, 46 }, { 28, 46 } };
            for (auto [a, b] : { std::pair { playL, pauseL }, std::pair { playR, pauseR } })
            {
                p.startNewSubPath (lerp (a[0], b[0]));
                for (int i = 1; i < 4; ++i) p.lineTo (lerp (a[i], b[i]));
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

    void setValue (float v, bool notify)
    {
        value = juce::jlimit (0.0f, 1.0f, v);
        shown.target = value;
        if (notify && onChange) onChange (value);
    }

    void animate (float dt) override
    {
        const bool a = shown.tick (dt, 22.0f);
        const bool b = hover.tick (dt, 16.0f);
        const bool c = drag.tick (dt, 20.0f);
        if (a || b || c) repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override { hover.target = 1.0f; }
    void mouseExit (const juce::MouseEvent&) override  { hover.target = 0.0f; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag.target = 1.0f;
        if (e.x < trackArea().getX() - 4) { toggleMute(); return; }
        setFromX ((float) e.x);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (e.getMouseDownX() >= trackArea().getX() - 4) setFromX ((float) e.x); }
    void mouseUp (const juce::MouseEvent&) override   { drag.target = 0.0f; }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        setValue (value + (w.deltaY + w.deltaX) * 0.25f, true);
    }

    void paint (juce::Graphics& g) override
    {
        const auto cy = (float) getHeight() * 0.5f;

        // speaker + waves that fade in with level
        juce::Path spk;
        spk.startNewSubPath (2, cy - 4); spk.lineTo (7, cy - 4); spk.lineTo (14, cy - 9);
        spk.lineTo (14, cy + 9); spk.lineTo (7, cy + 4); spk.lineTo (2, cy + 4); spk.closeSubPath();
        g.setColour (theme::icon.interpolatedWith (theme::iconBright, hover.current * 0.5f));
        g.fillPath (spk);
        for (int i = 0; i < 2; ++i)
        {
            const float a = juce::jlimit (0.0f, 1.0f, shown.current * 2.5f - (float) i * 0.9f);
            if (a <= 0.0f) continue;
            juce::Path wave;
            const float r = 5.0f + (float) i * 5.0f;
            wave.addCentredArc (14, cy, r, r, 0, 0.75f, 2.39f, true);
            g.setColour (theme::icon.withAlpha (a));
            g.strokePath (wave, juce::PathStrokeType (2.0f));
        }

        const auto track = trackArea();
        const float th = 4.0f + hover.current * 1.5f;
        const auto line = Rectangle<float> (track.getX(), cy - th * 0.5f, track.getWidth(), th);
        g.setColour (Colour (0xff5f5f5f));
        g.fillRoundedRectangle (line, th * 0.5f);
        g.setColour (Colour (0xffe6e6e6));
        g.fillRoundedRectangle (line.withWidth (line.getWidth() * shown.current), th * 0.5f);

        const float r = 7.0f + hover.current * 1.5f + drag.current * 1.0f;
        const Point<float> thumb (line.getX() + line.getWidth() * shown.current, cy);
        if (drag.current > 0.01f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f * drag.current));
            g.fillEllipse (Rectangle<float> (r * 3.2f, r * 3.2f).withCentre (thumb));
        }
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (thumb.translated (0, 1.5f)));
        g.setColour (juce::Colours::white);
        g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (thumb));

        if (hover.current > 0.01f || drag.current > 0.01f)
        {
            g.setColour (theme::label.withAlpha (juce::jmax (hover.current, drag.current)));
            g.setFont (theme::mono (10.5f, false));
            g.drawText (juce::String (juce::roundToInt (value * 100.0f)) + "%", Rectangle<float> (thumb.x - 20, cy - 26, 40, 14), juce::Justification::centred, false);
        }
    }

private:
    Rectangle<float> trackArea() const { return getLocalBounds().toFloat().withTrimmedLeft (34).withTrimmedRight (9); }
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
        const bool a = state.tick (dt);
        const bool b = active.tick (dt, 14.0f);
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
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const float s = 1.0f - 0.05f * state.press.current;
        r = r.withSizeKeepingCentre (r.getWidth() * s, r.getHeight() * s);

        g.setColour (theme::button.interpolatedWith (theme::buttonHover, state.hover.current));
        g.fillRoundedRectangle (r, 3.0f);
        if (active.current > 0.01f)
        {
            g.setColour (theme::accent.withAlpha (0.10f * active.current));
            g.fillRoundedRectangle (r, 3.0f);
        }
        g.setColour (theme::buttonBorder.interpolatedWith (theme::accent, active.current));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        const auto iconColour = Colour (0xff9b9b9b).interpolatedWith (juce::Colours::white, state.hover.current * 0.45f)
                                                   .interpolatedWith (theme::accent, active.current);
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
    meta = { "or click the cover to open one" };
}

HeaderView::~HeaderView() = default;

void HeaderView::setTrack (const juce::Image& newCover, const juce::String& newTitle, const juce::StringArray& newMeta)
{
    previousCover = cover;
    cover = newCover;
    artFade.snap (0.0f);
    artFade.target = 1.0f;

    previousTitle = title;
    previousMeta = meta;
    title = newTitle;
    meta = newMeta;
    textFade.snap (0.0f);
    textFade.target = 1.0f;
    repaint();
}

void HeaderView::setPlaying (bool p)
{
    playing = p;
    playButton->setPlaying (p);
}

void HeaderView::setVolume (float v) { volume->setValue (v, false); }

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

juce::Rectangle<int> HeaderView::textArea() const
{
    const int x = artArea().getRight() + 8;
    const int w = juce::jlimit (180, 430, playButton->getX() - 70 - x);
    return { x, artArea().getY(), w, 136 };
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
        vinylAngle += dt * 3.5f;   // ~33 rpm
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

    g.setColour (Colour (0xff262626).withMultipliedAlpha (alpha));
    g.fillRect (r);
    const auto c = r.getCentre();
    g.setColour (Colour (0xff1c1c1c).withMultipliedAlpha (alpha));
    g.fillEllipse (Rectangle<float> (112, 112).withCentre (c));
    g.setColour (Colour (0xff2f2f2f).withMultipliedAlpha (alpha));
    for (float rad : { 48.0f, 41.0f, 34.0f, 27.0f })
        g.drawEllipse (Rectangle<float> (rad * 2, rad * 2).withCentre (c), 1.0f);

    // light sheen that rotates while playing
    juce::Path sheen;
    sheen.addPieSegment (Rectangle<float> (108, 108).withCentre (c), vinylAngle, vinylAngle + 0.5f, 0.32f);
    sheen.addPieSegment (Rectangle<float> (108, 108).withCentre (c), vinylAngle + juce::MathConstants<float>::pi,
                         vinylAngle + juce::MathConstants<float>::pi + 0.5f, 0.32f);
    g.setColour (juce::Colours::white.withAlpha (0.05f * alpha));
    g.fillPath (sheen);

    g.setColour (theme::accent.withMultipliedAlpha (alpha));
    g.fillEllipse (Rectangle<float> (34, 34).withCentre (c));
    g.setColour (Colour (0xff1c1c1c).withMultipliedAlpha (alpha));
    g.fillEllipse (Rectangle<float> (5, 5).withCentre (c));
    g.setColour (Colour (0xff9d9d9d).withMultipliedAlpha (alpha));
    g.setFont (theme::font (8.5f));
    g.drawText ("TRANSPORT", r.reduced (8, 5), juce::Justification::bottomLeft, false);
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
    auto drawText = [&] (const juce::String& t, const juce::StringArray& lines, float alpha, float yOffset, bool withTime)
    {
        if (alpha <= 0.01f) return;
        auto r = text.translated (0, yOffset);
        g.setColour (theme::text.withAlpha (alpha));
        g.setFont (theme::font (21.0f, true));
        g.drawText (t, r.withHeight (30).withTrimmedTop (2), juce::Justification::topLeft, true);

        g.setFont (theme::font (12.5f));
        g.setColour (Colour (0xfff2f2f2).withAlpha (alpha));
        auto all = lines;
        if (withTime && timeLine.isNotEmpty()) all.add (timeLine);
        float y = r.getBottom() - 19.0f * (float) all.size();
        for (auto& l : all)
        {
            g.drawText (l, Rectangle<float> (r.getX(), y, r.getWidth(), 19.0f), juce::Justification::centredLeft, true);
            y += 19.0f;
        }
    };

    const float t = easeOutCubic (textFade.current);
    drawText (previousTitle, previousMeta, 1.0f - t, -10.0f * t, false);
    drawText (title, meta, t, 10.0f * (1.0f - t), true);
}

void HeaderView::resized()
{
    const int w = getWidth(), h = getHeight();
    const int qCols = juce::jmax (1, quickColumns);
    const int qRows = (int) (quick.size() + (size_t) qCols - 1) / qCols;
    const int qw = qCols * 62 + (qCols - 1) * 3;
    const int qh = qRows * 62 + (qRows - 1) * 3;
    const int qx = w - 10 - qw;

    for (size_t i = 0; i < quick.size(); ++i)
    {
        const int c = (int) i % qCols, r = (int) i / qCols;
        quick[i]->setBounds (qx + c * 65, (h - qh) / 2 + r * 65, 62, 62);
    }

    const int volW = juce::jlimit (150, 270, w / 5);
    volume->setBounds (qx - juce::jlimit (24, 96, w / 15) - volW, h / 2 - 20, volW, 40);

    const int textRight = artArea().getRight() + 8 + juce::jlimit (180, 430, w / 3 - 40);
    const int transportCentre = (textRight + volume->getX()) / 2;
    playButton->setBounds (transportCentre - 31, h / 2 - 33, 62, 66);
    prevButton->setBounds (playButton->getX() - 18 - 46, h / 2 - 23, 46, 46);
    nextButton->setBounds (playButton->getRight() + 18, h / 2 - 23, 46, 46);
}

void HeaderView::mouseUp (const juce::MouseEvent& e)
{
    if (artArea().contains (e.getPosition()) && onOpen)
        onOpen();
}
} // namespace tp
