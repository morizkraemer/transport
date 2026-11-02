#pragma once

#include "Theme.h"

namespace tp
{
/** Rotary control in the player's style. Vertical drag (Shift = fine), wheel, double-click resets.
    Hover/drag swaps the label for the value. Call animate() every frame. */
class Knob final : public juce::Component
{
public:
    Knob (juce::String labelText, float minValue, float maxValue, float defaultValue,
          std::function<juce::String (float)> formatter, bool logarithmic = false)
        : label (std::move (labelText)), lo (minValue), hi (maxValue), def (defaultValue),
          format (std::move (formatter)), logScale (logarithmic)
    {
        value = def;
        shown.snap (toNorm (def));
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setTitle (label);
    }

    std::function<void (float)> onChange;

    float getValue() const { return value; }
    void setValue (float v, bool notify)
    {
        v = juce::jlimit (lo, hi, v);
        if (v == value) return;
        value = v;
        shown.target = toNorm (v);
        if (notify && onChange) onChange (value);
    }

    void animate (float dt)
    {
        const bool a = shown.tick (dt, 26.0f), b = hover.tick (dt, 16.0f), c = active.tick (dt, 20.0f);
        if (a || b || c) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto text = r.removeFromBottom (16.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight()) - 4.0f;
        const auto c = r.getCentre();
        const float radius = size * 0.5f;
        const float start = -2.4f, end = 2.4f;
        const float angle = start + (end - start) * shown.current;

        juce::Path track, fill;
        track.addCentredArc (c.x, c.y, radius - 2, radius - 2, 0, start, end, true);
        g.setColour (juce::Colour (0xff3f3f3f));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float zero = lo < 0.0f && hi > 0.0f ? start + (end - start) * toNorm (0.0f) : start;
        fill.addCentredArc (c.x, c.y, radius - 2, radius - 2, 0, juce::jmin (zero, angle), juce::jmax (zero, angle), true);
        g.setColour (theme::accent.interpolatedWith (juce::Colours::white, active.current * 0.25f));
        g.strokePath (fill, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float bodyR = radius - 7.0f + hover.current * 1.0f;
        g.setColour (juce::Colour (0xff6a6a6a).interpolatedWith (juce::Colour (0xff7a7a7a), hover.current));
        g.fillEllipse (juce::Rectangle<float> (bodyR * 2, bodyR * 2).withCentre (c));
        g.setColour (juce::Colour (0xff4a4a4a));
        g.drawEllipse (juce::Rectangle<float> (bodyR * 2, bodyR * 2).withCentre (c), 1.0f);

        const juce::Point<float> tip (c.x + std::sin (angle) * (bodyR - 3), c.y - std::cos (angle) * (bodyR - 3));
        const juce::Point<float> mid (c.x + std::sin (angle) * bodyR * 0.35f, c.y - std::cos (angle) * bodyR * 0.35f);
        g.setColour (juce::Colours::white);
        g.drawLine ({ mid, tip }, 2.0f);

        const float showValue = juce::jmax (hover.current, active.current);
        g.setFont (theme::font (10.0f, showValue > 0.5f));
        g.setColour (theme::label.withAlpha (1.0f - showValue));
        g.drawText (label, text, juce::Justification::centred, false);
        g.setColour (juce::Colours::white.withAlpha (showValue));
        g.drawText (format (value), text, juce::Justification::centred, false);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover.target = 1.0f; }
    void mouseExit (const juce::MouseEvent&) override  { hover.target = 0.0f; }
    void mouseDown (const juce::MouseEvent&) override  { dragStart = toNorm (value); active.target = 1.0f; }
    void mouseUp (const juce::MouseEvent&) override    { active.target = 0.0f; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float sensitivity = e.mods.isShiftDown() ? 0.0015f : 0.006f;
        setValue (fromNorm (juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() * sensitivity)), true);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { setValue (def, true); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        setValue (fromNorm (juce::jlimit (0.0f, 1.0f, toNorm (value) + w.deltaY * (e.mods.isShiftDown() ? 0.02f : 0.08f))), true);
    }

private:
    float toNorm (float v) const
    {
        if (logScale) return std::log (v / lo) / std::log (hi / lo);
        return (v - lo) / (hi - lo);
    }
    float fromNorm (float n) const
    {
        if (logScale) return lo * std::pow (hi / lo, n);
        return lo + n * (hi - lo);
    }

    juce::String label;
    float lo, hi, def, value;
    std::function<juce::String (float)> format;
    bool logScale;
    Smoothed shown, hover, active;
    float dragStart = 0.0f;
};

inline juce::String fmtDb (float v)    { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
inline juce::String fmtMs (float v)    { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
inline juce::String fmtRatio (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + ":1"; }
inline juce::String fmtHz (float v)    { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
} // namespace tp
