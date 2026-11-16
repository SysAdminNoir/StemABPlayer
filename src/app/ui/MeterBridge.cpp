#include "app/ui/MeterBridge.h"

#include "app/Theme.h"

namespace stemab
{

using theme::C;
using theme::colour;

namespace
{
    constexpr std::array<C, 3> labelColours { C::colourA, C::colourB, C::text };
    const char* const labelText[3] = { "A", "B", "Out" };
    constexpr float scaleMarksDb[] = { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f };
}

MeterBridge::MeterBridge()
{
    for (auto& pair : meters)
        for (auto& m : pair)
            addAndMakeVisible (m);

    const juce::String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    readouts.fill (minus + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")));
    liveNull = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 dB"));
}

void MeterBridge::setUiScale (float s)
{
    uiScale = s;
    resized();
    repaint();
}

void MeterBridge::setLevels (int m, int channel, float peak, float rms)
{
    meters[(size_t) m][(size_t) channel].setLevels (peak, rms);

    // Numeric readouts: the held (falling-back) peak, refreshed ~10 times a second so they stay readable.
    if (m == out && channel == 1 && ++tick % 3 == 0)
    {
        for (size_t i = 0; i < 3; ++i)
        {
            const auto text = theme::dbText (juce::jmax (meters[i][0].getShownPeak(), meters[i][1].getShownPeak()));
            if (text != readouts[i])
            {
                readouts[i] = text;
                repaint (readoutRows[i]);
            }
        }
    }
}

void MeterBridge::setLiveNullDepth (const juce::String& s)
{
    if (s != liveNull)
    {
        liveNull = s;
        repaint (nullBox);
    }
}

void MeterBridge::resized()
{
    const auto s = uiScale;
    const auto px = [s] (float v) { return theme::scaled (v, s); };
    auto r = getLocalBounds();

    levelsLabel = r.removeFromTop (px (24.0f));
    r.removeFromTop (px (10.0f));

    // NULL DEPTH screen (padding 10 12): silk label + the 24 px value
    const int nullH = px (10.0f + 13.0f + 2.0f + 28.0f + 10.0f);
    nullBox = r.removeFromBottom (nullH);
    r.removeFromBottom (px (10.0f));

    // The meter screen (padding 12 8 10): scale column, then three bus meters centred
    meterScreen = r;
    auto inner = r.withTrimmedTop (px (12.0f)).withTrimmedBottom (px (10.0f)).reduced (px (8.0f), 0);

    const int scaleW = px (20.0f), colGap = px (7.0f);
    const int chanW = juce::jmax (4, px (8.0f)), chanGap = juce::jmax (1, px (2.0f)), pad = juce::jmax (2, px (3.0f));
    const int housingW = 2 * pad + 2 * chanW + chanGap;
    const int totalW = scaleW + 3 * (colGap + housingW);
    inner = inner.withSizeKeepingCentre (juce::jmin (inner.getWidth(), totalW), inner.getHeight());

    scaleColumn = inner.removeFromLeft (scaleW);

    const int labelH = px (16.0f), readoutH = px (13.0f);
    for (size_t m = 0; m < 3; ++m)
    {
        inner.removeFromLeft (colGap);
        auto col = inner.removeFromLeft (housingW);
        readoutRows[m] = col.removeFromBottom (readoutH).expanded (px (6.0f), 0);
        meterLabels[m] = col.removeFromBottom (labelH).expanded (px (6.0f), 0);
        col.removeFromBottom (px (4.0f));
        housings[m] = col;

        auto cells = col.reduced (pad);
        meters[m][0].setBounds (cells.removeFromLeft (chanW));
        meters[m][1].setBounds (cells.removeFromRight (chanW));
    }

    // The scale spans exactly the cells.
    scaleColumn = scaleColumn.withY (meters[0][0].getY()).withHeight (meters[0][0].getHeight());
}

void MeterBridge::paint (juce::Graphics& g)
{
    const auto s = uiScale;

    theme::drawSilk (g, "Levels", levelsLabel.toFloat(), 10.0f * s, colour (C::silk));
    theme::drawScreen (g, meterScreen.toFloat(), s);

    // dB scale (9 px mono)
    const auto scaleFont = theme::monoFont (9.0f * s);
    g.setFont (scaleFont);
    g.setColour (colour (C::screenDim));
    const juce::String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    for (const float db : scaleMarksDb)
    {
        const float p = (db - theme::meterFloorDb) / -theme::meterFloorDb;
        const float y = (float) scaleColumn.getBottom() - p * (float) scaleColumn.getHeight();
        const auto row = juce::Rectangle<float> ((float) scaleColumn.getX() - 6.0f * s, y - scaleFont.getHeight() * 0.5f,
                                                 (float) scaleColumn.getWidth() + 6.0f * s, scaleFont.getHeight());
        g.drawText (db < 0.0f ? minus + juce::String ((int) -db) : juce::String ("0"), row, juce::Justification::centredRight, false);
    }

    // Housings: #04080D, inset 0 2px 5px #000, inset 0 0 0 1px #000, 0 1px 0 #ffffff17
    for (size_t m = 0; m < 3; ++m)
    {
        const auto h = housings[m].toFloat();
        const float rad = 3.0f * s;
        g.setColour (juce::Colour (0x17ffffff));
        g.fillRoundedRectangle (h.translated (0.0f, 1.0f), rad);
        g.setColour (colour (C::screen));
        g.fillRoundedRectangle (h, rad);
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path p;
            p.addRoundedRectangle (h, rad);
            g.reduceClipRegion (p);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black, 0.0f, h.getY(),
                                                     juce::Colours::transparentBlack, 0.0f, h.getY() + 6.0f * s, false));
            g.fillRect (h.withHeight (6.0f * s));
        }
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (h.reduced (0.5f), rad, 1.0f);

