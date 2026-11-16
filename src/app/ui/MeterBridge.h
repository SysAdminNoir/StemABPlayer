#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "app/ui/LevelMeter.h"

namespace stemab
{

/** The meter column right of the waveform screen: "LEVELS", a meter screen with a dB scale and three LED bus
    meters A / B / OUT (two bars each, L and R, in a sunken housing with a glass sheen), the held peak of each
    bus under its label, and the live "NULL DEPTH" screen. */
class MeterBridge final : public juce::Component
{
public:
    enum Index { a = 0, b = 1, out = 2 };

    MeterBridge();

    /** Linear peak / RMS of meter `m` (Index), channel 0 or 1. */
    void setLevels (int m, int channel, float peak, float rms);
    void setLiveNullDepth (const juce::String&);
    void setUiScale (float);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    std::array<std::array<LevelMeter, 2>, 3> meters;

    std::array<juce::String, 3> readouts;
    juce::String liveNull;
    float uiScale = 1.0f;
    int tick = 0;

    juce::Rectangle<int> levelsLabel, meterScreen, scaleColumn, nullBox;
    std::array<juce::Rectangle<int>, 3> housings, meterLabels, readoutRows;
};

} // namespace stemab
