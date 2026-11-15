#include "app/ui/WaveformLane.h"

#include "app/Theme.h"

namespace stemab
{

WaveformLane::WaveformLane (juce::Colour wave, juce::Colour bg, bool drawCentreLine)
    : waveColour (wave), background (bg), centreLine (drawCentreLine)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
}

void WaveformLane::setSummary (std::shared_ptr<const WaveformSummary> s)
{
    if (s == summary)
        return;
    summary = std::move (s);
    cacheValid = false;
    repaint();
}

void WaveformLane::setPlaceholder (const juce::String& text)
{
    if (text == placeholder)
        return;
    placeholder = text;
    repaint();
}

void WaveformLane::setFaded (bool f)
{
    if (f == faded)
        return;
    faded = f;
    repaint();
}

void WaveformLane::setDisplayGain (float g)
{
    displayGain = g;
    cacheValid = false;
    repaint();
}

void WaveformLane::setVerticalPadding (float px)
{
    basePadding = px;
    cacheValid = false;
    repaint();
}

void WaveformLane::setUiScale (float s)
{
    if (s == uiScale)
        return;
    uiScale = s;
    cacheValid = false;
    repaint();
}

void WaveformLane::resized()
{
    cacheValid = false;
}

void WaveformLane::renderCache()
{
    cacheValid = true;
    cache = {};

    if (summary == nullptr || summary->isEmpty() || getWidth() <= 0 || getHeight() <= 0)
        return;

    // Render at the display's pixel density so the waveform stays crisp on scaled displays.
    cacheScale = juce::jmax (1.0f, juce::Component::getApproximateScaleFactorForComponent (this));
    const int w = juce::roundToInt ((float) getWidth() * cacheScale);
    const int h = juce::roundToInt ((float) getHeight() * cacheScale);
    if (w <= 0 || h <= 0)
        return;

    cache = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (cache);

    const float pad = juce::jmin (basePadding * uiScale * cacheScale, (float) h * 0.25f);
    const float centre = (float) h * 0.5f;
    const float half = juce::jmax (1.0f, centre - pad);
    const double length = (double) summary->getLength();

    const auto peakColour = waveColour.withAlpha (0.35f);
    const auto rmsColour = waveColour;

    for (int x = 0; x < w; ++x)
    {
        const auto start = (juce::int64) (length * x / w);
        const auto end = juce::jmax (start + 1, (juce::int64) (length * (x + 1) / w));
        const auto level = summary->getLevel (start, end);

        const float pk = juce::jmin (1.0f, level.peak * displayGain) * half;
        const float rm = juce::jmin (1.0f, level.rms * displayGain) * half;

        if (pk > 0.0f)
        {
            g.setColour (peakColour);
            g.fillRect (juce::Rectangle<float> ((float) x, centre - pk, 1.0f, juce::jmax (1.0f, 2.0f * pk)));
        }
        if (rm > 0.0f)
        {
            g.setColour (rmsColour);
            g.fillRect (juce::Rectangle<float> ((float) x, centre - rm, 1.0f, juce::jmax (1.0f, 2.0f * rm)));
        }
    }
}

void WaveformLane::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll (background);

    if (! cacheValid
        || (cache.isValid() && cacheScale != juce::jmax (1.0f, juce::Component::getApproximateScaleFactorForComponent (this))))
        renderCache();

    if (centreLine)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (0, bounds.getHeight() / 2, bounds.getWidth(), 1);
    }

    if (cache.isValid())
    {
        g.setOpacity (faded ? 0.22f : 1.0f);
        g.drawImage (cache, bounds.toFloat(),juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
    }
    else if (placeholder.isNotEmpty())
    {
        g.setColour (theme::colour (theme::mutedTextArgb).withAlpha (0.8f));
        g.setFont (theme::uiFont (theme::em (0.72f, uiScale)));
        g.drawText (placeholder, bounds.reduced (theme::scaled (12.0f, uiScale), 0), juce::Justification::centredLeft, true);
    }

    // Bottom hairline (the lane separators)
    g.setColour (theme::colour (theme::hairlineArgb));
    g.fillRect (0, bounds.getHeight() - 1, bounds.getWidth(), 1);
}

} // namespace stemab
