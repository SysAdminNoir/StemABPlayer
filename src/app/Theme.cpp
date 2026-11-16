#include "app/Theme.h"

#include "StemABFonts.h"

namespace stemab::theme
{

//==============================================================================
// Palettes
namespace
{
    using Table = std::array<juce::uint32, (size_t) C::count>;

    constexpr Table makeStandard()
    {
        Table t {};
        const auto set = [&t] (C c, juce::uint32 argb) { t[(size_t) c] = argb; };
        set (C::ground,        0xff222428);
        set (C::panel,         0xff2A2C31);
        set (C::panelHi,       0xff3A3D42);
        set (C::panelEdge,     0xff17181B);
        set (C::residualStrip, 0xff26282C);
        set (C::key,           0xff3A3D42);
        set (C::keyHi,         0xff5A5E64);
        set (C::keyShadow,     0xff151618);
        set (C::padOff,        0xff2F3236);
        set (C::padShadow,     0xff121315);
        set (C::screen,        0xff04080D);
        set (C::screenLine,    0xff16263A);
        set (C::screenText,    0xffCFE3F5);
        set (C::screenDim,     0xff5D7A94);
        set (C::ledOff,        0xff0D1620);
        set (C::slot,          0xff121315);
        set (C::tick,          0xff4A4E54);
        set (C::knobHi,        0xff4A4E54);
        set (C::knobLo,        0xff26282C);
        set (C::knobRim,       0xff6A6E74);
        set (C::capHi,         0xff55595F);
        set (C::capLo,         0xff33363B);
        set (C::capRim,        0xff777B81);
        set (C::text,          0xffE7E8EA);
        set (C::silk,          0xffB9BCC0);
        set (C::silkDim,       0xff7E8288);
        set (C::muted,         0xff8C9096);
        set (C::inkDark,       0xff0B1622);
        set (C::inkPad,        0xff1A1B1E);

        set (C::colourA,       0xffE7E2D2);
        set (C::colourB,       0xff3FC3F0);
        set (C::accent,        0xffE8402F);
        set (C::go,            0xff3FD07A);
        set (C::amber,         0xffF2B53A);
        set (C::meterRed,      0xffFF4A36);
        set (C::nullDepth,     0xff5BE3A0);
        set (C::stemDrums,     0xffFF8A3D);
        set (C::stemBass,      0xffB98CFF);
        set (C::stemOther,     0xff3FD07A);
        set (C::stemVocals,    0xff3FC3F0);
        return t;
    }

    /** Red-green colour-blind safe (HardwareCvd.dc.html): Okabe-Ito style hues, the same panel greys. */
    constexpr Table makeColourBlindSafe()
    {
        Table t = makeStandard();
        const auto set = [&t] (C c, juce::uint32 argb) { t[(size_t) c] = argb; };
        set (C::colourB,       0xff9FD8F5);
        set (C::accent,        0xffD55E00);
        set (C::go,            0xff4BA3E3);
        set (C::amber,         0xffF0E442);
        set (C::meterRed,      0xffD55E00);
        set (C::nullDepth,     0xff4BA3E3);
        set (C::stemDrums,     0xffE69F00);
        set (C::stemBass,      0xffCC79A7);
        set (C::stemOther,     0xffF0E442);
        set (C::stemVocals,    0xff9FD8F5);
        return t;
    }

    constexpr Table standardTable = makeStandard();
    constexpr Table colourBlindTable = makeColourBlindSafe();

    const Table* activeTable = &standardTable;
    PaletteId activeId = PaletteId::standard;
}

void setPalette (PaletteId id)
{
    activeId = id;
    activeTable = id == PaletteId::colourBlindSafe ? &colourBlindTable : &standardTable;
}

PaletteId activePalette() noexcept                  { return activeId; }
juce::Colour colour (C c) noexcept                  { return juce::Colour ((*activeTable)[(size_t) c]); }
juce::Colour stemColour (int index) noexcept        { return colour (stemRole (index)); }

//==============================================================================
// Fonts
namespace
{
    struct Fonts
    {
        std::array<juce::Typeface::Ptr, 4> ui;      // Weight order
        juce::Typeface::Ptr narrowBold, mono, monoSemi;
        bool loaded = false;
    };

