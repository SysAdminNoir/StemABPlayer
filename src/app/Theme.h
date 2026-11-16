#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>

namespace stemab::theme
{

//==============================================================================
// Palette (the approved "hardware panel" mockup, looks/project/Hardware.dc.html, and its colour-blind safe
// variant HardwareCvd.dc.html). Every colour is looked up by role through the active palette, so switching
// palettes at runtime is setPalette() + a repaint (plus re-rendering the cached waveform images).

enum class PaletteId { standard = 0, colourBlindSafe = 1 };

/** Colour roles. The panel greys are the same in both palettes; the signal colours (below `colourA`) change. */
enum class C
{
    ground,         // #222428 window ground between the panels
    panel,          // #2A2C31 top / bottom panels, channel strips
    panelHi,        // #3A3D42 inset top highlight of panels and strips
    panelEdge,      // #17181B dark panel edge
    residualStrip,  // #26282C the Residual strip
    key,            // #3A3D42 key face
    keyHi,          // #5A5E64 key inset top highlight
    keyShadow,      // #151618 key bottom shadow
    padOff,         // #2F3236 unlit pad
    padShadow,      // #121315 pad bottom shadow
    screen,         // #04080D sunken screens
    screenLine,     // #16263A lane separators, ruler border
    screenText,     // #CFE3F5 text on screens
    screenDim,      // #5D7A94 dim text on screens (ruler, scale)
    ledOff,         // #0D1620 unlit LED cell
    slot,           // #121315 fader slot, knob arc track
    tick,           // #4A4E54 fader ticks
    knobHi,         // #4A4E54 knob gradient centre
    knobLo,         // #26282C knob gradient edge
    knobRim,        // #6A6E74 knob inset highlight
    capHi,          // #55595F fader cap gradient top
    capLo,          // #33363B fader cap gradient bottom
    capRim,         // #777B81 fader cap inset highlight
    text,           // #E7E8EA
    silk,           // #B9BCC0 silkscreen labels
    silkDim,        // #7E8288 dim silkscreen labels
    muted,          // #8C9096 muted text, unlit pad text
    inkDark,        // #0B1622 text on light pads and the playhead tag
    inkPad,         // #1A1B1E text on amber pads

    // Signal colours (palette-dependent)
    colourA,        // A = ivory
    colourB,        // B (and Solo, Vocals)
    accent,         // wordmark, Residual, Master arc
    go,             // Play, meter low cells, "aligned" dot
    amber,          // Mute, Equal power, meter mid cells, notes
    meterRed,       // meter top cells, Clip, errors
    nullDepth,      // the live Null Depth value
    stemDrums,
    stemBass,
    stemOther,
    stemVocals,

    count
};

void setPalette (PaletteId);
PaletteId activePalette() noexcept;

juce::Colour colour (C) noexcept;
/** Drums, Bass, Other, Vocals. */
juce::Colour stemColour (int index) noexcept;
inline C stemRole (int index) noexcept       { return (C) ((int) C::stemDrums + juce::jlimit (0, 3, index)); }

//==============================================================================
// Metrics: the mockup is specified at 100 %. Every metric and font size is multiplied by one interface scale
// (MainComponent::uiScale: 0.9 / 1.0 / 1.15 / 1.3).
inline constexpr float baseFontPx = 14.0f;

/** The interface-scale steps: the one list behind A- / A+, Ctrl+- / Ctrl+= / Ctrl+0 and Preferences. */
inline constexpr std::array<float, 4> scaleSteps { 0.9f, 1.0f, 1.15f, 1.3f };
inline constexpr int defaultScaleIndex = 1;     // 100 %

inline int scaled (float px, float uiScale) noexcept         { return juce::roundToInt (px * uiScale); }
inline float em (float ems, float uiScale) noexcept          { return baseFontPx * ems * uiScale; }

//==============================================================================
// Fonts: Archivo (text), Archivo Narrow Bold (silkscreen labels) and JetBrains Mono (numbers, screens), bundled
// as binary data (SIL OFL, resources/fonts). Falls back to Segoe UI / Cascadia Mono if they cannot be loaded.
enum class Weight { regular, semiBold, bold, extraBold };

/** Text font, `px` = CSS-like em size in component pixels. */
juce::Font uiFont (float px, Weight);
inline juce::Font uiFont (float px, bool bold = false)       { return uiFont (px, bold ? Weight::bold : Weight::regular); }

/** Numbers, times, file names. `semiBold`: weight 600. */
juce::Font monoFont (float px, bool semiBold = false);

/** The silkscreen label font (Archivo Narrow 700); draw with drawSilk() for the tracking and capitals. */
juce::Font silkFont (float px);

/** True when the bundled fonts loaded (false: the Segoe UI / Cascadia Mono fallback is in use). */
bool bundledFontsLoaded();

/** Archivo Regular (or null): the LookAndFeel's default sans-serif typeface. */
juce::Typeface::Ptr defaultSansTypeface();

/** Drops the bundled typefaces (application shutdown, after the windows and the LookAndFeel are gone). */
void releaseFonts();

/** Text with letter spacing. */
inline void drawTrackedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                             const juce::Font& font, float trackingEms, juce::Justification just)
{
    g.setFont (font.withExtraKerningFactor (trackingEms));
    g.drawText (text, area, just, true);
}

