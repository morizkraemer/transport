#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace tp
{
/** Where settings live: ~/.config/Transport on Linux, the usual app-data folders elsewhere. */
inline juce::String configFolder()
{
   #if JUCE_LINUX
    return ".config/Transport";
   #else
    return "Transport";
   #endif
}

/** Literal UTF-8 text (juce::String treats plain char* as ASCII). */
inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

namespace theme
{
// Palette taken from the prototype
inline const juce::Colour background   { 0xff3e3e3e };
inline const juce::Colour titleBar     { 0xff3a3a3a };
inline const juce::Colour titleBlock   { 0xff1b1b1b };
inline const juce::Colour header       { 0xff383838 };
inline const juce::Colour waveBg       { 0xff4b4b4b };
inline const juce::Colour panel        { 0xff565656 };
inline const juce::Colour panelHover   { 0xff5d5d5d };
inline const juce::Colour button       { 0xff575757 };
inline const juce::Colour buttonHover  { 0xff636363 };
inline const juce::Colour buttonBorder { 0xff626262 };
inline const juce::Colour icon         { 0xff8c8c8c };
inline const juce::Colour iconBright   { 0xffd2d2d2 };
inline const juce::Colour text         { 0xffffffff };
inline const juce::Colour textDim      { 0xffababab };
inline const juce::Colour label        { 0xffd4d4d4 };
inline const juce::Colour scale        { 0xffbdbdbd };
inline const juce::Colour waveUnplayed { 0xffb4b4b4 };
inline const juce::Colour accent       { 0xffe8a33a };
inline const juce::Colour dialog       { 0xff353535 };
inline const juce::Colour deep         { 0xff2a2a2a };
inline const juce::Colour field        { 0xff1f1f1f };

inline juce::String sansName()
{
   #if JUCE_WINDOWS
    return "Segoe UI";
   #else
    return juce::Font::getDefaultSansSerifFontName();
   #endif
}

/** Font sized in CSS-like pixels (the design mockups' sizes). */
inline juce::Font font (float px, bool bold = false)
{
    return juce::Font (juce::FontOptions (sansName(), px * 1.33f, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font mono (float px, bool bold = true)
{
   #if JUCE_WINDOWS
    const juce::String name = "Consolas";
   #elif JUCE_MAC
    const juce::String name = "Menlo";
   #else
    const juce::String name = juce::Font::getDefaultMonospacedFontName();
   #endif
    return juce::Font (juce::FontOptions (name, px * 1.33f, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace theme

/** Frame-rate independent exponential smoothing. */
struct Smoothed
{
    float current = 0.0f, target = 0.0f;

    /** Returns true while still moving. speed ~ 1/time-constant in seconds. */
    bool tick (float dt, float speed) noexcept
    {
        const float prev = current;
        current += (target - current) * (1.0f - std::exp (-speed * dt));
        if (std::abs (target - current) < 0.0005f)
            current = target;
        return current != prev;
    }

    void snap (float v) noexcept { current = target = v; }
    bool moving() const noexcept { return current != target; }
};

/** Critically-damped-ish spring for needles and anything that should feel physical. */
struct Spring
{
    float position = 0.0f, velocity = 0.0f, target = 0.0f;
    float stiffness = 180.0f, damping = 22.0f;

    bool tick (float dt) noexcept
    {
        dt = juce::jmin (dt, 1.0f / 30.0f);
        const float a = stiffness * (target - position) - damping * velocity;
        velocity += a * dt;
        position += velocity * dt;
        return std::abs (velocity) > 1.0e-4f || std::abs (target - position) > 1.0e-4f;
    }
};

inline float easeOutCubic (float t) noexcept { t = juce::jlimit (0.0f, 1.0f, t); return 1.0f - std::pow (1.0f - t, 3.0f); }
inline float easeInOutCubic (float t) noexcept
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

/** Everything that animates implements this; MainComponent ticks the tree once per vblank. */
struct Animated
{
    virtual ~Animated() = default;
    virtual void animate (float dt) = 0;
};

/** Small helper for hover/press state on custom-painted widgets. */
struct HoverState
{
    Smoothed hover, press;
    bool tick (float dt)
    {
        const bool a = hover.tick (dt, 18.0f);
        const bool b = press.tick (dt, 30.0f);
        return a || b;
    }
};
} // namespace tp
