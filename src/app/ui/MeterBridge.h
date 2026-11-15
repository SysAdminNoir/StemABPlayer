#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "app/ui/LevelMeter.h"

namespace stemab
{

/** The vertical meter bridge right of the track area: "LEVELS", a dB scale, three stereo meters
    A / B / OUT, the Live Null Depth readout and per-meter peak dB readouts. */
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
    void resized() override;

private:
    std::array<std::array<LevelMeter, 2>, 3> meters;   // vertical, meter gradient

    std::array<juce::String, 3> readouts;
    juce::String liveNull;
    float uiScale = 1.0f;
    int tick = 0;

    juce::Rectangle<int> levelsLabel, scaleColumn, nullBox, readoutRow;
    std::array<juce::Rectangle<int>, 3> meterLabels;
};

} // namespace stemab
