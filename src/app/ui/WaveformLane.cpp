#include "app/ui/WaveformLane.h"

#include <vector>

#include "app/Theme.h"

namespace stemab
{

WaveformLane::WaveformLane (juce::Colour wave, Look l)
    : waveColour (wave), look (l)
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);
}

void WaveformLane::setColour (juce::Colour c)
{
    if (c == waveColour)
        return;
    waveColour = c;
    cacheValid = false;
    repaint();
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

    // Per-column peak and RMS heights, then the mirrored outlines.
    std::vector<float> pk ((size_t) w), rm ((size_t) w);
    for (int x = 0; x < w; ++x)
    {
        const auto start = (juce::int64) (length * x / w);
        const auto end = juce::jmax (start + 1, (juce::int64) (length * (x + 1) / w));
        const auto level = summary->getLevel (start, end);
        pk[(size_t) x] = juce::jmin (1.0f, level.peak * displayGain) * half;
        rm[(size_t) x] = juce::jmin (1.0f, level.rms * displayGain) * half;
    }

    const auto mirrored = [&] (const std::vector<float>& v)
    {
        juce::Path p;
        p.startNewSubPath (0.0f, centre - v[0]);
        for (int x = 0; x < w; ++x)
            p.lineTo ((float) x + 0.5f, centre - v[(size_t) x]);
        p.lineTo ((float) w, centre - v[(size_t) w - 1]);
        p.lineTo ((float) w, centre + v[(size_t) w - 1]);
        for (int x = w; --x >= 0;)
            p.lineTo ((float) x + 0.5f, centre + v[(size_t) x]);
        p.lineTo (0.0f, centre + v[0]);
        p.closeSubPath();
        return p;
    };

    if (look == Look::filledPeak)
    {
        g.setColour (waveColour.withAlpha (0.6f));
        g.fillPath (mirrored (pk));
        return;
    }

    g.setColour (waveColour.withAlpha (0.35f));
    g.fillPath (mirrored (rm));
    g.setColour (waveColour);
    g.strokePath (mirrored (pk), juce::PathStrokeType (juce::jmax (1.0f, cacheScale), juce::PathStrokeType::mitered));
}

void WaveformLane::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    if (! cacheValid
        || (cache.isValid() && cacheScale != juce::jmax (1.0f, juce::Component::getApproximateScaleFactorForComponent (this))))
        renderCache();

    if (cache.isValid())
    {
        g.setOpacity (faded ? 0.22f : 1.0f);
        g.drawImage (cache, bounds.toFloat(), juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
    }
    else if (placeholder.isNotEmpty())
    {
        g.setColour (theme::colour (theme::C::screenDim));
        g.setFont (theme::monoFont (11.0f * uiScale));
        g.drawText (placeholder, bounds.reduced (theme::scaled (12.0f, uiScale), 0), juce::Justification::centredLeft, true);
    }
}

} // namespace stemab
