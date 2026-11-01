#pragma once

#include "Theme.h"
#include "core/AnalysisFrame.h"

namespace tp
{
/** A plugin that lives in one of the slots under the waveform. */
class Visualizer : public juce::Component
{
public:
    Visualizer() { setInterceptsMouseClicks (false, false); setOpaque (false); }

    /** Called once per display frame. Update internal state and call repaint() if anything moved. */
    virtual void update (const AnalysisFrame& frame, float dt) = 0;
};

struct VisualizerInfo
{
    juce::String id, name, category, monogram, description;
    std::function<std::unique_ptr<Visualizer>()> create;
};

const std::vector<VisualizerInfo>& visualizerRegistry();
const VisualizerInfo* findVisualizer (const juce::String& id);
juce::StringArray visualizerCategories();
} // namespace tp
