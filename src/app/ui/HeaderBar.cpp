#include "app/ui/HeaderBar.h"

namespace stemab
{

using theme::C;
using theme::colour;

namespace
{
    constexpr float topPanelPx = 58.0f, statusRowPx = 22.0f;

    juce::Font wordmarkFont (float s)       { return theme::uiFont (21.0f * s, theme::Weight::extraBold).withExtraKerningFactor (-0.01f); }
}

HeaderBar::HeaderBar()
{
    setOpaque (true);

    openOriginalButton.setButtonText ("Open A");
    openStemButton.setButtonText ("Open B");
    settingsButton.setButtonText ("Settings");
    scaleDownButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("A\xe2\x88\x92")));
    scaleUpButton.setButtonText ("A+");

    openOriginalButton.setTooltip ("Open the Original (A): the mastered track");
    openStemButton.setTooltip ("Open B: an NI .stem.m4a, or 1 to 4 separate stem files (e.g. drums.wav, bass.wav, other.wav, vocals.wav)");
    settingsButton.setTooltip ("Preferences: folders, interface size and colors, audio device, session (Ctrl+,)");
    scaleDownButton.setTooltip ("Smaller interface (Ctrl+-)");
    scaleUpButton.setTooltip ("Larger interface (Ctrl+=); Ctrl+0 resets");

    for (auto* b : { &openOriginalButton, &openStemButton, &settingsButton, &scaleDownButton, &scaleUpButton })
        addAndMakeVisible (b);
    addAndMakeVisible (unloadButton);
}

void HeaderBar::setFiles (const juce::File& original, const StemSource& stems)
{
    if (original != originalFile || stems != stemSource)
    {
        originalFile = original;
        stemSource = stems;
        originalName = original.getFileName();
        stemFileName = stems.displayName();
        repaint();
    }
}

juce::String HeaderBar::getTooltip()
{
    // A file screen shows its full path; nothing elsewhere (the status line has no tooltip).
    const auto p = getMouseXYRelative();
    if (chipA.contains (p))
        return originalFile.getFullPathName();
    if (chipB.contains (p))
        return stemSource.describePaths();
    return {};
}

juce::String HeaderBar::keyOf (const juce::Array<Segment>& segs)
{
    juce::String k;
    for (const auto& s : segs)
        k << s.text << "|" << juce::String::toHexString ((int) s.colour.getARGB()) << (s.mono ? "m" : "") << (s.dot ? "d" : "")
          << (s.silk ? "s" : "") << "\n";
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
    for (auto* b : { &openOriginalButton, &openStemButton, &settingsButton, &scaleDownButton, &scaleUpButton })
        b->setUiScale (s);
    unloadButton.setUiScale (s);
    resized();
    repaint();
}

int HeaderBar::getPreferredHeight() const
{
    return theme::scaled (topPanelPx, uiScale) + theme::scaled (statusRowPx, uiScale);
}

