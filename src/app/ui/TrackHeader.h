#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "app/Theme.h"
#include "app/ui/LevelMeter.h"
#include "app/ui/Widgets.h"

namespace stemab
{

/** One channel strip of the left column, beside its waveform lane: a raised strip with a colour bar, the name,
    an optional mono sub-label, optional M / S pads, an optional thin LED level meter and (Residual) the
    "+24 / 0 dB" view-gain pad. The strip is inset 3 px top and bottom inside the component (the 6 px gap
    between strips), so the component itself can span its lane's full height. */
class TrackHeader final : public juce::Component
{
public:
    enum class Kind { original, stem, residual };

    struct Style
    {
        Kind kind = Kind::stem;
        theme::C colour = theme::C::colourA;    // colour bar
        juce::String name;                      // e.g. "Original", "Drums"
        bool hasMuteSolo = false;
        bool hasMeter = false;
        bool hasViewGainButton = false;         // the Residual's "+24 dB / 0 dB" display toggle
    };

    explicit TrackHeader (Style);

    void setSubLabel (const juce::String&);
    void setUiScale (float);
    void setDimmed (bool);              // controls unusable (no Stems yet)

    HwButton mute { HwButton::Kind::pad, "M" }, solo { HwButton::Kind::pad, "S" };
    HwButton viewGain { HwButton::Kind::pad };   // shown only with Style::hasViewGainButton; the owner sets text + toggle
    LevelMeter* getMeter() noexcept     { return meter.get(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<int> strip() const;

    Style style;
    juce::String subLabel;
    float uiScale = 1.0f;
    bool dimmed = false;
    std::unique_ptr<LevelMeter> meter;
    juce::Rectangle<int> meterWell, textArea;
};

} // namespace stemab
