#include "SlotGrid.h"

namespace tp
{
using juce::Colour;
using juce::Rectangle;
using juce::Point;

namespace
{
constexpr float kGap = 8.0f;

Rectangle<float> approach (Rectangle<float> cur, Rectangle<float> target, float amount)
{
    return { cur.getX() + (target.getX() - cur.getX()) * amount,
             cur.getY() + (target.getY() - cur.getY()) * amount,
             cur.getWidth() + (target.getWidth() - cur.getWidth()) * amount,
             cur.getHeight() + (target.getHeight() - cur.getHeight()) * amount };
}

bool nearlySame (Rectangle<float> a, Rectangle<float> b)
{
    return std::abs (a.getX() - b.getX()) < 0.3f && std::abs (a.getY() - b.getY()) < 0.3f
        && std::abs (a.getWidth() - b.getWidth()) < 0.3f && std::abs (a.getHeight() - b.getHeight()) < 0.3f;
}
}

//==============================================================================
class SlotGrid::Slot final : public juce::Component
{
public:
    Slot (SlotGrid& g, const VisualizerInfo& i) : grid (g), info (&i)
    {
        vis = i.create();
        addAndMakeVisible (*vis);
        appear.snap (0.0f);
        appear.target = 1.0f;
        swapFade.snap (1.0f);
        setAlpha (0.0f);
    }

    SlotGrid& grid;
    const VisualizerInfo* info;
    std::unique_ptr<Visualizer> vis, outgoing;
    Rectangle<float> current, target;
    bool hasBounds = false, removing = false, beingDragged = false;
    float wait = 0.0f;
    Smoothed appear, hover, lift, swapFade, buttonHover[2];
    int pressedButton = -1;
    Point<float> grabOffset;

    void setVisualizer (const VisualizerInfo& i)
    {
        if (outgoing != nullptr) removeChildComponent (outgoing.get());
        outgoing = std::move (vis);
        info = &i;
        vis = i.create();
        addAndMakeVisible (*vis);
        vis->setAlpha (0.0f);
        swapFade.snap (0.0f);
        swapFade.target = 1.0f;
        resized();
    }

    /** Returns true once a removed slot has faded out and can be deleted. */
    bool step (float dt)
    {
        bool dirty = false;
        if (wait > 0.0f) wait -= dt;
        else dirty |= appear.tick (dt, removing ? 14.0f : 9.0f);

        dirty |= hover.tick (dt, 14.0f);
        dirty |= lift.tick (dt, 16.0f);
        for (auto& b : buttonHover) dirty |= b.tick (dt, 18.0f);

        if (swapFade.tick (dt, 8.0f) || outgoing != nullptr)
        {
            vis->setAlpha (swapFade.current);
            if (outgoing != nullptr)
            {
                outgoing->setAlpha (1.0f - swapFade.current);
                if (! swapFade.moving())
                {
                    removeChildComponent (outgoing.get());
                    outgoing.reset();
                }
            }
        }

        if (! hasBounds) { current = target; hasBounds = true; }
        else if (! beingDragged) current = approach (current, target, 1.0f - std::exp (-dt * 13.0f));
        if (! nearlySame (current, target) || beingDragged || getBounds() != current.toNearestInt())
            setBounds (current.toNearestInt());

        const float s = (0.9f + 0.1f * easeOutCubic (appear.current)) * (1.0f + 0.03f * lift.current);
        const auto c = current.getCentre();
        setTransform (juce::AffineTransform::scale (s, s, c.x, c.y));
        setAlpha (juce::jlimit (0.0f, 1.0f, appear.current));

        if (dirty) repaint();
        return removing && appear.current < 0.02f;
    }

