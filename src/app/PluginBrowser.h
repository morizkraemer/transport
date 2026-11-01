#pragma once

#include "Visualizers.h"

namespace tp
{
/** Full-window overlay for picking a plugin for a slot. Fades and scales in, cards react to hover. */
class PluginBrowser final : public juce::Component, public Animated
{
public:
    PluginBrowser();

    void open (int slotIndex, const juce::StringArray& idsInUse);
    void close();
    bool isOpen() const { return opened.target > 0.5f; }

    std::function<void (int slotIndex, const juce::String& id)> onPick;

    void animate (float dt) override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<float> panelArea() const;
    juce::Rectangle<float> closeArea() const;
    juce::Rectangle<float> categoryArea (int i) const;
    juce::Rectangle<float> cardsViewport() const;
    juce::Rectangle<float> cardArea (int visibleIndex) const;
    std::vector<int> visibleCards() const;
    int columnsForWidth() const;
    float contentHeight() const;
    void updateHover (juce::Point<float>);

    juce::StringArray categories, inUse;
    int selectedCategory = 0, slot = -1;
    int hoveredCard = -1, hoveredCategory = -1;
    bool hoverClose = false;

    Smoothed opened, cardsFade, scroll, closeHover;
    std::vector<Smoothed> cardHover, categoryHover;
};
} // namespace tp
