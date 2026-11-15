#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemab
{

/** One channel of a level meter: an RMS bar plus a peak tick, -60..0 dBFS, the tick red above 0 dBFS.
    A new peak holds for 0.5 s, then falls back at 20 dB/s (the engine's peak is read-and-reset per tick).
    Fed by the UI timer through setLevels(); repaints only when what it shows changes. */
class LevelMeter final : public juce::Component
{
public:
    enum class Orientation { vertical, horizontal };

    /** `solid`: transparent = the green/amber/red meter gradient; otherwise a single colour (stem meters). */
    explicit LevelMeter (Orientation = Orientation::vertical, juce::Colour solid = juce::Colours::transparentBlack);

    void setLevels (float peak, float rms);     // linear
    void setOrientation (Orientation);
    void setSolidColour (juce::Colour);
    void setBackgroundColour (juce::Colour);
    void reset();

    float getShownPeak() const noexcept     { return shownPeak; }

    void paint (juce::Graphics&) override;

private:
    Orientation orientation;
    juce::Colour solidColour, background;
    float shownPeak = 0.0f, shownRms = 0.0f;
    float paintedPeak = -1.0f, paintedRms = -1.0f;   // in meter proportions, to skip no-op repaints

    static constexpr double peakHoldMs = 500.0;       // a new peak stays put this long before falling
    static constexpr double peakFallDbPerSec = 20.0;  // then falls back at this rate
    double peakHeldSinceMs = 0.0, lastUpdateMs = 0.0;
};

} // namespace stemab
