#include "WaveformView.h"

namespace tp
{
using juce::Colour;
using juce::Rectangle;

namespace
{
juce::String formatTime (double s)
{
    const int t = juce::jmax (0, (int) s);
    return juce::String::formatted ("%d:%02d", t / 60, t % 60);
}
}

WaveformView::WaveformView (juce::AudioThumbnail& t) : thumb (t)
{
    thumb.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

WaveformView::~WaveformView() { thumb.removeChangeListener (this); }

void WaveformView::trackChanged()
{
    reveal = 0.0f;
    shownPos.snap (0.0f);
    refreshBars();
}

void WaveformView::setPosition (double seconds, double newLength)
{
    position = seconds;
    length = newLength;
}

void WaveformView::setLoop (double a, double b)
{
    loopA = a;
    loopB = b;
}

void WaveformView::refreshBars()
{
    const auto area = barArea();
    const int n = juce::jmax (1, (int) (area.getWidth() / 4.55f));
    bars.assign ((size_t) n, 0.0f);

    const double total = thumb.getTotalLength();
    if (total <= 0.0 || thumb.getNumChannels() == 0)
    {
        repaint();
        return;
    }

    float maxAmp = 1.0e-3f;
    for (int i = 0; i < n; ++i)
    {
        const double t0 = total * i / n, t1 = total * (i + 1) / n;
        float amp = 0.0f;
        for (int ch = 0; ch < thumb.getNumChannels(); ++ch)
        {
            float mn = 0, mx = 0;
            thumb.getApproximateMinMax (t0, t1, ch, mn, mx);
            amp = juce::jmax (amp, std::abs (mn), std::abs (mx));
        }
        bars[(size_t) i] = amp;
        maxAmp = juce::jmax (maxAmp, amp);
    }

    // gentle compression so quiet passages stay visible, like the prototype's envelope
    for (auto& b : bars)
        b = std::pow (b / maxAmp, 0.8f);
    repaint();
}

double WaveformView::xToTime (float x) const
{
    const auto a = barArea();
    return juce::jlimit (0.0, length, (double) ((x - a.getX()) / a.getWidth()) * length);
}

float WaveformView::timeToX (double t) const
{
    const auto a = barArea();
    return length > 0.0 ? a.getX() + a.getWidth() * (float) (t / length) : a.getX();
}

void WaveformView::animate (float dt)
{
    bool dirty = false;

    if (reveal < 1.0f) { reveal = juce::jmin (1.0f, reveal + dt / 0.9f); dirty = true; }

    shownPos.target = (float) position;
    if (scrubbing || std::abs (shownPos.target - shownPos.current) < 0.25f)
    {
        if (shownPos.current != shownPos.target) dirty = true;
        shownPos.snap ((float) position);
    }
    else
    {
        dirty |= shownPos.tick (dt, 14.0f);
    }

    loopAlpha.target = loopA >= 0.0 ? 1.0f : 0.0f;
    dirty |= hover.tick (dt, 14.0f);
    dirty |= loopAlpha.tick (dt, 10.0f);
    dirty |= dragging.tick (dt, 20.0f);

    if (dirty) repaint();
}

void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (theme::waveBg);

    const auto area = barArea();
    const int n = (int) bars.size();
    const float step = area.getWidth() / (float) juce::jmax (1, n);
    const float playX = timeToX (shownPos.current);
    const bool hasTrack = length > 0.0;
    const float previewTo = hover.current > 0.01f ? hoverX : playX;

    for (int i = 0; i < n; ++i)
    {
        const float x = area.getX() + ((float) i + 0.5f) * step - 1.0f;
        const float rv = easeOutCubic (reveal * 1.6f - (float) i / (float) n * 0.6f);
        const float amp = hasTrack ? bars[(size_t) i] : 0.05f;
        const float h = juce::jmax (2.0f, amp * area.getHeight() * rv);

        Colour c;
        if (! hasTrack)               c = Colour (0xff6a6a6a);
        else if (x + 2.0f <= playX)   c = juce::Colours::white;
        else if (x >= playX)          c = theme::waveUnplayed;
        else                          c = theme::waveUnplayed.interpolatedWith (juce::Colours::white, (playX - x) / 2.0f);

        if (hasTrack && x > playX && x < previewTo)
            c = c.interpolatedWith (juce::Colours::white, 0.35f * hover.current);

        g.setColour (c);
        g.fillRect (Rectangle<float> (x, area.getCentreY() - h * 0.5f, 2.0f, h));
    }

    if (! hasTrack)
    {
        g.setColour (theme::label.interpolatedWith (juce::Colours::white, hover.current));
        g.setFont (theme::font (12.0f, true));
        auto pill = Rectangle<float> (300, 26).withCentre (area.getCentre());
        g.setColour (theme::waveBg.withAlpha (0.92f));
        g.fillRoundedRectangle (pill, 13.0f);
        g.setColour (theme::label.interpolatedWith (juce::Colours::white, hover.current));
        g.drawText (utf8 ("Drop audio here  \xc2\xb7  or click to open"), pill, juce::Justification::centred, false);
        return;
    }

    // A-B loop region
    if (loopAlpha.current > 0.01f && loopA >= 0.0)
    {
        const float xa = timeToX (loopA);
        const float xb = loopB > loopA ? timeToX (loopB) : xa;
        const auto region = Rectangle<float>::leftTopRightBottom (xa, 3.0f, juce::jmax (xa + 1.0f, xb), (float) getHeight() - 3.0f);
        g.setColour (theme::accent.withAlpha (0.10f * loopAlpha.current));
        g.fillRect (region);
        g.setColour (theme::accent.withAlpha (loopAlpha.current));
        const float dash[] = { 4.0f, 3.0f };
        g.drawDashedLine ({ xa, 3.0f, xa, (float) getHeight() - 3.0f }, dash, 2, 1.2f);
        if (loopB > loopA)
            g.drawDashedLine ({ xb, 3.0f, xb, (float) getHeight() - 3.0f }, dash, 2, 1.2f);

        g.setFont (theme::font (9.5f, true));
        auto flag = [&] (float x, const char* t)
        {
            auto r = Rectangle<float> (x, 0.0f, 14.0f, 13.0f);
            g.setColour (theme::accent.withAlpha (loopAlpha.current));
            g.fillRect (r);
            g.setColour (theme::titleBlock.withAlpha (loopAlpha.current));
            g.drawText (t, r, juce::Justification::centred, false);
        };
        flag (xa, "A");
        if (loopB > loopA) flag (xb - 14.0f, "B");
    }

    // playhead with glow
    g.setColour (theme::accent.withAlpha (0.18f + 0.12f * dragging.current));
    g.fillRect (Rectangle<float> (playX - 3.0f, 4.0f, 6.0f, (float) getHeight() - 8.0f));
    g.setColour (theme::accent);
    g.fillRect (Rectangle<float> (playX - 1.0f, 4.0f, 2.0f, (float) getHeight() - 8.0f));

    // hover readout
    if (hover.current > 0.01f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.45f * hover.current));
        g.fillRect (Rectangle<float> (hoverX - 0.5f, 6.0f, 1.0f, (float) getHeight() - 12.0f));

