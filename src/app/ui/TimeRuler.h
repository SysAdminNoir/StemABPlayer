#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemab
{

/** The time ruler at the top of the waveform screen (transparent): a short tick and an m:ss label every 30 s
    (coarser when the labels would crowd). Clicks pass through to the PlayheadOverlay above it. */
class TimeRuler final : public juce::Component
{
public:
    TimeRuler();

    void setTimeline (juce::int64 lengthSamples, double sampleRate);
    void setUiScale (float);

    void paint (juce::Graphics&) override;

private:
    juce::int64 length = 0;
    double rate = 0.0;
    float uiScale = 1.0f;
};

} // namespace stemab
