#include "app/ui/TrackArea.h"

#include <cmath>

#include "app/Theme.h"
#include "decode/Decode.h"

namespace stemab
{

namespace
{
    // Base metrics at 100 % interface scale (the mockup).
    constexpr float channelColumnPx = 206.0f;
    constexpr float columnGapPx = 14.0f;
    constexpr float rulerPx = 24.0f;
    constexpr float groupRowPx = 22.0f;
    constexpr float screenBottomPx = 6.0f;
    constexpr float minLanePx = 44.0f;
    constexpr float aLaneFactor = 1.5f, rLaneFactor = 0.8f;
    constexpr float residualViewGain = 16.0f;   // +24 dB, so the Residual is visible

    using theme::C;
}

TrackArea::TrackArea()
{
    aHeader = std::make_unique<TrackHeader> (TrackHeader::Style { TrackHeader::Kind::original, C::colourA, "Original",
                                                                  false, true, false });
    rHeader = std::make_unique<TrackHeader> (TrackHeader::Style { TrackHeader::Kind::residual, C::accent, "Residual",
                                                                  false, false, true });
    rHeader->viewGain.setTooltip ("Residual waveform: amplified +24 dB so it is visible, or at its true level (0 dB)");
    rHeader->viewGain.onClick = [this] { setResidualAmplified (! residualAmplified); };

    aLane = std::make_unique<WaveformLane> (theme::colour (C::colourA), WaveformLane::Look::outline);
    aLane->setVerticalPadding (8.0f);
    rLane = std::make_unique<WaveformLane> (theme::colour (C::accent), WaveformLane::Look::filledPeak);
    rLane->setVerticalPadding (4.0f);
    setResidualAmplified (true);

    for (int i = 0; i < numStems; ++i)
    {
        stemAHeaders[(size_t) i] = std::make_unique<TrackHeader> (TrackHeader::Style {
            TrackHeader::Kind::stem, theme::stemRole (i), stemName ((Stem) i), true, true, false });
        stemALanes[(size_t) i] = std::make_unique<WaveformLane> (theme::stemColour (i), WaveformLane::Look::outline);
        stemALanes[(size_t) i]->setVerticalPadding (6.0f);

        stemHeaders[(size_t) i] = std::make_unique<TrackHeader> (TrackHeader::Style {
            TrackHeader::Kind::stem, theme::stemRole (i), stemName ((Stem) i), true, true, false });

        stemLanes[(size_t) i] = std::make_unique<WaveformLane> (theme::stemColour (i), WaveformLane::Look::outline);
        stemLanes[(size_t) i]->setVerticalPadding (6.0f);
    }

    linkMsButton.setButtonText ("LINK M/S");
    linkMsButton.setTooltip ("Link Mute and Solo between Stem A and Stem B");
    linkMsButton.setLitColours (C::colourB, C::inkDark);
    linkMsButton.setClickingTogglesState (true);
    content.addChildComponent (linkMsButton);

    content.addAndMakeVisible (*aHeader);
    for (auto& h : stemAHeaders)
        content.addChildComponent (*h);
    for (auto& h : stemHeaders)
        content.addAndMakeVisible (*h);
    content.addAndMakeVisible (*rHeader);

    content.addAndMakeVisible (ruler);
    content.addAndMakeVisible (*aLane);
    for (auto& l : stemALanes)
        content.addChildComponent (*l);
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
    for (auto& h : stemAHeaders)
        h->setDimmed (true);
    for (auto& h : stemHeaders)
        h->setDimmed (true);
    rHeader->setDimmed (true);
}

void TrackArea::setUiScale (float s)
{
    uiScale = s;
    linkMsButton.setUiScale (s);
    aHeader->setUiScale (s);
    rHeader->setUiScale (s);
    for (auto& h : stemAHeaders)
        h->setUiScale (s);
    for (auto& h : stemHeaders)
        h->setUiScale (s);
    aLane->setUiScale (s);
    rLane->setUiScale (s);
    for (auto& l : stemALanes)
        l->setUiScale (s);
    for (auto& l : stemLanes)
        l->setUiScale (s);
    ruler.setUiScale (s);
    overlay.setUiScale (s);
    resized();
    repaint();
}

void TrackArea::setStemVsStemMode (bool m)
{
    if (m == stemVsStemMode)
        return;

    stemVsStemMode = m;
    aHeader->setVisible (! m);
    aLane->setVisible (! m);
    linkMsButton.setVisible (m);
    for (int i = 0; i < numStems; ++i)
    {
        stemAHeaders[(size_t) i]->setVisible (m);
        stemALanes[(size_t) i]->setVisible (m);
    }
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

void TrackArea::setStemASummaries (const std::array<SummaryPtr, numStems>& stems)
{
    for (int i = 0; i < numStems; ++i)
        stemALanes[(size_t) i]->setSummary (stemAPresent[(size_t) i] ? stems[(size_t) i] : nullptr);
}

void TrackArea::clearStemASummaries()
{
    for (auto& l : stemALanes)
        l->setSummary (nullptr);
}

void TrackArea::setStemAState (StemsState s)
{
    if (s == stemsAState)
        return;
    stemsAState = s;
    updateStemPlaceholders();
    updateStemDimming();
}

void TrackArea::setStemsAPresent (const std::array<bool, numStems>& present)
{
    if (present == stemAPresent)
        return;

    stemAPresent = present;
    for (int i = 0; i < numStems; ++i)
        if (! stemAPresent[(size_t) i])
            stemALanes[(size_t) i]->setSummary (nullptr);

    updateStemPlaceholders();
    updateStemDimming();
}

void TrackArea::setAudibleStemsA (const std::array<bool, numStems>& audible)
{
    for (int i = 0; i < numStems; ++i)
        stemALanes[(size_t) i]->setFaded (! audible[(size_t) i]);
}

void TrackArea::setStemSummaries (const std::array<SummaryPtr, numStems>& stems, SummaryPtr residual)
{
    for (int i = 0; i < numStems; ++i)
        stemLanes[(size_t) i]->setSummary (stemPresent[(size_t) i] ? stems[(size_t) i] : nullptr);   // no file: no (silent) waveform
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
    updateStemDimming();
}

void TrackArea::setStemsPresent (const std::array<bool, numStems>& present)
{
    if (present == stemPresent)
        return;

    stemPresent = present;
    for (int i = 0; i < numStems; ++i)
        if (! stemPresent[(size_t) i])
            stemLanes[(size_t) i]->setSummary (nullptr);

    updateStemPlaceholders();
    updateStemDimming();
}

void TrackArea::updateStemDimming()
{
    const bool dimA = stemsAState != StemsState::ready;
    for (int i = 0; i < numStems; ++i)
        stemAHeaders[(size_t) i]->setDimmed (dimA || ! stemAPresent[(size_t) i]);

    const bool dim = stemsState != StemsState::ready;
    for (int i = 0; i < numStems; ++i)
        stemHeaders[(size_t) i]->setDimmed (dim || ! stemPresent[(size_t) i]);
    rHeader->setDimmed (dim);
}

void TrackArea::setResidualAmplified (bool amplified)
{
    residualAmplified = amplified;
    rLane->setDisplayGain (amplified ? residualViewGain : 1.0f);

    const juce::String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    rHeader->viewGain.setButtonText (amplified ? "+24" : "0 dB");
    rHeader->viewGain.setToggleState (amplified, juce::dontSendNotification);
    rHeader->setSubLabel ("A " + minus + " all stems");   // the pad itself shows the view gain
}

void TrackArea::refreshPalette()
{
    aLane->setColour (theme::colour (C::colourA));
    rLane->setColour (theme::colour (C::accent));
    for (int i = 0; i < numStems; ++i)
    {
        stemALanes[(size_t) i]->setColour (theme::stemColour (i));
        stemLanes[(size_t) i]->setColour (theme::stemColour (i));
    }

    aHeader->repaint();
    rHeader->repaint();
    for (auto& h : stemAHeaders)
        h->repaint();
    for (auto& h : stemHeaders)
        h->repaint();
    content.repaint();
    repaint();
}

void TrackArea::updateStemPlaceholders()
{
    const juce::String textA = stemsAState == StemsState::loading
                                   ? juce::String (juce::CharPointer_UTF8 ("Loading stems\xe2\x80\xa6"))
                                   : stemsAState == StemsState::ready || stemsAState == StemsState::preview
                                         ? juce::String (juce::CharPointer_UTF8 ("Rendering waveform\xe2\x80\xa6"))
                                         : juce::String();
    for (int i = 0; i < numStems; ++i)
        stemALanes[(size_t) i]->setPlaceholder (stemAPresent[(size_t) i] ? textA : juce::String ("No file"));

    const juce::String text = stemsState == StemsState::loading
                                  ? juce::String (juce::CharPointer_UTF8 ("Loading stems\xe2\x80\xa6"))
                                  : stemsState == StemsState::ready || stemsState == StemsState::preview
                                        ? juce::String (juce::CharPointer_UTF8 ("Rendering waveform\xe2\x80\xa6"))
                                        : juce::String();
    for (int i = 0; i < numStems; ++i)
        stemLanes[(size_t) i]->setPlaceholder (stemPresent[(size_t) i] ? text : juce::String ("No file"));
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
    if (stemVsStemMode)
    {
        const float fixed = (rulerPx + 2.0f * groupRowPx + screenBottomPx) * uiScale;
        const float fill = ((float) viewportHeight - fixed) / (2.0f * (float) numStems + rLaneFactor);
        return juce::jmax (theme::scaled (minLanePx, uiScale), (int) std::floor (fill));
    }

    const float fixed = (rulerPx + groupRowPx + screenBottomPx) * uiScale;
    const float fill = ((float) viewportHeight - fixed) / (aLaneFactor + (float) numStems + rLaneFactor);
    return juce::jmax (theme::scaled (minLanePx, uiScale), (int) std::floor (fill));
}

int TrackArea::contentHeightFor (int lane) const
{
    if (stemVsStemMode)
        return theme::scaled (rulerPx, uiScale) + 2 * theme::scaled (groupRowPx, uiScale) + theme::scaled (screenBottomPx, uiScale)
               + 2 * numStems * lane + juce::roundToInt ((float) lane * rLaneFactor);

    return theme::scaled (rulerPx, uiScale) + theme::scaled (groupRowPx, uiScale) + theme::scaled (screenBottomPx, uiScale)
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
    const int columnW = theme::scaled (channelColumnPx, uiScale);
    const int screenX = columnW + theme::scaled (columnGapPx, uiScale);
    const int screenW = juce::jmax (1, width - screenX);
    const int rulerH = theme::scaled (rulerPx, uiScale);
    const int groupH = theme::scaled (groupRowPx, uiScale);
    const int aH = juce::roundToInt ((float) lane * aLaneFactor);
    const int rH = juce::roundToInt ((float) lane * rLaneFactor);

    separatorYs.clearQuick();

    int y = 0;
    channelsLabel = { 0, y, columnW, rulerH };
    if (stemVsStemMode)
    {
        const int linkW = theme::scaled (68.0f, uiScale);
        const int linkH = theme::scaled (18.0f, uiScale);
        linkMsButton.setFaceBounds ({ columnW - linkW, y + (rulerH - linkH) / 2, linkW, linkH });
    }
    ruler.setBounds (screenX, y, screenW, rulerH);
    y += rulerH;

    juce::Array<juce::Rectangle<int>> darken;
    const auto place = [&] (TrackHeader& header, WaveformLane& l, int h)
    {
        header.setBounds (0, y, columnW, h);
        l.setBounds (screenX, y, screenW, h);
        darken.add ({ 0, y, screenW, h });
        y += h;
    };

    if (stemVsStemMode)
    {
        groupALabel = { 0, y, columnW, groupH };
        y += groupH;

        for (int i = 0; i < numStems; ++i)
        {
            separatorYs.add (y);
            place (*stemAHeaders[(size_t) i], *stemALanes[(size_t) i], lane);
        }
        separatorYs.add (y - 1);
    }
    else
    {
        place (*aHeader, *aLane, aH);
        separatorYs.add (y - 1);            // A's bottom border
    }

    groupLabel = { 0, y, columnW, groupH };
    y += groupH;

    for (int i = 0; i < numStems; ++i)
    {
        separatorYs.add (y);                // each stem lane's top border
        place (*stemHeaders[(size_t) i], *stemLanes[(size_t) i], lane);
    }

    separatorYs.add (y);                    // the Residual lane's top border
    place (*rHeader, *rLane, rH);
    y += theme::scaled (screenBottomPx, uiScale);

    screenArea = { screenX, 0, screenW, y };
    overlay.setBounds (screenX, 0, screenW, y);
    overlay.setRulerHeight (rulerH);
    overlay.setDarkenAreas (darken);
    content.repaint();
}

void TrackArea::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour (C::ground));
}

