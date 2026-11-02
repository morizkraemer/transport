#include "SlotGrid.h"

namespace tp
{
using juce::Colour;
using juce::Rectangle;
using juce::Point;

namespace
{
constexpr float kGap = 6.0f;
constexpr float kWidthFactor[SlotGrid::kWidthModes] = { 0.66f, 1.0f, 1.5f, 2.0f };

/** Natural width/height ratio of each plugin at "normal" width. */
float naturalAspect (const juce::String& id)
{
    static const std::map<juce::String, float> aspects {
        { "vu", 1.5f },        { "peakrms", 0.8f },      { "loudness", 1.9f },   { "truepeak", 1.4f },  { "dynamics", 1.4f },
        { "spectrum", 2.6f },  { "spectrogram", 2.4f },  { "waterfall", 2.2f },  { "tonal", 1.25f },    { "scope", 2.4f },
        { "polar", 1.65f },    { "goniometer", 1.0f },   { "phase", 1.7f },      { "imager", 1.5f },
        { "tempo", 1.5f },     { "key", 1.5f },          { "tuner", 1.35f },
        { "eq", 2.4f },        { "compressor", 2.1f },   { "limiter", 2.1f },
        { "led", 1.8f },       { "radial", 1.15f },      { "particles", 1.6f },  { "ambient", 1.6f },   { "vinyl", 1.5f }, { "reels", 1.7f },
    };
    const auto it = aspects.find (id);
    return it != aspects.end() ? it->second : 1.6f;
}

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

/** Justified-rows layout: tries every row count, picks the one whose natural height is closest
    to the area, then scales row heights to fill it exactly. Widths always fill each row. */
std::vector<Rectangle<float>> justify (const std::vector<float>& aspects, Rectangle<float> area)
{
    const int n = (int) aspects.size();
    std::vector<Rectangle<float>> out ((size_t) n);
    if (n == 0 || area.getWidth() < 10.0f || area.getHeight() < 10.0f) return out;

    float total = 0.0f;
    for (auto a : aspects) total += a;

    std::vector<std::vector<int>> bestRows;
    float bestScore = std::numeric_limits<float>::max();

    for (int r = 1; r <= n; ++r)
    {
        std::vector<std::vector<int>> rows (1);
        float done = 0.0f, acc = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            rows.back().push_back (i);
            acc += aspects[(size_t) i];
            const int rowsLeft = r - (int) rows.size();
            const int itemsLeft = n - i - 1;
            if (rowsLeft > 0 && itemsLeft >= rowsLeft)
            {
                const float target = (total - done) / (float) (rowsLeft + 1);
                const float next = itemsLeft > 0 ? aspects[(size_t) i + 1] : 0.0f;
                if (acc >= target || std::abs (acc - target) < std::abs (acc + next - target) || itemsLeft == rowsLeft)
                {
                    done += acc;
                    acc = 0.0f;
                    rows.emplace_back();
                }
            }
        }
        if (rows.back().empty()) rows.pop_back();

        float height = kGap * (float) (rows.size() - 1);
        for (auto& row : rows)
        {
            float sum = 0.0f;
            for (int i : row) sum += aspects[(size_t) i];
            height += (area.getWidth() - kGap * (float) (row.size() - 1)) / sum;
        }
        const float score = std::abs (std::log (height / area.getHeight()));
        if (score < bestScore) { bestScore = score; bestRows = rows; }
    }

    // natural heights, then scale to fill the area
    std::vector<float> heights;
    float sumH = 0.0f;
    for (auto& row : bestRows)
    {
        float sum = 0.0f;
        for (int i : row) sum += aspects[(size_t) i];
        heights.push_back ((area.getWidth() - kGap * (float) (row.size() - 1)) / sum);
        sumH += heights.back();
    }
    const float scale = (area.getHeight() - kGap * (float) (bestRows.size() - 1)) / sumH;

    float y = area.getY();
    for (size_t r = 0; r < bestRows.size(); ++r)
    {
        const float h = heights[r] * scale;
        float x = area.getX();
        for (size_t k = 0; k < bestRows[r].size(); ++k)
        {
            const int i = bestRows[r][k];
            const bool last = k + 1 == bestRows[r].size();
            const float w = last ? area.getRight() - x : aspects[(size_t) i] * heights[r];
            out[(size_t) i] = { x, y, w, h };
            x += w + kGap;
        }
        y += h + kGap;
    }
    return out;
}
} // namespace