    juce::Typeface::Ptr loadBundled (const char* fileName)
    {
        for (int i = 0; i < StemABFonts::namedResourceListSize; ++i)
        {
            const char* resource = StemABFonts::namedResourceList[i];
            if (juce::String (StemABFonts::getNamedResourceOriginalFilename (resource)) != fileName)
                continue;

            int size = 0;
            if (const auto* data = StemABFonts::getNamedResource (resource, size); data != nullptr && size > 0)
                return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        }
        return nullptr;
    }

    Fonts& fontStore()
    {
        static Fonts f;
        return f;
    }

    bool fontsAttempted = false, fontsReleased = false;

    const Fonts& fonts()
    {
        auto& r = fontStore();
        if (! fontsAttempted && ! fontsReleased)
        {
            fontsAttempted = true;
            r.ui = { loadBundled ("Archivo-Regular.ttf"), loadBundled ("Archivo-SemiBold.ttf"),
                     loadBundled ("Archivo-Bold.ttf"), loadBundled ("Archivo-ExtraBold.ttf") };
            r.narrowBold = loadBundled ("ArchivoNarrow-Bold.ttf");
            r.mono = loadBundled ("JetBrainsMono-Regular.ttf");
            r.monoSemi = loadBundled ("JetBrainsMono-SemiBold.ttf");
            r.loaded = r.ui[0] != nullptr && r.ui[1] != nullptr && r.ui[2] != nullptr && r.ui[3] != nullptr
                       && r.narrowBold != nullptr && r.mono != nullptr && r.monoSemi != nullptr;
        }
        return r;
    }

    juce::String fallbackMonoName()
    {
        static const juce::String name = []
        {
            const auto installed = juce::Font::findAllTypefaceNames();
            for (const char* candidate : { "Cascadia Mono", "Cascadia Code", "Consolas" })
                if (installed.contains (candidate))
                    return juce::String (candidate);
            return juce::Font::getDefaultMonospacedFontName();
        }();
        return name;
    }

    juce::Font fromTypeface (const juce::Typeface::Ptr& tf, float px)
    {
        return juce::Font (juce::FontOptions (tf).withPointHeight (juce::jmax (1.0f, px)));
    }
}

bool bundledFontsLoaded()                           { return fonts().loaded; }

void releaseFonts()
{
    fontsReleased = true;
    fontStore() = {};
}
juce::Typeface::Ptr defaultSansTypeface()           { return fonts().loaded ? fonts().ui[0] : nullptr; }

juce::Font uiFont (float px, Weight w)
{
    if (fonts().loaded)
        return fromTypeface (fonts().ui[(size_t) w], px);

    const char* style = w == Weight::regular ? "Regular" : w == Weight::semiBold ? "Semibold" : "Bold";
    return juce::Font (juce::FontOptions ("Segoe UI", style, 12.0f).withPointHeight (juce::jmax (1.0f, px)));
}

juce::Font monoFont (float px, bool semiBold)
{
    if (fonts().loaded)
        return fromTypeface (semiBold ? fonts().monoSemi : fonts().mono, px);

    return juce::Font (juce::FontOptions (fallbackMonoName(), 12.0f, semiBold ? juce::Font::bold : juce::Font::plain)
                           .withPointHeight (juce::jmax (1.0f, px)));
}

juce::Font silkFont (float px)
{
    if (fonts().loaded)
        return fromTypeface (fonts().narrowBold, px);

    return juce::Font (juce::FontOptions ("Segoe UI", "Bold", 12.0f).withPointHeight (juce::jmax (1.0f, px)))
               .withHorizontalScale (0.86f);
}

//==============================================================================
// Drawing helpers
namespace
{
    /** A 1 px band along the top of `shape` (the CSS "inset 0 1px 0" highlight). */
    void insetTopLine (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c, float thickness)
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path p;
        p.addRoundedRectangle (r, radius);
        g.reduceClipRegion (p);
        g.setColour (c);
        g.fillRect (r.withHeight (thickness));
    }
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float s, C fill, float radius, bool topHighlight)
{
    const float rad = radius * s;
    g.setColour (colour (fill));
    if (rad > 0.0f)
        g.fillRoundedRectangle (r, rad);
    else
        g.fillRect (r);

    if (topHighlight)
        insetTopLine (g, r, rad, colour (C::panelHi), juce::jmax (1.0f, std::floor (s)));
}

