#include "WaveformView.h"
#include "core/Effects.h"

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

constexpr int kAnalysisBins = 3000;
const Colour kLowColour  { 0xffe8a33a };   // accent
const Colour kMidColour  { 0xffeeeeee };
const Colour kHighColour { 0xff8fc3e8 };
}

const juce::StringArray& WaveformView::styleNames()
{
    static const juce::StringArray names { "Bars", "Mirror", "Reflection", "Line", "Spectral colour", "3-Band" };
    return names;
}

const juce::StringArray& WaveformView::styleIds()
{
    static const juce::StringArray ids { "bars", "mirror", "reflection", "line", "spectral", "3band" };
    return ids;
}

//==============================================================================
class WaveformView::AnalysisJob final : public juce::ThreadPoolJob
{
public:
    AnalysisJob (WaveformView& v, juce::File f, int gen)
        : juce::ThreadPoolJob ("waveform bands"), view (&v), file (std::move (f)), generation (gen), formats (v.formats) {}

    JobStatus runJob() override
    {
        auto data = WaveformView::analyse (file, formats, [this] { return shouldExit(); });
        if (data != nullptr && ! shouldExit())
        {
            juce::MessageManager::callAsync ([safe = view, data, gen = generation]
            {
                if (safe != nullptr && safe->generation == gen)
                    safe->setBandData (data);
            });
        }
        return jobHasFinished;
    }

private:
    juce::Component::SafePointer<WaveformView> view;
    juce::File file;
    int generation;
    juce::AudioFormatManager& formats;
};

std::shared_ptr<const WaveformView::BandData> WaveformView::analyse (const juce::File& file, juce::AudioFormatManager& fm,
                                                                     const std::function<bool()>& shouldStop)
{
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;

    const double sr = reader->sampleRate;
    Biquad lowPass, highPass;
    lowPass.set (Biquad::Type::highCut, sr, 200.0, 0.707, 0.0);
    highPass.set (Biquad::Type::lowCut, sr, 3000.0, 0.707, 0.0);

    auto data = std::make_shared<BandData> ((size_t) kAnalysisBins);
    const juce::int64 total = reader->lengthInSamples;
    const int chunk = 65536;
    juce::AudioBuffer<float> buffer ((int) juce::jmax (1u, reader->numChannels), chunk);
    std::vector<double> sums ((size_t) kAnalysisBins * 3, 0.0);
    std::vector<int> counts ((size_t) kAnalysisBins, 0);

    for (juce::int64 pos = 0; pos < total; pos += chunk)
    {
        if (shouldStop()) return nullptr;
        const int n = (int) juce::jmin<juce::int64> (chunk, total - pos);
        reader->read (&buffer, 0, n, pos, true, true);
        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;

        for (int i = 0; i < n; ++i)
        {
            const auto bin = (size_t) juce::jmin<juce::int64> (kAnalysisBins - 1, (pos + i) * kAnalysisBins / total);
            const float m = 0.5f * (l[i] + r[i]);
            const float lo = lowPass.process (0, m), hi = highPass.process (0, m), mid = m - lo - hi;
            auto& b = (*data)[bin];
            b.peak = juce::jmax (b.peak, std::abs (l[i]), std::abs (r[i]));
            sums[bin * 3] += (double) lo * lo;
            sums[bin * 3 + 1] += (double) mid * mid;
            sums[bin * 3 + 2] += (double) hi * hi;
            ++counts[bin];
        }
    }

    for (size_t b = 0; b < data->size(); ++b)
    {
        const double c = juce::jmax (1, counts[b]);
        (*data)[b].low  = (float) std::sqrt (sums[b * 3] / c);
        (*data)[b].mid  = (float) std::sqrt (sums[b * 3 + 1] / c);
        (*data)[b].high = (float) std::sqrt (sums[b * 3 + 2] / c);
    }
    return data;
}

//==============================================================================
WaveformView::WaveformView (juce::AudioThumbnail& t, juce::AudioFormatManager& fm) : thumb (t), formats (fm)
{
    thumb.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    styleFade.snap (1.0f);
}