const char* SlotGrid::widthModeName (int m)
{
    static const char* names[kWidthModes] = { "Narrow", "Normal", "Wide", "Extra wide" };
    return names[juce::jlimit (0, kWidthModes - 1, m)];
}

//==============================================================================
class SlotGrid::Slot final : public juce::Component
{
public:
    Slot (SlotGrid& g, const VisualizerInfo& i) : grid (g), info (&i)
    {
        vis = i.create();
        attach (*vis);
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
    int widthMode = 1;
    float wait = 0.0f;
    Smoothed appear, hover, lift, swapFade, bypassShade, buttonHover[5], visibleWithMax { 1.0f, 1.0f };
    int pressedButton = -1;
    Point<float> grabOffset;

    float aspect() const { return naturalAspect (info->id) * kWidthFactor[widthMode]; }

    void setVisualizer (const VisualizerInfo& i)
    {
        if (outgoing != nullptr) removeChildComponent (outgoing.get());
        outgoing = std::move (vis);
        info = &i;
        vis = i.create();
        attach (*vis);
        addAndMakeVisible (*vis);
        vis->setAlpha (0.0f);
        swapFade.snap (0.0f);
        swapFade.target = 1.0f;
        resized();
    }

    void attach (Visualizer& v)
    {
        v.trackChanged (grid.cover);
        v.onStateChanged = [this] { if (grid.onStateChanged) grid.onStateChanged(); };
    }

    bool isEffect() const { return vis->effect() != nullptr; }
    bool isBypassed() const { return isEffect() && vis->effect()->bypass.load(); }

    enum class Btn { power, width, replace, remove, maximise };
    std::vector<Btn> buttons() const
    {
        std::vector<Btn> b;
        if (isEffect()) b.push_back (Btn::power);
        if (grid.customizing) { b.push_back (Btn::width); b.push_back (Btn::replace); b.push_back (Btn::remove); }
        else b.push_back (Btn::maximise);
        return b;
    }

    /** How strongly the buttons show: always in customize mode, otherwise on hover. */
    float chrome() const
    {
        return juce::jmax (grid.customizeAnim.current, hover.current);
    }

    /** Returns true once a removed slot has faded out and can be deleted. */
    bool step (float dt)
    {
        bool dirty = false;
        if (wait > 0.0f) wait -= dt;
        else dirty |= appear.tick (dt, removing ? 14.0f : 9.0f);

        hover.target = (! removing && isMouseOverOrDragging (true)) ? 1.0f : 0.0f;
        if (hover.target == 0.0f) for (auto& b : buttonHover) b.target = 0.0f;
        bypassShade.target = isBypassed() ? 1.0f : 0.0f;
        dirty |= hover.tick (dt, 14.0f);
        dirty |= lift.tick (dt, 16.0f);
        dirty |= bypassShade.tick (dt, 10.0f);
        for (auto& b : buttonHover) dirty |= b.tick (dt, 18.0f);
        if (grid.customizeAnim.moving()) dirty = true;
        visibleWithMax.target = (grid.maximised != nullptr && grid.maximised != this) ? 0.0f : 1.0f;
        dirty |= visibleWithMax.tick (dt, 12.0f);
        setInterceptsMouseClicks (! removing && visibleWithMax.target > 0.5f, true);
        setVisible (visibleWithMax.current > 0.01f);

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

        const float s = (0.9f + 0.1f * easeOutCubic (appear.current)) * (1.0f + 0.02f * lift.current);
        const auto c = current.getCentre();
        setTransform (juce::AffineTransform::scale (s, s, c.x, c.y));
        setAlpha (juce::jlimit (0.0f, 1.0f, appear.current * visibleWithMax.current));

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
        g.setColour (theme::panel.interpolatedWith (theme::panelHover, hover.current * 0.35f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (bypassShade.current > 0.01f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.28f * bypassShade.current));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
            g.setColour (theme::label.withAlpha (bypassShade.current));
            g.setFont (theme::font (9.5f, true));
            g.drawText ("BYPASSED", getLocalBounds().toFloat().reduced (12, 8).removeFromBottom (14), juce::Justification::centredRight, false);
        }

        const float c = grid.customizeAnim.current;
        if (c > 0.01f)
        {
            const float dash[] = { 5.0f, 4.0f };
            juce::Path outline;
            outline.addRoundedRectangle (getLocalBounds().toFloat().reduced (0.75f), 3.0f);
            juce::Path dashed;
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dash, 2);
            g.setColour (theme::accent.withAlpha (0.55f * c));
            g.fillPath (dashed);
        }

        const float h = chrome();
        if (h > 0.01f)
        {
            const auto btns = buttons();
            if (c > 0.01f)
            {
                // solid strip so the plugin's own labels don't show through
                g.setColour (Colour (0xff2c2c2c).withAlpha (0.96f * c));
                g.fillRoundedRectangle (getLocalBounds().toFloat().removeFromTop (32.0f), 3.0f);
                g.setColour (juce::Colours::white.withAlpha (c));
                g.setFont (theme::font (10.0f, true));
                auto nameArea = Rectangle<float> (10, 6, (float) getWidth() - 30.0f * (float) btns.size() - 20.0f, 20);
                g.drawText (info->name.toUpperCase(), nameArea, juce::Justification::centredLeft, true);
            }

            for (int i = 0; i < (int) btns.size(); ++i)
            {
                const auto r = buttonRect (i, (int) btns.size());
                const auto kind = btns[(size_t) i];
                g.setColour (Colour (0xff2e2e2e).interpolatedWith (kind == Btn::remove ? Colour (0xff6a3a2e) : Colour (0xff4a4a4a), buttonHover[i].current).withAlpha (0.9f * h));
                g.fillRoundedRectangle (r, 3.0f);
                auto iconColour = Colour (0xffd6d6d6).interpolatedWith (juce::Colours::white, buttonHover[i].current);
                if (kind == Btn::power) iconColour = isBypassed() ? Colour (0xff8a8a8a) : theme::accent;
                g.setColour (iconColour.withAlpha (h));
                const auto cc = r.getCentre();
                juce::Path p;
                switch (kind)
                {
                    case Btn::replace:
                        p.startNewSubPath (cc.x - 6, cc.y - 3); p.lineTo (cc.x + 5, cc.y - 3);
                        p.startNewSubPath (cc.x + 2, cc.y - 6); p.lineTo (cc.x + 5, cc.y - 3); p.lineTo (cc.x + 2, cc.y);
                        p.startNewSubPath (cc.x + 6, cc.y + 3); p.lineTo (cc.x - 5, cc.y + 3);
                        p.startNewSubPath (cc.x - 2, cc.y); p.lineTo (cc.x - 5, cc.y + 3); p.lineTo (cc.x - 2, cc.y + 6);
                        break;
                    case Btn::remove:
                        p.startNewSubPath (cc.x - 4.5f, cc.y - 4.5f); p.lineTo (cc.x + 4.5f, cc.y + 4.5f);
                        p.startNewSubPath (cc.x + 4.5f, cc.y - 4.5f); p.lineTo (cc.x - 4.5f, cc.y + 4.5f);
                        break;
                    case Btn::width:
                        p.startNewSubPath (cc.x - 7, cc.y); p.lineTo (cc.x + 7, cc.y);
                        p.startNewSubPath (cc.x - 4, cc.y - 3); p.lineTo (cc.x - 7, cc.y); p.lineTo (cc.x - 4, cc.y + 3);
                        p.startNewSubPath (cc.x + 4, cc.y - 3); p.lineTo (cc.x + 7, cc.y); p.lineTo (cc.x + 4, cc.y + 3);
                        break;
                    case Btn::maximise:
                        if (grid.maximised == this)
                        {
                            p.startNewSubPath (cc.x - 6, cc.y - 2); p.lineTo (cc.x - 2, cc.y - 2); p.lineTo (cc.x - 2, cc.y - 6);
                            p.startNewSubPath (cc.x + 6, cc.y + 2); p.lineTo (cc.x + 2, cc.y + 2); p.lineTo (cc.x + 2, cc.y + 6);
                        }
                        else
                        {
                            p.startNewSubPath (cc.x - 6, cc.y - 2); p.lineTo (cc.x - 6, cc.y - 6); p.lineTo (cc.x - 2, cc.y - 6);
                            p.startNewSubPath (cc.x + 6, cc.y + 2); p.lineTo (cc.x + 6, cc.y + 6); p.lineTo (cc.x + 2, cc.y + 6);
                        }
                        break;
                    case Btn::power:
                        p.addCentredArc (cc.x, cc.y + 0.5f, 5.0f, 5.0f, 0, 0.6f, 2.0f * juce::MathConstants<float>::pi - 0.6f, true);
                        p.startNewSubPath (cc.x, cc.y - 6.5f); p.lineTo (cc.x, cc.y - 1.0f);
                        break;
                }
                g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

        }
        if (lift.current > 0.01f)
        {
            g.setColour (theme::accent.withAlpha (0.9f * lift.current));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.75f), 3.0f, 1.5f);
        }
    }

    Rectangle<float> buttonRect (int i, int count) const { return { (float) getWidth() - 4.0f - 28.0f * (float) (count - i), 6.0f, 24.0f, 20.0f }; }
    int buttonAt (Point<float> p) const
    {
        if (chrome() < 0.3f) return -1;
        const int n = (int) buttons().size();
        for (int i = 0; i < n; ++i) if (buttonRect (i, n).expanded (2).contains (p)) return i;
        return -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int b = buttonAt (e.position);
        for (int i = 0; i < 5; ++i) buttonHover[i].target = i == b ? 1.0f : 0.0f;
        setMouseCursor (b >= 0 ? juce::MouseCursor::PointingHandCursor
                               : (grid.customizing ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { grid.showSlotMenu (*this); return; }
        pressedButton = buttonAt (e.position);
        grabOffset = e.position;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! grid.customizing || pressedButton >= 0 || e.mods.isPopupMenu()) return;
        if (! beingDragged && e.getDistanceFromDragStart() > 5)
            grid.beginDrag (*this);
        if (beingDragged)
        {
            const auto inGrid = e.getEventRelativeTo (&grid).position;
            grid.dragMoved (*this, inGrid - grabOffset + Point<float> (current.getWidth() * 0.5f, current.getHeight() * 0.5f));
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! grid.customizing && buttonAt (e.position) < 0) grid.toggleMaximise (*this);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (beingDragged) { grid.endDrag (*this); return; }
        if (pressedButton >= 0 && buttonAt (e.position) == pressedButton)
        {
            const int index = grid.indexOf (this);
            switch (buttons()[(size_t) pressedButton])
            {
                case Btn::power:   vis->effect()->bypass = ! vis->effect()->bypass.load(); if (grid.onStateChanged) grid.onStateChanged(); break;
                case Btn::width:   grid.setWidthMode (index, (widthMode + 1) % kWidthModes); break;
                case Btn::replace: if (grid.onBrowseRequested) grid.onBrowseRequested (index); break;
                case Btn::remove:  grid.removeSlot (index); break;
                case Btn::maximise: grid.toggleMaximise (*this); break;
            }
        }
        pressedButton = -1;
    }
};

