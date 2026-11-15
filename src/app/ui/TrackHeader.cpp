#include "app/ui/TrackHeader.h"

#include "app/Theme.h"

namespace stemab
{

namespace
{
    void styleMuteSolo (juce::TextButton& b, juce::uint32 onArgb)
    {
        b.setClickingTogglesState (false);   // the engine is the truth; MainComponent sets the toggle state
        b.setColour (juce::TextButton::buttonColourId, theme::colour (theme::raisedArgb));
        b.setColour (juce::TextButton::buttonOnColourId, theme::colour (onArgb));
        b.setColour (juce::TextButton::textColourOffId, theme::colour (theme::mutedTextArgb));
        b.setColour (juce::TextButton::textColourOnId, theme::colour (theme::groundArgb));
        b.getProperties().set ("fontRatio", 0.39);
        b.getProperties().set ("bold", true);
    }
}

TrackHeader::TrackHeader (Style s) : style (std::move (s))
{
    setOpaque (true);

    if (style.hasMuteSolo)
    {
        styleMuteSolo (mute, theme::amberArgb);
        styleMuteSolo (solo, theme::colourBArgb);
        addAndMakeVisible (mute);
        addAndMakeVisible (solo);
    }

    if (style.hasViewGainButton)
    {
        styleMuteSolo (viewGain, theme::residualArgb);
        viewGain.getProperties().set ("fontRatio", 0.36);
        addAndMakeVisible (viewGain);
    }

    if (style.hasMeter)
    {
        meter = std::make_unique<LevelMeter> (LevelMeter::Orientation::vertical, style.colour);
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

void TrackHeader::resized()
{
    auto r = getLocalBounds().withTrimmedBottom (1);
    r.removeFromRight (theme::scaled (10.0f, uiScale));

    if (meter != nullptr)
    {
        const int mw = juce::jmax (3, theme::scaled (5.0f, uiScale));
        const int mh = juce::roundToInt ((float) r.getHeight() * 0.7f);
        meter->setBounds (r.removeFromRight (mw).withSizeKeepingCentre (mw, mh));
        r.removeFromRight (theme::scaled (8.0f, uiScale));
    }

    if (style.hasMuteSolo)
    {
        const int bw = theme::scaled (28.0f, uiScale), bh = theme::scaled (26.0f, uiScale);
        solo.setBounds (r.removeFromRight (bw).withSizeKeepingCentre (bw, bh));
        r.removeFromRight (theme::scaled (8.0f, uiScale));
        mute.setBounds (r.removeFromRight (bw).withSizeKeepingCentre (bw, bh));
    }

    if (style.hasViewGainButton)
    {
        const int bw = theme::scaled (48.0f, uiScale), bh = theme::scaled (22.0f, uiScale);
        viewGain.setBounds (r.removeFromRight (bw).withSizeKeepingCentre (bw, bh));
    }
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.fillAll (style.background);

    g.setColour (theme::colour (theme::hairlineArgb));
    g.fillRect (0, r.getHeight() - 1, r.getWidth(), 1);
    r.removeFromBottom (1);

    g.setColour (style.colour);
    g.fillRect (r.removeFromLeft (juce::jmax (2, theme::scaled (4.0f, uiScale))));
    r.removeFromLeft (theme::scaled (10.0f, uiScale));

    // Text column: from after the colour bar to the first control.
    int right = getWidth() - theme::scaled (10.0f, uiScale);
    if (style.hasMuteSolo)
        right = mute.getX() - theme::scaled (6.0f, uiScale);
    else if (style.hasViewGainButton)
        right = viewGain.getX() - theme::scaled (6.0f, uiScale);
    else if (meter != nullptr)
        right = meter->getX() - theme::scaled (6.0f, uiScale);
    r.setRight (juce::jmax (r.getX(), right));

    const auto nameFont = theme::uiFont (theme::em (style.nameEms, uiScale), true);
    const auto subFont = theme::uiFont (theme::em (0.71f, uiScale));
    const float gap = 3.0f * uiScale;
    const float blockH = nameFont.getHeight() + gap + subFont.getHeight();
    auto text = r.toFloat().withSizeKeepingCentre ((float) r.getWidth(), blockH);

    const float alpha = dimmed ? 0.45f : 1.0f;

    juce::AttributedString name;
    if (style.prefix.isNotEmpty())
        name.append (style.prefix, nameFont, style.colour.withMultipliedAlpha (alpha));
    name.append (style.name, nameFont, theme::colour (theme::textArgb).withMultipliedAlpha (alpha));
    name.setWordWrap (juce::AttributedString::none);
    name.draw (g, text.removeFromTop (nameFont.getHeight()));

    text.removeFromTop (gap);
    g.setFont (subFont);
    g.setColour (theme::colour (theme::mutedTextArgb).withMultipliedAlpha (alpha));
    g.drawText (subLabel, text, juce::Justification::topLeft, true);
}

} // namespace stemab