        theme::drawSilk (g, labelText[m], meterLabels[m].toFloat(), 10.0f * s, colour (labelColours[m]),
                         juce::Justification::centred);
    }

    // Held peak of each bus (9 px mono)
    g.setFont (theme::monoFont (9.0f * s));
    g.setColour (colour (C::screenDim));
    for (size_t m = 0; m < 3; ++m)
        g.drawText (readouts[m], readoutRows[m], juce::Justification::centredTop, false);

    // NULL DEPTH screen
    {
        const auto box = nullBox.toFloat();
        theme::drawScreen (g, box, s);
        auto inner = box.reduced (12.0f * s, 10.0f * s);
        theme::drawSilk (g, "Null depth", inner.removeFromTop (13.0f * s), 10.0f * s, colour (C::silkDim));
        inner.removeFromTop (2.0f * s);

        juce::String value = liveNull, unit;
        if (value.endsWith (" dB"))
        {
            unit = " dB";
            value = value.dropLastCharacters (3);
        }
        const auto big = theme::monoFont (24.0f * s, true);
        const auto small = theme::monoFont (13.0f * s);
        juce::AttributedString text;
        text.append (value, big, colour (C::nullDepth));
        text.append (unit, small, colour (C::muted));
        text.setWordWrap (juce::AttributedString::none);
        text.setJustification (juce::Justification::bottomLeft);
        text.draw (g, inner);
    }
}

void MeterBridge::paintOverChildren (juce::Graphics& g)
{
    // The glass sheen over each housing
    const auto s = uiScale;
    for (const auto& housing : housings)
    {
        const auto h = housing.toFloat();
        juce::Graphics::ScopedSaveState save (g);
        juce::Path p;
        p.addRoundedRectangle (h, 3.0f * s);
        g.reduceClipRegion (p);

        juce::ColourGradient across (juce::Colour (0x14ffffff), h.getX(), 0.0f, juce::Colour (0x40000000), h.getRight(), 0.0f, false);
        across.addColour (0.3, juce::Colour (0x05ffffff));
        across.addColour (0.5, juce::Colour (0x00ffffff));
        g.setGradientFill (across);
        g.fillRect (h);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0x12ffffff), 0.0f, h.getY(),
                                                 juce::Colour (0x00ffffff), 0.0f, h.getY() + h.getHeight() * 0.12f, false));
        g.fillRect (h.withHeight (h.getHeight() * 0.12f));
    }
}

} // namespace stemab
