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
class ControlDeck final : public juce::Component,
                          public juce::TooltipClient      // the time readout's hint (the other widgets set their own)
{
public:
    ControlDeck();
    ~ControlDeck() override;

    IconButton toStartButton { "Go to start (Home)", IconButton::Icon::toStart },
               backButton    { "Back 5 s (Shift+Left)", IconButton::Icon::back },
               playButton    { "Play / Pause (Space)", IconButton::Icon::playPause },
               forwardButton { "Forward 5 s (Shift+Right)", IconButton::Icon::forward },
               loopButton    { "Loop the whole track (L)", IconButton::Icon::loop };

    juce::Slider crossfadeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    HwButton residualButton { HwButton::Kind::pad }, curveButton { HwButton::Kind::pad }, centreButton { HwButton::Kind::pad };

    juce::Slider masterSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    ClipButton clipButton;

    /** Updates the readout. The painted text and the Sample/Total lines always follow; an open time editor
        (see below) is never touched, so what the user is typing survives the timer ticks. */
    void setTime (const juce::String& time, const juce::String& samples);

    /** The time readout is click-to-type while a track is loaded: a click opens an editor over the time, Enter
        calls onTimeEntered (seconds, relative: a "+5" / "-2.5" offset) and closes it. Off: the click does nothing
        (and an open editor closes without seeking). */
    void setTimeEditable (bool);
    std::function<void (double seconds, bool relative)> onTimeEntered;
    /** The editor closed with the keyboard focus still ours (Enter, Escape, a click elsewhere in the window):
        MainComponent takes the focus back so its shortcuts work again. */
    std::function<void()> onTimeEditorClosed;

    void setCrossfadeReadout (const juce::String&);
    void setCrossfadeDimmed (bool);
    void setStemVsStemMode (bool);
    void setGainText (const juce::String&);
    void setUiScale (float);

    int getPreferredHeight (int width) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;
    juce::String getTooltip() override;

private:
    bool isOverEditableTime() const;
    /** Where the time screen puts the big time and the stacked Sample/Total lines (both inside timeBox). */
    struct TimeLayout { juce::Rectangle<float> time, right; };
    TimeLayout timeLayout() const;
    void openTimeEditor();
    void closeTimeEditor (bool restoreFocus);
    void commitTimeEditor();
    void layoutTimeEditor();
    void setTimeEditorInvalid (bool);

    /** Sees every mouse press in the app while the editor is open, so a click on a key (keys never take keyboard
        focus, so the editor would not notice) or anywhere else cancels the entry. */
    struct ClickAwayWatcher final : public juce::MouseListener
    {
        explicit ClickAwayWatcher (ControlDeck& d) : deck (d) {}
        void mouseDown (const juce::MouseEvent& e) override;
        ControlDeck& deck;
    };

    juce::TextEditor timeEditor;
    ClickAwayWatcher clickAwayWatcher { *this };
    bool timeEditable = false, timeEditorInvalid = false;
    juce::Time openingMouseDownTime;

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
