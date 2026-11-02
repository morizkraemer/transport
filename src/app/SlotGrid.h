#pragma once

#include "Visualizers.h"

namespace tp
{
/** The plugin area. Each plugin has a natural shape (goniometer square, spectrum wide, ...);
    slots are packed into justified rows that always fill the area. In customize mode slots
    show their controls, can be dragged, resized (narrow..extra wide) and the + tile appears. */
class SlotGrid final : public juce::Component, public Animated
{
public:
    SlotGrid();
    ~SlotGrid() override;

    void setIds (const juce::StringArray& ids, const juce::Array<juce::var>& states = {}, const juce::Array<int>& widths = {});
    juce::StringArray getIds() const;
    juce::Array<juce::var> getStates() const;
    juce::Array<int> getWidths() const;

    /** Effects of all active slots, in slot order (the audio chain). */
    std::vector<std::shared_ptr<EffectProcessor>> effectChain() const;
    void setCover (const juce::Image& cover);

    void addVisualizer (const juce::String& id);
    void replaceVisualizer (int index, const juce::String& id);
    void removeSlot (int index);
    void setWidthMode (int index, int mode);

    void setCustomizing (bool);
    /** Returns true if a maximised plugin was restored. */
    bool restoreMaximised();
    bool isCustomizing() const { return customizing; }

    std::function<void (int slotIndex)> onBrowseRequested;   // -1 = add new
    std::function<void()> onChanged;                          // slots added/removed/reordered/resized
    std::function<void()> onStateChanged;                     // a plugin setting changed (save only)
    std::function<void()> onCustomizeRequested;

    static constexpr int kWidthModes = 4;
    static const char* widthModeName (int mode);

    void updateFrame (const AnalysisFrame& f, float dt);
    void animate (float dt) override;
    void resized() override { layout (false); }
    void paint (juce::Graphics&) override;

private:
    class Slot;
    class AddTile;
    friend class Slot;

    std::vector<Slot*> activeSlots() const;
    int indexOf (const Slot*) const;
    void layout (bool snap);
    void showSlotMenu (Slot&);

    void toggleMaximise (Slot&);
    void beginDrag (Slot&);
    void dragMoved (Slot&, juce::Point<float> centreInGrid);
    void endDrag (Slot&);

    std::vector<std::unique_ptr<Slot>> slots;
    std::unique_ptr<AddTile> addTile;
    bool customizing = false;
    Smoothed customizeAnim;
    juce::Image cover;
    Slot* dragging = nullptr;
    Slot* maximised = nullptr;
};
} // namespace tp
