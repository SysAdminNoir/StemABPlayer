#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "analysis/WaveformSummary.h"

namespace stemab
{

/** One waveform lane: a DJ-style rendering of a WaveformSummary, mirrored around the centre line - a light
    peak envelope (35 % alpha) with a solid RMS body inside, in the lane colour.

    The waveform is rendered once into a cached juce::Image (at the display's pixel density) and only
    re-rendered on resize, interface-scale change or new data; paint() just blits it. "Faded" (a stem that
    the Mute/Solo rule makes inaudible) draws the cached image at 22 % opacity without re-rendering.
    The playhead and the played-region darkening are drawn by PlayheadOverlay, not here. */
class WaveformLane final : public juce::Component
{
public:
    WaveformLane (juce::Colour waveColour, juce::Colour background, bool drawCentreLine);

    /** nullptr: no waveform (the placeholder text is shown, if any). */
    void setSummary (std::shared_ptr<const WaveformSummary>);
    bool hasSummary() const noexcept                   { return summary != nullptr; }

    void setPlaceholder (const juce::String& text);
    void setFaded (bool);
    void setDisplayGain (float linear);                 // e.g. 16 (+24 dB) for the Residual
    void setVerticalPadding (float basePx);             // at 100 % interface scale
    void setUiScale (float);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void renderCache();

    std::shared_ptr<const WaveformSummary> summary;
    juce::Colour waveColour, background;
    bool centreLine = true;
    bool faded = false;
    float displayGain = 1.0f;
    float basePadding = 6.0f;
    float uiScale = 1.0f;
    juce::String placeholder;

    juce::Image cache;
    float cacheScale = 0.0f;
    bool cacheValid = false;
};

} // namespace stemab