void drawScreen (juce::Graphics& g, juce::Rectangle<float> r, float s, float radius)
{
    const float rad = radius * s;

    // 0 1px 0 #ffffff12 (a faint lip under the screen)
    g.setColour (juce::Colour (0x12ffffff));
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), rad);

    g.setColour (colour (C::screen));
    g.fillRoundedRectangle (r, rad);

    // inset 0 2px 8px #000c: strong along the top edge, light along the sides.
    juce::Graphics::ScopedSaveState save (g);
    juce::Path p;
    p.addRoundedRectangle (r, rad);
    g.reduceClipRegion (p);

    const float depth = 10.0f * s;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xcc000000), 0.0f, r.getY(),
                                             juce::Colours::transparentBlack, 0.0f, r.getY() + depth, false));
    g.fillRect (r.withHeight (depth));

    const float side = 5.0f * s;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x66000000), r.getX(), 0.0f,
                                             juce::Colours::transparentBlack, r.getX() + side, 0.0f, false));
    g.fillRect (r.withWidth (side));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x66000000), r.getRight(), 0.0f,
                                             juce::Colours::transparentBlack, r.getRight() - side, 0.0f, false));
    g.fillRect (r.withLeft (r.getRight() - side));
}

void drawKey (juce::Graphics& g, juce::Rectangle<float> face, float s, bool highlighted, bool down, bool enabled, float radius)
{
    const float rad = radius * s;
    const float a = enabled ? 1.0f : 0.45f;
    const auto body = down ? face.translated (0.0f, juce::jmax (1.0f, s)) : face;

    // 0 2px 0 #15161899
    g.setColour (colour (C::keyShadow).withAlpha (0.6f * a));
    g.fillRoundedRectangle (face.translated (0.0f, 2.0f * s), rad);

    auto fill = colour (C::key);
    if (down)
        fill = fill.darker (0.15f);
    else if (highlighted && enabled)
        fill = fill.brighter (0.1f);
    g.setColour (fill.withMultipliedAlpha (a));
    g.fillRoundedRectangle (body, rad);

    // inset 0 1px 0 #5A5E64
    insetTopLine (g, body, rad, colour (C::keyHi).withMultipliedAlpha (a), juce::jmax (1.0f, std::floor (s)));
}

void drawPad (juce::Graphics& g, juce::Rectangle<float> face, float s, bool lit, juce::Colour litColour,
              bool highlighted, bool down, bool enabled, float glowPx, float radius)
{
    const float rad = radius * s;
    const float a = enabled ? 1.0f : 0.45f;
    const auto body = down ? face.translated (0.0f, juce::jmax (1.0f, s)) : face;

    // 0 0 10px <colour>88: the glow of a lit pad
    if (lit && glowPx > 0.5f)
    {
        juce::Path p;
        p.addRoundedRectangle (face, rad);
        juce::DropShadow (litColour.withAlpha (0.55f * a), juce::jmax (1, juce::roundToInt (glowPx)), {}).drawForPath (g, p);
    }

    // 0 2px 0 #121315
    g.setColour (colour (C::padShadow).withMultipliedAlpha (a));
    g.fillRoundedRectangle (face.translated (0.0f, 2.0f * s), rad);

    auto fill = lit ? litColour : colour (C::padOff);
    if (down)
        fill = fill.darker (0.15f);
    else if (highlighted && enabled)
        fill = fill.brighter (lit ? 0.12f : 0.1f);
    g.setColour (fill.withMultipliedAlpha (a));
    g.fillRoundedRectangle (body, rad);

    // inset 0 1px 0 #ffffff22
    insetTopLine (g, body, rad, juce::Colours::white.withAlpha (0.13f * a), juce::jmax (1.0f, std::floor (s)));
}