void HeaderBar::resized()
{
    const auto s = uiScale;
    const auto px = [s] (float v) { return theme::scaled (v, s); };

    auto r = getLocalBounds();
    topPanel = r.removeFromTop (px (topPanelPx));
    statusRow = r.reduced (px (18.0f), 0);

    auto row = topPanel.reduced (px (18.0f), 0);
    const int keyH = px (30.0f), screenH = px (36.0f), gap = px (14.0f);

    const auto keyWidth = [&] (const HwButton& b)
    {
        const auto f = theme::uiFont (12.0f * s, theme::Weight::semiBold);
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, b.getButtonText())) + px (24.0f);
    };
    const auto placeKey = [&] (HwButton& b, int x, int w)
    {
        b.setFaceBounds ({ x, row.getCentreY() - keyH / 2 - px (1.0f), w, keyH });
    };

    // Left: the wordmark (+ 10 px margin)
    {
        const float w = juce::jmax (juce::GlyphArrangement::getStringWidth (wordmarkFont (s), "stema/b"),
                                    theme::silkWidth ("Null-test player", 9.0f * s));
        wordmarkArea = row.removeFromLeft ((int) std::ceil (w) + 2);
        row.removeFromLeft (px (10.0f) + gap);
    }

    // Right, from the edge: A+  label  A-  SIZE | Settings | eject
    {
        const int stepW = juce::jmax (keyWidth (scaleUpButton), keyWidth (scaleDownButton));
        placeKey (scaleUpButton, row.getRight() - stepW, stepW);
        row.removeFromRight (stepW);
        scaleLabelArea = row.removeFromRight (px (44.0f));
        placeKey (scaleDownButton, row.getRight() - stepW, stepW);
        row.removeFromRight (stepW + px (6.0f));
        sizeLabelArea = row.removeFromRight ((int) std::ceil (theme::silkWidth ("Size", 10.0f * s)) + 2);
        row.removeFromRight (px (6.0f) + gap);

        const int settingsW = keyWidth (settingsButton);
        placeKey (settingsButton, row.getRight() - settingsW, settingsW);
        row.removeFromRight (settingsW + gap);

        unloadButton.setFaceBounds ({ row.getRight() - px (40.0f), row.getCentreY() - keyH / 2 - px (1.0f), px (40.0f), keyH });
        row.removeFromRight (px (40.0f) + gap);
    }

    // Middle: [Open A][A screen]  [Open B][B screen]; the screens share what is left (A : B = 290 : 350,
    // at most twice the mockup's widths; the rest is the spacer).
    {
        const int aKeyW = keyWidth (openOriginalButton), bKeyW = keyWidth (openStemButton), keyGap = px (6.0f);
        const int forScreens = juce::jmax (0, row.getWidth() - aKeyW - bKeyW - 2 * keyGap - gap);
        const int aW = juce::jmin (px (580.0f), forScreens * 290 / 640);
        const int bW = juce::jmin (px (700.0f), forScreens - aW);

        placeKey (openOriginalButton, row.getX(), aKeyW);
        row.removeFromLeft (aKeyW + keyGap);
        chipA = row.removeFromLeft (aW).withSizeKeepingCentre (aW, screenH);
        row.removeFromLeft (gap);

        placeKey (openStemButton, row.getX(), bKeyW);
        row.removeFromLeft (bKeyW + keyGap);
        chipB = row.removeFromLeft (bW).withSizeKeepingCentre (bW, screenH);
    }
}

float HeaderBar::segmentWidth (const Segment& seg) const
{
    const float s = uiScale;
    if (seg.dot)
        return 13.0f * s;
    if (seg.silk)
        return theme::silkWidth (seg.text, 9.5f * s);
    return juce::GlyphArrangement::getStringWidth (theme::monoFont (11.0f * s), seg.text);
}

float HeaderBar::segmentsWidth (const juce::Array<Segment>& segs) const
{
    float w = 0.0f;
    for (const auto& seg : segs)
        w += segmentWidth (seg);
    return w;
}

void HeaderBar::drawSegments (juce::Graphics& g, const juce::Array<Segment>& segs, juce::Rectangle<float> area,
                              bool alignRight) const
{
    const float s = uiScale;
    const auto mono = theme::monoFont (11.0f * s);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.toNearestInt());

    const float total = segmentsWidth (segs);
    float x = alignRight && total <= area.getWidth() ? area.getRight() - total : area.getX();
    for (const auto& seg : segs)
    {
        const float w = segmentWidth (seg);
        if (seg.dot)
        {
            const float d = 6.0f * s;
            g.setColour (seg.colour);
            g.fillEllipse (x, area.getCentreY() - d * 0.5f, d, d);
        }
        else if (seg.silk)
        {
            theme::drawSilk (g, seg.text, { x, area.getY(), w + 2.0f, area.getHeight() }, 9.5f * s, seg.colour);
        }
        else
        {
            g.setFont (mono);
            g.setColour (seg.colour);
            g.drawText (seg.text, juce::Rectangle<float> (x, area.getY(), w + 2.0f, area.getHeight()),
                        juce::Justification::centredLeft, false);
        }
        x += w;
    }
}

