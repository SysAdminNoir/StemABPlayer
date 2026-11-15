#include "app/ui/HeaderBar.h"

#include "app/Theme.h"

namespace stemab
{

namespace
{
    const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));

    void styleHeaderButton (juce::TextButton& b, double fontRatio)
    {
        b.getProperties().set ("fontRatio", fontRatio);
    }
}

HeaderBar::HeaderBar()
{
    setOpaque (true);

    openOriginalButton.setButtonText ("Open original" + ellipsis);
    openStemButton.setButtonText ("Open stem file" + ellipsis);
    settingsButton.setButtonText ("Preferences" + ellipsis);
    scaleDownButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("A\xe2\x88\x92")));
    scaleUpButton.setButtonText ("A+");

    openOriginalButton.setTooltip ("Open the Original (A): the mastered track");
    openStemButton.setTooltip ("Open the Stem File (B): an NI .stem.m4a");
    settingsButton.setTooltip ("Preferences: folders, interface size, audio device, session (Ctrl+,)");
    scaleDownButton.setTooltip ("Smaller interface (Ctrl+-)");
    scaleUpButton.setTooltip ("Larger interface (Ctrl+=); Ctrl+0 resets");

    for (auto* b : { &openOriginalButton, &openStemButton, &settingsButton, &scaleDownButton, &scaleUpButton })
    {
        styleHeaderButton (*b, 0.36);
        addAndMakeVisible (b);
    }
}

void HeaderBar::setFileNames (const juce::String& original, const juce::String& stemFile)
{
    if (original != originalName || stemFile != stemFileName)
    {
        originalName = original;
        stemFileName = stemFile;
        repaint();
    }
}

juce::String HeaderBar::keyOf (const juce::Array<Segment>& segs)
{
    juce::String k;
    for (const auto& s : segs)
        k << s.text << "|" << juce::String::toHexString ((int) s.colour.getARGB()) << (s.mono ? "m" : "") << (s.dot ? "d" : "") << "\n";
    return k;
}

void HeaderBar::setStatus (const juce::Array<Segment>& left, const juce::Array<Segment>& right)
{
    const auto key = keyOf (left) + "||" + keyOf (right);
    if (key == statusKey)
        return;
    statusKey = key;
    leftStatus = left;
    rightStatus = right;
    repaint (statusRow);
}

void HeaderBar::setScaleLabel (const juce::String& s)
{
    scaleLabel = s;
    repaint (scaleLabelArea);
}

void HeaderBar::setUiScale (float s)
{
    uiScale = s;
    resized();
    repaint();
}

int HeaderBar::getPreferredHeight() const
{
    // padding 10 + row 30 + gap 6 + status line + padding 8
    return theme::scaled (10.0f + 30.0f + 6.0f + 8.0f, uiScale)
           + juce::roundToInt (theme::uiFont (theme::em (0.78f, uiScale)).getHeight());
}

