#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemab
{

/** One channel of a fine LED meter: 40 cells of 1.5 dB from -60 to 0 dBFS (fewer, coarser cells when the meter
    is too small for 40), green -> amber (from -12 dB) -> red (from -3 dB). The lit bar follows the RMS level;
    one more cell shows the peak, which holds for 0.5 s, then falls back at 20 dB/s (the engine's peak is
    read-and-reset per tick). Fed by the UI timer through setLevels(); repaints only when a cell changes.

    It draws only the cells; the housing (sunken well, glass sheen) is the parent's. */
class LevelMeter final : public juce::Component
{
public:
    enum class Orientation { vertical, horizontal };

    explicit LevelMeter (Orientation = Orientation::vertical);

    void setLevels (float peak, float rms);     // linear
    void setOrientation (Orientation);
    void reset();

    float getShownPeak() const noexcept     { return shownPeak; }

    /** The cell count for a meter `extent` pixels long (40 when 1.5 dB cells fit at >= 2 px each). */
    static int cellsFor (int extent) noexcept;

    void paint (juce::Graphics&) override;

private:
    struct Cells { int lit = 0, peak = -1; };
    Cells cellsToShow (int n) const noexcept;
    int extent() const noexcept;

    Orientation orientation;
    float shownPeak = 0.0f, shownRms = 0.0f;
    Cells painted { -1, -2 };

    static constexpr double peakHoldMs = 500.0;       // a new peak stays put this long before falling
    static constexpr double peakFallDbPerSec = 20.0;  // then falls back at this rate
    double peakHeldSinceMs = 0.0, lastUpdateMs = 0.0;
};

} // namespace stemab