    void resized() override
    {
        vis->setBounds (getLocalBounds());
        if (outgoing != nullptr) outgoing->setBounds (getLocalBounds());
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel.interpolatedWith (theme::panelHover, hover.current * 0.6f));
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        const float h = juce::jmax (hover.current, lift.current);
        if (h > 0.01f)
        {
            juce::ColourGradient grad (juce::Colours::black.withAlpha (0.42f * h), 0, 0, juce::Colours::transparentBlack, 0, 38, false);
            g.setGradientFill (grad);
            g.fillRect (getLocalBounds().removeFromTop (38));

            g.setColour (juce::Colours::white.withAlpha (h));
            g.setFont (theme::font (10.5f, true));
            g.drawText (info->name.toUpperCase(), Rectangle<float> (12, 6, (float) getWidth() - 90, 22), juce::Justification::centredLeft, true);

            for (int i = 0; i < 2; ++i)
            {
                const auto r = buttonRect (i);
                g.setColour (Colour (0xff2e2e2e).interpolatedWith (i == 1 ? Colour (0xff6a3a2e) : Colour (0xff4a4a4a), buttonHover[i].current).withAlpha (0.9f * h));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (Colour (0xffd6d6d6).interpolatedWith (juce::Colours::white, buttonHover[i].current).withAlpha (h));
                const auto c = r.getCentre();
                juce::Path p;
                if (i == 0)
                {
                    p.startNewSubPath (c.x - 6, c.y - 3); p.lineTo (c.x + 5, c.y - 3);
                    p.startNewSubPath (c.x + 2, c.y - 6); p.lineTo (c.x + 5, c.y - 3); p.lineTo (c.x + 2, c.y);
                    p.startNewSubPath (c.x + 6, c.y + 3); p.lineTo (c.x - 5, c.y + 3);
                    p.startNewSubPath (c.x - 2, c.y); p.lineTo (c.x - 5, c.y + 3); p.lineTo (c.x - 2, c.y + 6);
                }
                else
                {
                    p.startNewSubPath (c.x - 4.5f, c.y - 4.5f); p.lineTo (c.x + 4.5f, c.y + 4.5f);
                    p.startNewSubPath (c.x + 4.5f, c.y - 4.5f); p.lineTo (c.x - 4.5f, c.y + 4.5f);
                }
                g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        if (lift.current > 0.01f)
        {
            g.setColour (theme::accent.withAlpha (0.8f * lift.current));
            g.drawRect (getLocalBounds().toFloat(), 1.5f);
        }
    }

    Rectangle<float> buttonRect (int i) const { return { (float) getWidth() - 62.0f + 30.0f * (float) i, 7.0f, 26.0f, 22.0f }; }
    int buttonAt (Point<float> p) const { for (int i = 0; i < 2; ++i) if (buttonRect (i).expanded (2).contains (p)) return i; return -1; }

    void mouseEnter (const juce::MouseEvent&) override { hover.target = 1.0f; }
    void mouseExit (const juce::MouseEvent&) override  { hover.target = 0.0f; for (auto& b : buttonHover) b.target = 0.0f; }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int b = buttonAt (e.position);
        for (int i = 0; i < 2; ++i) buttonHover[i].target = i == b ? 1.0f : 0.0f;
        setMouseCursor (b >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { grid.showSlotMenu (*this); return; }
        pressedButton = buttonAt (e.position);
        grabOffset = e.position;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressedButton >= 0 || e.mods.isPopupMenu()) return;
        if (! beingDragged && e.getDistanceFromDragStart() > 5)
            grid.beginDrag (*this);
        if (beingDragged)
        {
            const auto inGrid = e.getEventRelativeTo (&grid).position;
            grid.dragMoved (*this, inGrid - grabOffset + Point<float> (current.getWidth() * 0.5f, current.getHeight() * 0.5f));
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (beingDragged) { grid.endDrag (*this); return; }
        if (pressedButton >= 0 && buttonAt (e.position) == pressedButton)
        {
            const int index = grid.indexOf (this);
            if (pressedButton == 0) { if (grid.onBrowseRequested) grid.onBrowseRequested (index); }
            else grid.removeSlot (index);
        }
        pressedButton = -1;
    }
};

//==============================================================================
class SlotGrid::AddTile final : public juce::Component, public juce::SettableTooltipClient
{
public:
    AddTile() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("Add a plugin"); }

    std::function<void()> onClick, onRightClick;
    Rectangle<float> current, target;
    bool hasBounds = false;
    HoverState state;
    Smoothed appear;
    float wait = 0.0f;