void HeaderBar::resized()
{
    const auto s = uiScale;
    auto r = getLocalBounds().withTrimmedBottom (1);
    r.removeFromTop (theme::scaled (10.0f, s));
    r.removeFromBottom (theme::scaled (8.0f, s));
    r.reduce (theme::scaled (16.0f, s), 0);

    auto row = r.removeFromTop (theme::scaled (30.0f, s));
    r.removeFromTop (theme::scaled (6.0f, s));
    statusRow = r;

    // Left: logo + title
    logoArea = row.removeFromLeft (theme::scaled (28.0f, s));
    row.removeFromLeft (theme::scaled (10.0f, s));
    const auto titleFont = theme::uiFont (theme::em (1.05f, s), true).withExtraKerningFactor (0.08f);
    const auto subFont = theme::uiFont (theme::em (0.68f, s)).withExtraKerningFactor (0.12f);
    const int titleW = (int) std::ceil (juce::jmax (juce::GlyphArrangement::getStringWidth (titleFont, "STEM A/B"),
                                                    juce::GlyphArrangement::getStringWidth (subFont, "CRITICAL LISTENING")));
    titleArea = row.removeFromLeft (titleW);
    row.removeFromLeft (theme::scaled (16.0f, s));

    // Right: scale buttons (after a hairline), then the file / settings buttons
    const int bh = theme::scaled (30.0f, s);
    const auto place = [&] (juce::TextButton& b, int w)
    {
        b.setBounds (row.removeFromRight (w).withSizeKeepingCentre (w, bh));
    };
    const auto fitWidth = [&] (juce::TextButton& b)
    {
        const auto f = theme::uiFont ((float) bh * 0.36f);
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, b.getButtonText())) + theme::scaled (20.0f, s);
    };

    place (scaleUpButton, fitWidth (scaleUpButton));
    scaleLabelArea = row.removeFromRight (theme::scaled (42.0f, s));
    place (scaleDownButton, fitWidth (scaleDownButton));
    row.removeFromRight (theme::scaled (12.0f, s));
    separatorX = row.getRight();
    row.removeFromRight (theme::scaled (12.0f, s));

    place (settingsButton, fitWidth (settingsButton));
    row.removeFromRight (theme::scaled (6.0f, s));
    place (openStemButton, fitWidth (openStemButton));
    row.removeFromRight (theme::scaled (6.0f, s));
    place (openOriginalButton, fitWidth (openOriginalButton));
    row.removeFromRight (theme::scaled (16.0f, s));

    // Middle: the two file chips share what is left
    const int chipGap = theme::scaled (8.0f, s);
    const int chipW = juce::jmax (0, (row.getWidth() - chipGap) / 2);
    chipA = row.removeFromLeft (chipW);
    row.removeFromLeft (chipGap);
    chipB = row.removeFromLeft (chipW);
}

float HeaderBar::segmentsWidth (const juce::Array<Segment>& segs) const
{
    const auto textFont = theme::uiFont (theme::em (0.78f, uiScale));
    const auto monoFont = theme::monoFont (theme::em (0.78f, uiScale));
    float w = 0.0f;
    for (const auto& seg : segs)
        w += seg.dot ? 13.0f * uiScale : juce::GlyphArrangement::getStringWidth (seg.mono ? monoFont : textFont, seg.text);
    return w;
}

void HeaderBar::drawSegments (juce::Graphics& g, const juce::Array<Segment>& segs, juce::Rectangle<float> area,
                              bool alignRight) const
{
    const auto textFont = theme::uiFont (theme::em (0.78f, uiScale));
    const auto monoFont = theme::monoFont (theme::em (0.78f, uiScale));

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.toNearestInt());

    const float total = segmentsWidth (segs);
    float x = alignRight && total <= area.getWidth() ? area.getRight() - total : area.getX();
    for (const auto& seg : segs)
    {
        if (seg.dot)
        {
            const float d = 7.0f * uiScale;
            g.setColour (seg.colour);
            g.fillEllipse (x, area.getCentreY() - d * 0.5f, d, d);
            x += 13.0f * uiScale;
            continue;
        }

        const auto& f = seg.mono ? monoFont : textFont;
        const float w = juce::GlyphArrangement::getStringWidth (f, seg.text);
        g.setFont (f);
        g.setColour (seg.colour);
        g.drawText (seg.text, juce::Rectangle<float> (x, area.getY(), w + 2.0f, area.getHeight()),
                    juce::Justification::centredLeft, false);
        x += w;
    }
}