//==============================================================================
class SlotGrid::AddTile final : public juce::Component, public juce::SettableTooltipClient
{
public:
    AddTile() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("Add a plugin"); }

    std::function<void()> onClick;
    Rectangle<float> current, target;
    bool hasBounds = false;
    HoverState state;
    Smoothed appear;

    void step (float dt)
    {
        bool dirty = state.tick (dt);
        dirty |= appear.tick (dt, 10.0f);
        if (! hasBounds) { current = target; hasBounds = true; }
        else current = approach (current, target, 1.0f - std::exp (-dt * 13.0f));
        if (getBounds() != current.toNearestInt()) setBounds (current.toNearestInt());
        setAlpha (appear.current);
        setVisible (appear.current > 0.01f);
        if (dirty) repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override { state.hover.target = 1.0f; }
    void mouseExit (const juce::MouseEvent&) override  { state.hover.target = 0.0f; state.press.target = 0.0f; }
    void mouseDown (const juce::MouseEvent&) override  { state.press.target = 1.0f; }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (state.press.target > 0.5f && getLocalBounds().contains (e.getPosition()) && onClick) onClick();
        state.press.target = 0.0f;
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.75f);
        g.setColour (theme::panel.withAlpha (0.35f + 0.4f * state.hover.current));
        g.fillRoundedRectangle (r, 3.0f);
        const float dash[] = { 5.0f, 4.0f };
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r, 3.0f);
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dash, 2);
        g.setColour (theme::accent.withAlpha (0.5f + 0.5f * state.hover.current));
        g.fillPath (dashed);

        const auto c = r.getCentre();
        const float s = juce::jmin (1.0f, juce::jmin (r.getWidth(), r.getHeight()) / 120.0f) * (1.0f + 0.08f * state.hover.current - 0.08f * state.press.current);
        juce::Path plus;
        plus.addRoundedRectangle (-2.5f, -20.0f, 5.0f, 40.0f, 1.5f);
        plus.addRoundedRectangle (-20.0f, -2.5f, 40.0f, 5.0f, 1.5f);
        g.setColour (theme::accent.interpolatedWith (juce::Colours::white, state.hover.current * 0.3f));
        g.fillPath (plus, juce::AffineTransform::rotation (state.hover.current * juce::MathConstants<float>::halfPi).scaled (s).translated (c.x, c.y - 6.0f));
        g.setFont (theme::font (10.5f, true));
        g.drawText ("Add plugin", Rectangle<float> (r.getX(), c.y + 22.0f * s, r.getWidth(), 14), juce::Justification::centred, false);
    }
};

