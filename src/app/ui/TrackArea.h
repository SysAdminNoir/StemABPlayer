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

/** The track area: a header column (A, the "B - Stem Sum" group row, the four Stems, R) beside the waveform
    column (time ruler + one lane per track), with one PlayheadOverlay over the whole waveform column.

    Lanes grow to fill the available height (A is 1.5x a stem lane, R 0.6x); when even the minimum lane
    height does not fit, the area scrolls vertically (juce::Viewport). Every metric follows setUiScale(). */
class TrackArea final : public juce::Component
{
public:
    static constexpr int numStems = 4;
    using SummaryPtr = std::shared_ptr<const WaveformSummary>;

    /** What the stem / residual lanes show while they have no waveform. */
    enum class StemsState { noSession, loading, ready };

    TrackArea();

    void setUiScale (float);
    void setTimeline (juce::int64 lengthSamples, double sampleRate);

    void setOriginalSummary (SummaryPtr);
    void setOriginalState (bool rendering);            // a session exists but A's waveform is still being computed
    void setStemSummaries (const std::array<SummaryPtr, numStems>&, SummaryPtr residual);
    void clearStemSummaries();
    void setStemsState (StemsState);
    void setAudibleStems (const std::array<bool, numStems>&);
    void setOriginalSubLabel (const juce::String&);
    void setHint (const juce::String&);

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
    void setResidualAmplified (bool);    // R lane display: +24 dB (visible) or true level

    float uiScale = 1.0f;
    bool residualAmplified = true;
    StemsState stemsState = StemsState::noSession;
    bool originalRendering = false;

    juce::Viewport viewport;
    Content content { *this };

    std::unique_ptr<TrackHeader> aHeader, rHeader;
    std::array<std::unique_ptr<TrackHeader>, numStems> stemHeaders;

    TimeRuler ruler;
    std::unique_ptr<WaveformLane> aLane, rLane;
    std::array<std::unique_ptr<WaveformLane>, numStems> stemLanes;
    PlayheadOverlay overlay;

    juce::Rectangle<int> tracksCell, groupHeaderRow, groupLaneRow;
    int headerWidth = 196;
};

} // namespace stemab
