#include "app/ui/ControlDeck.h"

#include <cmath>

#include "app/ui/Format.h"

namespace stemab
{

using theme::C;
using theme::colour;

namespace
{
    const juce::String middleDot (juce::CharPointer_UTF8 ("\xc2\xb7"));

    // Base metrics at 100 % (the mockup)
    constexpr float padXPx = 18.0f, padYPx = 12.0f, columnGapPx = 26.0f;
    constexpr float transportPx = 380.0f, masterPx = 250.0f;            // the mockup's columns
    constexpr float compactTransportPx = 336.0f, compactMasterPx = 196.0f;   // when space is short
    constexpr float comfortableCrossfadePx = 420.0f, minCrossfadePx = 260.0f;
    constexpr float timeScreenPx = 52.0f, rowGapPx = 10.0f;
    constexpr float keyWPx = 52.0f, playWPx = 76.0f, keyHPx = 44.0f, keyGapPx = 8.0f;
    constexpr float silkRowPx = 14.0f, faderPx = 40.0f, padRowPx = 30.0f;
    constexpr float knobBoxPx = 92.0f, clipPx = 44.0f, labelGapPx = 6.0f;
}

ControlDeck::ControlDeck()
{
    setOpaque (true);

    for (auto* b : { &toStartButton, &backButton, &playButton, &forwardButton, &loopButton })
        addAndMakeVisible (b);
    loopButton.setClickingTogglesState (true);

    crossfadeSlider.getProperties().set ("style", "crossfade");
    crossfadeSlider.setTooltip ("Crossfade between A (Original) and B (Stem Sum). Double-click: center. Keys A / B / C.");
    addAndMakeVisible (crossfadeSlider);

    residualButton.setButtonText ("R residual");
    residualButton.setTooltip ("Listen to the Residual: A minus all four Stems (R)");
    residualButton.setLitColours (C::accent, C::text);

    curveButton.setButtonText ("Equal power");
    curveButton.setTooltip ("Crossfade curve: Equal-power (lit) or Linear (unlit)");
    curveButton.setLitColours (C::amber, C::inkPad);

    centreButton.setButtonText ("Center");
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

    // The click-to-type time editor: single line, hidden until the time readout is clicked (see openTimeEditor()).
    timeEditor.setMultiLine (false);
    timeEditor.setReturnKeyStartsNewLine (false);
    timeEditor.setScrollbarsShown (false);
    timeEditor.setPopupMenuEnabled (false);
    timeEditor.setBorder ({});
    timeEditor.setIndents (0, 0);
    timeEditor.setJustification (juce::Justification::centredLeft);
    timeEditor.setSelectAllWhenFocused (true);
    timeEditor.setInputRestrictions (24, "0123456789:.,+- " + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")));
    timeEditor.onReturnKey = [this] { commitTimeEditor(); };
    timeEditor.onEscapeKey = [this] { closeTimeEditor (true); };
    timeEditor.onFocusLost = [this] { closeTimeEditor (false); };
    timeEditor.onTextChange = [this] { if (timeEditorInvalid) setTimeEditorInvalid (false); };
    addChildComponent (timeEditor);
}

ControlDeck::~ControlDeck()
{
    juce::Desktop::getInstance().removeGlobalMouseListener (&clickAwayWatcher);
}

void ControlDeck::setTime (const juce::String& time, const juce::String& samples)
{
    // Only the painted strings: an open time editor holds what the user is typing and is never touched here.
    if (time != timeText || samples != sampleText)
    {
        timeText = time;
        sampleText = samples;
        repaint (timeBox);
    }
}

//==============================================================================
// Click-to-type time readout
ControlDeck::TimeLayout ControlDeck::timeLayout() const
{
    const auto s = uiScale;
    const auto inner = timeBox.toFloat().reduced (14.0f * s, 8.0f * s);

    const auto small = theme::monoFont (11.0f * s);
    float linesW = 0.0f;
    for (const auto& line : juce::StringArray::fromLines (sampleText))
        linesW = juce::jmax (linesW, juce::GlyphArrangement::getStringWidth (small, line));

    const auto big = theme::monoFont (36.0f * s, true);
    const float timeW = juce::GlyphArrangement::getStringWidth (big, timeText);
    const auto right = inner.withLeft (juce::jmin (inner.getRight(), juce::jmax (inner.getX() + timeW + 12.0f * s, inner.getRight() - linesW)));
    return { inner.withRight (right.getX()), right };
}

bool ControlDeck::isOverEditableTime() const
{
    return timeEditable && timeBox.contains (getMouseXYRelative());
}

void ControlDeck::setTimeEditable (bool editable)
{
    if (timeEditable == editable)
        return;
    timeEditable = editable;
    if (! editable)
        closeTimeEditor (true);
    updateMouseCursor();
}

void ControlDeck::mouseDown (const juce::MouseEvent& e)
{
    if (timeEditable && ! timeEditor.isVisible() && e.mods.isLeftButtonDown() && ! e.mods.isPopupMenu()
        && timeBox.contains (e.getPosition()))
    {
        openingMouseDownTime = e.mouseDownTime;
        openTimeEditor();
    }
}

juce::MouseCursor ControlDeck::getMouseCursor()
{
    if (isOverEditableTime() && ! timeEditor.isVisible())
        return juce::MouseCursor::PointingHandCursor;
    return juce::Component::getMouseCursor();
}

juce::String ControlDeck::getTooltip()
{
    if (isOverEditableTime() && ! timeEditor.isVisible())
        return "Click to type a time (m:ss.mmm, seconds, or +/- offset)";
    return {};
}

void ControlDeck::layoutTimeEditor()
{
    const int vPad = theme::scaled (3.0f, uiScale);
    timeEditor.setBounds (timeLayout().time.toNearestInt().withTop (timeBox.getY() + vPad).withBottom (timeBox.getBottom() - vPad));

    // Same font as the painted time; the font is re-applied to the text so a UI-scale change shows at once.
    timeEditor.applyFontToAllText (theme::monoFont (36.0f * uiScale, true));
}

void ControlDeck::setTimeEditorInvalid (bool invalid)
{
    timeEditorInvalid = invalid;
    const auto c = colour (invalid ? C::meterRed : C::screenText);
    timeEditor.setColour (juce::TextEditor::textColourId, c);
    timeEditor.applyColourToAllText (c, true);
}

void ControlDeck::openTimeEditor()
{
    if (timeEditor.isVisible())
        return;

    // The editor is a child like the others, which MainComponent stripped of keyboard focus: give it back.
    timeEditor.setWantsKeyboardFocus (true);
    timeEditor.setMouseClickGrabsKeyboardFocus (true);

    timeEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);   // the screen shows through
    timeEditor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    timeEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    timeEditor.setColour (juce::TextEditor::highlightColourId, colour (C::screenDim).withAlpha (0.45f));
    timeEditor.setColour (juce::TextEditor::highlightedTextColourId, colour (C::screenText));
    timeEditor.setColour (juce::CaretComponent::caretColourId, colour (C::screenText));
    timeEditor.setFont (theme::monoFont (36.0f * uiScale, true));

    timeEditor.setText (timeText, false);
    setTimeEditorInvalid (false);
    layoutTimeEditor();
    timeEditor.setVisible (true);
    timeEditor.toFront (false);

    juce::Desktop::getInstance().addGlobalMouseListener (&clickAwayWatcher);
    timeEditor.grabKeyboardFocus();
    timeEditor.selectAll();
    repaint (timeBox);      // the painted time steps aside for the editor
}

void ControlDeck::closeTimeEditor (bool restoreFocus)
{
    if (! timeEditor.isVisible())
        return;

    juce::Desktop::getInstance().removeGlobalMouseListener (&clickAwayWatcher);
    timeEditor.setVisible (false);      // its focus-lost callback comes back here and returns at the check above
    repaint (timeBox);

    if (restoreFocus && onTimeEditorClosed != nullptr)
        onTimeEditorClosed();
}

void ControlDeck::commitTimeEditor()
{
    const auto parsed = format::parseTimeInput (timeEditor.getText());
    if (! parsed.has_value())
    {
        setTimeEditorInvalid (true);    // red until the next keystroke
        timeEditor.selectAll();
        return;
    }

    closeTimeEditor (true);
    if (onTimeEntered != nullptr)
        onTimeEntered (parsed->first, parsed->second);
}

void ControlDeck::ClickAwayWatcher::mouseDown (const juce::MouseEvent& e)
{
    if (! deck.timeEditor.isVisible() || e.mouseDownTime == deck.openingMouseDownTime)   // the very click that opened it
        return;
    if (e.eventComponent == &deck.timeEditor || deck.timeEditor.isParentOf (e.eventComponent))
        return;

    // A click in another window (Preferences, a dialog) is no reason to take the focus back from it.
    const bool ourWindow = e.eventComponent != nullptr && e.eventComponent->getTopLevelComponent() == deck.getTopLevelComponent();
    deck.closeTimeEditor (ourWindow);
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

void ControlDeck::setStemVsStemMode (bool m)
{
    if (m != stemVsStemMode)
    {
        stemVsStemMode = m;
        crossfadeSlider.setTooltip (m ? "Crossfade between Stem A Sum and Stem B Sum. Double-click: center. Keys A / B / C."
                                      : "Crossfade between A (Original) and B (Stem Sum). Double-click: center. Keys A / B / C.");
        repaint (xfLabelRow);
    }
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

ControlDeck::Metrics ControlDeck::metrics (int width) const
{
    const auto s = uiScale;
    const auto px = [s] (float v) { return theme::scaled (v, s); };
    Metrics m {};
    m.padX = px (padXPx);
    m.padY = px (padYPx);
    m.gap = px (columnGapPx);
    m.transportW = px (transportPx);
    m.masterW = px (masterPx);
    if (width - 2 * m.padX - 2 * m.gap - m.transportW - m.masterW < px (comfortableCrossfadePx))
    {
        m.transportW = px (compactTransportPx);
        m.masterW = px (compactMasterPx);
    }
    m.transportH = px (timeScreenPx + rowGapPx + keyHPx + 2.0f);
    m.xfH = px (silkRowPx + rowGapPx + faderPx + rowGapPx + padRowPx + 2.0f);
    m.masterH = px (knobBoxPx + labelGapPx + silkRowPx);
    return m;
}

bool ControlDeck::needsTwoRows (int width) const
{
    const auto m = metrics (width);
    return width - 2 * m.padX - m.transportW - m.masterW - 2 * m.gap < theme::scaled (minCrossfadePx, uiScale);
}

int ControlDeck::getPreferredHeight (int width) const
{
    const auto m = metrics (width);
    if (needsTwoRows (width))
        return 2 * m.padY + m.xfH + m.padY + juce::jmax (m.transportH, m.masterH);
    return 2 * m.padY + juce::jmax (m.transportH, m.xfH, m.masterH);
}

void ControlDeck::resized()
{
    const auto s = uiScale;
    const auto px = [s] (float v) { return theme::scaled (v, s); };
    const auto m = metrics (getWidth());
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

    layoutTimeEditor();
}

void ControlDeck::paint (juce::Graphics& g)
{
    const auto s = uiScale;

    // The panel: #2A2C31 with the inset top highlight
    theme::drawPanel (g, getLocalBounds().toFloat(), s, C::panel, 0.0f, true);

    // Time screen: 36 px time, "Sample ..." / "Total ..." stacked on the right
    {
        theme::drawScreen (g, timeBox.toFloat(), s);
        const auto layout = timeLayout();
        const auto right = layout.right;
        const auto small = theme::monoFont (11.0f * s);
        const auto lines = juce::StringArray::fromLines (sampleText);

        // While the time editor is open it draws the time itself (over this spot)
        if (! timeEditor.isVisible())
        {
            g.setColour (colour (C::screenText));
            g.setFont (theme::monoFont (36.0f * s, true));
            g.drawText (timeText, layout.time, juce::Justification::centredLeft, false);
        }

        g.setColour (colour (C::screenDim));
        g.setFont (small);
        const float lineH = 11.0f * 1.4f * s;
        auto block = right.withTop (right.getBottom() - lineH * (float) lines.size() - 2.0f * s);
        for (const auto& line : lines)
            g.drawFittedText (line, block.removeFromTop (lineH).toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
    }

    // Crossfade labels: A · ORIGINAL (or A · STEM A) ... CROSSFADE <readout> ... STEM SUM · B (or STEM B · B)
    {
        const float alpha = crossfadeDimmed ? 0.45f : 1.0f;
        const float px = 10.0f * s;
        const auto row = xfLabelRow.toFloat();
        theme::drawSilk (g, stemVsStemMode ? ("A " + middleDot + " stem a") : ("A " + middleDot + " original"),
                         row, px, colour (C::colourA).withMultipliedAlpha (alpha));
        theme::drawSilk (g, stemVsStemMode ? ("Stem B " + middleDot + " B") : ("Stem sum " + middleDot + " B"),
                         row, px, colour (C::colourB).withMultipliedAlpha (alpha),
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