//==============================================================================
SlotGrid::SlotGrid()
{
    addTile = std::make_unique<AddTile>();
    addTile->onClick = [this] { if (onBrowseRequested) onBrowseRequested (-1); };
    addChildComponent (*addTile);
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

void SlotGrid::setIds (const juce::StringArray& ids, const juce::Array<juce::var>& states, const juce::Array<int>& widths)
{
    for (auto& s : slots) removeChildComponent (s.get());
    slots.clear();
    maximised = nullptr;
    float wait = 0.12f;
    for (int n = 0; n < ids.size(); ++n)
        if (auto* info = findVisualizer (ids[n]))
        {
            auto s = std::make_unique<Slot> (*this, *info);
            if (n < states.size() && ! states[n].isVoid())
                s->vis->setState (states[n]);
            if (n < widths.size())
                s->widthMode = juce::jlimit (0, kWidthModes - 1, widths[n]);
            s->wait = wait;
            wait += 0.07f;
            addAndMakeVisible (*s);
            slots.push_back (std::move (s));
        }
    addTile->toBack();
    layout (true);
}

juce::Array<juce::var> SlotGrid::getStates() const
{
    juce::Array<juce::var> out;
    for (auto* s : activeSlots()) out.add (s->vis->getState());
    return out;
}

juce::Array<int> SlotGrid::getWidths() const
{
    juce::Array<int> out;
    for (auto* s : activeSlots()) out.add (s->widthMode);
    return out;
}

std::vector<std::shared_ptr<EffectProcessor>> SlotGrid::effectChain() const
{
    std::vector<std::shared_ptr<EffectProcessor>> out;
    for (auto* s : activeSlots())
        if (auto fx = s->vis->effect()) out.push_back (fx);
    return out;
}

void SlotGrid::setCover (const juce::Image& newCover)
{
    cover = newCover;
    for (auto& s : slots) s->vis->trackChanged (cover);
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
        auto it = std::find_if (slots.begin(), slots.end(), [] (auto& x) { return x->removing; });
        slots.insert (it, std::move (s));
        layout (false);
        raw->current = addTile->isVisible() ? addTile->current : raw->target;   // grow out of the + tile
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
        layout (false);
        if (onChanged) onChanged();
    }
}