/** A silkscreen label: Archivo Narrow 700, capitals, 0.08 em tracking. `px` is already scaled. */
inline void drawSilk (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float px,
                      juce::Colour c, juce::Justification just = juce::Justification::centredLeft)
{
    g.setColour (c);
    drawTrackedText (g, text.toUpperCase(), area, silkFont (px), 0.08f, just);
}

inline float silkWidth (const juce::String& text, float px)
{
    return juce::GlyphArrangement::getStringWidth (silkFont (px).withExtraKerningFactor (0.08f), text.toUpperCase());
}

//==============================================================================
// Levels: meters map -60..0 dBFS; above 0 dBFS is clipping. The LED meters use 1.5 dB cells.
inline constexpr float meterFloorDb = -60.0f;
inline constexpr int ledCells = 40;                   // 40 x 1.5 dB

inline float gainToDb (float linear) noexcept
{
    return linear > 0.0f ? 20.0f * std::log10 (linear) : -200.0f;
}

/** 0 at the floor (-60 dBFS), 1 at 0 dBFS. */
inline float meterProportion (float linear) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (gainToDb (linear) - meterFloorDb) / -meterFloorDb);
}

/** The LED colour of a cell whose lower edge is `cellLowDb`: green, amber from -12 dB, red from -3 dB. */
inline juce::Colour ledColour (float cellLowDb) noexcept
{
    return colour (cellLowDb >= -3.0f ? C::meterRed : cellLowDb >= -12.0f ? C::amber : C::go);
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
// Drawing helpers (the hardware panel language). `s` is the interface scale.

/** A raised panel area (#2A2C31) with the inset top highlight. */
void drawPanel (juce::Graphics&, juce::Rectangle<float>, float s, C fill = C::panel, float radius = 0.0f, bool topHighlight = true);

/** A sunken screen: #04080D, radius 6, inset shadow, faint bottom highlight. */
void drawScreen (juce::Graphics&, juce::Rectangle<float>, float s, float radius = 6.0f);

/** A key face (#3A3D42, radius 4, 2 px bottom shadow drawn below `face`, inset top highlight). */
void drawKey (juce::Graphics&, juce::Rectangle<float> face, float s, bool highlighted, bool down, bool enabled,
              float radius = 4.0f);

/** An RGB pad (radius 5): `lit` in `litColour` with a glow of `glowPx` (already scaled), else #2F3236. */
void drawPad (juce::Graphics&, juce::Rectangle<float> face, float s, bool lit, juce::Colour litColour,
              bool highlighted, bool down, bool enabled, float glowPx, float radius = 5.0f);

/** One LED meter cell. `alongX`: the meter runs horizontally (the cell's gradient then runs vertically). */
void drawLedCell (juce::Graphics&, juce::Rectangle<float> cell, bool lit, juce::Colour, bool alongX);

//==============================================================================
/** The hardware-panel LookAndFeel. Keys, pads and the transport are drawn by the widgets themselves
    (ui/Widgets.h); this styles the plain JUCE widgets that remain (Preferences, the audio device selector,
    tooltips, scrollbars) and the two sliders:

      - Slider property "style": "crossfade" (dark slot, ticks, chunky cap) or "knob" (the Master knob:
        radial knob, red value arc, white pointer); "thumbW" / "thumbH" (int): the crossfade cap size, scaled;
        "uiScale" (double) for the knob.
      - ToggleButton property "fontPx" (double): the text size, already scaled.

    applyPalette() re-reads the active palette into the JUCE colour ids. */
class StemLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StemLookAndFeel();

    void applyPalette();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    int getSliderThumbRadius (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minPos,
                           float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;

    // Tooltips: the UI font at the interface scale (JUCE's default is a fixed 13 px), left-aligned lines.
    // The same scale sizes the keys this LookAndFeel draws (Preferences, the device selector).
    void setTooltipScale (float s) noexcept     { tooltipScale = s; }
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

private:
    juce::TextLayout layoutTooltip (const juce::String& text) const;

    float tooltipScale = 1.0f;
};

} // namespace stemab::theme
