#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "analysis/WaveformSummary.h"
#include "app/ui/PlayheadOverlay.h"
#include "app/ui/TimeRuler.h"
#include "app/ui/TrackHeader.h"
#include "app/ui/WaveformLane.h"

namespace stemab
{

/** The track area: the channel column (raised strips: Original, the "B Stem Sum" group label, the four Stems,
    Residual) beside one sunken waveform screen (time ruler, the A lane, a group gap, four stem lanes and the
    Residual lane, 1 px separators), with one PlayheadOverlay over the whole screen. Each strip lines up with
    its lane.

    Lanes grow to fill the available height (A is 1.5x a stem lane, R 0.8x); when even the minimum lane
    height does not fit, the area scrolls vertically (juce::Viewport). Every metric follows setUiScale(). */
class TrackArea final : public juce::Component
{
public:
    static constexpr int numStems = 4;
    using SummaryPtr = std::shared_ptr<const WaveformSummary>;

    /** What the stem / residual lanes show while they have no waveform. `preview`: a Stem File chosen on its
        own - its Stems' waveforms are (being) previewed; the Residual waits for an Original. */
    enum class StemsState { noSession, preview, loading, ready };

    TrackArea();

    void setUiScale (float);
    void setTimeline (juce::int64 lengthSamples, double sampleRate);

    void setOriginalSummary (SummaryPtr);
    void setOriginalState (bool rendering);            // a session exists but A's waveform is still being computed
    void setStemSummaries (const std::array<SummaryPtr, numStems>&, SummaryPtr residual);
    void clearStemSummaries();
    void setStemsState (StemsState);
    /** Which Stems have a file (a Stem Set may have fewer than four). A Stem without one shows "No file", never
        a waveform, and its strip stays dimmed; the owner disables its Mute / Solo. Default: all four. */
    void setStemsPresent (const std::array<bool, numStems>&);
    bool isStemPresent (int i) const noexcept           { return stemPresent[(size_t) i]; }
    void setAudibleStems (const std::array<bool, numStems>&);
    void setOriginalSubLabel (const juce::String&);
    void setHint (const juce::String&);

    /** The R lane's view gain: +24 dB (visible) or the true level. Display only (the audio is untouched). */
    void setResidualAmplified (bool);
    bool isResidualAmplified() const noexcept           { return residualAmplified; }

    /** The palette changed: lane colours (re-rendered waveform images) and a repaint. */
    void refreshPalette();

    /** Timer tick. */
    void update (juce::int64 playheadSample)            { overlay.update (playheadSample); }

    PlayheadOverlay& getOverlay() noexcept              { return overlay; }
    TrackHeader& getOriginalHeader() noexcept           { return *aHeader; }
    TrackHeader& getStemHeader (int i) noexcept         { return *stemHeaders[(size_t) i]; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Content final : public juce::Component
    {
        explicit Content (TrackArea& o) : owner (o) { setOpaque (true); }
        void paint (juce::Graphics& g) override     { owner.paintContent (g); }
        TrackArea& owner;
    };

    void paintContent (juce::Graphics&);
    void layoutContent (int width, int laneHeight);
    int laneHeightFor (int viewportHeight) const;
    int contentHeightFor (int laneHeight) const;
    void updateStemPlaceholders();
    void updateStemDimming();

    float uiScale = 1.0f;
    bool residualAmplified = true;
    StemsState stemsState = StemsState::noSession;
    std::array<bool, numStems> stemPresent { true, true, true, true };
    bool originalRendering = false;

    juce::Viewport viewport;
    Content content { *this };

    std::unique_ptr<TrackHeader> aHeader, rHeader;
    std::array<std::unique_ptr<TrackHeader>, numStems> stemHeaders;

    TimeRuler ruler;
    std::unique_ptr<WaveformLane> aLane, rLane;
    std::array<std::unique_ptr<WaveformLane>, numStems> stemLanes;
    PlayheadOverlay overlay;

    juce::Rectangle<int> channelsLabel, groupLabel, screenArea;
    juce::Array<int> separatorYs;
};

} // namespace stemab
