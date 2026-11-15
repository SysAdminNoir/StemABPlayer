#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <memory>

#include "SessionLoader.h"
#include "engine/PlaybackEngine.h"

namespace stemab
{

/** Horizontal level meter: RMS bar plus a peak tick, -60..0 dBFS, red above 0 dBFS. Fed by the UI timer. */
class MeterBar final : public juce::Component
{
public:
    void setLevels (float peak, float rms);     // linear; applies a peak fall-back
    void paint (juce::Graphics&) override;

private:
    float shownPeak = 0.0f, shownRms = 0.0f;
};

/** Checkpoint-4 test harness (functional, not the mockup): open a pair, transport, A/B/R crossfade,
    Mute/Solo, meters, Master Gain, Clip, Live Null Depth, device/rate status. The audio callback calls only
    PlaybackEngine::process; everything else polls the engine from a 30 fps timer. */
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Same as choosing both files in the dialogs (used for "Stem AB Player.exe <original> <stem file>"). */
    void loadPair (const juce::File& original, const juce::File& stems);

private:
    /** The device callback: forwards to the engine and nothing else. */
    class EngineCallback final : public juce::AudioIODeviceCallback
    {
    public:
        explicit EngineCallback (PlaybackEngine& e) : engine (e) {}

        void audioDeviceAboutToStart (juce::AudioIODevice* device) override
        {
            engine.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
        }

        void audioDeviceStopped() override {}

        void audioDeviceIOCallbackWithContext (const float* const*, int,
                                               float* const* outputChannelData, int numOutputChannels,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override
        {
            engine.process (outputChannelData, numOutputChannels, numSamples);
        }

    private:
        PlaybackEngine& engine;
    };

    struct StemRow
    {
        juce::Label name;
        juce::TextButton mute { "M" }, solo { "S" };
        MeterBar meter;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void chooseOriginal();
    void chooseStemFile();
    void startLoadIfReady();
    void showAudioSettings();
    void updateStatusText();
    void updateMixButtons();
    void seekBySeconds (double seconds);
    void toggleStemSolo (int i);
    void toggleStemMute (int i);
    void setListenAB (float x);
    void toggleResidual();

    static juce::String formatTime (double seconds);
    juce::File defaultFolder (const char* name) const;

    // Destruction order matters (reverse of declaration): the loader and the device go before the engine.
    PlaybackEngine engine;
    juce::AudioDeviceManager deviceManager;
    EngineCallback engineCallback { engine };
    SessionLoader loader { deviceManager, engine };
    juce::String initialiseError;

    juce::File originalFile, stemFile;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::DialogWindow> settingsDialog;

    juce::TextButton openOriginalButton { "Open original..." }, openStemButton { "Open stem file..." },
                     settingsButton { "Audio settings..." };
    juce::Label statusLabel, deviceLabel;

    juce::TextButton playButton { "Play" }, loopButton { "Loop" };
    juce::Slider positionSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label timeLabel;

    juce::TextButton aButton { "A" }, bButton { "B" }, rButton { "R" }, curveButton { "Equal power" };
    juce::Slider crossfadeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label liveNullLabel;

    std::array<StemRow, Session::numStems> stemRows;

    juce::Label meterLabels[3];
    MeterBar meters[3][2];   // A, B, Output x L/R
    juce::Label clipLabel;
    juce::Label masterLabel;
    juce::Slider masterSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    double clipShownUntilMs = 0.0;
    bool lastClipShown = false;
    bool stemControlsEnabled = false;   // the playing session has Stems (B, R, Crossfade, Mute/Solo usable)
    bool focusInitialised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace stemab
