#include "app/ui/Widgets.h"

#include "app/Theme.h"

namespace stemab
{

namespace
{
    // SVG paths from the mockup, in a 24 x 24 box.
    const juce::Path& iconPath (IconButton::Icon icon, bool playing)
    {
        static const juce::Path toStart = juce::Drawable::parseSVGPath ("M6 5v14M18 6l-9 6 9 6z");
        static const juce::Path back    = juce::Drawable::parseSVGPath ("M11 6l-7 6 7 6zM20 6l-7 6 7 6z");
        static const juce::Path forward = juce::Drawable::parseSVGPath ("M4 6l7 6-7 6zM13 6l7 6-7 6z");
        static const juce::Path loop    = juce::Drawable::parseSVGPath (
            "M17 2l3 3-3 3M4 11V9a4 4 0 0 1 4-4h12M7 22l-3-3 3-3M20 13v2a4 4 0 0 1-4 4H4");
        static const juce::Path play    = juce::Drawable::parseSVGPath ("M8 5l11 7-11 7z");
        static const juce::Path pause   = []
        {
            juce::Path p;
            p.addRoundedRectangle (6.0f, 5.0f, 4.0f, 14.0f, 1.0f);
            p.addRoundedRectangle (14.0f, 5.0f, 4.0f, 14.0f, 1.0f);
            return p;
        }();

        switch (icon)
        {
            case IconButton::Icon::toStart:   return toStart;
            case IconButton::Icon::back:      return back;
            case IconButton::Icon::forward:   return forward;
            case IconButton::Icon::loop:      return loop;
            case IconButton::Icon::playPause: break;
        }
        return playing ? pause : play;
    }

    juce::AffineTransform iconTransform (juce::Rectangle<float> box)
    {
        return juce::AffineTransform::scale (box.getWidth() / 24.0f, box.getHeight() / 24.0f)
                   .translated (box.getX(), box.getY());
    }
}

//==============================================================================
IconButton::IconButton (const juce::String& name, Icon i) : juce::Button (name), icon (i)
{
    setTooltip (name);
}

void IconButton::setPlaying (bool p)
{
    if (p != playing)
    {
        playing = p;
        repaint();
    }
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using theme::colour;
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    if (icon == Icon::playPause)
    {
        const auto fill = colour (playing ? theme::amberArgb : theme::colourAArgb);
        const auto circle = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
        g.setColour (down ? fill.darker (0.15f) : highlighted ? fill.brighter (0.1f) : fill);
        g.fillEllipse (circle);

        const float iconSize = 22.0f * uiScale;
        g.setColour (colour (theme::groundArgb));
        g.fillPath (iconPath (icon, playing), iconTransform (circle.withSizeKeepingCentre (iconSize, iconSize)));
        return;
    }

    const bool on = icon == Icon::loop && getToggleState();
    const float corner = juce::jmin (6.0f, r.getHeight() * 0.2f);
    g.setColour (colour (theme::raisedArgb).brighter (down ? 0.08f : 0.0f));
    g.fillRoundedRectangle (r, corner);
    g.setColour (on ? colour (theme::colourBArgb) : colour (highlighted ? theme::hoverBorderArgb : theme::hairline2Argb));
    g.drawRoundedRectangle (r, corner, 1.0f);

    const float iconSize = 18.0f * uiScale;
    const auto fg = on ? colour (theme::colourBArgb) : colour (highlighted ? theme::textArgb : theme::buttonTextArgb);
    g.setColour (fg);
    g.strokePath (iconPath (icon, false),
                  juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  iconTransform (r.withSizeKeepingCentre (iconSize, iconSize)));
}

//==============================================================================
ClipButton::ClipButton() : juce::Button ("Clip")
{
    setTooltip ("Clip indicator (output above 0 dBFS) - click to reset. Lower the Master Gain to fix it.");
}

void ClipButton::setLit (bool l)
{
    if (l != lit)
    {
        lit = l;
        repaint();
    }
}

void ClipButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using theme::colour;
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float corner = juce::jmin (6.0f, r.getHeight() * 0.12f);

    g.setColour (colour (theme::raisedArgb).brighter (down ? 0.08f : 0.0f));
    g.fillRoundedRectangle (r, corner);
    g.setColour (colour (highlighted ? theme::hoverBorderArgb : theme::hairline2Argb));
    g.drawRoundedRectangle (r, corner, 1.0f);

    const auto font = theme::uiFont (theme::em (0.62f, uiScale));
    const float led = 12.0f * uiScale, gap = 4.0f * uiScale;
    const float blockH = led + gap + font.getHeight();
    auto block = r.withSizeKeepingCentre (r.getWidth(), blockH);
    const auto ledBox = block.removeFromTop (led).withSizeKeepingCentre (led, led);
    block.removeFromTop (gap);

    if (lit)
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (colour (theme::clipOnArgb).withAlpha (0.12f));
            g.fillEllipse (ledBox.expanded ((float) i * 2.5f * uiScale));
        }
    }
    g.setColour (colour (lit ? theme::clipOnArgb : theme::clipOffArgb));
    g.fillEllipse (ledBox);

    g.setColour (colour (lit ? theme::textArgb : theme::buttonTextArgb));
    theme::drawTrackedText (g, "CLIP", block, font, 0.1f, juce::Justification::centredTop);
}

} // namespace stemab
