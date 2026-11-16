#include "app/ui/TrackArea.h"

#include <cmath>

#include "app/Theme.h"
#include "decode/Decode.h"

namespace stemab
{

namespace
{
    // Base metrics at 100 % interface scale (the mockup).
    constexpr float headerColumnPx = 196.0f;
    constexpr float rulerPx = 30.0f;
    constexpr float groupRowPx = 28.0f;
    constexpr float minLanePx = 56.0f;
    constexpr float aLaneFactor = 1.5f, rLaneFactor = 0.6f;
    constexpr float residualViewGain = 16.0f;   // +24 dB, so the Residual is visible

    const juce::String middleDot (juce::CharPointer_UTF8 ("\xc2\xb7"));
}

TrackArea::TrackArea()
{
    using theme::colour;

    aHeader = std::make_unique<TrackHeader> (TrackHeader::Style {
        colour (theme::colourAArgb), colour (theme::panelArgb), "A", " " + middleDot + " Original", 1.0f, false, true });
    rHeader = std::make_unique<TrackHeader> (TrackHeader::Style {
        colour (theme::residualArgb), colour (theme::residualLaneArgb), "R", " " + middleDot + " Residual", 0.88f, false, false, true });
    rHeader->viewGain.setTooltip ("Residual waveform: amplified +24 dB so it is visible, or at its true level (0 dB)");
    rHeader->viewGain.onClick = [this] { setResidualAmplified (! residualAmplified); };

    aLane = std::make_unique<WaveformLane> (colour (theme::colourAArgb), colour (theme::laneArgb), true);
    aLane->setVerticalPadding (8.0f);
    rLane = std::make_unique<WaveformLane> (colour (theme::residualArgb), colour (theme::residualLaneArgb), false);
    rLane->setVerticalPadding (5.0f);
    setResidualAmplified (true);

    for (int i = 0; i < numStems; ++i)
    {
        const auto c = colour (theme::stemArgb[(size_t) i]);
        stemHeaders[(size_t) i] = std::make_unique<TrackHeader> (TrackHeader::Style {
            c, colour (theme::panelArgb), {}, stemName ((Stem) i), 0.92f, true, true });
        stemHeaders[(size_t) i]->setSubLabel ("Stem " + juce::String (i + 1) + " of " + juce::String (numStems));

        stemLanes[(size_t) i] = std::make_unique<WaveformLane> (c, colour (theme::laneArgb), true);
        stemLanes[(size_t) i]->setVerticalPadding (6.0f);
    }

    content.addAndMakeVisible (*aHeader);
    for (auto& h : stemHeaders)
        content.addAndMakeVisible (*h);
    content.addAndMakeVisible (*rHeader);

    content.addAndMakeVisible (ruler);
    content.addAndMakeVisible (*aLane);
    for (auto& l : stemLanes)
        content.addAndMakeVisible (*l);
    content.addAndMakeVisible (*rLane);
    content.addAndMakeVisible (overlay);   // last: above the ruler and every lane

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    setOpaque (true);
    updateStemPlaceholders();
    for (auto& h : stemHeaders)
        h->setDimmed (true);
    rHeader->setDimmed (true);
}

void TrackArea::setUiScale (float s)
{
    uiScale = s;
    aHeader->setUiScale (s);
    rHeader->setUiScale (s);
    for (auto& h : stemHeaders)
        h->setUiScale (s);
    aLane->setUiScale (s);
    rLane->setUiScale (s);
    for (auto& l : stemLanes)
        l->setUiScale (s);
    ruler.setUiScale (s);
    overlay.setUiScale (s);
    resized();
    repaint();
}

void TrackArea::setTimeline (juce::int64 lengthSamples, double sampleRate)
{
    ruler.setTimeline (lengthSamples, sampleRate);
    overlay.setTimeline (lengthSamples, sampleRate);
}

void TrackArea::setOriginalSummary (SummaryPtr s)
{
    aLane->setSummary (std::move (s));
}

void TrackArea::setOriginalState (bool rendering)
{
    originalRendering = rendering;
    aLane->setPlaceholder (rendering ? juce::String (juce::CharPointer_UTF8 ("Rendering waveform\xe2\x80\xa6")) : juce::String());
}

void TrackArea::setStemSummaries (const std::array<SummaryPtr, numStems>& stems, SummaryPtr residual)
{
    for (int i = 0; i < numStems; ++i)
        stemLanes[(size_t) i]->setSummary (stems[(size_t) i]);
    rLane->setSummary (std::move (residual));
}

void TrackArea::clearStemSummaries()
{
    for (auto& l : stemLanes)
        l->setSummary (nullptr);
    rLane->setSummary (nullptr);
}

void TrackArea::setStemsState (StemsState s)
{
    if (s == stemsState)
        return;
    stemsState = s;
    updateStemPlaceholders();

    const bool dim = s != StemsState::ready;
    for (auto& h : stemHeaders)
        h->setDimmed (dim);
    rHeader->setDimmed (dim);
}

void TrackArea::setResidualAmplified (bool amplified)
{
    residualAmplified = amplified;
    rLane->setDisplayGain (amplified ? residualViewGain : 1.0f);

    const juce::String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    rHeader->viewGain.setButtonText (amplified ? "+24 dB" : "0 dB");
    rHeader->viewGain.setToggleState (amplified, juce::dontSendNotification);
    rHeader->setSubLabel ("A " + minus + " all stems");   // the button itself shows the view gain
}

void TrackArea::updateStemPlaceholders()
{
    const juce::String text = stemsState == StemsState::loading
                                  ? juce::String (juce::CharPointer_UTF8 ("Loading stems\xe2\x80\xa6"))
                                  : stemsState == StemsState::ready || stemsState == StemsState::preview
                                        ? juce::String (juce::CharPointer_UTF8 ("Rendering waveform\xe2\x80\xa6"))
                                        : juce::String();
    for (auto& l : stemLanes)
        l->setPlaceholder (text);
    rLane->setPlaceholder (stemsState == StemsState::preview ? juce::String ("Needs an original") : text);
}

void TrackArea::setAudibleStems (const std::array<bool, numStems>& audible)
{
    for (int i = 0; i < numStems; ++i)
        stemLanes[(size_t) i]->setFaded (! audible[(size_t) i]);
}

void TrackArea::setOriginalSubLabel (const juce::String& s)
{
    aHeader->setSubLabel (s);
}

void TrackArea::setHint (const juce::String& h)
{
    overlay.setHint (h);
}

//==============================================================================
int TrackArea::laneHeightFor (int viewportHeight) const
{
    const float fixed = (rulerPx + groupRowPx) * uiScale;
    const float fill = ((float) viewportHeight - fixed) / (aLaneFactor + (float) numStems + rLaneFactor);
    return juce::jmax (theme::scaled (minLanePx, uiScale), (int) std::floor (fill));
}

int TrackArea::contentHeightFor (int lane) const
{
    return theme::scaled (rulerPx, uiScale) + theme::scaled (groupRowPx, uiScale)
           + juce::roundToInt ((float) lane * aLaneFactor) + numStems * lane + juce::roundToInt ((float) lane * rLaneFactor);
}

void TrackArea::resized()
{
    viewport.setBounds (getLocalBounds());

    const int vh = viewport.getHeight();
    const int lane = laneHeightFor (vh);
    const int h = contentHeightFor (lane);
    const int w = viewport.getWidth() - (h > vh ? viewport.getScrollBarThickness() : 0);

    layoutContent (juce::jmax (1, w), lane);
    content.setSize (juce::jmax (1, w), juce::jmax (h, vh));
}

void TrackArea::layoutContent (int width, int lane)
{
    headerWidth = theme::scaled (headerColumnPx, uiScale);
    const int rulerH = theme::scaled (rulerPx, uiScale);
    const int groupH = theme::scaled (groupRowPx, uiScale);
    const int aH = juce::roundToInt ((float) lane * aLaneFactor);
    const int rH = juce::roundToInt ((float) lane * rLaneFactor);
    const int laneW = juce::jmax (1, width - headerWidth);

    int y = 0;
    tracksCell = { 0, y, headerWidth, rulerH };
    ruler.setBounds (headerWidth, y, laneW, rulerH);
    y += rulerH;

    juce::Array<juce::Rectangle<int>> darken;
    const auto place = [&] (TrackHeader& header, WaveformLane& l, int h)
    {
        header.setBounds (0, y, headerWidth - 1, h);
        l.setBounds (headerWidth, y, laneW, h);
        darken.add ({ 0, y, laneW, h });
        y += h;
    };

    place (*aHeader, *aLane, aH);

    groupHeaderRow = { 0, y, headerWidth - 1, groupH };
    groupLaneRow = { headerWidth, y, laneW, groupH };
    y += groupH;

    for (int i = 0; i < numStems; ++i)
        place (*stemHeaders[(size_t) i], *stemLanes[(size_t) i], lane);

    place (*rHeader, *rLane, rH);

    overlay.setBounds (headerWidth, 0, laneW, y);
    overlay.setRulerHeight (rulerH);
    overlay.setDarkenAreas (darken);
    content.repaint();
}

void TrackArea::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour (theme::trackBgArgb));
}

