#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

#include "app/ui/LevelMeter.h"
#include "app/ui/Widgets.h"

namespace stemab
{

/** The bottom control deck, three groups:

      1. Transport - go to start, back 5 s, Play/Pause, forward 5 s, Loop; time (m:ss.mmm) + sample readout.
      2. Crossfade - "A - ORIGINAL ... A 50 - B 50 ... STEM SUM - B" labels, the crossfade fader, then
         R - Residual, Equal-power and Centre (the group dims while Residual is on).
      3. Master - Master Gain fader with its readout, the stereo Out meter and the CLIP LED.

    The widgets are public plain JUCE widgets: MainComponent wires them to the engine. When the crossfade
    group would get too narrow, it moves to its own row above the other two. */
class ControlDeck final : public juce::Component
{
public:
    ControlDeck();

    IconButton toStartButton { "Go to start (Home)", IconButton::Icon::toStart },
               backButton    { "Back 5 s (Shift+Left)", IconButton::Icon::back },
               playButton    { "Play / Pause (Space)", IconButton::Icon::playPause },
               forwardButton { "Forward 5 s (Shift+Right)", IconButton::Icon::forward },
               loopButton    { "Loop the whole track (L)", IconButton::Icon::loop };

    juce::Slider crossfadeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton residualButton, curveButton, centreButton;

    juce::Slider masterSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    ClipButton clipButton;
    std::array<LevelMeter, 2> outMeters;    // horizontal (set in the constructor)

    void setTime (const juce::String& time, const juce::String& samples);
    void setCrossfadeReadout (const juce::String&);
    void setCrossfadeDimmed (bool);
    void setGainText (const juce::String&);
    void setUiScale (float);

    int getPreferredHeight (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Metrics
    {
        int transportW, masterW, rowH, xfH, masterH, padX, padY, gap;
    };
    Metrics metrics() const;
    bool needsTwoRows (int width) const;

    float uiScale = 1.0f;
    bool crossfadeDimmed = false;
    juce::String timeText { "0:00.000" }, sampleText, xfReadout, gainText;

    juce::Rectangle<int> timeBox, xfLabelRow, hintArea, masterBox, masterLabelRow;
};

} // namespace stemab
