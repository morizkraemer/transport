#include "PluginBrowser.h"

namespace tp
{
using juce::Colour;
using juce::Rectangle;

namespace
{
constexpr float kHeader = 52.0f, kSidebar = 184.0f, kCardH = 92.0f, kGap = 10.0f, kPad = 16.0f;
}

PluginBrowser::PluginBrowser()
{
    categories.add ("All");
    categories.addArray (visualizerCategories());
    cardHover.resize (visualizerRegistry().size());
    categoryHover.resize ((size_t) categories.size());
    setWantsKeyboardFocus (true);
    setVisible (false);
}

void PluginBrowser::open (int slotIndex, const juce::StringArray& idsInUse)
{
    slot = slotIndex;
    inUse = idsInUse;
    scroll.snap (0.0f);
    cardsFade.snap (0.0f);
    cardsFade.target = 1.0f;
    opened.target = 1.0f;
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void PluginBrowser::close()
{
    opened.target = 0.0f;
}

Rectangle<float> PluginBrowser::panelArea() const
{
    const auto b = getLocalBounds().toFloat();
    return Rectangle<float> (juce::jmin (1000.0f, b.getWidth() - 48.0f), juce::jmin (540.0f, b.getHeight() - 36.0f)).withCentre (b.getCentre());
}

Rectangle<float> PluginBrowser::closeArea() const
{
    const auto p = panelArea();
    return { p.getRight() - 48.0f, p.getY() + 4.0f, 44.0f, 44.0f };
}

Rectangle<float> PluginBrowser::categoryArea (int i) const
{
    const auto p = panelArea();
    return { p.getX(), p.getY() + kHeader + 10.0f + 40.0f * (float) i, kSidebar, 40.0f };
}

Rectangle<float> PluginBrowser::cardsViewport() const
{
    return panelArea().withTrimmedTop (kHeader).withTrimmedLeft (kSidebar).reduced (kPad);
}

int PluginBrowser::columnsForWidth() const { return cardsViewport().getWidth() > 700.0f ? 3 : 2; }

std::vector<int> PluginBrowser::visibleCards() const
{
    std::vector<int> out;
    const auto& reg = visualizerRegistry();
    for (size_t i = 0; i < reg.size(); ++i)
        if (selectedCategory == 0 || reg[i].category == categories[selectedCategory])
            out.push_back ((int) i);
    return out;
}

Rectangle<float> PluginBrowser::cardArea (int k) const
{
    const auto v = cardsViewport();
    const int cols = columnsForWidth();
    const float w = (v.getWidth() - kGap * (float) (cols - 1)) / (float) cols;
    return { v.getX() + (float) (k % cols) * (w + kGap), v.getY() + (float) (k / cols) * (kCardH + kGap) - scroll.current, w, kCardH };
}

float PluginBrowser::contentHeight() const
{
    const int rows = ((int) visibleCards().size() + columnsForWidth() - 1) / columnsForWidth();
    return (float) rows * (kCardH + kGap) - kGap;
}

void PluginBrowser::animate (float dt)
{
    if (! isVisible()) return;

    bool dirty = opened.tick (dt, opened.target > 0.5f ? 14.0f : 18.0f);
    dirty |= cardsFade.tick (dt, 12.0f);
    dirty |= scroll.tick (dt, 18.0f);
    closeHover.target = hoverClose ? 1.0f : 0.0f;
    dirty |= closeHover.tick (dt, 18.0f);
    for (size_t i = 0; i < cardHover.size(); ++i)
    {
        cardHover[i].target = (int) i == hoveredCard ? 1.0f : 0.0f;
        dirty |= cardHover[i].tick (dt, 16.0f);
    }
    for (size_t i = 0; i < categoryHover.size(); ++i)
    {
        categoryHover[i].target = (int) i == hoveredCategory || (int) i == selectedCategory ? 1.0f : 0.0f;
        dirty |= categoryHover[i].tick (dt, 16.0f);
    }

    if (opened.target == 0.0f && opened.current <= 0.001f)
    {
        setVisible (false);
        return;
    }
    if (dirty) repaint();
}

void PluginBrowser::paint (juce::Graphics& g)
{
    const float t = easeOutCubic (opened.current);
    g.fillAll (juce::Colours::black.withAlpha (0.58f * opened.current));
    if (opened.current < 0.01f) return;

    const auto panel = panelArea();
    const float s = 0.965f + 0.035f * t;
    g.addTransform (juce::AffineTransform::scale (s, s, panel.getCentreX(), panel.getCentreY()).translated (0.0f, 14.0f * (1.0f - t)));
    g.beginTransparencyLayer (juce::jlimit (0.0f, 1.0f, opened.current * 1.2f));

    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 40, { 0, 18 }).drawForRectangle (g, panel.toNearestInt());
    g.setColour (theme::dialog);
    g.fillRect (panel);
    g.setColour (Colour (0xff5c5c5c));
    g.drawRect (panel, 1.0f);

    // header
    auto header = panel.withHeight (kHeader);
    g.setColour (theme::deep);
    g.fillRect (header);
    g.setColour (theme::text);
    g.setFont (theme::font (14.0f, true));
    const juce::String title = slot < 0 ? "ADD PLUGIN" : "REPLACE PLUGIN";
    g.drawText (title, header.withTrimmedLeft (20), juce::Justification::centredLeft, false);
    g.setColour (Colour (0xffbdbdbd));
    g.setFont (theme::font (13.0f));
    const float titleW = juce::GlyphArrangement::getStringWidth (theme::font (14.0f, true), title);
    g.drawText (slot < 0 ? utf8 ("\xe2\x86\x92 new slot") : utf8 ("\xe2\x86\x92 slot ") + juce::String (slot + 1),
                header.withTrimmedLeft (30 + titleW), juce::Justification::centredLeft, false);

    const auto ca = closeArea();
    g.setColour (Colour (0xff3a3a3a).withAlpha (closeHover.current));
    g.fillRoundedRectangle (ca.reduced (6), 3.0f);
    g.setColour (theme::accent.interpolatedWith (juce::Colours::white, closeHover.current * 0.4f));
    const auto cc = ca.getCentre();
    g.drawLine (cc.x - 5, cc.y - 5, cc.x + 5, cc.y + 5, 1.8f);
    g.drawLine (cc.x + 5, cc.y - 5, cc.x - 5, cc.y + 5, 1.8f);

    // sidebar
    auto side = panel.withTrimmedTop (kHeader).withWidth (kSidebar);
    g.setColour (Colour (0xff2f2f2f));
    g.fillRect (side);
    const auto& reg = visualizerRegistry();
    for (int i = 0; i < categories.size(); ++i)
    {
        const auto r = categoryArea (i);
        const float h = categoryHover[(size_t) i].current;
        g.setColour (Colour (0xff454545).withAlpha (h * (i == selectedCategory ? 1.0f : 0.6f)));
        g.fillRect (r);
        if (i == selectedCategory)
        {
            g.setColour (theme::accent);
            g.fillRect (r.withWidth (3.0f));
        }
        g.setColour (theme::text);
        g.setFont (theme::font (13.0f, i == selectedCategory));
        g.drawText (categories[i], r.withTrimmedLeft (18), juce::Justification::centredLeft, false);

        int count = 0;
        for (auto& v : reg) if (i == 0 || v.category == categories[i]) ++count;
        g.setColour (Colour (0xffc4c4c4));
        g.setFont (theme::font (11.0f));
        g.drawText (juce::String (count), r.withTrimmedRight (16), juce::Justification::centredRight, false);
    }
    g.setColour (Colour (0xffc4c4c4));
    g.setFont (theme::font (11.0f));
    g.drawFittedText ("Drag slots to reorder.\nRight-click a slot for more.", side.reduced (16, 14).removeFromBottom (40).toNearestInt(), juce::Justification::bottomLeft, 2);

    // cards
    const auto view = cardsViewport();
    g.saveState();
    g.reduceClipRegion (view.expanded (4).toNearestInt());
    const auto cards = visibleCards();
    const float ct = easeOutCubic (cardsFade.current);
    for (size_t k = 0; k < cards.size(); ++k)
    {
        const int idx = cards[k];
        const auto& info = reg[(size_t) idx];
        const float h = cardHover[(size_t) idx].current;
        const float stagger = juce::jlimit (0.0f, 1.0f, ct * 1.5f - (float) k * 0.06f);
        auto r = cardArea ((int) k).translated (0.0f, 10.0f * (1.0f - stagger) - 2.0f * h);
        if (! r.intersects (view.expanded (4))) continue;

        g.beginTransparencyLayer (stagger);
        g.setColour (Colour (0xff3d3d3d).interpolatedWith (Colour (0xff474747), h));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Colour (0xff4f4f4f).interpolatedWith (theme::accent, h * 0.85f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        auto inner = r.reduced (12);
        auto tile = inner.removeFromLeft (54).withHeight (54);
        inner.removeFromLeft (12);
        g.setColour (theme::deep);
        g.fillRoundedRectangle (tile, 3.0f);
        g.setColour (Colour (0xff5c5c5c));
        g.drawRoundedRectangle (tile, 3.0f, 1.0f);
        g.setColour (Colour (0xffe0e0e0).interpolatedWith (theme::accent, h));
        g.setFont (theme::mono (12.0f));
        g.drawText (info.monogram, tile, juce::Justification::centred, false);

        auto nameRow = inner.removeFromTop (20);
        const bool used = inUse.contains (info.id);
        if (used)
        {
            g.setColour (theme::accent);
            g.setFont (theme::font (9.5f, true));
            g.drawText ("IN USE", nameRow.removeFromRight (44), juce::Justification::centredRight, false);
        }
        g.setColour (theme::text);
        g.setFont (theme::font (13.5f, true));
        g.drawText (info.name, nameRow, juce::Justification::centredLeft, true);

        g.setColour (Colour (0xffd0d0d0));
        g.setFont (theme::font (11.5f));
        g.drawFittedText (info.description, inner.removeFromTop (40).toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
        g.endTransparencyLayer();
    }
    g.restoreState();

    // scroll hint
    const float maxScroll = juce::jmax (0.0f, contentHeight() - view.getHeight());
    if (maxScroll > 0.0f && scroll.current < maxScroll - 2.0f)
    {
        juce::ColourGradient fade (theme::dialog.withAlpha (0.0f), 0, view.getBottom() - 30, theme::dialog, 0, view.getBottom() + kPad, false);
        g.setGradientFill (fade);
        g.fillRect (view.withTop (view.getBottom() - 30).withBottom (view.getBottom() + kPad));
    }

    g.endTransparencyLayer();
}

void PluginBrowser::updateHover (juce::Point<float> p)
{
    hoveredCard = -1;
    hoveredCategory = -1;
    hoverClose = closeArea().contains (p);

    for (int i = 0; i < categories.size(); ++i)
        if (categoryArea (i).contains (p)) hoveredCategory = i;

    if (cardsViewport().contains (p))
    {
        const auto cards = visibleCards();
        for (size_t k = 0; k < cards.size(); ++k)
            if (cardArea ((int) k).contains (p)) hoveredCard = cards[k];
    }

    setMouseCursor (hoveredCard >= 0 || hoveredCategory >= 0 || hoverClose ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void PluginBrowser::mouseMove (const juce::MouseEvent& e) { updateHover (e.position); }
void PluginBrowser::mouseExit (const juce::MouseEvent&)   { hoveredCard = hoveredCategory = -1; hoverClose = false; }

void PluginBrowser::mouseDown (const juce::MouseEvent& e)
{
    if (! isOpen()) return;
    updateHover (e.position);

    if (! panelArea().contains (e.position) || hoverClose) { close(); return; }

    if (hoveredCategory >= 0 && hoveredCategory != selectedCategory)
    {
        selectedCategory = hoveredCategory;
        scroll.snap (0.0f);
        cardsFade.snap (0.0f);
        cardsFade.target = 1.0f;
        return;
    }

    if (hoveredCard >= 0)
    {
        if (onPick) onPick (slot, visualizerRegistry()[(size_t) hoveredCard].id);
        close();
    }
}

void PluginBrowser::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float maxScroll = juce::jmax (0.0f, contentHeight() - cardsViewport().getHeight());
    scroll.target = juce::jlimit (0.0f, maxScroll, scroll.target - w.deltaY * 260.0f);
}

bool PluginBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}
} // namespace tp