void TrackArea::paintContent (juce::Graphics& g)
{
    using theme::colour;
    g.fillAll (colour (theme::trackBgArgb));

    // Header column background and its right border
    g.setColour (colour (theme::panelArgb));
    g.fillRect (0, 0, headerWidth, content.getHeight());
    g.setColour (colour (theme::hairlineArgb));
    g.fillRect (headerWidth - 1, 0, 1, content.getHeight());

    // "TRACKS"
    g.setColour (colour (theme::mutedTextArgb));
    theme::drawTrackedText (g, "TRACKS", tracksCell.toFloat().withTrimmedLeft (12.0f * uiScale),
                            theme::uiFont (theme::em (0.68f, uiScale)), 0.12f, juce::Justification::centredLeft);
    g.setColour (colour (theme::hairlineArgb));
    g.fillRect (tracksCell.getX(), tracksCell.getBottom() - 1, tracksCell.getWidth(), 1);

    // Group row "B - STEM SUM - SUM OF 4 STEMS", both columns
    g.setColour (colour (theme::groupRowArgb));
    g.fillRect (groupHeaderRow);
    g.fillRect (groupLaneRow);
    g.setColour (colour (theme::hairlineArgb));
    g.fillRect (groupHeaderRow.getX(), groupHeaderRow.getBottom() - 1, groupHeaderRow.getWidth(), 1);
    g.fillRect (groupLaneRow.getX(), groupLaneRow.getBottom() - 1, groupLaneRow.getWidth(), 1);

    auto row = groupHeaderRow.toFloat().withTrimmedLeft (12.0f * uiScale).withTrimmedBottom (1.0f);
    const auto badgeFont = theme::uiFont (theme::em (0.72f, uiScale), true);
    const float badgeW = juce::GlyphArrangement::getStringWidth (badgeFont, "B") + 10.0f * uiScale;
    const auto badge = row.removeFromLeft (badgeW).withSizeKeepingCentre (badgeW, badgeFont.getHeight() + 2.0f * uiScale);
    g.setColour (colour (theme::colourBArgb));
    g.fillRoundedRectangle (badge, 3.0f * uiScale);
    g.setColour (colour (theme::groundArgb));
    g.setFont (badgeFont);
    g.drawText ("B", badge, juce::Justification::centred, false);

    row.removeFromLeft (8.0f * uiScale);
    g.setColour (colour (theme::mutedTextArgb));
    theme::drawTrackedText (g, "STEM SUM " + middleDot + " " + juce::String (juce::CharPointer_UTF8 ("\xce\xa3")) + " 4 STEMS",
                            row, theme::uiFont (theme::em (0.72f, uiScale)), 0.1f, juce::Justification::centredLeft);
}

} // namespace stemab
