#include "VideoView.h"

namespace tp
{
using juce::Colour;
using juce::Rectangle;

namespace
{
juce::String clock (double s)
{
    const int t = (int) std::floor (juce::jmax (0.0, s));
    const int h = t / 3600, m = (t / 60) % 60, sec = t % 60;
    return h > 0 ? juce::String::formatted ("%d:%02d:%02d", h, m, sec) : juce::String::formatted ("%d:%02d", m, sec);
}
constexpr float kDoubleClickWindow = 0.24f;
}

VideoView::VideoView (VideoPlayer& p) : player (p)
{
    setOpaque (false);
    setRepaintsOnMouseActivity (false);
}

void VideoView::setFullscreen (bool f)
{
    fullscreen = f;
    activity();
    repaint();
}

void VideoView::setTrack (const juce::String& t, const juce::String& a)
{
    title = t;
    artist = a;
    repaint();
}

void VideoView::setTransport (double pos, double len, bool isPlaying)
{
    const bool changed = isPlaying != playing || (chrome.current > 0.01f && std::abs (pos - position) > 0.02);
    position = pos;
    length = len;
    playing = isPlaying;
    if (changed) repaint();
}

void VideoView::activity()
{
    idle = 0.0f;
    chrome.target = 1.0f;
    if (cursorHidden) { setMouseCursor (juce::MouseCursor::NormalCursor); cursorHidden = false; }
}

void VideoView::animate (float dt)
{
    idle += dt;
    // controls fade out after a while without movement (sooner in full screen, and never while paused there)
    const float timeout = fullscreen ? 2.2f : 1.6f;
    if (! scrubbing && idle > timeout && (playing || ! fullscreen))
        chrome.target = fullscreen ? 0.0f : (hover.target > 0.5f ? 0.35f : 0.0f);
    if (! fullscreen && hover.target < 0.5f) chrome.target = 0.0f;

    if (fullscreen && chrome.target == 0.0f && chrome.current < 0.02f && ! cursorHidden && isMouseOver (true))
    {
        setMouseCursor (juce::MouseCursor::NoCursor);
        cursorHidden = true;
    }

    bool dirty = hover.tick (dt, 14.0f);
    dirty |= chrome.tick (dt, 10.0f);
    for (auto& b : buttonHover) dirty |= b.tick (dt, 18.0f);
    if (flash.current > 0.0f || flash.target > 0.0f)
    {
        flash.target = 0.0f;
        dirty |= flash.tick (dt, 4.5f);
    }

    if (pendingClick >= 0.0f && (pendingClick += dt) > kDoubleClickWindow)
    {
        pendingClick = -1.0f;
        flashPlaying = ! playing;
        flash.snap (1.0f);
        if (onTogglePlay) onTogglePlay();
        dirty = true;
    }
    if (dirty) repaint();
}

Rectangle<float> VideoView::buttonRect (int index) const
{
    const float w = 28.0f, h = 24.0f, gap = 4.0f, pad = fullscreen ? 18.0f : 8.0f;
    return { (float) getWidth() - pad - w - (float) index * (w + gap), pad, w, h };
}

Rectangle<float> VideoView::progressRect() const
{
    return { 28.0f, (float) getHeight() - 34.0f, (float) getWidth() - 56.0f, 14.0f };
}

VideoView::Hit VideoView::hitAt (juce::Point<float> p) const
{
    if (chrome.current < 0.05f) return Hit::none;
    if (buttonRect (0).contains (p)) return Hit::fullscreen;
    if (! fullscreen && buttonRect (1).contains (p)) return Hit::plugins;
    if (fullscreen && progressRect().expanded (0, 6).contains (p)) return Hit::progress;
    return Hit::none;
}

juce::String VideoView::getTooltip()
{
    switch (hitAt (getMouseXYRelative().toFloat()))
    {
        case Hit::fullscreen: return fullscreen ? "Exit full screen (Esc)" : "Full screen (F)";
        case Hit::plugins:    return "Show plugins (V)";
        default:              return {};
    }
}

void VideoView::mouseMove (const juce::MouseEvent& e)
{
    activity();
    const auto h = hitAt (e.position);
    buttonHover[0].target = h == Hit::fullscreen ? 1.0f : 0.0f;
    buttonHover[1].target = h == Hit::plugins ? 1.0f : 0.0f;
}

void VideoView::mouseEnter (const juce::MouseEvent& e) { hover.target = 1.0f; mouseMove (e); }

void VideoView::mouseExit (const juce::MouseEvent&)
{
    hover.target = 0.0f;
    for (auto& b : buttonHover) b.target = 0.0f;
}

void VideoView::seekTo (float x)
{
    const auto r = progressRect();
    if (onSeek && length > 0.0)
        onSeek (juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth())) * length);
}

void VideoView::mouseDown (const juce::MouseEvent& e)
{
    activity();
    if (hitAt (e.position) == Hit::progress) { scrubbing = true; seekTo (e.position.x); }
}

void VideoView::mouseDrag (const juce::MouseEvent& e)
{
    activity();
    if (scrubbing) seekTo (e.position.x);
}

void VideoView::mouseUp (const juce::MouseEvent& e)
{
    if (scrubbing) { scrubbing = false; return; }
    if (e.mouseWasDraggedSinceMouseDown() || ! e.mods.isLeftButtonDown()) return;

    switch (hitAt (e.position))
    {
        case Hit::fullscreen: if (onToggleFullscreen) onToggleFullscreen(); return;
        case Hit::plugins:    if (onShowPlugins) onShowPlugins(); return;
        case Hit::progress:   return;
        case Hit::none:       break;
    }
    if (e.getNumberOfClicks() == 1) pendingClick = 0.0f;
}

