#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace stemab
{

/** Transparent layer over the whole waveform column (ruler + every lane):

      - draws the played-region darkening, the single playhead (2 px line with glow, triangle cap, m:ss time
        bubble on the ruler) and, before any session, a centred hint;
      - turns click / drag anywhere into seeks: one on mouse-down, then at most ~30 per second while dragging
        (the drawn playhead follows the mouse at once), and the last position on mouse-up.

    update() is called from the UI timer; it repaints only the narrow strip around the old and the new
    playhead, so the cached lane images underneath are just blitted, never re-rendered. */
class PlayheadOverlay final : public juce::Component
{
public:
    PlayheadOverlay();

    /** Called with the target sample (0 .. length) on the message thread. */
    std::function<void (juce::int64)> onSeek;

    void setTimeline (juce::int64 lengthSamples, double sampleRate);
    void setRulerHeight (int);
    void setDarkenAreas (juce::Array<juce::Rectangle<int>> localAreas);
    void setHint (const juce::String&);
    void setUiScale (float);

    /** Timer tick: the engine's playhead (source-rate samples). Also flushes a throttled drag seek. */
    void update (juce::int64 playheadSample);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    float sampleToX (juce::int64) const noexcept;
    juce::int64 xToSample (float x) const noexcept;
    juce::String bubbleText() const;
    juce::Rectangle<int> stripAround (float x) const;
    void moveShownTo (juce::int64 sample);
    void sendSeek (juce::int64 sample);

    static constexpr double dragSeekIntervalMs = 33.0;   // ~30 seeks per second while dragging
    static constexpr double holdAfterDragMs = 150.0;     // keep showing the target until the engine has jumped

    juce::int64 length = 0;
    double rate = 0.0;
    int rulerHeight = 30;
    float uiScale = 1.0f;
    juce::Array<juce::Rectangle<int>> darkenAreas;
    juce::String hint;

    juce::int64 shownSample = 0;
    bool dragging = false;
    juce::int64 pendingSeek = -1;
    double lastSeekMs = 0.0;
    double holdUntilMs = 0.0;

    float paintedX = -1.0f;
    juce::String paintedBubble;
};

} // namespace stemab
