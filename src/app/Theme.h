#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemab::theme
{

// Colours from the plan's UI section.
inline constexpr juce::uint32 windowArgb = 0xff202020;   // window / background
inline constexpr juce::uint32 panelArgb  = 0xff272727;   // panels / widgets
inline constexpr juce::uint32 accentArgb = 0xff60CDFF;   // accent
inline constexpr juce::uint32 textArgb   = 0xfff3f3f3;   // near-white text
inline constexpr juce::uint32 dimTextArgb = 0xff9d9d9d;
inline constexpr juce::uint32 outlineArgb = 0xff3d3d3d;

/** Dark LookAndFeel_V4 using the colours above and Segoe UI Variable (falling back to Segoe UI). */
class StemLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StemLookAndFeel()
    {
        using Scheme = juce::LookAndFeel_V4::ColourScheme;

        auto scheme = getDarkColourScheme();
        scheme.setUIColour (Scheme::windowBackground, juce::Colour (windowArgb));
        scheme.setUIColour (Scheme::widgetBackground, juce::Colour (panelArgb));
        scheme.setUIColour (Scheme::menuBackground,   juce::Colour (panelArgb));
        scheme.setUIColour (Scheme::outline,          juce::Colour (outlineArgb));
        scheme.setUIColour (Scheme::defaultText,      juce::Colour (textArgb));
        scheme.setUIColour (Scheme::defaultFill,      juce::Colour (accentArgb));
        scheme.setUIColour (Scheme::highlightedText,  juce::Colour (windowArgb));   // dark text on accent
        scheme.setUIColour (Scheme::highlightedFill,  juce::Colour (accentArgb));
        scheme.setUIColour (Scheme::menuText,         juce::Colour (textArgb));
        setColourScheme (scheme);

        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (windowArgb));
        setColour (juce::Label::textColourId,                 juce::Colour (textArgb));
        setColour (juce::ScrollBar::thumbColourId,            juce::Colour (outlineArgb).brighter (0.3f));

        setDefaultSansSerifTypefaceName (pickUiFontName());
    }

    static juce::String pickUiFontName()
    {
        // The variable font's installed family names are "Segoe UI Variable Display/Text/Small".
        const auto installed = juce::Font::findAllTypefaceNames();

        for (const char* candidate : { "Segoe UI Variable Text", "Segoe UI Variable", "Segoe UI Variable Display" })
            if (installed.contains (candidate))
                return candidate;

        return "Segoe UI";
    }
};

} // namespace stemab::theme
