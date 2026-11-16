#include "app/ui/ControlDeck.h"

#include <cmath>

namespace stemab
{

using theme::C;
using theme::colour;

namespace
{
    const juce::String middleDot (juce::CharPointer_UTF8 ("\xc2\xb7"));

    // Base metrics at 100 % (the mockup)
    constexpr float padXPx = 18.0f, padYPx = 16.0f, columnGapPx = 26.0f;
    constexpr float transportPx = 380.0f, masterPx = 250.0f, minCrossfadePx = 300.0f;
    constexpr float timeScreenPx = 58.0f, rowGapPx = 12.0f;
    constexpr float keyWPx = 52.0f, playWPx = 76.0f, keyHPx = 44.0f, keyGapPx = 8.0f;
    constexpr float silkRowPx = 14.0f, faderPx = 44.0f, padRowPx = 30.0f;
    constexpr float knobBoxPx = 100.0f, clipPx = 44.0f, labelGapPx = 8.0f;
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

    residualButton.setButtonText ("R residual");
    residualButton.setTooltip ("Listen to the Residual: A minus all four Stems (R)");
    residualButton.setLitColours (C::accent, C::text);

    curveButton.setButtonText ("Equal power");
    curveButton.setTooltip ("Crossfade curve: Equal-power (lit) or Linear (unlit)");
    curveButton.setLitColours (C::amber, C::inkPad);

    centreButton.setButtonText ("Centre");
    centreButton.setTooltip ("Crossfade to A 50 / B 50 (C)");

    for (auto* b : { &residualButton, &curveButton, &centreButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }

    masterSlider.getProperties().set ("style", "knob");
    masterSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    masterSlider.setMouseDragSensitivity (220);
    masterSlider.setTooltip ("Master Gain: drag or scroll (double-click: 0 dB). It never goes above 0 dB.");
    addAndMakeVisible (masterSlider);

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
    // The R pad stays bright: it is how you leave Residual.
    for (auto* c : std::initializer_list<juce::Component*> { &crossfadeSlider, &curveButton, &centreButton })
        c->setAlpha (d ? 0.45f : 1.0f);
    repaint (xfLabelRow);
}

void ControlDeck::setGainText (const juce::String& s)
{
    if (s != gainText)
    {
        gainText = s;
        repaint (masterLabel);
    }
}

void ControlDeck::setUiScale (float s)
{
    uiScale = s;
    for (auto* b : { &toStartButton, &backButton, &playButton, &forwardButton, &loopButton })
        b->setUiScale (s);
    for (auto* b : { &residualButton, &curveButton, &centreButton })
        b->setUiScale (s);
    clipButton.setUiScale (s);

    crossfadeSlider.getProperties().set ("thumbW", theme::scaled (26.0f, s));
    crossfadeSlider.getProperties().set ("thumbH", theme::scaled (42.0f, s));
    crossfadeSlider.getProperties().set ("uiScale", (double) s);
    masterSlider.getProperties().set ("uiScale", (double) s);
    crossfadeSlider.sendLookAndFeelChange();   // re-runs the slider layout with the new thumb indent
    masterSlider.repaint();

    resized();
    repaint();
}

ControlDeck::Metrics ControlDeck::metrics() const
{
    const auto s = uiScale;
    const auto px = [s] (float v) { return theme::scaled (v, s); };
    Metrics m {};
    m.padX = px (padXPx);
    m.padY = px (padYPx);
    m.gap = px (columnGapPx);
    m.transportW = px (transportPx);
    m.masterW = px (masterPx);
    m.transportH = px (timeScreenPx + rowGapPx + keyHPx + 2.0f);
    m.xfH = px (silkRowPx + rowGapPx + faderPx + rowGapPx + padRowPx + 2.0f);
    m.masterH = px (knobBoxPx + labelGapPx + silkRowPx);
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
        return 2 * m.padY + m.xfH + m.padY + juce::jmax (m.transportH, m.masterH);
    return 2 * m.padY + juce::jmax (m.transportH, m.xfH, m.masterH);
}

void ControlDeck::resized()
{
    const auto s = uiScale;
    const auto px = [s] (float v) { return theme::scaled (v, s); };
    const auto m = metrics();
    auto area = getLocalBounds().reduced (m.padX, m.padY);

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

    // 1. Transport: the time screen, then the keys
    {
        auto r = transportArea.withHeight (m.transportH);
        timeBox = r.removeFromTop (px (timeScreenPx));
        r.removeFromTop (px (rowGapPx));

        auto keys = r.removeFromTop (px (keyHPx));
        const int kw = px (keyWPx), pw = px (playWPx), kg = px (keyGapPx);
        const auto place = [&] (IconButton& b, int w)
        {
            b.setFaceBounds (keys.removeFromLeft (w));
            keys.removeFromLeft (kg);
        };
        place (toStartButton, kw);
        place (backButton, kw);
        place (playButton, pw);
        place (forwardButton, kw);
        place (loopButton, kw);
    }

    // 2. Crossfade: labels, fader, pads
    {
        auto r = crossfadeArea.withHeight (m.xfH);
        xfLabelRow = r.removeFromTop (px (silkRowPx));
        r.removeFromTop (px (rowGapPx));

        // The slot spans the column; the travel (and the ticks) is inset by half the cap (the thumb radius).
        crossfadeSlider.setBounds (r.removeFromTop (px (faderPx)));
        r.removeFromTop (px (rowGapPx));

        auto row = r.removeFromTop (px (padRowPx));
        const auto fit = [&] (HwButton& b)
        {
            const auto f = theme::uiFont (11.0f * s, theme::Weight::extraBold);
            const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, b.getButtonText())) + px (24.0f);
            b.setFaceBounds (row.removeFromLeft (w));
            row.removeFromLeft (px (8.0f));
        };
        fit (residualButton);
        fit (curveButton);
        fit (centreButton);
    }