    void step (float dt)
    {
        bool dirty = state.tick (dt);
        if (wait > 0.0f) wait -= dt; else dirty |= appear.tick (dt, 9.0f);
        if (! hasBounds) { current = target; hasBounds = true; }
        else current = approach (current, target, 1.0f - std::exp (-dt * 13.0f));
        if (getBounds() != current.toNearestInt()) setBounds (current.toNearestInt());
        setAlpha (appear.current);
        if (dirty) repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override { state.hover.target = 1.0f; }
    void mouseExit (const juce::MouseEvent&) override  { state.hover.target = 0.0f; state.press.target = 0.0f; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        state.press.target = 1.0f;
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (state.press.target > 0.5f && getLocalBounds().contains (e.getPosition()) && onClick) onClick();
        state.press.target = 0.0f;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel.interpolatedWith (theme::panelHover, state.hover.current));

        const auto c = getLocalBounds().toFloat().getCentre();
        const float s = (juce::jmin (1.0f, (float) juce::jmin (getWidth(), getHeight()) / 160.0f)) * (1.0f + 0.08f * state.hover.current - 0.08f * state.press.current);
        const float angle = state.hover.current * juce::MathConstants<float>::halfPi;

        juce::Path plus;
        plus.addRectangle (-4.0f, -30.0f, 8.0f, 60.0f);
        plus.addRectangle (-30.0f, -4.0f, 60.0f, 8.0f);
        g.setColour (Colour (0xff8a8a8a).interpolatedWith (Colour (0xffcfcfcf), state.hover.current));
        g.fillPath (plus, juce::AffineTransform::rotation (angle).scaled (s).translated (c.x, c.y));

        if (state.hover.current > 0.01f && getHeight() > 120)
        {
            g.setColour (theme::label.withAlpha (state.hover.current));
            g.setFont (theme::font (11.0f));
            g.drawText ("Add plugin", Rectangle<float> (c.x - 60, c.y + 40 * s + 4 + 6 * (1.0f - state.hover.current), 120, 16), juce::Justification::centred, false);
        }
    }
};

//==============================================================================
SlotGrid::SlotGrid()
{
    addTile = std::make_unique<AddTile>();
    addTile->onClick = [this] { if (onBrowseRequested) onBrowseRequested (-1); };
    addTile->onRightClick = [this] { showGridMenu(); };
    addAndMakeVisible (*addTile);
}

SlotGrid::~SlotGrid() = default;

std::vector<SlotGrid::Slot*> SlotGrid::activeSlots() const
{
    std::vector<Slot*> out;
    for (auto& s : slots)
        if (! s->removing) out.push_back (s.get());
    return out;
}

int SlotGrid::indexOf (const Slot* s) const
{
    const auto a = activeSlots();
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] == s) return (int) i;
    return -1;
}

void SlotGrid::setIds (const juce::StringArray& ids)
{
    for (auto& s : slots) removeChildComponent (s.get());
    slots.clear();
    float wait = 0.12f;
    for (auto& id : ids)
        if (auto* info = findVisualizer (id))
        {
            auto s = std::make_unique<Slot> (*this, *info);
            s->wait = wait;
            wait += 0.07f;
            addAndMakeVisible (*s);
            slots.push_back (std::move (s));
        }
    addTile->wait = wait;
    addTile->appear.snap (0.0f);
    addTile->appear.target = 1.0f;
    addTile->toBack();
    layout (true);
}

juce::StringArray SlotGrid::getIds() const
{
    juce::StringArray ids;
    for (auto* s : activeSlots()) ids.add (s->info->id);
    return ids;
}

void SlotGrid::addVisualizer (const juce::String& id)
{
    if (auto* info = findVisualizer (id))
    {
        auto s = std::make_unique<Slot> (*this, *info);
        addAndMakeVisible (*s);
        auto* raw = s.get();
        // insert after the last active slot so removed ones keep fading at the end
        auto it = std::find_if (slots.begin(), slots.end(), [] (auto& x) { return x->removing; });
        slots.insert (it, std::move (s));
        layout (false);
        // start from where the + tile was, so it looks like the tile turned into the plugin
        raw->current = addTile->current;
        raw->hasBounds = true;
        if (onChanged) onChanged();
    }
}

void SlotGrid::replaceVisualizer (int index, const juce::String& id)
{
    const auto a = activeSlots();
    if (auto* info = findVisualizer (id); info != nullptr && juce::isPositiveAndBelow (index, (int) a.size()))
    {
        a[(size_t) index]->setVisualizer (*info);
        if (onChanged) onChanged();
    }
}

void SlotGrid::removeSlot (int index)
{
    const auto a = activeSlots();
    if (! juce::isPositiveAndBelow (index, (int) a.size())) return;
    auto* s = a[(size_t) index];
    s->removing = true;
    s->appear.target = 0.0f;
    s->setInterceptsMouseClicks (false, false);
    layout (false);
    if (onChanged) onChanged();
}

void SlotGrid::setColumns (int c)
{
    columns = juce::jlimit (1, 8, c);
    layout (false);
    if (onChanged) onChanged();
}

juce::Rectangle<float> SlotGrid::cell (int index, int count) const
{
    const auto area = getLocalBounds().toFloat();
    const int cols = juce::jmax (1, juce::jmin (columns, count));
    const int rows = (count + cols - 1) / cols;
    const float w = (area.getWidth() - kGap * (float) (cols - 1)) / (float) cols;
    const float h = (area.getHeight() - kGap * (float) (rows - 1)) / (float) rows;
    const int r = index / cols, c = index % cols;
    return { area.getX() + (float) c * (w + kGap), area.getY() + (float) r * (h + kGap), w, h };
}

int SlotGrid::rowsFor (int count) const
{
    const int cols = juce::jmax (1, juce::jmin (columns, count));
    return (count + cols - 1) / cols;
}