void SlotGrid::removeSlot (int index)
{
    const auto a = activeSlots();
    if (! juce::isPositiveAndBelow (index, (int) a.size())) return;
    auto* s = a[(size_t) index];
    if (maximised == s) maximised = nullptr;
    s->removing = true;
    s->appear.target = 0.0f;
    s->setInterceptsMouseClicks (false, false);
    layout (false);
    if (onChanged) onChanged();
}

void SlotGrid::setWidthMode (int index, int mode)
{
    const auto a = activeSlots();
    if (! juce::isPositiveAndBelow (index, (int) a.size())) return;
    a[(size_t) index]->widthMode = juce::jlimit (0, kWidthModes - 1, mode);
    layout (false);
    if (onChanged) onChanged();
}

void SlotGrid::toggleMaximise (Slot& s)
{
    maximised = maximised == &s ? nullptr : &s;
    if (maximised != nullptr) maximised->toFront (false);
    layout (false);
}

bool SlotGrid::restoreMaximised()
{
    if (maximised == nullptr) return false;
    maximised = nullptr;
    layout (false);
    return true;
}

void SlotGrid::setCustomizing (bool c)
{
    if (c == customizing) return;
    customizing = c;
    maximised = nullptr;
    customizeAnim.target = c ? 1.0f : 0.0f;
    addTile->appear.target = c ? 1.0f : 0.0f;
    if (c) addTile->setVisible (true);
    layout (false);
}