void TrackArea::paintContent (juce::Graphics& g)
{
    using theme::colour;
    const float s = uiScale;
    g.fillAll (colour (C::ground));

    // The waveform screen and its lane separators (the lanes and the ruler are transparent)
    theme::drawScreen (g, screenArea.toFloat(), s);
    g.setColour (colour (C::screenLine));
    for (const int y : separatorYs)
        g.fillRect (screenArea.getX(), y, screenArea.getWidth(), 1);

    // Channel column labels: "CHANNELS" and the group row(s)
    theme::drawSilk (g, "Channels", channelsLabel.toFloat(), 10.0f * s, colour (C::silk));

    const float px = 10.0f * s;
    if (stemVsStemMode)
    {
        auto rowA = groupALabel.toFloat();
        theme::drawSilk (g, "A", rowA.removeFromLeft (theme::silkWidth ("A", px) + 1.0f), px, colour (C::colourA));
        rowA.removeFromLeft (8.0f * s);
        theme::drawSilk (g, "Stem A", rowA, px, colour (C::silk));

        auto rowB = groupLabel.toFloat();
        theme::drawSilk (g, "B", rowB.removeFromLeft (theme::silkWidth ("B", px) + 1.0f), px, colour (C::colourB));
        rowB.removeFromLeft (8.0f * s);
        theme::drawSilk (g, "Stem B", rowB, px, colour (C::silk));
    }
    else
    {
        auto row = groupLabel.toFloat();
        theme::drawSilk (g, "B", row.removeFromLeft (theme::silkWidth ("B", px) + 1.0f), px, colour (C::colourB));
        row.removeFromLeft (8.0f * s);
        theme::drawSilk (g, "Stem sum", row, px, colour (C::silk));
    }
}

} // namespace stemab