void drawLedCell (juce::Graphics& g, juce::Rectangle<float> cell, bool lit, juce::Colour c, bool alongX)
{
    const bool bevel = cell.getHeight() >= 4.0f;

    if (lit)
    {
        // 0 0 3px <colour>80, cheaply: a soft 1 px halo
        g.setColour (c.withAlpha (0.25f));
        g.fillRoundedRectangle (cell.expanded (1.0f), 1.5f);

        // linear-gradient(90deg, c b0 0%, c 40%, c 60%, c 90 100%)
        juce::ColourGradient grad = alongX
            ? juce::ColourGradient (c.withAlpha (0.69f), 0.0f, cell.getY(), c.withAlpha (0.565f), 0.0f, cell.getBottom(), false)
            : juce::ColourGradient (c.withAlpha (0.69f), cell.getX(), 0.0f, c.withAlpha (0.565f), cell.getRight(), 0.0f, false);
        grad.addColour (0.4, c);
        grad.addColour (0.6, c);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (cell, 1.0f);

        if (bevel)
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f));                          // inset 0 1px 0 #ffffff66
            g.fillRect (cell.withHeight (1.0f).reduced (0.5f, 0.0f));
            g.setColour (juce::Colours::black.withAlpha (0.35f));                         // inset 0 -1px 0 #00000059
            g.fillRect (cell.withTop (cell.getBottom() - 1.0f).reduced (0.5f, 0.0f));
        }
    }
    else
    {
        g.setColour (colour (C::ledOff));
        g.fillRoundedRectangle (cell, 1.0f);

        if (bevel)
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f));                         // inset 0 1px 0 #ffffff0a
            g.fillRect (cell.withHeight (1.0f).reduced (0.5f, 0.0f));
            g.setColour (juce::Colours::black.withAlpha (0.5f));                          // inset 0 -1px 0 #00000080
            g.fillRect (cell.withTop (cell.getBottom() - 1.0f).reduced (0.5f, 0.0f));
        }
    }
}

//==============================================================================
StemLookAndFeel::StemLookAndFeel()
{
    applyPalette();

    if (auto tf = defaultSansTypeface())
        setDefaultSansSerifTypeface (tf);
    else
        setDefaultSansSerifTypefaceName ("Segoe UI");
}