void SlotGrid::layout (bool snap)
{
    const auto a = activeSlots();
    const int count = (int) a.size() + 1;
    const int rows = rowsFor (count);
    if (rows != lastRows)
    {
        const bool notify = lastRows != 0;
        lastRows = rows;
        if (notify && onRowsChanged) onRowsChanged (rows);
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        a[i]->target = cell ((int) i, count);
        if (snap) { a[i]->current = a[i]->target; a[i]->hasBounds = true; a[i]->setBounds (a[i]->current.toNearestInt()); }
    }
    addTile->target = cell (count - 1, count);
    if (snap) { addTile->current = addTile->target; addTile->hasBounds = true; addTile->setBounds (addTile->current.toNearestInt()); }
}

void SlotGrid::updateFrame (const AnalysisFrame& f, float dt)
{
    for (auto& s : slots)
    {
        s->vis->update (f, dt);
        if (s->outgoing != nullptr) s->outgoing->update (f, dt);
    }
}

void SlotGrid::animate (float dt)
{
    for (size_t i = 0; i < slots.size();)
    {
        if (slots[i]->step (dt))
        {
            removeChildComponent (slots[i].get());
            slots.erase (slots.begin() + (std::ptrdiff_t) i);
        }
        else ++i;
    }
    addTile->step (dt);
    if (dragging != nullptr) repaint();
}

void SlotGrid::paint (juce::Graphics& g)
{
    if (dragging != nullptr && dragging->lift.current > 0.01f)
    {
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.5f * dragging->lift.current), 28, { 0, 10 });
        shadow.drawForRectangle (g, dragging->getBounds());
    }
}

void SlotGrid::beginDrag (Slot& s)
{
    dragging = &s;
    s.beingDragged = true;
    s.lift.target = 1.0f;
    s.toFront (false);
}

void SlotGrid::dragMoved (Slot& s, Point<float> centre)
{
    s.current = s.current.withCentre (centre);

    auto a = activeSlots();
    const int count = (int) a.size() + 1;
    int best = indexOf (&s);
    float bestDist = std::numeric_limits<float>::max();
    for (int i = 0; i < (int) a.size(); ++i)
    {
        const float d = cell (i, count).getCentre().getDistanceFrom (centre);
        if (d < bestDist) { bestDist = d; best = i; }
    }

    if (best != indexOf (&s))
    {
        std::vector<std::unique_ptr<Slot>> reordered, removed;
        std::unique_ptr<Slot> moving;
        for (auto& x : slots)
        {
            if (x.get() == &s) moving = std::move (x);
            else if (x->removing) removed.push_back (std::move (x));
            else reordered.push_back (std::move (x));
        }
        reordered.insert (reordered.begin() + best, std::move (moving));
        for (auto& x : removed) reordered.push_back (std::move (x));
        slots = std::move (reordered);
        layout (false);
    }
}

void SlotGrid::endDrag (Slot& s)
{
    s.beingDragged = false;
    s.lift.target = 0.0f;
    dragging = nullptr;
    layout (false);
    repaint();
    if (onChanged) onChanged();
}

void SlotGrid::showSlotMenu (Slot& s)
{
    const int index = indexOf (&s);
    juce::PopupMenu m, replace, cols;
    m.addSectionHeader (s.info->name);

    const auto& reg = visualizerRegistry();
    for (auto& cat : visualizerCategories())
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < reg.size(); ++i)
            if (reg[i].category == cat)
                sub.addItem (1000 + (int) i, reg[i].name, true, reg[i].id == s.info->id);
        replace.addSubMenu (cat, sub);
    }
    m.addSubMenu ("Replace with", replace);
    m.addItem (2, "Remove");
    m.addSeparator();
    for (int c = 1; c <= 6; ++c) cols.addItem (3000 + c, juce::String (c), true, c == columns);
    m.addSubMenu ("Columns", cols);

    juce::Component::SafePointer<SlotGrid> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, index] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 3000) safe->setColumns (r - 3000);
        else if (r >= 1000) safe->replaceVisualizer (index, visualizerRegistry()[(size_t) (r - 1000)].id);
        else if (r == 2) safe->removeSlot (index);
    });
}

void SlotGrid::showGridMenu()
{
    juce::PopupMenu m, cols;
    m.addItem (1, utf8 ("Add plugin\xe2\x80\xa6"));
    for (int c = 1; c <= 6; ++c) cols.addItem (3000 + c, juce::String (c), true, c == columns);
    m.addSubMenu ("Columns", cols);

    juce::Component::SafePointer<SlotGrid> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1 && safe->onBrowseRequested) safe->onBrowseRequested (-1);
        if (r >= 3000) safe->setColumns (r - 3000);
    });
}
} // namespace tp
