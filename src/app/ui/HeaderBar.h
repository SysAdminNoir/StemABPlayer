#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ui/Widgets.h"

namespace stemab
{

/** The header bar: logo + "STEM A/B", the A and B file chips, the file / Unload all / Preferences buttons and the
    interface-scale buttons (row 1); a status line (row 2) - load progress, errors and notices on the left,
    Alignment Offset / Track Null Depth / rate / device on the right. The full detail is in the tooltip. */
class HeaderBar final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    /** One run of status text. */
    struct Segment
    {
        juce::String text;
        juce::Colour colour;
        bool mono = false;
        bool dot = false;       // draw a small filled circle in `colour` instead of text
    };

    HeaderBar();

    juce::TextButton openOriginalButton, openStemButton, settingsButton, scaleDownButton, scaleUpButton;
    IconButton unloadButton { "Unload all files", IconButton::Icon::eject };

    /** The chosen files (empty File = none): the chips show their names, the chip tooltips their full paths. */
    void setFiles (const juce::File& original, const juce::File& stemFile);
    juce::String getTooltip() override;
    void setStatus (const juce::Array<Segment>& left, const juce::Array<Segment>& right);
    void setScaleLabel (const juce::String&);
    void setUiScale (float);

    int getPreferredHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static juce::String keyOf (const juce::Array<Segment>&);
    float segmentsWidth (const juce::Array<Segment>&) const;
    void drawSegments (juce::Graphics&, const juce::Array<Segment>&, juce::Rectangle<float> area, bool alignRight) const;
    void drawChip (juce::Graphics&, juce::Rectangle<int> area, const juce::String& badge, juce::Colour badgeColour,
                   const juce::String& name, const juce::String& emptyText) const;

    juce::File originalFile, stemFilePath;
    juce::String originalName, stemFileName, scaleLabel { "100%" };
    juce::Array<Segment> leftStatus, rightStatus;
    juce::String statusKey;
    float uiScale = 1.0f;

    juce::Rectangle<int> logoArea, titleArea, chipA, chipB, scaleLabelArea, statusRow;
    int separatorX = 0;
};

} // namespace stemab
