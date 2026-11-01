#pragma once

#include "Theme.h"
#include "QuickActions.h"

namespace tp
{
/** Album art, track text, transport buttons, volume and the quick-access grid. */
class HeaderView final : public juce::Component, public Animated
{
public:
    HeaderView();
    ~HeaderView() override;

    void setTrack (const juce::Image& cover, const juce::String& title, const juce::StringArray& metaLines);
    void setPlaying (bool playing);
    void setVolume (float v);
    void setQuickActions (const std::vector<QuickAction>& actions, int columns);
    void setQuickActive (QuickAction a, bool active);
    void setTimeLine (const juce::String& line) { if (line != timeLine) { timeLine = line; repaint (textArea()); } }

    std::function<void()> onPrev, onPlayPause, onNext, onOpen;
    std::function<void (float)> onVolumeChanged;
    std::function<void (int index, QuickAction)> onQuickAction;
    std::function<void (int index)> onQuickRightClick;

    void animate (float dt) override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class IconButton;
    class VolumeSlider;
    class QuickButton;

    juce::Rectangle<int> artArea() const { return { 6, (getHeight() - 136) / 2, 136, 136 }; }
    juce::Rectangle<int> textArea() const;
    void paintArt (juce::Graphics&, juce::Rectangle<float>, const juce::Image&, float alpha);

    std::unique_ptr<IconButton> prevButton, playButton, nextButton;
    std::unique_ptr<VolumeSlider> volume;
    std::vector<std::unique_ptr<QuickButton>> quick;
    int quickColumns = 2;

    juce::Image cover, previousCover;
    Smoothed artFade;
    juce::String title, previousTitle, timeLine;
    juce::StringArray meta, previousMeta;
    Smoothed textFade;
    float vinylAngle = 0.0f;
    bool playing = false;
};
} // namespace tp
