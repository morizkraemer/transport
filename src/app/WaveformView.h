#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Theme.h"

namespace tp
{
/** Bar waveform with a glowing playhead, hover time readout, drag-to-seek and A-B region. */
class WaveformView final : public juce::Component, public Animated, private juce::ChangeListener
{
public:
    explicit WaveformView (juce::AudioThumbnail& thumbnail);
    ~WaveformView() override;

    void trackChanged();                       // restart the reveal animation
    void setPosition (double seconds, double length);
    void setLoop (double a, double b);         // negative = unset
    void refreshBars();

    std::function<void (double seconds)> onSeek;
    std::function<void()> onEmptyClick;

    void animate (float dt) override;
    void paint (juce::Graphics&) override;
    void resized() override { refreshBars(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshBars(); }
    juce::Rectangle<float> barArea() const { return getLocalBounds().toFloat().reduced (14, 6); }
    double xToTime (float x) const;
    float timeToX (double t) const;

    juce::AudioThumbnail& thumb;
    std::vector<float> bars;
    double length = 0.0, position = 0.0, loopA = -1.0, loopB = -1.0;
    Smoothed shownPos;          // seconds, glides on seeks
    float reveal = 1.0f;        // 0..1 entrance animation
    Smoothed hover, loopAlpha, dragging;
    float hoverX = 0.0f;
    bool scrubbing = false;
};
} // namespace tp
