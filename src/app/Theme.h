#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>

namespace stemab::theme
{

//==============================================================================
// Palette (the approved "VST plugin" mockup, design/project/Main.dc.html)
inline constexpr juce::uint32 groundArgb     = 0xff0B0D10;   // window ground, wells, meter backgrounds
inline constexpr juce::uint32 panelArgb      = 0xff13161B;   // header bar, track headers, meter bridge
inline constexpr juce::uint32 trackBgArgb    = 0xff0E1115;   // behind the track area
inline constexpr juce::uint32 laneArgb       = 0xff111419;   // waveform lanes
inline constexpr juce::uint32 groupRowArgb   = 0xff10141A;   // the "B - Stem Sum" group row
inline constexpr juce::uint32 residualLaneArgb = 0xff12131A;
inline constexpr juce::uint32 raisedArgb     = 0xff1A1E24;   // buttons and other raised controls
inline constexpr juce::uint32 hairlineArgb   = 0xff232830;
inline constexpr juce::uint32 hairline2Argb  = 0xff2C323B;   // button borders, ruler ticks
inline constexpr juce::uint32 hoverBorderArgb = 0xff3C4450;
inline constexpr juce::uint32 capBorderArgb  = 0xff5A6270;
inline constexpr juce::uint32 textArgb       = 0xffE6E8EB;
inline constexpr juce::uint32 buttonTextArgb = 0xffC9CED6;
inline constexpr juce::uint32 mutedTextArgb  = 0xff8B93A1;
inline constexpr juce::uint32 toggleOnArgb   = 0xff2A3340;   // neutral "on" (Equal-power)

inline constexpr juce::uint32 colourAArgb    = 0xffECE6D6;   // A = ivory
inline constexpr juce::uint32 colourBArgb    = 0xff3BB8F2;   // B = blue accent
inline constexpr juce::uint32 residualArgb   = 0xffF2708C;   // R = pink
inline constexpr juce::uint32 amberArgb      = 0xffF2C14E;   // master / gain / mute / playing
inline constexpr juce::uint32 meterLowArgb   = 0xff2F8F63;
inline constexpr juce::uint32 meterMidArgb   = 0xff4CC38A;
inline constexpr juce::uint32 meterHighArgb  = 0xffF2C14E;
inline constexpr juce::uint32 meterClipArgb  = 0xffFF5A5A;
inline constexpr juce::uint32 clipOnArgb     = 0xffFF5A5A;
inline constexpr juce::uint32 clipOffArgb    = 0xff3A1E22;
inline constexpr juce::uint32 goodArgb       = 0xff4CC38A;   // status dot: aligned

/** Drums, Bass, Other, Vocals. */
inline constexpr std::array<juce::uint32, 4> stemArgb { 0xffFF7A45, 0xffA08BFF, 0xff4CC38A, 0xff3BB8F2 };

// Older names, kept for code that still refers to them.
inline constexpr juce::uint32 windowArgb  = groundArgb;
inline constexpr juce::uint32 accentArgb  = colourBArgb;
inline constexpr juce::uint32 dimTextArgb = mutedTextArgb;
inline constexpr juce::uint32 outlineArgb = hairline2Argb;

inline juce::Colour colour (juce::uint32 argb) noexcept     { return juce::Colour (argb); }

//==============================================================================
// Metrics: the mockup is specified at 100 % with a 14 px base font. Every metric and font size is multiplied
// by one interface scale (MainComponent::uiScale: 0.9 / 1.0 / 1.15 / 1.3).
inline constexpr float baseFontPx = 14.0f;

inline int scaled (float px, float uiScale) noexcept         { return juce::roundToInt (px * uiScale); }
inline float em (float ems, float uiScale) noexcept          { return baseFontPx * ems * uiScale; }

//==============================================================================
// Fonts
inline juce::String pickUiFontName()
{
    // The variable font's installed family names are "Segoe UI Variable Display/Text/Small".
    const auto installed = juce::Font::findAllTypefaceNames();

    for (const char* candidate : { "Segoe UI Variable Text", "Segoe UI Variable", "Segoe UI Variable Display" })
        if (installed.contains (candidate))
            return candidate;

    return "Segoe UI";
}

inline juce::String pickMonoFontName()
{
    const auto installed = juce::Font::findAllTypefaceNames();

    for (const char* candidate : { "Cascadia Mono", "Cascadia Code", "Consolas" })
        if (installed.contains (candidate))
            return candidate;

    return juce::Font::getDefaultMonospacedFontName();
}

inline const juce::String& uiFontName()
{
    static const juce::String name = pickUiFontName();
    return name;
}

inline const juce::String& monoFontName()
{
    static const juce::String name = pickMonoFontName();
    return name;
}

/** Text font, `px` = CSS-like em size in component pixels. */
inline juce::Font uiFont (float px, bool bold = false)
{
    return juce::Font (juce::FontOptions (uiFontName(), 12.0f, bold ? juce::Font::bold : juce::Font::plain)
                           .withPointHeight (juce::jmax (1.0f, px)));
}

/** Numbers and times. */
inline juce::Font monoFont (float px, bool bold = false)
{
    return juce::Font (juce::FontOptions (monoFontName(), 12.0f, bold ? juce::Font::bold : juce::Font::plain)
                           .withPointHeight (juce::jmax (1.0f, px)));
}

/** Text with letter spacing (the mockup's small-caps section labels use 0.1 - 0.12 em tracking). */
inline void drawTrackedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                             const juce::Font& font, float trackingEms, juce::Justification just)
{
    g.setFont (font.withExtraKerningFactor (trackingEms));
    g.drawText (text, area, just, true);
}