WaveformView::~WaveformView()
{
    thumb.removeChangeListener (this);
    pool.removeAllJobs (true, 2000);
}

void WaveformView::trackChanged (const juce::File& file)
{
    reveal = 0.0f;
    shownPos.snap (0.0f);
    bandData = nullptr;
    bandFade.snap (0.0f);
    ++generation;
    pool.removeAllJobs (true, 500);
    if (file.existsAsFile())
        pool.addJob (new AnalysisJob (*this, file, generation), true);
    refreshBars();
}

void WaveformView::setBandData (std::shared_ptr<const BandData> data)
{
    bandData = std::move (data);
    bandFade.target = 1.0f;
    refreshBars();
}

void WaveformView::setStyle (Style s, bool animate)
{
    if (s == style) return;
    previousStyle = style;
    style = s;
    if (animate) { styleFade.snap (0.0f); styleFade.target = 1.0f; }
    else styleFade.snap (1.0f);
    repaint();
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
    bars.assign ((size_t) n, {});

    if (bandData != nullptr && ! bandData->empty())
    {
        const auto& d = *bandData;
        Bin maxima;
        for (int i = 0; i < n; ++i)
        {
            const size_t b0 = d.size() * (size_t) i / (size_t) n, b1 = juce::jmax (b0 + 1, d.size() * (size_t) (i + 1) / (size_t) n);
            Bin acc;
            for (size_t b = b0; b < b1 && b < d.size(); ++b)
            {
                acc.peak = juce::jmax (acc.peak, d[b].peak);
                acc.low += d[b].low; acc.mid += d[b].mid; acc.high += d[b].high;
            }
            const float c = (float) (b1 - b0);
            acc.low /= c; acc.mid /= c; acc.high /= c;
            bars[(size_t) i] = acc;
            maxima.peak = juce::jmax (maxima.peak, acc.peak);
            maxima.low = juce::jmax (maxima.low, acc.low);
            maxima.mid = juce::jmax (maxima.mid, acc.mid);
            maxima.high = juce::jmax (maxima.high, acc.high);
        }
        for (auto& b : bars)
        {
            b.peak = std::pow (b.peak / juce::jmax (1.0e-4f, maxima.peak), 0.8f);
            b.low  = b.low / juce::jmax (1.0e-5f, maxima.low);
            b.mid  = b.mid / juce::jmax (1.0e-5f, maxima.mid);
            b.high = b.high / juce::jmax (1.0e-5f, maxima.high);
        }
        repaint();
        return;
    }

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
        bars[(size_t) i].peak = amp;
        maxAmp = juce::jmax (maxAmp, amp);
    }
    for (auto& b : bars)
        b.peak = std::pow (b.peak / maxAmp, 0.8f);
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
    dirty |= styleFade.tick (dt, 7.0f);
    dirty |= bandFade.tick (dt, 5.0f);

    if (dirty) repaint();
}

