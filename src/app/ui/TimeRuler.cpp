#include "app/ui/TimeRuler.h"

#include "app/Theme.h"
#include "app/ui/Format.h"

namespace stemab
{

TimeRuler::TimeRuler()
{
    setOpaque (true);
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
    const auto bounds = getLocalBounds();
    g.fillAll (theme::colour (theme::panelArgb));
    g.setColour (theme::colour (theme::hairlineArgb));
    g.fillRect (0, bounds.getHeight() - 1, bounds.getWidth(), 1);

    if (length <= 0 || rate <= 0.0 || bounds.getWidth() <= 0)
        return;

    const double seconds = (double) length / rate;
    const double pxPerSecond = bounds.getWidth() / seconds;

    double step = 30.0;
    while (step * pxPerSecond < 56.0 * uiScale && step < seconds)
        step *= 2.0;

    const auto font = theme::monoFont (theme::em (0.68f, uiScale));
    const int pad = theme::scaled (5.0f, uiScale);
    g.setFont (font);

    for (double t = 0.0; t < seconds; t += step)
    {
        const int x = juce::roundToInt (t * pxPerSecond);
        g.setColour (theme::colour (theme::hairline2Argb));
        g.fillRect (x, 0, 1, bounds.getHeight());

        g.setColour (theme::colour (theme::mutedTextArgb));
        g.drawText (format::minutesSeconds (t),
                    juce::Rectangle<int> (x + pad, 0, theme::scaled (60.0f, uiScale), bounds.getHeight() - pad),
                    juce::Justification::bottomLeft, false);
    }
}

} // namespace stemab