void HeaderBar::drawChip (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& badge, juce::Colour badgeColour,
                          const juce::String& name, const juce::String& emptyText) const
{
    using theme::colour;
    const auto s = uiScale;
    if (area.getWidth() < theme::scaled (40.0f, s))
        return;

    auto chip = area.toFloat().withSizeKeepingCentre ((float) area.getWidth(), 28.0f * s);
    g.setColour (colour (theme::groundArgb));
    g.fillRoundedRectangle (chip, 6.0f * s);
    g.setColour (colour (theme::hairlineArgb));
    g.drawRoundedRectangle (chip.reduced (0.5f), 6.0f * s, 1.0f);

    auto inner = chip.reduced (10.0f * s, 0.0f);
    const auto badgeFont = theme::uiFont (theme::em (0.75f, s), true);
    const float bw = juce::GlyphArrangement::getStringWidth (badgeFont, badge) + 10.0f * s;
    const auto b = inner.removeFromLeft (bw).withSizeKeepingCentre (bw, badgeFont.getHeight() + 2.0f * s);
    g.setColour (badgeColour);
    g.fillRoundedRectangle (b, 3.0f * s);
    g.setColour (colour (theme::groundArgb));
    g.setFont (badgeFont);
    g.drawText (badge, b, juce::Justification::centred, false);

    inner.removeFromLeft (8.0f * s);
    g.setFont (theme::uiFont (theme::em (0.82f, s)));
    g.setColour (name.isNotEmpty() ? colour (theme::textArgb) : colour (theme::mutedTextArgb));
    g.drawText (name.isNotEmpty() ? name : emptyText, inner, juce::Justification::centredLeft, true);
}

void HeaderBar::paint (juce::Graphics& g)
{
    using theme::colour;
    const auto s = uiScale;

    g.fillAll (colour (theme::panelArgb));
    g.setColour (colour (theme::hairlineArgb));
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    // Logo: two overlapping rings, ivory (A) and blue (B), in a 28 x 28 box
    {
        const auto box = logoArea.toFloat().withSizeKeepingCentre (28.0f * s, 28.0f * s);
        const float k = box.getWidth() / 28.0f, rad = 8.0f * k;
        g.setColour (colour (theme::colourAArgb));
        g.drawEllipse (box.getX() + (11.0f * k) - rad, box.getY() + 14.0f * k - rad, 2.0f * rad, 2.0f * rad, 2.0f * k);
        g.setColour (colour (theme::colourBArgb));
        g.drawEllipse (box.getX() + (17.0f * k) - rad, box.getY() + 14.0f * k - rad, 2.0f * rad, 2.0f * rad, 2.0f * k);
    }

    // Title
    {
        const auto titleFont = theme::uiFont (theme::em (1.05f, s), true);
        const auto subFont = theme::uiFont (theme::em (0.68f, s));
        const float blockH = titleFont.getHeight() + subFont.getHeight();
        auto t = titleArea.toFloat().withSizeKeepingCentre ((float) titleArea.getWidth(), blockH);
        g.setColour (colour (theme::textArgb));
        theme::drawTrackedText (g, "STEM A/B", t.removeFromTop (titleFont.getHeight()), titleFont, 0.08f,
                                juce::Justification::centredLeft);
        g.setColour (colour (theme::mutedTextArgb));
        theme::drawTrackedText (g, "CRITICAL LISTENING", t, subFont, 0.12f, juce::Justification::centredLeft);
    }

    drawChip (g, chipA, "A", colour (theme::colourAArgb), originalName, "No original");
    drawChip (g, chipB, "B", colour (theme::colourBArgb), stemFileName, "No stem file");

    // Scale group
    g.setColour (colour (theme::hairlineArgb));
    g.fillRect (separatorX, scaleDownButton.getY(), 1, scaleDownButton.getHeight());
    g.setColour (colour (theme::mutedTextArgb));
    g.setFont (theme::monoFont (theme::em (0.75f, s)));
    g.drawText (scaleLabel, scaleLabelArea, juce::Justification::centred, false);

    // Status line
    const auto row = statusRow.toFloat();
    const float rightW = juce::jmin (segmentsWidth (rightStatus), row.getWidth() * 0.62f);
    drawSegments (g, rightStatus, row.withLeft (row.getRight() - rightW), true);
    drawSegments (g, leftStatus, row.withRight (row.getRight() - rightW - 16.0f * s), false);
}

} // namespace stemab
