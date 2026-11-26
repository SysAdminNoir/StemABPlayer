#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ui/Widgets.h"

namespace stemab
{

/** The bottom control panel, three columns:

      1. Transport - the time screen (m:ss.mmm, with "Sample ..." / "Total ..." stacked on the right), then the
         keys To start, Back 5 s, Play/Pause (a green pad), Forward 5 s, Loop (lit while looping).
      2. Crossfade - "A · ORIGINAL ... CROSSFADE A 50 · B 50 ... STEM SUM · B", the hardware fader, then the
         pads R Residual, Equal power and Centre (the group dims while Residual is on).
      3. Master - the Master Gain knob (red arc, "MASTER 0.0 dB" under it) and the CLIP pad.

    The widgets are public plain JUCE widgets: MainComponent wires them to the engine. When the crossfade
    column would get too narrow, it moves to its own row above the other two. */
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
    HwButton residualButton { HwButton::Kind::pad }, curveButton { HwButton::Kind::pad }, centreButton { HwButton::Kind::pad };

    juce::Slider masterSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    ClipButton clipButton;

    void setTime (const juce::String& time, const juce::String& samples);
    void setCrossfadeReadout (const juce::String&);
    void setCrossfadeDimmed (bool);
    void setStemVsStemMode (bool);
    void setGainText (const juce::String&);
    void setUiScale (float);

    int getPreferredHeight (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Metrics
    {
        int transportW, masterW, transportH, xfH, masterH, padX, padY, gap;
    };
    /** Column widths shrink (transport 380 -> 336, master 250 -> 196) when the crossfade column gets narrow. */
    Metrics metrics (int width) const;
    bool needsTwoRows (int width) const;

    float uiScale = 1.0f;
    bool crossfadeDimmed = false;
    bool stemVsStemMode = false;
    juce::String timeText { "0:00.000" }, sampleText, xfReadout, gainText;

    juce::Rectangle<int> timeBox, xfLabelRow, masterLabel, clipLabel;
};

} // namespace stemab