    // 3. Master: the knob column and the clip column, right-aligned and vertically centred
    {
        auto r = masterArea.withSizeKeepingCentre (masterArea.getWidth(), m.masterH);
        const int labelH = px (silkRowPx), gap = px (labelGapPx);

        auto clipCol = r.removeFromRight (juce::jmax (px (clipPx), (int) std::ceil (theme::silkWidth ("Clip", 10.0f * s))));
        r.removeFromRight (px (18.0f));
        const int knobBox = px (knobBoxPx);
        auto knobCol = r.removeFromRight (knobBox);

        masterSlider.setBounds (knobCol.removeFromTop (knobBox));
        knobCol.removeFromTop (gap);
        masterLabel = knobCol.removeFromTop (labelH).expanded (px (40.0f), 0);

        // The clip pad's centre lines up with the knob's
        const int clip = px (clipPx);
        clipButton.setFaceBounds (juce::Rectangle<int> (clip, clip).withCentre ({ clipCol.getCentreX(), masterSlider.getBounds().getCentreY() }));
        clipLabel = juce::Rectangle<int> (clipCol.getX() - px (10.0f), masterLabel.getY(), clipCol.getWidth() + px (20.0f), labelH);
    }
}

void ControlDeck::paint (juce::Graphics& g)
{
    const auto s = uiScale;

    // The panel: #2A2C31 with the inset top highlight
    theme::drawPanel (g, getLocalBounds().toFloat(), s, C::panel, 0.0f, true);

    // Time screen: 36 px time, "Sample ..." / "Total ..." stacked on the right
    {
        const auto box = timeBox.toFloat();
        theme::drawScreen (g, box, s);
        auto inner = box.reduced (14.0f * s, 8.0f * s);

        const auto small = theme::monoFont (11.0f * s);
        const auto lines = juce::StringArray::fromLines (sampleText);
        float linesW = 0.0f;
        for (const auto& line : lines)
            linesW = juce::jmax (linesW, juce::GlyphArrangement::getStringWidth (small, line));

        const auto big = theme::monoFont (36.0f * s, true);
        const float timeW = juce::GlyphArrangement::getStringWidth (big, timeText);
        auto right = inner.withLeft (juce::jmin (inner.getRight(), juce::jmax (inner.getX() + timeW + 12.0f * s, inner.getRight() - linesW)));

        g.setColour (colour (C::screenText));
        g.setFont (big);
        g.drawText (timeText, inner.withRight (right.getX()), juce::Justification::centredLeft, false);

        g.setColour (colour (C::screenDim));
        g.setFont (small);
        const float lineH = 11.0f * 1.4f * s;
        auto block = right.withTop (right.getBottom() - lineH * (float) lines.size() - 2.0f * s);
        for (const auto& line : lines)
            g.drawFittedText (line, block.removeFromTop (lineH).toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
    }

    // Crossfade labels: A · ORIGINAL ... CROSSFADE <readout> ... STEM SUM · B
    {
        const float alpha = crossfadeDimmed ? 0.45f : 1.0f;
        const float px = 10.0f * s;
        const auto row = xfLabelRow.toFloat();
        theme::drawSilk (g, "A " + middleDot + " original", row, px, colour (C::colourA).withMultipliedAlpha (alpha));
        theme::drawSilk (g, "Stem sum " + middleDot + " B", row, px, colour (C::colourB).withMultipliedAlpha (alpha),
                         juce::Justification::centredRight);

        const auto mono = theme::monoFont (10.0f * s);
        const float labelW = theme::silkWidth ("Crossfade", px);
        const float readoutW = juce::GlyphArrangement::getStringWidth (mono, xfReadout);
        const float gap = 8.0f * s;
        auto centre = row.withSizeKeepingCentre (labelW + gap + readoutW + 2.0f, row.getHeight());
        theme::drawSilk (g, "Crossfade", centre.removeFromLeft (labelW + 1.0f), px, colour (C::silk).withMultipliedAlpha (alpha));
        centre.removeFromLeft (gap);
        g.setColour (colour (C::muted).withMultipliedAlpha (alpha));
        g.setFont (mono);
        g.drawText (xfReadout, centre, juce::Justification::centredLeft, false);
    }

    // MASTER <gain>  and  CLIP
    {
        const float px = 10.0f * s;
        const auto mono = theme::monoFont (10.0f * s);
        const float labelW = theme::silkWidth ("Master", px);
        const float valueW = juce::GlyphArrangement::getStringWidth (mono, gainText);
        const float gap = 6.0f * s;
        auto row = masterLabel.toFloat().withSizeKeepingCentre (labelW + gap + valueW + 2.0f, (float) masterLabel.getHeight());
        theme::drawSilk (g, "Master", row.removeFromLeft (labelW + 1.0f), px, colour (C::silk));
        row.removeFromLeft (gap);
        g.setColour (colour (C::text));
        g.setFont (mono);
        g.drawText (gainText, row, juce::Justification::centredLeft, false);

        theme::drawSilk (g, "Clip", clipLabel.toFloat(), px, colour (C::silk), juce::Justification::centred);
    }
}

} // namespace stemab