void VideoView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (hitAt (e.position) != Hit::none) return;
    pendingClick = -1.0f;
    if (onToggleFullscreen) onToggleFullscreen();
}

void VideoView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float radius = fullscreen ? 0.0f : 3.0f;

    g.setColour (fullscreen ? juce::Colours::black : Colour (0xff1a1a1a));
    g.fillRoundedRectangle (bounds, radius);

    const auto& picture = player.picture();
    if (picture.isValid())
    {
        juce::Graphics::ScopedSaveState state (g);
        if (! fullscreen)
        {
            juce::Path clip;
            clip.addRoundedRectangle (bounds, radius);
            g.reduceClipRegion (clip);
        }
        VideoPlayer::drawUpright (g, picture, fullscreen ? bounds : bounds.reduced (1.0f), player.info());
    }

    // play / pause flash
    if (flash.current > 0.01f)
    {
        const float a = flash.current;
        const float size = 64.0f * (1.15f - 0.15f * a);
        const auto c = bounds.getCentre();
        g.setColour (juce::Colours::black.withAlpha (0.45f * a));
        g.fillEllipse (Rectangle<float> (size, size).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.92f * a));
        juce::Path p;
        const float s = size / 64.0f;
        if (flashPlaying) p.addTriangle (c.x - 8 * s, c.y - 12 * s, c.x + 13 * s, c.y, c.x - 8 * s, c.y + 12 * s);
        else { p.addRoundedRectangle (c.x - 10 * s, c.y - 11 * s, 7 * s, 22 * s, 1.5f); p.addRoundedRectangle (c.x + 3 * s, c.y - 11 * s, 7 * s, 22 * s, 1.5f); }
        g.fillPath (p);
    }

    const float cAlpha = chrome.current;
    if (cAlpha < 0.01f) return;

    if (fullscreen)
    {
        // bottom overlay: title, progress, time
        auto overlay = bounds.withTop (bounds.getBottom() - 120.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0, overlay.getY(),
                                                 juce::Colours::black.withAlpha (0.7f * cAlpha), 0, overlay.getBottom(), false));
        g.fillRect (overlay);

        const auto pr = progressRect();
        g.setColour (juce::Colours::white.withAlpha (cAlpha));
        g.setFont (theme::font (15.0f, true));
        g.drawText (title, Rectangle<float> (pr.getX(), pr.getY() - 52.0f, pr.getWidth() - 160.0f, 22.0f), juce::Justification::centredLeft, true);
        if (artist.isNotEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.65f * cAlpha));
            g.setFont (theme::font (12.0f));
            g.drawText (artist, Rectangle<float> (pr.getX(), pr.getY() - 30.0f, pr.getWidth() - 160.0f, 18.0f), juce::Justification::centredLeft, true);
        }
        g.setColour (juce::Colours::white.withAlpha (0.8f * cAlpha));
        g.setFont (theme::mono (11.0f, false));
        g.drawText (clock (position) + " / " + clock (length), Rectangle<float> (pr.getRight() - 160.0f, pr.getY() - 30.0f, 160.0f, 18.0f),
                    juce::Justification::centredRight, false);

        const auto track = pr.withSizeKeepingCentre (pr.getWidth(), 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f * cAlpha));
        g.fillRoundedRectangle (track, 2.0f);
        const float frac = length > 0.0 ? (float) juce::jlimit (0.0, 1.0, position / length) : 0.0f;
        g.setColour (theme::accent.withAlpha (cAlpha));
        g.fillRoundedRectangle (track.withWidth (track.getWidth() * frac), 2.0f);
        g.fillEllipse (Rectangle<float> (10, 10).withCentre ({ track.getX() + track.getWidth() * frac, track.getCentreY() }));
    }

    // buttons (top right)
    const int count = fullscreen ? 1 : 2;
    for (int i = 0; i < count; ++i)
    {
        const auto r = buttonRect (i);
        g.setColour (Colour (0xff2e2e2e).interpolatedWith (Colour (0xff4a4a4a), buttonHover[i].current).withAlpha (0.88f * cAlpha));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Colour (0xffd6d6d6).interpolatedWith (juce::Colours::white, buttonHover[i].current).withAlpha (cAlpha));
        const auto cc = r.getCentre();
        juce::Path p;
        if (i == 0)
        {
            const float in = fullscreen ? 1.0f : 0.0f;   // arrows point in when leaving full screen
            for (int k = 0; k < 4; ++k)
            {
                const float sx = (k % 2) ? 1.0f : -1.0f, sy = (k / 2) ? 1.0f : -1.0f;
                const auto corner = cc.translated (sx * (6.0f - 3.0f * in), sy * (5.0f - 3.0f * in));
                const float dir = in > 0.5f ? 1.0f : -1.0f;
                p.startNewSubPath (corner.translated (dir * sx * 4.0f, 0));
                p.lineTo (corner);
                p.lineTo (corner.translated (0, dir * sy * 3.5f));
            }
            g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            for (int k = 0; k < 4; ++k)
                g.drawRoundedRectangle (Rectangle<float> (5.5f, 5.5f).withCentre (cc.translated ((k % 2) ? 3.5f : -3.5f, (k / 2) ? 3.5f : -3.5f)), 1.2f, 1.2f);
        }
    }
}
} // namespace tp