void HeaderBar::drawFileScreen (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& letter,
                                juce::Colour letterColour, const juce::String& name, const juce::String& emptyText) const
{
    const auto s = uiScale;
    if (area.getWidth() < theme::scaled (40.0f, s))
        return;

    const auto box = area.toFloat();
    theme::drawScreen (g, box, s);

    auto inner = box.reduced (12.0f * s, 0.0f);
    const float lw = theme::silkWidth (letter, 10.0f * s);
    theme::drawSilk (g, letter, inner.removeFromLeft (lw + 1.0f), 10.0f * s, letterColour);
    inner.removeFromLeft (10.0f * s);

    g.setFont (theme::monoFont (13.0f * s));
    g.setColour (name.isNotEmpty() ? colour (C::screenText) : colour (C::screenDim));
    g.drawText (name.isNotEmpty() ? name : emptyText, inner, juce::Justification::centredLeft, true);
}

void HeaderBar::paint (juce::Graphics& g)
{
    const auto s = uiScale;

    // The top panel (#2A2C31, dark bottom edge) and the status row on the ground colour
    g.fillAll (colour (C::ground));
    g.setColour (colour (C::panel));
    g.fillRect (topPanel);
    g.setColour (colour (C::panelEdge));
    g.fillRect (topPanel.getX(), topPanel.getBottom() - 1, topPanel.getWidth(), 1);

    // Wordmark: "stem" + "a/b" in the accent, "NULL-TEST PLAYER" under it
    {
        const auto font = wordmarkFont (s);
        const float silkPx = 9.0f * s;
        const float blockH = font.getAscent() + 4.0f * s + theme::silkFont (silkPx).getHeight();
        auto block = wordmarkArea.toFloat().withSizeKeepingCentre ((float) wordmarkArea.getWidth(), blockH);

        juce::AttributedString mark;
        mark.append ("stem", font, colour (C::text));
        mark.append ("a/b", font, colour (C::accent));
        mark.setWordWrap (juce::AttributedString::none);
        mark.draw (g, block.removeFromTop (font.getHeight()).translated (0.0f, -font.getDescent() * 0.35f));

        block = wordmarkArea.toFloat().withSizeKeepingCentre ((float) wordmarkArea.getWidth(), blockH);
        block.removeFromTop (font.getAscent() + 4.0f * s);
        theme::drawSilk (g, "Null-test player", block, silkPx, colour (C::silkDim), juce::Justification::topLeft);
    }

    drawFileScreen (g, chipA, "A", colour (C::colourA), originalName, "No original");
    drawFileScreen (g, chipB, "B", colour (C::colourB), stemFileName, "No stem file");

    // Size stepper
    theme::drawSilk (g, "Size", sizeLabelArea.toFloat(), 10.0f * s, colour (C::silk));
    g.setColour (colour (C::text));
    g.setFont (theme::monoFont (11.0f * s));
    g.drawText (scaleLabel, scaleLabelArea.translated (0, -theme::scaled (1.0f, s)), juce::Justification::centred, false);

    // Status line
    // Right: when the groups do not all fit, drop whole groups from the front (Aligned, then Track null depth)
    // so the rest is never cut mid-word; the tooltip / Details still carry everything.
    const auto row = statusRow.toFloat();
    const float maxRightW = row.getWidth() * 0.62f;
    juce::Array<Segment> right (rightStatus);
    while (segmentsWidth (right) > maxRightW)
    {
        int next = -1;
        for (int i = 0; i < right.size(); ++i)
            if (right.getReference (i).text.trim() == juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")))
            {
                next = i + 1;   // the first segment after the first separator
                break;
            }
        if (next < 0 || next >= right.size())
            break;          // one group left: it is clipped (rare: a very narrow window)
        right.removeRange (0, next);
    }
    const float rightW = juce::jmin (segmentsWidth (right), maxRightW);
    drawSegments (g, right, row.withLeft (row.getRight() - rightW), true);
    drawSegments (g, leftStatus, row.withRight (row.getRight() - rightW - 16.0f * s), false);
}

} // namespace stemab