void SlotGrid::layout (bool snap)
{
    const auto a = activeSlots();
    std::vector<float> aspects;
    for (auto* s : a) aspects.push_back (s->aspect());
    if (customizing) aspects.push_back (0.55f);   // the + tile

    const auto rects = justify (aspects, getLocalBounds().toFloat());
    for (size_t i = 0; i < a.size(); ++i)
    {
        a[i]->target = a[i] == maximised ? getLocalBounds().toFloat() : rects[i];
        if (snap) { a[i]->current = a[i]->target; a[i]->hasBounds = true; a[i]->setBounds (a[i]->current.toNearestInt()); }
    }

    if (customizing)
        addTile->target = rects.back();
    else if (! a.empty())
        addTile->target = addTile->current.withCentre (a.back()->target.getCentre());   // shrink into the last slot as it fades
    if (snap) { addTile->current = addTile->target; addTile->hasBounds = true; }
    repaint();
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
    const bool wasMoving = customizeAnim.moving();
    customizeAnim.tick (dt, 10.0f);
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
    if (dragging != nullptr || wasMoving) repaint();
}

void SlotGrid::paint (juce::Graphics& g)
{
    if (activeSlots().empty() && ! customizing)
    {
        g.setColour (theme::label.withAlpha (0.7f));
        g.setFont (theme::font (12.0f));
        g.drawText (utf8 ("No plugins \xe2\x80\x94 MENU \xe2\x86\x92 Customize layout to add some"), getLocalBounds(), juce::Justification::centred, false);
    }
    if (dragging != nullptr && dragging->lift.current > 0.01f)
    {
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.5f * dragging->lift.current), 24, { 0, 8 });
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

    const auto a = activeSlots();
    int best = indexOf (&s);
    float bestDist = std::numeric_limits<float>::max();
    for (int i = 0; i < (int) a.size(); ++i)
    {
        const float d = a[(size_t) i]->target.getCentre().getDistanceFrom (centre);
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
    if (onChanged) onChanged();
}

void SlotGrid::showSlotMenu (Slot& s)
{
    const int index = indexOf (&s);
    juce::PopupMenu m, replace, width;
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
    for (int w = 0; w < kWidthModes; ++w) width.addItem (3000 + w, widthModeName (w), true, w == s.widthMode);
    m.addItem (4, maximised == &s ? "Restore" : "Maximize");
    m.addSubMenu ("Replace with", replace);
    m.addSubMenu ("Width", width);
    m.addItem (2, "Remove");
    m.addSeparator();
    m.addItem (3, customizing ? "Done customizing" : utf8 ("Customize layout\xe2\x80\xa6"));

    juce::Component::SafePointer<SlotGrid> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, index] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 3000) safe->setWidthMode (index, r - 3000);
        else if (r >= 1000) safe->replaceVisualizer (index, visualizerRegistry()[(size_t) (r - 1000)].id);
        else if (r == 2) safe->removeSlot (index);
        else if (r == 3 && safe->onCustomizeRequested) safe->onCustomizeRequested();
        else if (r == 4)
        {
            const auto act = safe->activeSlots();
            if (juce::isPositiveAndBelow (index, (int) act.size())) safe->toggleMaximise (*act[(size_t) index]);
        }
    });
}
} // namespace tp
