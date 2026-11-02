#pragma once

#include "Visualizers.h"

namespace tp
{
/** The plugin row: any number of slots plus the "+" tile. Slots animate into place,
    fade in/out, and can be dragged to reorder. */
class SlotGrid final : public juce::Component, public Animated
{
public:
    SlotGrid();
    ~SlotGrid() override;

    void setIds (const juce::StringArray& ids, const juce::Array<juce::var>& states = {});
    juce::StringArray getIds() const;
    juce::Array<juce::var> getStates() const;

    /** Effects of all active slots, in slot order (the audio chain). */
    std::vector<std::shared_ptr<EffectProcessor>> effectChain() const;
    void setCover (const juce::Image& cover);

    void addVisualizer (const juce::String& id);
    void replaceVisualizer (int index, const juce::String& id);
    void removeSlot (int index);

    void setColumns (int c);
    int getColumns() const { return columns; }

    std::function<void (int slotIndex)> onBrowseRequested;   // -1 = add new
    std::function<void()> onChanged;
    std::function<void (int rows)> onRowsChanged;
    std::function<void()> onStateChanged;   // a plugin setting changed (save, no relayout)   // fired when adding/removing changes the number of rows

    int rowsFor (int itemCount) const;

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
    juce::Rectangle<float> cell (int index, int count) const;
    void showSlotMenu (Slot&);
    void showGridMenu();

    void beginDrag (Slot&);
    void dragMoved (Slot&, juce::Point<float> centreInGrid);
    void endDrag (Slot&);

    std::vector<std::unique_ptr<Slot>> slots;
    std::unique_ptr<AddTile> addTile;
    int columns = 3, lastRows = 0;
    juce::Image cover;
    Slot* dragging = nullptr;
};
} // namespace tp
