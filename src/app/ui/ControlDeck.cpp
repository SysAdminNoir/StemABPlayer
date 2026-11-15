#include "app/ui/ControlDeck.h"

#include <cmath>

#include "app/Theme.h"

namespace stemab
{

namespace
{
    const juce::String middleDot (juce::CharPointer_UTF8 ("\xc2\xb7"));

    // Base metrics at 100 % (the mockup)
    constexpr float padXPx = 20.0f, padYPx = 14.0f, groupGapPx = 24.0f;
    constexpr float iconButtonPx = 40.0f, playButtonPx = 58.0f, buttonGapPx = 6.0f;
    constexpr float timeBoxMinPx = 150.0f;
    constexpr float masterColumnPx = 210.0f, clipPx = 54.0f;
    constexpr float minCrossfadePx = 320.0f;

    void styleDeckButton (juce::TextButton& b, juce::uint32 onFill, juce::uint32 onText, juce::uint32 offText)
    {
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonColourId, theme::colour (theme::raisedArgb));
        b.setColour (juce::TextButton::buttonOnColourId, theme::colour (onFill));
        b.setColour (juce::TextButton::textColourOnId, theme::colour (onText));
        b.setColour (juce::TextButton::textColourOffId, theme::colour (offText));
        b.getProperties().set ("fontRatio", 0.35);
    }
}

ControlDeck::ControlDeck()
{
    setOpaque (true);

    for (auto* b : { &toStartButton, &backButton, &playButton, &forwardButton, &loopButton })
        addAndMakeVisible (b);
    loopButton.setClickingTogglesState (true);

    crossfadeSlider.getProperties().set ("style", "crossfade");
    crossfadeSlider.setTooltip ("Crossfade between A (Original) and B (Stem Sum). Double-click: centre. Keys A / B / C.");
    addAndMakeVisible (crossfadeSlider);

    residualButton.setButtonText (juce::String ("R ") + middleDot + " Residual");
    residualButton.setTooltip ("Listen to the Residual: A minus all four Stems (R)");
    styleDeckButton (residualButton, theme::residualArgb, theme::groundArgb, theme::buttonTextArgb);

    curveButton.setButtonText ("Equal-power");
    curveButton.setTooltip ("Crossfade curve: Equal-power (on) or Linear (off)");
    styleDeckButton (curveButton, theme::toggleOnArgb, 0xffffffff, theme::mutedTextArgb);

    centreButton.setButtonText ("Centre");
    centreButton.setTooltip ("Crossfade to A 50 / B 50 (C)");
    styleDeckButton (centreButton, theme::raisedArgb, theme::buttonTextArgb, theme::buttonTextArgb);

    for (auto* b : { &residualButton, &curveButton, &centreButton })
        addAndMakeVisible (b);

    masterSlider.getProperties().set ("style", "fader");
    masterSlider.setTooltip ("Master Gain (double-click: 0 dB). It never goes above 0 dB.");
    addAndMakeVisible (masterSlider);

    for (auto& m : outMeters)
    {
        m.setOrientation (LevelMeter::Orientation::horizontal);
        m.setBackgroundColour (theme::colour (theme::raisedArgb));
        addAndMakeVisible (m);
    }

    addAndMakeVisible (clipButton);
}

void ControlDeck::setTime (const juce::String& time, const juce::String& samples)
{
    if (time != timeText || samples != sampleText)
    {
        timeText = time;
        sampleText = samples;
        repaint (timeBox);
    }
}

void ControlDeck::setCrossfadeReadout (const juce::String& s)
{
    if (s != xfReadout)
    {
        xfReadout = s;
        repaint (xfLabelRow);
    }
}

void ControlDeck::setCrossfadeDimmed (bool d)
{
    if (d == crossfadeDimmed)
        return;
    crossfadeDimmed = d;
    // The R button stays bright: it is how you leave Residual.
    for (auto* c : std::initializer_list<juce::Component*> { &crossfadeSlider, &curveButton, &centreButton })
        c->setAlpha (d ? 0.45f : 1.0f);
    repaint (xfLabelRow);
    repaint (hintArea);
}

void ControlDeck::setGainText (const juce::String& s)
{
    if (s != gainText)
    {
        gainText = s;
        repaint (masterLabelRow);
    }
}

void ControlDeck::setUiScale (float s)
{
    uiScale = s;
    for (auto* b : { &toStartButton, &backButton, &playButton, &forwardButton, &loopButton })
        b->setUiScale (s);
    clipButton.setUiScale (s);

    crossfadeSlider.getProperties().set ("thumbW", theme::scaled (22.0f, s));
    crossfadeSlider.getProperties().set ("thumbH", theme::scaled (38.0f, s));
    masterSlider.getProperties().set ("thumbW", theme::scaled (16.0f, s));
    masterSlider.getProperties().set ("thumbH", theme::scaled (28.0f, s));
    crossfadeSlider.sendLookAndFeelChange();   // re-runs the slider layout with the new thumb indent
    masterSlider.sendLookAndFeelChange();

    resized();
    repaint();
}

