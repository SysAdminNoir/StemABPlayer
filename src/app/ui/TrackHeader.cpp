#include "app/ui/TrackHeader.h"

namespace stemab
{

using theme::C;
using theme::colour;

TrackHeader::TrackHeader (Style s) : style (std::move (s))
{
    if (style.hasMuteSolo)
    {
        for (auto* b : { &mute, &solo })
        {
            b->setClickingTogglesState (false);   // the engine is the truth; MainComponent sets the toggle state
            addAndMakeVisible (b);
        }
        mute.setLitColours (C::amber, C::inkPad);
        solo.setLitColours (C::colourB, C::inkDark);
    }

    if (style.hasViewGainButton)
    {
        viewGain.setClickingTogglesState (false);
        viewGain.setLitColours (C::accent, C::text);
        addAndMakeVisible (viewGain);
    }

    if (style.hasMeter)
    {
        meter = std::make_unique<LevelMeter> (LevelMeter::Orientation::vertical);
        addAndMakeVisible (*meter);
    }
}

void TrackHeader::setSubLabel (const juce::String& s)
{
    if (s != subLabel)
    {
        subLabel = s;
        repaint();
    }
}

void TrackHeader::setUiScale (float s)
{
    uiScale = s;
    for (auto* b : { &mute, &solo, &viewGain })
        b->setUiScale (s);
    resized();
    repaint();
}

void TrackHeader::setDimmed (bool d)
{
    if (d != dimmed)
    {
        dimmed = d;
        repaint();
    }
}

juce::Rectangle<int> TrackHeader::strip() const
{
    return getLocalBounds().reduced (0, theme::scaled (3.0f, uiScale));
}

void TrackHeader::resized()
{
    const float s = uiScale;
    const auto px = [s] (float v) { return theme::scaled (v, s); };

    auto r = strip();
    r.removeFromLeft (px (12.0f));
    r.removeFromRight (px (10.0f));

    // Colour bar + gap
    r.removeFromLeft (px (4.0f) + px (style.kind == Kind::original ? 10.0f : 8.0f));

    if (meter != nullptr)
    {
        // A thin LED meter in a small sunken well at the right edge
        const int ww = juce::jmax (5, px (8.0f));
        const int wh = juce::jmax (12, strip().getHeight() - px (style.kind == Kind::original ? 28.0f : 20.0f));
        meterWell = r.removeFromRight (ww).withSizeKeepingCentre (ww, wh);
        meter->setBounds (meterWell.reduced (juce::jmax (1, px (1.5f))));
        r.removeFromRight (px (8.0f));
    }

    const int pad = px (30.0f);
    if (style.hasMuteSolo)
    {
        solo.setFaceBounds (r.removeFromRight (pad).withSizeKeepingCentre (pad, pad));
        r.removeFromRight (px (8.0f));
        mute.setFaceBounds (r.removeFromRight (pad).withSizeKeepingCentre (pad, pad));
        r.removeFromRight (px (8.0f));
    }

    if (style.hasViewGainButton)
    {
        const int w = px (46.0f);
        const int h = juce::jmin (pad, juce::jmax (px (18.0f), strip().getHeight() - px (10.0f)));
        viewGain.setFaceBounds (r.removeFromRight (w).withSizeKeepingCentre (w, h));
        r.removeFromRight (px (8.0f));
    }

    textArea = r;
}

void TrackHeader::paint (juce::Graphics& g)
{
    const float s = uiScale;
    const auto box = strip().toFloat();

    // The strip: raised #2A2C31 (radius 6, inset top highlight); the Residual's is flatter (#26282C).
    if (style.kind == Kind::residual)
        theme::drawPanel (g, box, s, C::residualStrip, 6.0f, false);
    else
        theme::drawPanel (g, box, s, C::panel, 6.0f, true);

    const float alpha = dimmed ? 0.45f : 1.0f;

    // Colour bar
    {
        const float barW = juce::jmax (2.0f, 4.0f * s);
        const float barH = style.kind == Kind::original ? box.getHeight() - 28.0f * s
                         : style.kind == Kind::stem     ? 36.0f * s
                                                        : 20.0f * s;
        const auto bar = juce::Rectangle<float> (box.getX() + 12.0f * s, box.getCentreY() - juce::jmax (4.0f, barH) * 0.5f,
                                                 barW, juce::jmax (4.0f, juce::jmin (barH, box.getHeight() - 8.0f * s)));
        g.setColour (colour (style.colour).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (bar, barW * 0.5f);
    }

    // Meter well
    if (meter != nullptr)
    {
        g.setColour (colour (C::screen));
        g.fillRoundedRectangle (meterWell.toFloat(), 2.0f * s);
    }

    // Name (+ mono sub-label when it fits)
    const float namePx = (style.kind == Kind::original ? 15.0f : style.kind == Kind::stem ? 14.0f : 13.0f) * s;
    const auto nameFont = theme::uiFont (namePx, theme::Weight::bold);
    const auto subFont = theme::monoFont (11.0f * s);
    const float gap = 3.0f * s;
    const bool withSub = subLabel.isNotEmpty()
                         && nameFont.getHeight() + gap + subFont.getHeight() <= (float) textArea.getHeight() - 4.0f * s;
    const float blockH = nameFont.getHeight() + (withSub ? gap + subFont.getHeight() : 0.0f);
    auto text = textArea.toFloat().withSizeKeepingCentre ((float) textArea.getWidth(), blockH);

    g.setColour (colour (C::text).withMultipliedAlpha (alpha));
    g.setFont (nameFont);
    g.drawText (style.name, text.removeFromTop (nameFont.getHeight()), juce::Justification::centredLeft, true);

    if (withSub)
    {
        text.removeFromTop (gap);
        g.setFont (subFont);
        g.setColour (colour (C::muted).withMultipliedAlpha (alpha));
        g.drawText (subLabel, text, juce::Justification::topLeft, true);
    }
}

} // namespace stemab
