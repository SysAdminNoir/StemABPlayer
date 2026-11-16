#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ui/Widgets.h"
#include "decode/StemSet.h"

namespace stemab
{

/** The top panel: the "stem a/b" wordmark, [Open A] + the A file screen, [Open B] + the B file screen, the
    Unload-all and Settings keys and the interface size stepper (SIZE  A-  115%  A+); under it a slim status
    line - load progress, errors and notices on the left, Alignment Offset / Track Null Depth / device / rate
    on the right. The full detail is in the status line's tooltip; a file screen's tooltip is its full path. */
class HeaderBar final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    /** One run of status text. */
    struct Segment
    {
        juce::String text;
        juce::Colour colour;
        bool mono = false;      // (all status text is mono; kept for the callers)
        bool dot = false;       // draw a small filled circle in `colour` instead of text
        bool silk = false;      // a silkscreen label (capitals, narrow, tracked)
    };

    HeaderBar();

    HwButton openOriginalButton, openStemButton, settingsButton, scaleDownButton, scaleUpButton;
    IconButton unloadButton { "Unload all files", IconButton::Icon::eject };

    /** The chosen files (empty = none): the screens show their names (a Stem Set its short label), their
        tooltips the full paths (a Stem Set's one per line). */
    void setFiles (const juce::File& original, const StemSource& stems);
    juce::String getTooltip() override;
    void setStatus (const juce::Array<Segment>& left, const juce::Array<Segment>& right);
    void setScaleLabel (const juce::String&);
    void setUiScale (float);

    int getPreferredHeight() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static juce::String keyOf (const juce::Array<Segment>&);
    float segmentWidth (const Segment&) const;
    float segmentsWidth (const juce::Array<Segment>&) const;
    void drawSegments (juce::Graphics&, const juce::Array<Segment>&, juce::Rectangle<float> area, bool alignRight) const;
    void drawFileScreen (juce::Graphics&, juce::Rectangle<int> area, const juce::String& letter, juce::Colour letterColour,
                         const juce::String& name, const juce::String& emptyText) const;

    juce::File originalFile;
    StemSource stemSource;
    juce::String originalName, stemFileName, scaleLabel { "100%" };
    juce::Array<Segment> leftStatus, rightStatus;
    juce::String statusKey;
    float uiScale = 1.0f;

    juce::Rectangle<int> topPanel, wordmarkArea, chipA, chipB, sizeLabelArea, scaleLabelArea, statusRow;
};

} // namespace stemab