//==============================================================================
// Levels: meters map -60..0 dBFS linearly; above 0 dBFS is clipping.
inline constexpr float meterFloorDb = -60.0f;

inline float gainToDb (float linear) noexcept
{
    return linear > 0.0f ? 20.0f * std::log10 (linear) : -200.0f;
}

/** 0 at the floor (-60 dBFS), 1 at 0 dBFS. */
inline float meterProportion (float linear) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (gainToDb (linear) - meterFloorDb) / -meterFloorDb);
}

/** The meter gradient (green -> amber -> red) running from `floorPoint` (-60 dB) to `topPoint` (0 dB). */
inline juce::ColourGradient meterGradient (juce::Point<float> floorPoint, juce::Point<float> topPoint)
{
    juce::ColourGradient grad (colour (meterLowArgb), floorPoint, colour (meterClipArgb), topPoint, false);
    grad.addColour (0.66, colour (meterMidArgb));
    grad.addColour (0.84, colour (meterHighArgb));
    return grad;
}

/** "-12.3" style dB text with a real minus sign; "-inf" below the floor. */
inline juce::String dbText (float linear, int decimals = 1)
{
    const juce::String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    if (linear <= 0.0f || gainToDb (linear) < -99.0f)
        return minus + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));   // minus infinity

    const float db = gainToDb (linear);
    const auto s = juce::String (std::abs (db), decimals);
    return db < 0.0f && s.getDoubleValue() != 0.0 ? minus + s : s;
}

//==============================================================================
// Drawing helpers

/** The plugin-style fader cap: dark vertical gradient with a bright centre stripe, 1 px border, drop shadow. */
inline void drawFaderCap (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour stripe, float corner)
{
    juce::Path shape;
    shape.addRoundedRectangle (r, corner);
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), juce::roundToInt (r.getHeight() * 0.2f),
                      { 0, juce::roundToInt (r.getHeight() * 0.08f) }).drawForPath (g, shape);

    juce::ColourGradient grad (juce::Colour (0xff4A515C), r.getX(), r.getY(),
                               juce::Colour (0xff1C2026), r.getX(), r.getBottom(), false);
    grad.addColour (0.48, juce::Colour (0xff262B33));
    g.setGradientFill (grad);
    g.fillPath (shape);

    const float stripeH = juce::jmax (1.0f, r.getHeight() * 0.03f);
    g.setColour (stripe);
    g.fillRect (r.getX() + 1.0f, r.getCentreY() - stripeH * 0.5f, r.getWidth() - 2.0f, stripeH);

    g.setColour (colour (capBorderArgb));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

//==============================================================================
/** Dark LookAndFeel_V4 in the mockup palette, Segoe UI Variable text.

    Per-component styling through properties (so the widgets stay plain JUCE ones):
      - TextButton "fontRatio" (double): font size as a fraction of the button height (default 0.37);
        "bold" (bool).
      - Slider "style": "crossfade" (gradient track, tall cap) or "fader" (dark track, amber cap);
        "thumbW" / "thumbH" (int): the cap size, already scaled. */
class StemLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StemLookAndFeel()
    {
        using Scheme = juce::LookAndFeel_V4::ColourScheme;

        auto scheme = getDarkColourScheme();
        scheme.setUIColour (Scheme::windowBackground, colour (groundArgb));
        scheme.setUIColour (Scheme::widgetBackground, colour (raisedArgb));
        scheme.setUIColour (Scheme::menuBackground,   colour (panelArgb));
        scheme.setUIColour (Scheme::outline,          colour (hairline2Argb));
        scheme.setUIColour (Scheme::defaultText,      colour (textArgb));
        scheme.setUIColour (Scheme::defaultFill,      colour (colourBArgb));
        scheme.setUIColour (Scheme::highlightedText,  colour (groundArgb));   // dark text on accent
        scheme.setUIColour (Scheme::highlightedFill,  colour (colourBArgb));
        scheme.setUIColour (Scheme::menuText,         colour (textArgb));
        setColourScheme (scheme);

        setColour (juce::ResizableWindow::backgroundColourId, colour (groundArgb));
        setColour (juce::Label::textColourId,                 colour (textArgb));
        setColour (juce::ScrollBar::thumbColourId,            colour (hairline2Argb).brighter (0.3f));
        setColour (juce::TextButton::buttonColourId,          colour (raisedArgb));
        setColour (juce::TextButton::buttonOnColourId,        colour (colourBArgb));
        setColour (juce::TextButton::textColourOffId,         colour (buttonTextArgb));
        setColour (juce::TextButton::textColourOnId,          colour (groundArgb));
        setColour (juce::TooltipWindow::backgroundColourId,   colour (panelArgb));
        setColour (juce::TooltipWindow::textColourId,         colour (textArgb));
        setColour (juce::TooltipWindow::outlineColourId,      colour (hairline2Argb));

        setDefaultSansSerifTypefaceName (uiFontName());
    }

    static juce::String pickUiFontName()    { return theme::pickUiFontName(); }

    juce::Font getTextButtonFont (juce::TextButton& b, int buttonHeight) override
    {
        const auto ratio = (float) (double) b.getProperties().getWithDefault ("fontRatio", 0.37);
        const bool bold = (bool) b.getProperties().getWithDefault ("bold", false);
        return uiFont ((float) buttonHeight * ratio, bold);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float corner = juce::jmin (6.0f, r.getHeight() * 0.2f);
        const bool on = b.getToggleState();

        auto fill = on ? b.findColour (juce::TextButton::buttonOnColourId) : background;
        if (down)
            fill = fill.brighter (0.08f);

        g.setColour (fill.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.fillRoundedRectangle (r, corner);

        auto border = on ? fill : colour (highlighted ? hoverBorderArgb : hairline2Argb);
        g.setColour (border.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        auto c = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        if (highlighted && ! b.getToggleState())
            c = c.brighter (0.4f);
        g.setColour (c.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 1.0f);
    }

    int getSliderThumbRadius (juce::Slider& s) override
    {
        if (s.getProperties().contains ("thumbW"))
            return juce::jmax (1, (int) s.getProperties()["thumbW"] / 2);
        return juce::LookAndFeel_V4::getSliderThumbRadius (s);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        const auto kind = s.getProperties()["style"].toString();
        if (kind != "crossfade" && kind != "fader")
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, s);
            return;
        }

        const bool crossfade = kind == "crossfade";
        const float thumbW = (float) (int) s.getProperties().getWithDefault ("thumbW", 16);
        const float thumbH = (float) (int) s.getProperties().getWithDefault ("thumbH", 28);
        const float cy = (float) y + (float) height * 0.5f;
        const float trackH = juce::jmax (3.0f, thumbH * 0.21f);   // 8 px under a 38 px cap, 6 px under 28 px

        // Track spans the full slider bounds plus the thumb indent on each side.
        auto track = juce::Rectangle<float> ((float) x - thumbW * 0.5f, cy - trackH * 0.5f,
                                             (float) width + thumbW, trackH);

        if (crossfade)
        {
            juce::ColourGradient grad (colour (colourAArgb), track.getX(), cy, colour (colourBArgb), track.getRight(), cy, false);
            grad.addColour (0.5, juce::Colour (0xff3A3F48));
            g.setGradientFill (grad);
        }
        else
        {
            g.setColour (colour (groundArgb));
        }
        g.fillRoundedRectangle (track, trackH * 0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (track, trackH * 0.5f, 1.0f);

        if (crossfade)
        {
            // Centre (A 50 / B 50) tick
            g.setColour (colour (mutedTextArgb).withAlpha (0.6f));
            g.fillRect (juce::Rectangle<float> (track.getCentreX() - 0.5f, track.getBottom() + 3.0f, 1.0f, trackH));
        }

        const auto cap = juce::Rectangle<float> (thumbW, thumbH).withCentre ({ sliderPos, cy });
        drawFaderCap (g, cap, crossfade ? colour (textArgb) : colour (amberArgb), crossfade ? 5.0f : 4.0f);
    }
};

} // namespace stemab::theme