ControlDeck::Metrics ControlDeck::metrics() const
{
    const auto s = uiScale;
    Metrics m {};
    m.padX = theme::scaled (padXPx, s);
    m.padY = theme::scaled (padYPx, s);
    m.gap = theme::scaled (groupGapPx, s);

    const auto timeFont = theme::monoFont (theme::em (1.55f, s), true);
    const int timeW = juce::jmax (theme::scaled (timeBoxMinPx, s),
                                  (int) std::ceil (juce::GlyphArrangement::getStringWidth (timeFont, "00:00.000")) + theme::scaled (24.0f, s));
    m.transportW = 4 * theme::scaled (iconButtonPx, s) + theme::scaled (playButtonPx, s) + 4 * theme::scaled (buttonGapPx, s)
                   + theme::scaled (16.0f, s) + timeW;
    m.masterW = theme::scaled (14.0f + masterColumnPx + 14.0f + clipPx + 14.0f, s);

    const int labelH = juce::roundToInt (theme::uiFont (theme::em (0.72f, s), true).getHeight());
    m.xfH = labelH + theme::scaled (6.0f + 40.0f + 6.0f + 30.0f, s);

    const int gainH = juce::roundToInt (theme::monoFont (theme::em (0.95f, s), true).getHeight());
    m.masterH = theme::scaled (20.0f, s) + gainH + theme::scaled (4.0f + 32.0f + 4.0f + 5.0f + 3.0f + 5.0f, s);
    m.rowH = juce::jmax (theme::scaled (playButtonPx, s), m.masterH);
    return m;
}

bool ControlDeck::needsTwoRows (int width) const
{
    const auto m = metrics();
    return width - 2 * m.padX - m.transportW - m.masterW - 2 * m.gap < theme::scaled (minCrossfadePx, uiScale);
}

int ControlDeck::getPreferredHeight (int width) const
{
    const auto m = metrics();
    if (needsTwoRows (width))
        return 2 * m.padY + m.xfH + m.padY + m.rowH;
    return 2 * m.padY + juce::jmax (m.rowH, m.xfH);
}

void ControlDeck::resized()
{
    const auto s = uiScale;
    const auto m = metrics();
    auto area = getLocalBounds().reduced (m.padX, m.padY);
    area.removeFromTop (1);   // top border

    juce::Rectangle<int> transportArea, crossfadeArea, masterArea;
    if (needsTwoRows (getWidth()))
    {
        crossfadeArea = area.removeFromTop (m.xfH);
        area.removeFromTop (m.padY);
        transportArea = area.removeFromLeft (m.transportW);
        masterArea = area.removeFromRight (m.masterW);
    }
    else
    {
        transportArea = area.removeFromLeft (m.transportW);
        area.removeFromLeft (m.gap);
        masterArea = area.removeFromRight (m.masterW);
        area.removeFromRight (m.gap);
        crossfadeArea = area;
    }

    // 1. Transport
    {
        auto r = transportArea.withSizeKeepingCentre (transportArea.getWidth(), theme::scaled (playButtonPx, s));
        const int ib = theme::scaled (iconButtonPx, s), pb = theme::scaled (playButtonPx, s), bg = theme::scaled (buttonGapPx, s);
        const auto placeIcon = [&] (juce::Component& c, int size)
        {
            c.setBounds (r.removeFromLeft (size).withSizeKeepingCentre (size, size));
            r.removeFromLeft (bg);
        };
        placeIcon (toStartButton, ib);
        placeIcon (backButton, ib);
        placeIcon (playButton, pb);
        placeIcon (forwardButton, ib);
        placeIcon (loopButton, ib);
        r.removeFromLeft (theme::scaled (16.0f, s) - bg);

        const int timeH = theme::scaled (12.0f + 2.0f, s)
                          + juce::roundToInt (theme::monoFont (theme::em (1.55f, s), true).getHeight()
                                              + 2.0f * theme::monoFont (theme::em (0.66f, s)).getHeight());
        timeBox = r.withSizeKeepingCentre (r.getWidth(), timeH);
    }

    // 2. Crossfade
    {
        auto r = crossfadeArea.withSizeKeepingCentre (crossfadeArea.getWidth(), m.xfH);
        xfLabelRow = r.removeFromTop (juce::roundToInt (theme::uiFont (theme::em (0.72f, s), true).getHeight()));
        r.removeFromTop (theme::scaled (6.0f, s));
        crossfadeSlider.setBounds (r.removeFromTop (theme::scaled (40.0f, s)));
        r.removeFromTop (theme::scaled (6.0f, s));

        auto row = r.removeFromTop (theme::scaled (30.0f, s));
        const auto fit = [&] (juce::TextButton& b)
        {
            const auto f = theme::uiFont ((float) row.getHeight() * 0.35f);
            const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, b.getButtonText())) + theme::scaled (20.0f, s);
            b.setBounds (row.removeFromLeft (w));
            row.removeFromLeft (theme::scaled (8.0f, s));
        };
        fit (residualButton);
        fit (curveButton);
        fit (centreButton);
        hintArea = row;
    }

    // 3. Master
    {
        masterBox = masterArea.withSizeKeepingCentre (masterArea.getWidth(), m.masterH);
        auto r = masterBox.reduced (theme::scaled (14.0f, s), theme::scaled (10.0f, s));

        const int clip = theme::scaled (clipPx, s);
        clipButton.setBounds (r.removeFromRight (clip).withSizeKeepingCentre (clip, clip));
        r.removeFromRight (theme::scaled (14.0f, s));

        masterLabelRow = r.removeFromTop (juce::roundToInt (theme::monoFont (theme::em (0.95f, s), true).getHeight()));
        r.removeFromTop (theme::scaled (4.0f, s));
        masterSlider.setBounds (r.removeFromTop (theme::scaled (32.0f, s)));
        r.removeFromTop (theme::scaled (4.0f, s));
        const int mh = juce::jmax (3, theme::scaled (5.0f, s));
        outMeters[0].setBounds (r.removeFromTop (mh));
        r.removeFromTop (theme::scaled (3.0f, s));
        outMeters[1].setBounds (r.removeFromTop (mh));
    }
}