void WaveformView::paintStyle (juce::Graphics& g, Style s, float alpha, float playX)
{
    if (alpha <= 0.001f) return;

    const auto area = barArea();
    const int n = (int) bars.size();
    const float step = area.getWidth() / (float) juce::jmax (1, n);
    const bool hasTrack = length > 0.0;
    const float previewTo = hover.current > 0.01f ? hoverX : playX;
    const bool needsBands = s == Style::spectral || s == Style::threeBand;
    const float bandAmount = needsBands ? bandFade.current : 0.0f;

    auto revealAt = [&] (int i) { return easeOutCubic (reveal * 1.6f - (float) i / (float) n * 0.6f); };
    auto ampAt = [&] (int i) { return hasTrack ? bars[(size_t) i].peak * revealAt (i) : 0.05f; };
    auto xAt = [&] (int i) { return area.getX() + ((float) i + 0.5f) * step; };

    auto playedColour = [&] (float x, Colour played, Colour unplayed)
    {
        if (! hasTrack) return Colour (0xff6a6a6a);
        if (x + 2.0f <= playX) return played;
        if (x >= playX)
        {
            if (x < previewTo) return unplayed.interpolatedWith (played, 0.35f * hover.current);
            return unplayed;
        }
        return unplayed.interpolatedWith (played, (playX - x) / 2.0f);
    };

    auto spectralColour = [&] (const Bin& b)
    {
        // squared weights so the dominant band shows clearly instead of averaging to beige
        const float l2 = b.low * b.low, m2 = b.mid * b.mid, h2 = b.high * b.high;
        const float total = l2 + m2 + h2 + 1.0e-6f;
        const float wl = l2 / total, wm = m2 / total, wh = h2 / total;
        const auto c = Colour::fromFloatRGBA (kLowColour.getFloatRed() * wl + kMidColour.getFloatRed() * wm + kHighColour.getFloatRed() * wh,
                                              kLowColour.getFloatGreen() * wl + kMidColour.getFloatGreen() * wm + kHighColour.getFloatGreen() * wh,
                                              kLowColour.getFloatBlue() * wl + kMidColour.getFloatBlue() * wm + kHighColour.getFloatBlue() * wh, 1.0f);
        return Colour (0xffd8d8d8).interpolatedWith (c, bandAmount);
    };

    switch (s)
    {
        case Style::bars:
        case Style::spectral:
            for (int i = 0; i < n; ++i)
            {
                const float x = xAt (i) - 1.0f;
                const float h = juce::jmax (2.0f, ampAt (i) * area.getHeight());
                Colour c;
                if (s == Style::spectral && hasTrack)
                {
                    const auto base = spectralColour (bars[(size_t) i]);
                    c = playedColour (x, base, base.withMultipliedBrightness (0.62f).withMultipliedSaturation (0.7f));
                }
                else c = playedColour (x, juce::Colours::white, theme::waveUnplayed);
                g.setColour (c.withMultipliedAlpha (alpha));
                g.fillRect (Rectangle<float> (x, area.getCentreY() - h * 0.5f, 2.0f, h));
            }
            break;

        case Style::reflection:
        {
            const float base = area.getY() + area.getHeight() * 0.7f;
            for (int i = 0; i < n; ++i)
            {
                const float x = xAt (i) - 1.25f;
                const float a = ampAt (i);
                const float up = juce::jmax (1.5f, a * area.getHeight() * 0.7f);
                const float down = a * area.getHeight() * 0.3f;
                const auto c = playedColour (x, juce::Colours::white, theme::waveUnplayed);
                g.setColour (c.withMultipliedAlpha (alpha));
                g.fillRect (Rectangle<float> (x, base - up, 2.5f, up));
                g.setColour (c.withMultipliedAlpha (alpha * 0.35f));
                g.fillRect (Rectangle<float> (x, base + 1.0f, 2.5f, down));
            }
            break;
        }

        case Style::mirror:
        case Style::line:
        {
            juce::Path top, outline;
            const float cy = area.getCentreY();
            for (int i = 0; i < n; ++i)
            {
                const float y = cy - juce::jmax (1.0f, ampAt (i) * area.getHeight() * 0.5f);
                if (i == 0) top.startNewSubPath (xAt (i), y); else top.lineTo (xAt (i), y);
            }
            juce::Path bottom;
            for (int i = n - 1; i >= 0; --i)
            {
                const float y = cy + juce::jmax (1.0f, ampAt (i) * area.getHeight() * 0.5f);
                if (i == n - 1) bottom.startNewSubPath (xAt (i), y); else bottom.lineTo (xAt (i), y);
            }

            auto drawWith = [&] (Colour c, Rectangle<float> clip)
            {
                juce::Graphics::ScopedSaveState state (g);
                g.reduceClipRegion (clip.getSmallestIntegerContainer());
                g.setColour (c.withMultipliedAlpha (alpha));
                if (s == Style::mirror)
                {
                    juce::Path filled (top);
                    for (int i = n - 1; i >= 0; --i) filled.lineTo (xAt (i), cy + juce::jmax (1.0f, ampAt (i) * area.getHeight() * 0.5f));
                    filled.closeSubPath();
                    g.fillPath (filled);
                }
                else
                {
                    g.strokePath (top, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
                    g.strokePath (bottom, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
                    g.setColour (c.withMultipliedAlpha (alpha * 0.25f));
                    g.drawHorizontalLine ((int) cy, area.getX(), area.getRight());
                }
            };
            const auto full = getLocalBounds().toFloat();
            if (! hasTrack) { drawWith (Colour (0xff6a6a6a), full); break; }
            drawWith (juce::Colours::white, full.withRight (playX));
            drawWith (theme::waveUnplayed, full.withLeft (playX));
            break;
        }

        case Style::threeBand:
        {
            for (int i = 0; i < n; ++i)
            {
                const float x = xAt (i) - 1.25f;
                const auto& b = bars[(size_t) i];
                const float rv = hasTrack ? revealAt (i) : 0.0f;
                const bool played = x <= playX;
                const float dim = played ? 1.0f : 0.55f;
                const float H = area.getHeight();
                const float layers[3] = { b.low * rv * bandAmount, b.mid * rv * bandAmount * 0.8f, b.high * rv * bandAmount * 0.6f };
                const Colour cols[3] = { kLowColour, kMidColour, kHighColour };

                if (bandAmount < 1.0f)   // grey bars until the analysis arrives
                {
                    const float h = juce::jmax (2.0f, ampAt (i) * H);
                    g.setColour (playedColour (x, juce::Colours::white, theme::waveUnplayed).withMultipliedAlpha (alpha * (1.0f - bandAmount)));
                    g.fillRect (Rectangle<float> (x + 0.25f, area.getCentreY() - h * 0.5f, 2.0f, h));
                }
                for (int k = 0; k < 3; ++k)
                {
                    const float h = layers[k] * H;
                    if (h < 0.5f) continue;
                    g.setColour (cols[k].withMultipliedBrightness (dim).withMultipliedAlpha (alpha * (k == 0 ? 0.95f : 0.9f)));
                    g.fillRect (Rectangle<float> (x, area.getCentreY() - h * 0.5f, 2.5f, h));
                }
            }
            break;
        }
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (theme::waveBg);

    const auto area = barArea();
    const float playX = timeToX (shownPos.current);
    const bool hasTrack = length > 0.0;

    paintStyle (g, previousStyle, 1.0f - styleFade.current, playX);
    paintStyle (g, style, styleFade.current, playX);

    if (! hasTrack)
    {
        auto pill = Rectangle<float> (300, 26).withCentre (area.getCentre());
        g.setColour (theme::waveBg.withAlpha (0.92f));
        g.fillRoundedRectangle (pill, 13.0f);
        g.setColour (theme::label.interpolatedWith (juce::Colours::white, hover.current));
        g.setFont (theme::font (12.0f, true));
        g.drawText (utf8 ("Drop audio here  \xc2\xb7  or click to open"), pill, juce::Justification::centred, false);
        return;
    }

    if ((style == Style::spectral || style == Style::threeBand) && bandData == nullptr)
    {
        g.setColour (theme::label.withAlpha (0.8f));
        g.setFont (theme::font (9.5f));
        g.drawText (utf8 ("analysing\xe2\x80\xa6"), area.withHeight (12), juce::Justification::centredRight, false);
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

void WaveformView::showStyleMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Waveform style");
    for (int i = 0; i < styleNames().size(); ++i)
        m.addItem (i + 1, styleNames()[i], true, (int) style == i);

    juce::Component::SafePointer<WaveformView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        safe->setStyle ((Style) (r - 1));
        if (safe->onStyleChanged) safe->onStyleChanged (safe->style);
    });
}

void WaveformView::mouseMove (const juce::MouseEvent& e)  { hoverX = (float) e.x; if (length > 0.0) repaint(); }
void WaveformView::mouseEnter (const juce::MouseEvent& e) { hoverX = (float) e.x; hover.target = 1.0f; }
void WaveformView::mouseExit (const juce::MouseEvent&)    { hover.target = 0.0f; }

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showStyleMenu(); return; }
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
