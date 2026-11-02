#pragma once

#include "Theme.h"
#include "core/AnalysisFrame.h"
#include "core/Effects.h"

namespace tp
{
/** A plugin that lives in one of the slots under the waveform: a visualizer, or an effect
    that also processes the audio (effect() returns its processor). */
class Visualizer : public juce::Component
{
public:
    Visualizer() { setInterceptsMouseClicks (false, false); setOpaque (false); }

    /** Called once per display frame. Update internal state and call repaint() if anything moved. */
    virtual void update (const AnalysisFrame& frame, float dt) = 0;

    /** Interactive plugins get mouse events (except in the slot's header strip). */
    virtual bool isInteractive() const { return false; }
    /** Non-null for effects: the processor the audio engine runs. */
    virtual std::shared_ptr<EffectProcessor> effect() const { return {}; }

    /** Settings that should survive a restart. */
    virtual juce::var getState() const { return {}; }
    virtual void setState (const juce::var&) {}

    /** The current track's cover art (may be invalid). */
    virtual void trackChanged (const juce::Image&) {}

    /** Call when the user changed a setting, so the layout gets saved. */
    std::function<void()> onStateChanged;

protected:
    void stateChanged() { if (onStateChanged) onStateChanged(); }
};

/** Base for plugins with controls: the top strip stays with the slot (drag, header buttons). */
class InteractivePlugin : public Visualizer
{
public:
    static constexpr int kHeaderStrip = 34;
    InteractivePlugin() { setInterceptsMouseClicks (true, true); }
    bool isInteractive() const override { return true; }
    bool hitTest (int, int y) override { return y >= kHeaderStrip; }
};

struct VisualizerInfo
{
    juce::String id, name, category, monogram, description;
    std::function<std::unique_ptr<Visualizer>()> create;
};

const std::vector<VisualizerInfo>& visualizerRegistry();
const VisualizerInfo* findVisualizer (const juce::String& id);
juce::StringArray visualizerCategories();

// factories implemented in the other plugin files
std::unique_ptr<Visualizer> makeTuner();
std::unique_ptr<Visualizer> makeKeyDisplay();
std::unique_ptr<Visualizer> makeTempo();
std::unique_ptr<Visualizer> makeDynamics();
std::unique_ptr<Visualizer> makeTruePeak();
std::unique_ptr<Visualizer> makeImager();
std::unique_ptr<Visualizer> makeTonalBalance();
std::unique_ptr<Visualizer> makeWaterfall();
std::unique_ptr<Visualizer> makeRadial();
std::unique_ptr<Visualizer> makeAmbient();
std::unique_ptr<Visualizer> makeVinyl();
std::unique_ptr<Visualizer> makeReels();
std::unique_ptr<Visualizer> makeParticles();
std::unique_ptr<Visualizer> makeEqualizer();
std::unique_ptr<Visualizer> makeCompressor();
std::unique_ptr<Visualizer> makeLimiter();

/** Shared helpers for plugin painting. */
namespace plug
{
float bandLevel (const AnalysisFrame& f, float lo, float hi);   // dB with +4.5 dB/oct tilt
float bandEdge (int i, int n, float lo = 20.0f, float hi = 20000.0f);
juce::String formatDb (float v, int decimals = 1);
void drawLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r,
                juce::Justification j = juce::Justification::centredLeft, juce::Colour c = theme::label, float px = 10.0f);
}
} // namespace tp