void ControlDeck::paint (juce::Graphics& g)
{
    using theme::colour;
    const auto s = uiScale;

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff181C22), 0.0f, 0.0f,
                                             colour (theme::panelArgb), 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour (colour (theme::hairline2Argb));
    g.fillRect (0, 0, getWidth(), 1);

    // Time readout
    {
        const auto box = timeBox.toFloat();
        g.setColour (colour (theme::groundArgb));
        g.fillRoundedRectangle (box, 6.0f * s);
        g.setColour (colour (theme::hairlineArgb));
        g.drawRoundedRectangle (box.reduced (0.5f), 6.0f * s, 1.0f);

        auto inner = box.reduced (12.0f * s, 6.0f * s);
        const auto big = theme::monoFont (theme::em (1.55f, s), true);
        const auto small = theme::monoFont (theme::em (0.66f, s));
        g.setColour (colour (theme::textArgb));
        g.setFont (big);
        g.drawText (timeText, inner.removeFromTop (big.getHeight()), juce::Justification::centredLeft, false);
        inner.removeFromTop (2.0f * s);
        g.setColour (colour (theme::mutedTextArgb));
        g.setFont (small);
        // Two stacked lines ("Sample: N" / "Total: N"), split on '\n'.
        for (const auto& line : juce::StringArray::fromLines (sampleText))
            g.drawText (line, inner.removeFromTop (small.getHeight()), juce::Justification::centredLeft, true);
    }

    // Crossfade labels and hint
    {
        const float alpha = crossfadeDimmed ? 0.45f : 1.0f;
        const auto f = theme::uiFont (theme::em (0.72f, s), true);
        const auto row = xfLabelRow.toFloat();
        g.setColour (colour (theme::colourAArgb).withMultipliedAlpha (alpha));
        theme::drawTrackedText (g, "A " + middleDot + " ORIGINAL", row, f, 0.1f, juce::Justification::centredLeft);
        g.setColour (colour (theme::colourBArgb).withMultipliedAlpha (alpha));
        theme::drawTrackedText (g, "STEM SUM " + middleDot + " B", row, f, 0.1f, juce::Justification::centredRight);
        g.setColour (colour (theme::mutedTextArgb).withMultipliedAlpha (alpha));
        g.setFont (theme::monoFont (theme::em (0.72f, s)));
        g.drawText (xfReadout, row, juce::Justification::centred, false);

        const auto hintFont = theme::uiFont (theme::em (0.7f, s));
        const juce::String hint ("Click or drag in any waveform to move the playhead");
        if (juce::GlyphArrangement::getStringWidth (hintFont, hint) <= (float) hintArea.getWidth())
        {
            g.setFont (hintFont);
            g.drawText (hint, hintArea, juce::Justification::centredRight, false);
        }
    }

    // Master box
    {
        const auto box = masterBox.toFloat();
        g.setColour (colour (theme::groundArgb));
        g.fillRoundedRectangle (box, 8.0f * s);
        g.setColour (colour (theme::hairlineArgb));
        g.drawRoundedRectangle (box.reduced (0.5f), 8.0f * s, 1.0f);

        const auto row = masterLabelRow.toFloat();
        g.setColour (colour (theme::mutedTextArgb));
        theme::drawTrackedText (g, "MASTER", row, theme::uiFont (theme::em (0.68f, s)), 0.12f, juce::Justification::centredLeft);
        g.setColour (colour (theme::amberArgb));
        g.setFont (theme::monoFont (theme::em (0.95f, s), true));
        g.drawText (gainText, row, juce::Justification::centredRight, false);
    }
}

} // namespace stemab
