#include "app/ui/MeterBridge.h"

#include "app/Theme.h"

namespace stemab
{

namespace
{
    constexpr std::array<juce::uint32, 3> labelArgb { theme::colourAArgb, theme::colourBArgb, theme::amberArgb };
    const char* const labelText[3] = { "A", "B", "OUT" };
    constexpr float scaleMarksDb[] = { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -60.0f };
}

MeterBridge::MeterBridge()
{
    setOpaque (true);
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
        bool changed = false;
        for (size_t i = 0; i < 3; ++i)
        {
            const auto text = theme::dbText (juce::jmax (meters[i][0].getShownPeak(), meters[i][1].getShownPeak()));
            if (text != readouts[i])
            {
                readouts[i] = text;
                changed = true;
            }
        }
        if (changed)
            repaint (readoutRow);
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
    auto r = getLocalBounds().withTrimmedLeft (1).reduced (theme::scaled (12.0f, s));
    const int gap = theme::scaled (12.0f, s);

    levelsLabel = r.removeFromTop (juce::roundToInt (theme::uiFont (theme::em (0.68f, s)).getHeight()));
    r.removeFromTop (gap);

    const int readoutH = juce::roundToInt (theme::monoFont (theme::em (0.64f, s)).getHeight());
    readoutRow = r.removeFromBottom (readoutH);
    r.removeFromBottom (gap);

    const int nullH = theme::scaled (16.0f, s) + juce::roundToInt (theme::uiFont (theme::em (0.64f, s)).getHeight())
                      + theme::scaled (2.0f, s) + juce::roundToInt (theme::monoFont (theme::em (1.25f, s), true).getHeight());
    nullBox = r.removeFromBottom (nullH);
    r.removeFromBottom (gap);

    // Meters: scale column + three stereo meters with a label underneath.
    const int labelH = juce::roundToInt (theme::uiFont (theme::em (0.72f, s), true).getHeight());
    scaleColumn = r.removeFromLeft (theme::scaled (24.0f, s));
    r.removeFromLeft (theme::scaled (6.0f, s));

    const int colGap = theme::scaled (10.0f, s);
    const int colW = (r.getWidth() - 2 * colGap) / 3;
    const int barGap = theme::scaled (3.0f, s);

    for (int m = 0; m < 3; ++m)
    {
        auto col = r.removeFromLeft (colW);
        if (m < 2)
            r.removeFromLeft (colGap);

        meterLabels[(size_t) m] = col.removeFromBottom (labelH);
        col.removeFromBottom (theme::scaled (6.0f, s));

        const int barW = (col.getWidth() - barGap) / 2;
        meters[(size_t) m][0].setBounds (col.removeFromLeft (barW));
        meters[(size_t) m][1].setBounds (col.removeFromRight (barW));
    }

    // The scale spans exactly the bars.
    scaleColumn = scaleColumn.withY (meters[0][0].getY()).withHeight (meters[0][0].getHeight());
}

void MeterBridge::paint (juce::Graphics& g)
{
    using theme::colour;
    const auto s = uiScale;

    g.fillAll (colour (theme::panelArgb));
    g.setColour (colour (theme::hairlineArgb));
    g.fillRect (0, 0, 1, getHeight());

    g.setColour (colour (theme::mutedTextArgb));
    theme::drawTrackedText (g, "LEVELS", levelsLabel.toFloat(), theme::uiFont (theme::em (0.68f, s)), 0.12f,
                            juce::Justification::centredLeft);

    // dB scale
    const auto scaleFont = theme::monoFont (theme::em (0.6f, s));
    g.setFont (scaleFont);
    const juce::String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    for (const float db : scaleMarksDb)
    {
        const float p = (db - theme::meterFloorDb) / -theme::meterFloorDb;
        const float y = (float) scaleColumn.getBottom() - p * (float) scaleColumn.getHeight();
        const auto row = juce::Rectangle<float> ((float) scaleColumn.getX(), y - scaleFont.getHeight() * 0.5f,
                                                 (float) scaleColumn.getWidth(), scaleFont.getHeight())
                             .constrainedWithin (getLocalBounds().toFloat());
        g.setColour (colour (theme::mutedTextArgb));
        g.drawText (db < 0.0f ? minus + juce::String ((int) -db) : juce::String ("0"), row, juce::Justification::centredRight, false);
    }

    // Meter labels
    const auto labelFont = theme::uiFont (theme::em (0.72f, s), true);
    g.setFont (labelFont);
    for (int m = 0; m < 3; ++m)
    {
        g.setColour (colour (labelArgb[(size_t) m]));
        g.drawText (labelText[m], meterLabels[(size_t) m], juce::Justification::centred, false);
    }

    // Live Null Depth
    const auto box = nullBox.toFloat();
    g.setColour (colour (theme::groundArgb));
    g.fillRoundedRectangle (box, 6.0f * s);
    g.setColour (colour (theme::hairlineArgb));
    g.drawRoundedRectangle (box.reduced (0.5f), 6.0f * s, 1.0f);

    auto inner = box.reduced (10.0f * s, 8.0f * s);
    const auto capFont = theme::uiFont (theme::em (0.64f, s));
    g.setColour (colour (theme::mutedTextArgb));
    theme::drawTrackedText (g, "LIVE NULL DEPTH", inner.removeFromTop (capFont.getHeight()), capFont, 0.1f,
                            juce::Justification::centredLeft);
    inner.removeFromTop (2.0f * s);
    g.setColour (colour (theme::residualArgb));
    g.setFont (theme::monoFont (theme::em (1.25f, s), true));
    g.drawText (liveNull, inner, juce::Justification::centredLeft, true);

    // Peak readouts, one under each meter
    g.setFont (theme::monoFont (theme::em (0.64f, s)));
    g.setColour (colour (theme::buttonTextArgb));
    for (int m = 0; m < 3; ++m)
    {
        const auto& lab = meterLabels[(size_t) m];
        g.drawText (readouts[(size_t) m], juce::Rectangle<int> (lab.getX() - 4, readoutRow.getY(), lab.getWidth() + 8, readoutRow.getHeight()),
                    juce::Justification::centred, false);
    }
}

} // namespace stemab
