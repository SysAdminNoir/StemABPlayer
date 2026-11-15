#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "app/ui/LevelMeter.h"

namespace stemab
{

/** The left-hand header of one track lane: colour bar, name (an optional coloured prefix such as "A" or "R"),
    sub-label, optional M / S toggle buttons and an optional slim vertical level meter. */
class TrackHeader final : public juce::Component
{
public:
    struct Style
    {
        juce::Colour colour;            // colour bar, prefix, meter
        juce::Colour background;
        juce::String prefix;            // e.g. "A" (drawn in `colour`), may be empty
        juce::String name;              // e.g. " · Original" or "Drums"
        float nameEms = 1.0f;
        bool hasMuteSolo = false;
        bool hasMeter = false;
        bool hasViewGainButton = false; // the Residual's "+24 dB / 0 dB" display toggle
    };

    explicit TrackHeader (Style);

    void setSubLabel (const juce::String&);
    void setUiScale (float);
    void setDimmed (bool);              // controls unusable (no Stems yet)

    juce::TextButton mute { "M" }, solo { "S" };
    juce::TextButton viewGain;          // shown only with Style::hasViewGainButton; the owner sets text + toggle
    LevelMeter* getMeter() noexcept     { return meter.get(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Style style;
    juce::String subLabel;
    float uiScale = 1.0f;
    bool dimmed = false;
    std::unique_ptr<LevelMeter> meter;
};

} // namespace stemab
