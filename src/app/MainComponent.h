#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "core/Analyzer.h"
#include "AudioEngine.h"
#include "HeaderView.h"
#include "PluginBrowser.h"
#include "SlotGrid.h"
#include "TitleBar.h"
#include "VideoPlayer.h"
#include "VideoView.h"
#include "WaveformView.h"

namespace tp
{
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget
{
public:
    explicit MainComponent (bool headless = false);
    ~MainComponent() override;

    bool openFile (const juce::File& file, bool autoplay);
    void tickFrame (float dt);   // one display frame; driven by vblank (or manually when headless)

    AudioEngine& getEngine()    { return engine; }
    SlotGrid& getSlots()        { return slotGrid; }
    WaveformView& getWaveform() { return waveform; }
    void openPluginBrowser()    { browser.open (-1, slotGrid.getIds()); }
    void setCustomizing (bool);
    void setAreaHeights (int header, int wave) { headerHeight = header; waveHeight = wave; resized(); }

    // video
    bool hasVideo() const { return videoLoaded; }
    void setShowVideo (bool);
    void setFullscreenVideo (bool);
    /** Headless: decode the picture for the current position before taking a snapshot. */
    bool syncVideoNow (int timeoutMs);

    // taskbar mini player hooks
    void previousTrack()  { skip (-1); }
    void nextTrack()      { skip (1); }
    void togglePlayback() { if (header.onPlayPause) header.onPlayPause(); }
    bool isPlayingNow()   { return engine.isPlaying(); }
    juce::Image renderMiniPlayer (int width, int height);
    juce::String miniPlayerKey();
    bool isTaskbarPlayerEnabled() const { return taskbarPlayer; }
    std::function<void (bool)> onTaskbarPlayerChanged;

    /** Geometry-only hit test used by the window for native caption / button behaviour. */
    juce::Component::WindowControlKind controlAt (juce::Point<float> local) const;

    std::function<void()> onMinimise, onMaximise, onClose;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropGlow.target = 1.0f; }
    void fileDragExit (const juce::StringArray&) override              { dropGlow.target = 0.0f; }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void onVBlank (double timestamp);
    void showMainMenu();
    void showOpenDialog();
    void runQuickAction (int index, QuickAction action);
    void showQuickMenu (int index);
    void showMonitorMenu();
    void showPlaylistMenu();
    void refreshQuickState();
    void applyQuickActions();
    void skip (int direction);
    void setCompact (bool);
    void setUiScale (float);
    struct Heights { int header, wave; };
    Heights effectiveHeights() const;
    void saveSnapshot();
    bool videoWanted() const { return videoLoaded && showVideo && ! customizing && ! compact; }
    void updateVideoLayout();
    void loadVideoCover (const juce::File&, double duration);
    void showAudioSettings();
    void toast (const juce::String& text);
    void saveSettings();
    juce::String formatClock (double seconds) const;

    const bool headless;
    AudioEngine engine;
    Analyzer analyzer;
    AnalysisFrame frame;
    std::vector<float> popL, popR;
    float silenceDebt = 0.0f;
    float settingsDirtyFor = -1.0f;   // >= 0: seconds since a plugin setting changed

    TitleBar titleBar;
    HeaderView header;
    WaveformView waveform;
    SlotGrid slotGrid;
    PluginBrowser browser;
    VideoPlayer video;
    VideoView videoView { video };
    bool videoLoaded = false, showVideo = true, videoFullscreen = false;
    Smoothed videoMix;
    juce::Point<int> videoPixels;
    float videoPixelsAge = 0.0f;
    int coverToken = 0;
    juce::TooltipWindow tooltips { this, 700 };

    juce::ApplicationProperties properties;
    std::vector<QuickAction> quick;
    int quickColumns = 2;

    juce::Array<juce::File> playlist;
    int playIndex = -1;
    double loopA = -1.0, loopB = -1.0;

    class Splitter;
    class DefaultPrompt;
    std::unique_ptr<DefaultPrompt> defaultPrompt;
    void askToBecomeDefault();
    std::unique_ptr<Splitter> splitHeader, splitWave;
    int headerHeight = HeaderView::kDefaultHeight, waveHeight = 56;
    bool customizing = false, taskbarPlayer = true;
    float uiScale = 1.0f;
    juce::String currentTitle, currentArtist;
    juce::Image currentCover;

    bool compact = false;
    int expandedHeight = 560;
    Smoothed windowHeight;
    bool animatingHeight = false;

    Smoothed dropGlow, toastAlpha;
    juce::String toastText;
    float toastTime = 0.0f;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::VBlankAttachment vblank;
    double lastTimestamp = 0.0;
};
} // namespace tp
