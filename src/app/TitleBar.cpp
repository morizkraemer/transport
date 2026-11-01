#include "TitleBar.h"

namespace tp
{
TitleBar::TitleBar() = default;

TitleBar::Hit TitleBar::hitAt (juce::Point<float> p) const
{
    if (menuArea().contains (p)) return Hit::menu;
    for (int i = 0; i < 3; ++i)
        if (buttonArea (i).contains (p))
            return i == 0 ? Hit::minimise : (i == 1 ? Hit::maximise : Hit::close);
    if (controlsArea().contains (p)) return Hit::none;
    return Hit::caption;
}

void TitleBar::animate (float dt)
{
    bool dirty = false;
    for (auto& h : hover) dirty |= h.tick (dt, 18.0f);
    if (dirty) repaint();
}

void TitleBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::titleBar);

    // MENU block
    const auto menu = menuArea();
    g.setColour (theme::titleBlock.interpolatedWith (juce::Colour (0xff2a2a2a), hover[0].current));
    g.fillRect (menu);

    const float cy = menu.getCentreY();
    g.setColour (juce::Colour (0xffa0a0a0).interpolatedWith (juce::Colours::white, hover[0].current * 0.5f));
    g.drawEllipse (juce::Rectangle<float> (18, 18).withCentre ({ 23.0f, cy }), 1.6f);
    g.drawEllipse (juce::Rectangle<float> (18, 18).withCentre ({ 34.0f, cy }), 1.6f);
    g.drawEllipse (juce::Rectangle<float> (8, 8).withCentre ({ 28.5f, cy }), 1.6f);

    g.setColour (theme::text);
    g.setFont (theme::font (14.0f, true));
    g.drawText ("MENU", menu.withTrimmedLeft (62), juce::Justification::centredLeft, false);

    // name
    g.setColour (theme::textDim);
    g.setFont (theme::font (14.0f));
    g.drawText ("TRANSPORT", getLocalBounds(), juce::Justification::centred, false);

    // window controls
    g.setColour (theme::titleBlock);
    g.fillRect (controlsArea());

    for (int i = 0; i < 3; ++i)
    {
        const auto r = buttonArea (i);
        const float hv = hover[i + 1].current;
        if (hv > 0.01f)
        {
            g.setColour ((i == 2 ? juce::Colour (0xffc42b1c) : juce::Colour (0xff333333)).withAlpha (hv));
            g.fillRect (r);
        }
        const auto c = r.getCentre();
        const auto col = theme::accent.interpolatedWith (juce::Colours::white, i == 2 ? hv : hv * 0.3f);
        g.setColour (col);

        if (i == 0)
        {
            g.fillRect (juce::Rectangle<float> (12, 2).withCentre (c.translated (0, 4)));
        }
        else if (i == 1)
        {
            const auto box = juce::Rectangle<float> (13, 11).withCentre (c);
            g.drawRect (box, 1.6f);
            g.fillRect (box.withHeight (2.5f));
        }
        else
        {
            g.drawLine (c.x - 5, c.y - 5, c.x + 5, c.y + 5, 1.8f);
            g.drawLine (c.x + 5, c.y - 5, c.x - 5, c.y + 5, 1.8f);
        }
    }
}

void TitleBar::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.position);
    hover[0].target = h == Hit::menu ? 1.0f : 0.0f;
    hover[1].target = h == Hit::minimise ? 1.0f : 0.0f;
    hover[2].target = h == Hit::maximise ? 1.0f : 0.0f;
    hover[3].target = h == Hit::close ? 1.0f : 0.0f;
    setMouseCursor (h == Hit::caption ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
}

void TitleBar::mouseExit (const juce::MouseEvent&)
{
    for (auto& h : hover) h.target = 0.0f;
}

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
    }
    pressed = Hit::none;
}

void TitleBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (hitAt (e.position) == Hit::caption && onMaximise)
        onMaximise();
}
} // namespace tp