void StemLookAndFeel::applyPalette()
{
    using Scheme = juce::LookAndFeel_V4::ColourScheme;

    auto scheme = getDarkColourScheme();
    scheme.setUIColour (Scheme::windowBackground, colour (C::ground));
    scheme.setUIColour (Scheme::widgetBackground, colour (C::key));
    scheme.setUIColour (Scheme::menuBackground,   colour (C::panel));
    scheme.setUIColour (Scheme::outline,          colour (C::panelEdge));
    scheme.setUIColour (Scheme::defaultText,      colour (C::text));
    scheme.setUIColour (Scheme::defaultFill,      colour (C::colourB));
    scheme.setUIColour (Scheme::highlightedText,  colour (C::inkDark));
    scheme.setUIColour (Scheme::highlightedFill,  colour (C::colourB));
    scheme.setUIColour (Scheme::menuText,         colour (C::text));
    setColourScheme (scheme);

    setColour (juce::ResizableWindow::backgroundColourId, colour (C::ground));
    setColour (juce::DocumentWindow::backgroundColourId,  colour (C::ground));
    setColour (juce::Label::textColourId,                 colour (C::text));
    setColour (juce::ScrollBar::thumbColourId,            colour (C::tick));
    setColour (juce::TextButton::buttonColourId,          colour (C::key));
    setColour (juce::TextButton::buttonOnColourId,        colour (C::colourB));
    setColour (juce::TextButton::textColourOffId,         colour (C::text));
    setColour (juce::TextButton::textColourOnId,          colour (C::inkDark));
    setColour (juce::ToggleButton::textColourId,          colour (C::text));
    setColour (juce::ComboBox::backgroundColourId,        colour (C::key));
    setColour (juce::ComboBox::textColourId,              colour (C::text));
    setColour (juce::ComboBox::outlineColourId,           juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId,             colour (C::silk));
    setColour (juce::ComboBox::buttonColourId,            colour (C::key));
    setColour (juce::PopupMenu::backgroundColourId,       colour (C::panel));
    setColour (juce::PopupMenu::textColourId,             colour (C::text));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour (C::colourB));
    setColour (juce::PopupMenu::highlightedTextColourId,  colour (C::inkDark));
    setColour (juce::ListBox::backgroundColourId,         colour (C::screen));
    setColour (juce::ListBox::outlineColourId,            colour (C::screenLine));
    setColour (juce::ListBox::textColourId,               colour (C::screenText));
    setColour (juce::TooltipWindow::backgroundColourId,   colour (C::text));
    setColour (juce::TooltipWindow::textColourId,         colour (C::inkPad));
    setColour (juce::TooltipWindow::outlineColourId,      colour (C::panelEdge));
}

juce::Font StemLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return uiFont (juce::jmin (12.0f * tooltipScale, (float) buttonHeight * 0.45f), Weight::semiBold);
}

void StemLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                            bool highlighted, bool down)
{
    const float s = tooltipScale;
    const auto face = b.getLocalBounds().toFloat().withTrimmedBottom (2.0f * s);
    if (b.getToggleState())
        drawPad (g, face, s, true, colour (C::colourB), highlighted, down, b.isEnabled(), 0.0f, 4.0f);
    else
        drawKey (g, face, s, highlighted, down, b.isEnabled());
}

void StemLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const float s = tooltipScale;
    auto face = b.getLocalBounds().withTrimmedBottom (juce::roundToInt (2.0f * s));
    if (down)
        face = face.translated (0, juce::jmax (1, juce::roundToInt (s)));

    g.setFont (getTextButtonFont (b, face.getHeight()));
    const auto c = b.getToggleState() ? colour (C::inkDark) : colour (C::text);
    g.setColour (c.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.drawFittedText (b.getButtonText(), face.reduced (juce::roundToInt (6.0f * s), 0), juce::Justification::centred, 1, 1.0f);
}

void StemLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    const auto fontPx = (float) (double) b.getProperties().getWithDefault ("fontPx", 14.0);
    const float s = tooltipScale;
    const float alpha = b.isEnabled() ? 1.0f : 0.45f;
    const float box = std::round (fontPx * 1.2f);
    const auto r = b.getLocalBounds().toFloat();
    const juce::Rectangle<float> face (r.getX() + 1.0f, r.getCentreY() - box * 0.5f - s, box, box);
    const bool on = b.getToggleState();

    drawPad (g, face, s, on, colour (C::colourB), highlighted, down, b.isEnabled(), 0.0f, 4.0f);

    if (on)
    {
        juce::Path p;
        p.startNewSubPath (face.getX() + box * 0.25f, face.getY() + box * 0.52f);
        p.lineTo (face.getX() + box * 0.43f, face.getY() + box * 0.70f);
        p.lineTo (face.getX() + box * 0.76f, face.getY() + box * 0.32f);
        g.setColour (colour (C::inkDark).withMultipliedAlpha (alpha));
        g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, box * 0.13f), juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    g.setColour (colour (C::text).withMultipliedAlpha (alpha * (highlighted ? 1.0f : 0.92f)));
    g.setFont (uiFont (fontPx));
    g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (box + fontPx * 0.7f).toNearestInt(),
                      juce::Justification::centredLeft, 2, 1.0f);
}

