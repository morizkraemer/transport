#pragma once

#include "Theme.h"
#include "QuickActions.h"

namespace tp
{
/** Cover, title/artist/meta, transport, volume and the quick-access grid.
    Adapts to its height (1 or 2 tile rows, compact text) and width (nothing ever overlaps). */
class HeaderView final : public juce::Component, public Animated
{
public:
    static constexpr int kMinHeight = 72, kMaxHeight = 170, kDefaultHeight = 112;

    HeaderView();
    ~HeaderView() override;

    void setTrack (const juce::Image& cover, const juce::String& title, const juce::String& artist, const juce::StringArray& metaLines);
    void setPlaying (bool playing);
    void setVolume (float v);
    void setQuickActions (const std::vector<QuickAction>& actions, int columns);
    void setQuickActive (QuickAction a, bool active);
    void setTimeLine (const juce::String& line);

    /** Right edge of the cover column, so the title bar's MENU block can line up with it. */
    int coverRight() const { return artArea().getRight(); }

    std::function<void()> onPrev, onPlayPause, onNext, onOpen;
    std::function<void (float)> onVolumeChanged;
    std::function<void (int index, QuickAction)> onQuickAction;
    std::function<void (int index)> onQuickRightClick;
    std::function<void()> onLayoutChanged;   // cover column moved

    void animate (float dt) override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class IconButton;
    class VolumeSlider;
    class QuickButton;

    static constexpr int kPad = 8;
    juce::Rectangle<int> artArea() const;
    juce::Rectangle<int> textArea() const { return textBounds; }
    void paintArt (juce::Graphics&, juce::Rectangle<float>, const juce::Image&, float alpha);

    std::unique_ptr<IconButton> prevButton, playButton, nextButton;
    std::unique_ptr<VolumeSlider> volume;
    std::vector<std::unique_ptr<QuickButton>> quick;
    int quickColumns = 2;
    juce::Rectangle<int> textBounds;

    juce::Image cover, previousCover;
    Smoothed artFade;
    juce::String title, artist, previousTitle, previousArtist, timeLine;
    juce::StringArray meta, previousMeta;
    Smoothed textFade;
    float vinylAngle = 0.0f;
    bool playing = false;
};
} // namespace tp