        const auto t = formatTime (xToTime (hoverX));
        auto bubble = Rectangle<float> (44, 17).withCentre ({ hoverX, 13.0f });
        bubble.setX (juce::jlimit (2.0f, (float) getWidth() - 46.0f, bubble.getX()));
        g.setColour (theme::field.withAlpha (0.9f * hover.current));
        g.fillRoundedRectangle (bubble, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (hover.current));
        g.setFont (theme::mono (10.5f, false));
        g.drawText (t, bubble, juce::Justification::centred, false);
    }
}

void WaveformView::mouseMove (const juce::MouseEvent& e)  { hoverX = (float) e.x; if (length > 0.0) repaint(); }
void WaveformView::mouseEnter (const juce::MouseEvent& e) { hoverX = (float) e.x; hover.target = 1.0f; }
void WaveformView::mouseExit (const juce::MouseEvent&)    { hover.target = 0.0f; }

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (length <= 0.0)
    {
        if (onEmptyClick) onEmptyClick();
        return;
    }
    scrubbing = true;
    dragging.target = 1.0f;
    hoverX = (float) e.x;
    position = xToTime ((float) e.x);
    if (onSeek) onSeek (position);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (! scrubbing) return;
    hoverX = (float) e.x;
    position = xToTime ((float) e.x);
    if (onSeek) onSeek (position);
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    scrubbing = false;
    dragging.target = 0.0f;
}
} // namespace tp