int StemLookAndFeel::getSliderThumbRadius (juce::Slider& s)
{
    if (s.getProperties().contains ("thumbW"))
        return juce::jmax (1, (int) s.getProperties()["thumbW"] / 2);
    return juce::LookAndFeel_V4::getSliderThumbRadius (s);
}

void StemLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float minPos, float maxPos,
                                        juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (slider.getProperties()["style"].toString() != "crossfade")
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
        return;
    }

    const float s = (float) (double) slider.getProperties().getWithDefault ("uiScale", 1.0);
    const float thumbW = (float) (int) slider.getProperties().getWithDefault ("thumbW", 26);
    const float thumbH = (float) (int) slider.getProperties().getWithDefault ("thumbH", 42);
    const float cy = (float) y + (float) height * 0.5f;

    // The slot: 6 px, #121315, inset 0 1px 2px #000
    const auto slot = juce::Rectangle<float> ((float) x - thumbW * 0.5f, cy - 3.0f * s, (float) width + thumbW, 6.0f * s);
    g.setColour (colour (C::slot));
    g.fillRoundedRectangle (slot, 3.0f * s);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillRect (slot.reduced (3.0f * s, 0.0f).withHeight (juce::jmax (1.0f, s)));

    // Ticks every 10 %, the centre one taller
    g.setColour (colour (C::tick));
    for (int i = 0; i <= 10; ++i)
    {
        const float tx = std::round ((float) x + (float) width * (float) i / 10.0f);
        const float half = (i == 5 ? 20.0f : 14.0f) * s;
        g.fillRect (juce::Rectangle<float> (tx, cy - half, 1.0f, 2.0f * half));
    }

    // The cap: 26 x 42, gradient #55595F -> #33363B, 0 3px 6px #000a, inset 0 1px 0 #777b81, a white line
    const auto cap = juce::Rectangle<float> (thumbW, thumbH).withCentre ({ sliderPos, cy });
    const float rad = 4.0f * s;
    juce::Path shape;
    shape.addRoundedRectangle (cap, rad);
    juce::DropShadow (juce::Colours::black.withAlpha (0.67f), juce::roundToInt (6.0f * s),
                      { 0, juce::roundToInt (3.0f * s) }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (colour (C::capHi), 0.0f, cap.getY(), colour (C::capLo), 0.0f, cap.getBottom(), false));
    g.fillPath (shape);
    insetTopLine (g, cap, rad, colour (C::capRim), juce::jmax (1.0f, std::floor (s)));

    const float lineH = juce::jmax (1.0f, std::round (2.0f * s));
    g.setColour (colour (C::text));
    g.fillRect (juce::Rectangle<float> (cap.getX() + 3.0f * s, std::round (cap.getCentreY() - lineH * 0.5f),
                                        cap.getWidth() - 6.0f * s, lineH));
}

void StemLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    if (slider.getProperties()["style"].toString() != "knob")
    {
        juce::LookAndFeel_V4::drawRotarySlider (g, x, y, width, height, pos, startAngle, endAngle, slider);
        return;
    }

    const float s = (float) (double) slider.getProperties().getWithDefault ("uiScale", 1.0);
    const auto box = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float size = juce::jmin (box.getWidth(), box.getHeight());
    const auto centre = box.getCentre();
    const float knobD = size * 84.0f / 100.0f;
    const auto knob = juce::Rectangle<float> (knobD, knobD).withCentre (centre);
    const float angle = startAngle + pos * (endAngle - startAngle);

    // The value arc around the knob: #121315 track, accent value
    const float stroke = juce::jmax (2.0f, 4.0f * s);
    const float arcR = size * 0.5f - stroke * 0.5f;
    const juce::PathStrokeType arcStroke (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (colour (C::slot));
        g.strokePath (track, arcStroke);
    }
    if (pos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (colour (C::accent).withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.5f));
        g.strokePath (value, arcStroke);
    }

    // The knob: radial-gradient(circle at 50% 35%, #4A4E54, #26282C 70%), 0 3px 6px #000a, inset 0 1px 0 #6A6E74
    juce::Path disc;
    disc.addEllipse (knob);
    juce::DropShadow (juce::Colours::black.withAlpha (0.67f), juce::roundToInt (6.0f * s),
                      { 0, juce::roundToInt (3.0f * s) }).drawForPath (g, disc);
    const juce::Point<float> hot (knob.getCentreX(), knob.getY() + knob.getHeight() * 0.35f);
    g.setGradientFill (juce::ColourGradient (colour (C::knobHi), hot, colour (C::knobLo),
                                             hot.translated (knobD * 0.574f, 0.0f), true));
    g.fillPath (disc);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (disc);
        juce::Path rim;
        rim.addEllipse (knob.translated (0.0f, 1.0f));
        juce::Path outside;
        outside.addRectangle (knob.expanded (2.0f));
        outside.addPath (rim);
        outside.setUsingNonZeroWinding (false);
        g.setGradientFill (juce::ColourGradient (colour (C::knobRim), 0.0f, knob.getY(),
                                                 colour (C::knobRim).withAlpha (0.0f), 0.0f, knob.getY() + knobD * 0.35f, false));
        g.fillPath (outside);
    }

    // The pointer: 3 x 16, from 18/42 to 34/42 of the knob radius
    const float k = knobD / 84.0f;
    const auto pointer = juce::Rectangle<float> (3.0f * k, 16.0f * k).withCentre ({ centre.x, centre.y - 26.0f * k });
    juce::Path p;
    p.addRoundedRectangle (pointer, 1.5f * k);
    g.setColour (colour (C::text).withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.5f));
    g.fillPath (p, juce::AffineTransform::rotation (angle, centre.x, centre.y));
}

void StemLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool down, int, int, int, int,
                                    juce::ComboBox& box)
{
    const float s = tooltipScale;
    const auto face = juce::Rectangle<float> ((float) width, (float) height).withTrimmedBottom (2.0f * s);
    drawKey (g, face, s, box.isMouseOver (true), down, box.isEnabled());

    // The arrow
    const float aw = 8.0f * s, ah = 5.0f * s;
    const auto c = juce::Point<float> (face.getRight() - 14.0f * s, face.getCentreY());
    juce::Path arrow;
    arrow.addTriangle (c.x - aw * 0.5f, c.y - ah * 0.5f, c.x + aw * 0.5f, c.y - ah * 0.5f, c.x, c.y + ah * 0.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.45f));
    g.fillPath (arrow);
}

//==============================================================================
juce::Rectangle<int> StemLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                        juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltip (tipText);
    float textW = 0.0f;
    for (int i = 0; i < layout.getNumLines(); ++i)
        textW = juce::jmax (textW, layout.getLine (i).getLineBoundsX().getLength());

    const int padX = scaled (10.0f, tooltipScale), padY = scaled (6.0f, tooltipScale);
    const int w = (int) std::ceil (textW) + 2 * padX;
    const int h = (int) std::ceil (layout.getHeight()) + 2 * padY;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h)
        .constrainedWithin (parentArea);
}

void StemLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    // Light box, dark text: stands apart from the dark panels without needing a shadow.
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
    g.fillRect (bounds);
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRect (bounds, 1.0f);

    layoutTooltip (text).draw (g, bounds.reduced ((float) scaled (10.0f, tooltipScale), (float) scaled (6.0f, tooltipScale)));
}

juce::TextLayout StemLookAndFeel::layoutTooltip (const juce::String& text) const
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::topLeft);
    s.append (text, uiFont (em (1.0f, tooltipScale)), findColour (juce::TooltipWindow::textColourId));

    juce::TextLayout layout;
    layout.createLayout (s, 640.0f * tooltipScale);
    return layout;
}

} // namespace stemab::theme
