#pragma once

#include "Theme.h"
#include "VideoPlayer.h"

namespace tp
{
/** The picture of the current video. Lives where the plugin grid is, or fills the screen.
    Click: play/pause. Double-click: full screen. Hover: buttons for plugins and full screen. */
class VideoView final : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    explicit VideoView (VideoPlayer&);

    void pictureChanged() { repaint(); }
    void setFullscreen (bool);
    bool isFullscreen() const { return fullscreen; }
    void setTrack (const juce::String& title, const juce::String& artist);
    void setTransport (double position, double length, bool playing);

    std::function<void()> onTogglePlay, onToggleFullscreen, onShowPlugins;
    std::function<void (double seconds)> onSeek;

    void animate (float dt) override;
    void paint (juce::Graphics&) override;
    juce::String getTooltip() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Hit { none, plugins, fullscreen, progress };
    Hit hitAt (juce::Point<float>) const;
    juce::Rectangle<float> buttonRect (int index) const;
    juce::Rectangle<float> progressRect() const;
    void activity();
    void seekTo (float x);

    VideoPlayer& player;
    bool fullscreen = false, playing = false, scrubbing = false;
    juce::String title, artist;
    double position = 0.0, length = 0.0;

    Smoothed hover, chrome;          // chrome: controls / overlay visibility
    Smoothed buttonHover[2], flash;
    bool flashPlaying = false;
    float idle = 0.0f;               // seconds since the mouse last moved
    float pendingClick = -1.0f;      // single click waits briefly in case it becomes a double click
    bool cursorHidden = false;
};
} // namespace tp
