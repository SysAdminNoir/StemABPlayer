#include "app/ui/LevelMeter.h"

#include <cmath>

#include "app/Theme.h"

namespace stemab
{

LevelMeter::LevelMeter (Orientation o) : orientation (o)
{
    setInterceptsMouseClicks (false, false);
}

void LevelMeter::setOrientation (Orientation o)  { orientation = o; repaint(); }

void LevelMeter::reset()
{
    shownPeak = shownRms = 0.0f;
    repaint();
}

int LevelMeter::extent() const noexcept
{
    return orientation == Orientation::vertical ? getHeight() : getWidth();
}

int LevelMeter::cellsFor (int length) noexcept
{
    // 1 px gaps; each cell at least 2 px.
    for (const int n : { theme::ledCells, 20, 12, 10, 6 })
        if (length - (n - 1) >= 2 * n)
            return n;
    return 4;
}

LevelMeter::Cells LevelMeter::cellsToShow (int n) const noexcept
{
    const float cellDb = -theme::meterFloorDb / (float) n;
    const auto cellsAt = [&] (float linear)
    {
        const float db = theme::gainToDb (linear);
        return (int) std::ceil ((db - theme::meterFloorDb) / cellDb);
    };

    Cells c;
    c.lit = juce::jlimit (0, n, cellsAt (shownRms));
    c.peak = shownPeak > 1.0f ? n - 1 : shownPeak > 0.0f ? juce::jlimit (-1, n - 1, cellsAt (shownPeak) - 1) : -1;
    return c;
}

void LevelMeter::setLevels (float peak, float rms)
{
    // Peak cell: a new peak holds for peakHoldMs, then falls back at peakFallDbPerSec. RMS follows directly.
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = lastUpdateMs > 0.0 ? juce::jlimit (0.0, 250.0, now - lastUpdateMs) : 0.0;
    lastUpdateMs = now;

    if (peak >= shownPeak)
    {
        shownPeak = peak;
        peakHeldSinceMs = now;
    }
    else if (now - peakHeldSinceMs > peakHoldMs)
    {
        shownPeak = juce::jmax (peak, shownPeak * (float) std::pow (10.0, -peakFallDbPerSec * dt / 20000.0));
    }

    if (shownPeak < 1.0e-6f)
        shownPeak = 0.0f;
    shownRms = rms;

    // Repaint only when a cell changes.
    const auto c = cellsToShow (cellsFor (extent()));
    if (c.lit != painted.lit || c.peak != painted.peak)
        repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const bool vertical = orientation == Orientation::vertical;
    const int length = extent();
    const int n = cellsFor (length);
    const float cellDb = -theme::meterFloorDb / (float) n;
    painted = cellsToShow (n);

    // Cell i spans [edge (i), edge (i + 1) - 1): whole pixels, 1 px gaps.
    const auto edge = [&] (int i) { return juce::roundToInt ((float) i * (float) (length + 1) / (float) n); };
    const auto across = vertical ? getWidth() : getHeight();

    for (int i = 0; i < n; ++i)
    {
        const int a = edge (i), b = edge (i + 1) - 1;
        const auto cell = vertical ? juce::Rectangle<int> (0, length - b, across, b - a)
                                   : juce::Rectangle<int> (a, 0, b - a, across);
        const bool lit = i < painted.lit || i == painted.peak;
        theme::drawLedCell (g, cell.toFloat(), lit, theme::ledColour (theme::meterFloorDb + (float) i * cellDb), ! vertical);
    }
}

} // namespace stemab
