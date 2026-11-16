#include "app/ui/Widgets.h"

namespace stemab
{

using theme::C;
using theme::colour;

namespace
{
    // Icon paths in a 24 x 24 box.
    const juce::Path& iconPath (IconButton::Icon icon, bool playing)
    {
        static const juce::Path toStart = juce::Drawable::parseSVGPath ("M6 5v14M18 6l-9 6 9 6z");
        static const juce::Path back    = juce::Drawable::parseSVGPath ("M11 6l-7 6 7 6zM20 6l-7 6 7 6z");
        static const juce::Path forward = juce::Drawable::parseSVGPath ("M4 6l7 6-7 6zM13 6l7 6-7 6z");
        static const juce::Path loop    = juce::Drawable::parseSVGPath (
            "M17 2l3 3-3 3M4 11V9a4 4 0 0 1 4-4h12M7 22l-3-3 3-3M20 13v2a4 4 0 0 1-4 4H4");
        static const juce::Path play    = juce::Drawable::parseSVGPath ("M8 5l11 7-11 7z");
        static const juce::Path eject   = juce::Drawable::parseSVGPath ("M12 5l7 8H5zM5 18h14");
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
            case IconButton::Icon::eject:     return eject;
            case IconButton::Icon::playPause: break;
        }
        return playing ? pause : play;
    }

    juce::AffineTransform iconTransform (juce::Rectangle<float> box)
    {
        return juce::AffineTransform::scale (box.getWidth() / 24.0f, box.getHeight() / 24.0f)
                   .translated (box.getX(), box.getY());
    }

    /** The face inside a button's bounds: a margin all round, or (no margin) 2 px kept for the bottom shadow. */
    juce::Rectangle<int> faceIn (juce::Rectangle<int> bounds, int margin, float s)
    {
        return margin > 0 ? bounds.reduced (margin) : bounds.withTrimmedBottom (theme::scaled (2.0f, s));
    }

    juce::Rectangle<int> boundsFor (juce::Rectangle<int> face, int margin, float s)
    {
        return margin > 0 ? face.expanded (margin) : face.withHeight (face.getHeight() + theme::scaled (2.0f, s));
    }
}

//==============================================================================
HwButton::HwButton (Kind k, const juce::String& text)
    : juce::TextButton (text), kind (k), fontPx (k == Kind::pad ? 11.0f : 12.0f)
{
}

void HwButton::setLitColours (C fill, C ink, bool glow)
{
    litFill = fill;
    litInk = ink;
    litGlow = glow;
    hasLit = true;
    repaint();
}

void HwButton::setUiScale (float s)     { uiScale = s; repaint(); }
void HwButton::setFontPx (float px)     { fontPx = px; repaint(); }

int HwButton::margin() const
{
    return kind == Kind::pad || (hasLit && litGlow) ? theme::scaled (8.0f, uiScale) : 0;
}

void HwButton::setFaceBounds (juce::Rectangle<int> f)   { setBounds (boundsFor (f, margin(), uiScale)); }
juce::Rectangle<int> HwButton::getFaceBounds() const    { return faceIn (getBounds(), margin(), uiScale); }

bool HwButton::hitTest (int x, int y)
{
    return faceIn (getLocalBounds(), margin(), uiScale).contains (x, y);
}

void HwButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const float s = uiScale;
    const auto face = faceIn (getLocalBounds(), margin(), s).toFloat();
    const bool lit = hasLit && getToggleState();

    if (lit)
        theme::drawPad (g, face, s, true, colour (litFill), highlighted, down, isEnabled(), litGlow ? 10.0f * s : 0.0f,
                        kind == Kind::key ? 4.0f : 5.0f);
    else if (kind == Kind::pad)
        theme::drawPad (g, face, s, false, {}, highlighted, down, isEnabled(), 0.0f);
    else
        theme::drawKey (g, face, s, highlighted, down, isEnabled());

    auto label = face;
    if (down)
        label = label.translated (0.0f, juce::jmax (1.0f, s));

    const auto ink = lit ? colour (litInk) : kind == Kind::pad ? colour (C::muted) : colour (C::text);
    g.setColour (ink.withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f));
    g.setFont (theme::uiFont (fontPx * s, kind == Kind::pad ? theme::Weight::extraBold : theme::Weight::semiBold));
    g.drawFittedText (getButtonText(), label.reduced (4.0f * s, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.9f);
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

int IconButton::margin() const                          { return glows() ? theme::scaled (8.0f, uiScale) : 0; }
juce::Rectangle<int> IconButton::face() const           { return faceIn (getLocalBounds(), margin(), uiScale); }
void IconButton::setFaceBounds (juce::Rectangle<int> f) { setBounds (boundsFor (f, margin(), uiScale)); }
bool IconButton::hitTest (int x, int y)                 { return face().contains (x, y); }

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const float s = uiScale;
    const auto r = face().toFloat();
    const bool lit = icon == Icon::playPause || (icon == Icon::loop && getToggleState());
    const auto litColour = colour (icon == Icon::playPause ? C::go : C::colourB);

    if (lit)
        theme::drawPad (g, r, s, true, litColour, highlighted, down, isEnabled(), (icon == Icon::playPause ? 14.0f : 10.0f) * s, 4.0f);
    else
        theme::drawKey (g, r, s, highlighted, down, isEnabled());

    auto body = r;
    if (down)
        body = body.translated (0.0f, juce::jmax (1.0f, s));

    const float iconSize = juce::jmin (body.getHeight() * 0.62f,
                                       (icon == Icon::playPause ? 20.0f : icon == Icon::eject ? 15.0f : 17.0f) * s);
    const auto box = body.withSizeKeepingCentre (iconSize, iconSize);
    auto fg = lit ? colour (C::inkDark) : colour (C::text);
    fg = fg.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f);
    g.setColour (fg);

    const auto& path = iconPath (icon, playing);
    const auto t = iconTransform (box);
    const juce::PathStrokeType stroke (icon == Icon::loop ? 2.0f : 1.5f,      // in the 24-unit icon box
                                       juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    if (icon != Icon::loop)
        g.fillPath (path, t);
    g.strokePath (path, stroke, t);
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

void ClipButton::setFaceBounds (juce::Rectangle<int> f)     { setBounds (f.expanded (margin())); }
juce::Rectangle<int> ClipButton::getFaceBounds() const      { return getBounds().reduced (margin()); }
bool ClipButton::hitTest (int x, int y)                     { return getLocalBounds().reduced (margin()).contains (x, y); }

void ClipButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const float s = uiScale;
    theme::drawPad (g, getLocalBounds().reduced (margin()).toFloat(), s, lit, colour (C::meterRed), highlighted, down,
                    true, 12.0f * s, 6.0f);
}

} // namespace stemab
