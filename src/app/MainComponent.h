#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_opengl/juce_opengl.h>

#include "core/Analyzer.h"
#include "AudioEngine.h"
#include "HeaderView.h"
#include "PluginBrowser.h"
#include "SlotGrid.h"
#include "TitleBar.h"
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
    void saveSnapshot();
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

    TitleBar titleBar;
    HeaderView header;
    WaveformView waveform;
    SlotGrid slotGrid;
    PluginBrowser browser;
    juce::TooltipWindow tooltips { this, 700 };

    juce::ApplicationProperties properties;
    std::vector<QuickAction> quick;
    int quickColumns = 2;

    juce::Array<juce::File> playlist;
    int playIndex = -1;
    double loopA = -1.0, loopB = -1.0;

    bool compact = false;
    int expandedHeight = 560;
    Smoothed windowHeight;
    bool animatingHeight = false;

    Smoothed dropGlow, toastAlpha;
    juce::String toastText;
    float toastTime = 0.0f;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::OpenGLContext openGL;
    juce::VBlankAttachment vblank;
    double lastTimestamp = 0.0;
};
} // namespace tp
