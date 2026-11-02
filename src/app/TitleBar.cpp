#include "TitleBar.h"

namespace tp
{
namespace
{
#if JUCE_MAC
constexpr bool kMacStyle = true;
#else
constexpr bool kMacStyle = false;
#endif
constexpr float kLightsWidth = 76.0f;   // macOS traffic lights area
}

TitleBar::TitleBar() = default;

juce::Rectangle<float> TitleBar::menuArea() const
{
    if (kMacStyle) return { kLightsWidth, 0.0f, 64.0f, (float) getHeight() };
    return { 0.0f, 0.0f, (float) juce::jmax (96, menuWidth), (float) getHeight() };
}

juce::Rectangle<float> TitleBar::buttonArea (int i) const
{
    if (kMacStyle)
    {
        const int k = i == 2 ? 0 : i + 1;   // close, minimise, zoom from the left
        return juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ 20.0f + 20.0f * (float) k, (float) getHeight() * 0.5f });
    }
    return { (float) getWidth() - 46.0f * (float) (3 - i), 0.0f, 46.0f, (float) getHeight() };
}

juce::Rectangle<float> TitleBar::doneArea() const
{
    return juce::Rectangle<float> (64.0f, 22.0f).withCentre ({ (float) getWidth() * 0.5f + 70.0f, (float) getHeight() * 0.5f });
}

TitleBar::Hit TitleBar::hitAt (juce::Point<float> p) const
{
    if (customizing.target > 0.5f && doneArea().contains (p)) return Hit::done;
    for (int i = 0; i < 3; ++i)
        if (buttonArea (i).contains (p))
            return i == 0 ? Hit::minimise : (i == 1 ? Hit::maximise : Hit::close);
    if (menuArea().contains (p)) return Hit::menu;
    return Hit::caption;
}

void TitleBar::animate (float dt)
{
    bool dirty = customizing.tick (dt, 12.0f);
    for (auto& h : hover) dirty |= h.tick (dt, 22.0f);
    if (dirty) repaint();
}

void TitleBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::titleBar);

    // MENU
    const auto menu = menuArea();
    if (! kMacStyle)
    {
        g.setColour (theme::titleBlock.interpolatedWith (juce::Colour (0xff262626), hover[0].current));
        g.fillRect (menu);
    }
    else if (hover[0].current > 0.01f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f * hover[0].current));
        g.fillRoundedRectangle (menu.reduced (2, 5), 4.0f);
    }

    const float cy = menu.getCentreY();
    const float lx = menu.getX() + 12.0f;
    g.setColour (juce::Colour (0xff9a9a9a).interpolatedWith (juce::Colours::white, hover[0].current * 0.5f));
    g.drawEllipse (juce::Rectangle<float> (13, 13).withCentre ({ lx + 6.5f, cy }), 1.3f);
    g.drawEllipse (juce::Rectangle<float> (13, 13).withCentre ({ lx + 14.5f, cy }), 1.3f);
    g.drawEllipse (juce::Rectangle<float> (6, 6).withCentre ({ lx + 10.5f, cy }), 1.3f);
    g.setColour (juce::Colour (0xffe6e6e6));
    g.setFont (theme::font (11.0f, true));
    g.drawText ("MENU", menu.withTrimmedLeft (36), juce::Justification::centredLeft, false);

    // centre: name, or the customize banner
    const float c = customizing.current;
    if (c < 0.99f)
    {
        g.setColour (theme::textDim.withAlpha (1.0f - c));
        g.setFont (theme::font (11.5f));
        g.drawText ("TRANSPORT", getLocalBounds(), juce::Justification::centred, false);
    }
    if (c > 0.01f)
    {
        const auto done = doneArea();
        g.setColour (theme::accent.withAlpha (c));
        g.setFont (theme::font (11.0f, true));
        g.drawText ("CUSTOMIZE LAYOUT", juce::Rectangle<float> (done.getX() - 190.0f, 0, 176.0f, (float) getHeight()), juce::Justification::centredRight, false);
        g.setColour (theme::accent.interpolatedWith (juce::Colours::white, hover[4].current * 0.25f).withAlpha (c));
        g.fillRoundedRectangle (done, 11.0f);
        g.setColour (theme::titleBlock.withAlpha (c));
        g.drawText ("Done", done, juce::Justification::centred, false);
    }

    // window controls
    if (kMacStyle)
    {
        const juce::Colour lights[] = { juce::Colour (0xffff5f57), juce::Colour (0xfffebc2e), juce::Colour (0xff28c840) };
        const int order[] = { 2, 0, 1 };   // close, minimise, zoom (left to right)
        const float anyHover = juce::jmax (hover[1].current, hover[2].current, hover[3].current);
        for (int k = 0; k < 3; ++k)
        {
            const int i = order[k];
            const auto r = juce::Rectangle<float> (12, 12).withCentre ({ 20.0f + 20.0f * (float) k, (float) getHeight() * 0.5f });
            g.setColour (lights[k]);
            g.fillEllipse (r);
            if (anyHover > 0.01f)
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f * anyHover));
                const auto cc = r.getCentre();
                if (i == 2) { g.drawLine (cc.x - 3, cc.y - 3, cc.x + 3, cc.y + 3, 1.2f); g.drawLine (cc.x + 3, cc.y - 3, cc.x - 3, cc.y + 3, 1.2f); }
                else if (i == 0) g.drawLine (cc.x - 3.5f, cc.y, cc.x + 3.5f, cc.y, 1.2f);
                else { g.drawLine (cc.x - 3, cc.y, cc.x + 3, cc.y, 1.2f); g.drawLine (cc.x, cc.y - 3, cc.x, cc.y + 3, 1.2f); }
            }
        }
        return;
    }

    // Windows 11 caption buttons: 46x32, 10px glyphs, 1px strokes
    for (int i = 0; i < 3; ++i)
    {
        const auto r = buttonArea (i);
        const float hv = hover[i + 1].current;
        if (hv > 0.01f)
        {
            g.setColour (i == 2 ? juce::Colour (0xffc42b1c).withAlpha (hv) : juce::Colours::white.withAlpha (0.08f * hv));
            g.fillRect (r);
        }
        g.setColour (juce::Colour (0xffd6d6d6).interpolatedWith (juce::Colours::white, hv));
        const float cx = std::floor (r.getCentreX()) + 0.5f, cyy = std::floor (r.getCentreY()) + 0.5f;
        if (i == 0)
        {
            g.drawLine (cx - 5.0f, cyy, cx + 5.0f, cyy, 1.0f);
        }
        else if (i == 1)
        {
            if (maximised)
            {
                g.drawRoundedRectangle (juce::Rectangle<float> (cx - 5.0f, cyy - 3.0f, 8.0f, 8.0f), 1.0f, 1.0f);
                juce::Path back;
                back.startNewSubPath (cx - 3.0f, cyy - 5.0f);
                back.lineTo (cx + 5.0f, cyy - 5.0f);
                back.lineTo (cx + 5.0f, cyy + 3.0f);
                g.strokePath (back, juce::PathStrokeType (1.0f));
            }
            else
            {
                g.drawRoundedRectangle (juce::Rectangle<float> (cx - 5.0f, cyy - 5.0f, 10.0f, 10.0f), 1.0f, 1.0f);
            }
        }
        else
        {
            g.drawLine (cx - 5.0f, cyy - 5.0f, cx + 5.0f, cyy + 5.0f, 1.0f);
            g.drawLine (cx + 5.0f, cyy - 5.0f, cx - 5.0f, cyy + 5.0f, 1.0f);
        }
    }
}

void TitleBar::updateHover (Hit h)
{
    hover[0].target = h == Hit::menu ? 1.0f : 0.0f;
    hover[1].target = h == Hit::minimise ? 1.0f : 0.0f;
    hover[2].target = h == Hit::maximise ? 1.0f : 0.0f;
    hover[3].target = h == Hit::close ? 1.0f : 0.0f;
    hover[4].target = h == Hit::done ? 1.0f : 0.0f;
    if (kMacStyle && (h == Hit::minimise || h == Hit::maximise || h == Hit::close))
        hover[1].target = hover[2].target = hover[3].target = 1.0f;   // macOS shows all glyphs together
}

void TitleBar::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.position);
    updateHover (h);
    setMouseCursor (h == Hit::caption ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
}

void TitleBar::mouseExit (const juce::MouseEvent&) { updateHover (Hit::none); }

void TitleBar::mouseDown (const juce::MouseEvent& e)
{
    pressed = hitAt (e.position);
    if (pressed == Hit::menu)
    {
        if (onMenu) onMenu();
        pressed = Hit::none;
    }
    else if (pressed == Hit::caption)
    {
        if (auto* top = getTopLevelComponent())
            dragger.startDraggingComponent (top, e.getEventRelativeTo (top));
    }
}

void TitleBar::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed == Hit::caption)
        if (auto* top = getTopLevelComponent())
            dragger.dragComponent (top, e.getEventRelativeTo (top), nullptr);
}

void TitleBar::mouseUp (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.position);
    if (h == pressed)
    {
        if (h == Hit::minimise && onMinimise) onMinimise();
        if (h == Hit::maximise && onMaximise) onMaximise();
        if (h == Hit::close && onClose) onClose();
        if (h == Hit::done && onDone) onDone();
    }
    pressed = Hit::none;
}

void TitleBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (hitAt (e.position) == Hit::caption && onMaximise)
        onMaximise();
}
} // namespace tp
