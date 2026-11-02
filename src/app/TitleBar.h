#pragma once

#include "Theme.h"

namespace tp
{
/** Title bar. Windows: MENU block aligned with the cover column + Windows 11 caption buttons.
    macOS: traffic lights on the left, then MENU. In customize mode the centre shows a Done button. */
class TitleBar final : public juce::Component, public Animated
{
public:
    static constexpr int kHeight = 32;

    TitleBar();

    std::function<void()> onMenu, onMinimise, onMaximise, onClose, onDone;

    void setMenuWidth (int w)        { menuWidth = w; repaint(); }
    void setMaximised (bool m)       { maximised = m; repaint(); }
    void setCustomizing (bool c)     { customizing.target = c ? 1.0f : 0.0f; }

    enum class Hit { none, menu, caption, minimise, maximise, close, done };
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
    juce::Rectangle<float> menuArea() const;
    juce::Rectangle<float> buttonArea (int i) const;   // 0 = minimise, 1 = maximise, 2 = close
    juce::Rectangle<float> doneArea() const;
    void updateHover (Hit h);

    int menuWidth = 120;
    bool maximised = false;
    Smoothed hover[5];   // menu, min, max, close, done
    Smoothed customizing;
    Hit pressed = Hit::none;
    juce::ComponentDragger dragger;
};
} // namespace tp
