#include "app/ui/TimeRuler.h"

#include "app/Theme.h"
#include "app/ui/Format.h"

namespace stemab
{

TimeRuler::TimeRuler()
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);
}

void TimeRuler::setTimeline (juce::int64 lengthSamples, double sampleRate)
{
    if (lengthSamples == length && sampleRate == rate)
        return;
    length = lengthSamples;
    rate = sampleRate;
    repaint();
}

void TimeRuler::setUiScale (float s)
{
    uiScale = s;
    repaint();
}

void TimeRuler::paint (juce::Graphics& g)
{
    // Transparent: the waveform screen shows through. A 1 px #16263A border along the bottom.
    const auto bounds = getLocalBounds();
    g.setColour (theme::colour (theme::C::screenLine));
    g.fillRect (0, bounds.getHeight() - 1, bounds.getWidth(), 1);

    if (length <= 0 || rate <= 0.0 || bounds.getWidth() <= 0)
        return;

    const double seconds = (double) length / rate;
    const double pxPerSecond = bounds.getWidth() / seconds;

    double step = 30.0;
    while (step * pxPerSecond < 56.0 * uiScale && step < seconds)
        step *= 2.0;

    const auto font = theme::monoFont (10.0f * uiScale);
    const int pad = theme::scaled (6.0f, uiScale);
    const int tickH = theme::scaled (5.0f, uiScale);
    g.setFont (font);

    for (double t = 0.0; t < seconds; t += step)
    {
        const int x = juce::roundToInt (t * pxPerSecond);
        g.setColour (theme::colour (theme::C::screenLine));
        g.fillRect (x, bounds.getHeight() - 1 - tickH, 1, tickH);

        g.setColour (theme::colour (theme::C::screenDim));
        g.drawText (format::minutesSeconds (t),
                    juce::Rectangle<int> (x + pad, pad, theme::scaled (60.0f, uiScale), juce::roundToInt (font.getHeight())),
                    juce::Justification::topLeft, false);
    }
}

} // namespace stemab
