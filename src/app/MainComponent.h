#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace stemab
{

/** Checkpoint-1 main view: audio-device selector plus a live read-out of the active device.
    Owns the AudioDeviceManager (0 in / 2 out) and a callback that just writes silence. */
class MainComponent final : public juce::Component,
                            private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** Writes silence. Allocation-free and lock-free; safe on the audio thread. */
    class SilenceCallback final : public juce::AudioIODeviceCallback
    {
    public:
        void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
        void audioDeviceStopped() override {}

        void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                               int numInputChannels,
                                               float* const* outputChannelData,
                                               int numOutputChannels,
                                               int numSamples,
                                               const juce::AudioIODeviceCallbackContext& context) override;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateDeviceInfo();

    // Declaration order = destruction order in reverse: the selector must die before the manager.
    juce::AudioDeviceManager deviceManager;
    SilenceCallback silenceCallback;
    juce::String initialiseError;

    juce::Label infoLabel;
    juce::Viewport selectorViewport;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;

    juce::Rectangle<int> panelBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace stemab
