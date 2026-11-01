#pragma once

#include "Theme.h"

namespace tp
{
/** The prototype's title bar: MENU block, app name, orange window controls. */
class TitleBar final : public juce::Component, public Animated
{
public:
    TitleBar();

    std::function<void()> onMenu, onMinimise, onMaximise, onClose;

    enum class Hit { none, menu, caption, minimise, maximise, close };
    Hit hitAt (juce::Point<float> local) const;   // pure geometry, safe for window hit-testing

    void animate (float dt) override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> menuArea() const    { return { 0.0f, 0.0f, 150.0f, (float) getHeight() }; }
    juce::Rectangle<float> controlsArea() const { return { (float) getWidth() - 150.0f, 0.0f, 150.0f, (float) getHeight() }; }
    juce::Rectangle<float> buttonArea (int i) const { return { (float) getWidth() - 144.0f + 46.0f * (float) i, 0.0f, 46.0f, (float) getHeight() }; }

    Smoothed hover[4];   // menu, min, max, close
    Hit pressed = Hit::none;
    juce::ComponentDragger dragger;
};
} // namespace tp
