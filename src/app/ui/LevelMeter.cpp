#include "app/ui/LevelMeter.h"

#include <cmath>

#include "app/Theme.h"

namespace stemab
{

LevelMeter::LevelMeter (Orientation o, juce::Colour solid)
    : orientation (o), solidColour (solid), background (theme::colour (theme::groundArgb))
{
    setInterceptsMouseClicks (false, false);
}

void LevelMeter::setOrientation (Orientation o)  { orientation = o; repaint(); }
void LevelMeter::setSolidColour (juce::Colour c)       { solidColour = c; repaint(); }
void LevelMeter::setBackgroundColour (juce::Colour c)  { background = c; repaint(); }

void LevelMeter::reset()
{
    shownPeak = shownRms = 0.0f;
    repaint();
}

void LevelMeter::setLevels (float peak, float rms)
{
    // Peak tick: a new peak holds for peakHoldMs, then falls back at peakFallDbPerSec. RMS follows directly.
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

    const float p = theme::meterProportion (shownPeak) + (shownPeak > 1.0f ? 1.0f : 0.0f);
    const float r = theme::meterProportion (shownRms);
    const float extent = (float) juce::jmax (1, orientation == Orientation::vertical ? getHeight() : getWidth());

    // Repaint only for a visible change (about half a pixel).
    if (std::abs (p - paintedPeak) * extent >= 0.5f || std::abs (r - paintedRms) * extent >= 0.5f)
        repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool vertical = orientation == Orientation::vertical;
    const float corner = juce::jmin (2.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.4f);

    g.setColour (background);
    g.fillRoundedRectangle (r, corner);

    paintedPeak = theme::meterProportion (shownPeak) + (shownPeak > 1.0f ? 1.0f : 0.0f);
    paintedRms = theme::meterProportion (shownRms);

    const auto portion = [&] (float proportion)
    {
        return vertical ? r.withTop (r.getBottom() - r.getHeight() * proportion)
                        : r.withWidth (r.getWidth() * proportion);
    };

    if (solidColour.isTransparent())
        g.setGradientFill (vertical ? theme::meterGradient (r.getBottomLeft(), r.getTopLeft())
                                    : theme::meterGradient (r.getTopLeft(), r.getTopRight()));
    else
        g.setColour (solidColour);

    const float rmsP = theme::meterProportion (shownRms);
    if (rmsP > 0.0f)
        g.fillRoundedRectangle (portion (rmsP), corner);

    if (shownPeak > 0.0f)
    {
        const float peakP = theme::meterProportion (shownPeak);
        const bool clipped = shownPeak > 1.0f;
        const float tick = 2.0f;
        juce::Rectangle<float> t;
        if (vertical)
            t = { r.getX(), juce::jmax (r.getY(), r.getBottom() - r.getHeight() * peakP - tick * 0.5f), r.getWidth(), tick };
        else
            t = { juce::jmin (r.getRight() - tick, r.getX() + r.getWidth() * peakP - tick * 0.5f), r.getY(), tick, r.getHeight() };

        if (clipped)
            g.setColour (theme::colour (theme::meterClipArgb));
        else if (solidColour.isTransparent())
            g.setColour (theme::colour (theme::textArgb).withAlpha (0.85f));
        else
            g.setColour (solidColour.brighter (0.5f));
        g.fillRect (t);
    }
}

} // namespace stemab
