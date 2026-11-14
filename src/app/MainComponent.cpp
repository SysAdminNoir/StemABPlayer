#include "MainComponent.h"

#include "Theme.h"
#include "decode/Decode.h"

namespace stemab
{

//==============================================================================
void MainComponent::SilenceCallback::audioDeviceIOCallbackWithContext (const float* const*,
                                                                       int,
                                                                       float* const* outputChannelData,
                                                                       int numOutputChannels,
                                                                       int numSamples,
                                                                       const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
}

//==============================================================================
MainComponent::MainComponent()
{
    // 0 inputs, 2 outputs.
    initialiseError = deviceManager.initialiseWithDefaultDevices (0, 2);

    deviceManager.addAudioCallback (&silenceCallback);
    deviceManager.addChangeListener (this);

    infoLabel.setJustificationType (juce::Justification::topLeft);
    infoLabel.setFont (juce::Font (juce::FontOptions (15.0f)));
    infoLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (infoLabel);

    // Outputs only; advanced options (sample rate / buffer size) always shown.
    deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager,
                                                                           0, 0,      // input channels (none)
                                                                           0, 2,      // output channels
                                                                           false,     // MIDI input
                                                                           false,     // MIDI output
                                                                           true,      // stereo pairs
                                                                           false);    // don't hide advanced options
    deviceSelector->setSize (700, 400);
    selectorViewport.setViewedComponent (deviceSelector.get(), false);
    selectorViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (selectorViewport);

    updateDeviceInfo();
    setSize (1280, 800);
}

MainComponent::~MainComponent()
{
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&silenceCallback);
    deviceManager.closeAudioDevice();

    selectorViewport.setViewedComponent (nullptr, false);
    deviceSelector.reset();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (theme::windowArgb));

    g.setColour (juce::Colour (theme::panelArgb));
    g.fillRoundedRectangle (panelBounds.toFloat(), 8.0f);

    g.setColour (juce::Colour (theme::outlineArgb));
    g.drawRoundedRectangle (panelBounds.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void MainComponent::resized()
{
    constexpr int margin = 16;
    constexpr int pad = 16;

    panelBounds = getLocalBounds().reduced (margin);
    auto inner = panelBounds.reduced (pad);

    infoLabel.setBounds (inner.removeFromTop (112));
    inner.removeFromTop (8);

    selectorViewport.setBounds (inner);
    deviceSelector->setSize (juce::jmax (400, selectorViewport.getMaximumVisibleWidth()),
                             deviceSelector->getHeight());
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateDeviceInfo();
}

void MainComponent::updateDeviceInfo()
{
    juce::StringArray lines;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        lines.add ("Device type:  " + deviceManager.getCurrentAudioDeviceType());
        lines.add ("Device:  " + device->getName());
        lines.add ("Sample rate:  " + juce::String (device->getCurrentSampleRate(), 0) + " Hz"
                   + "      Buffer size:  " + juce::String (device->getCurrentBufferSizeSamples()) + " samples"
                   + "      Output latency:  " + juce::String (device->getOutputLatencyInSamples()) + " samples");
    }
    else
    {
        lines.add ("No audio device open" + (initialiseError.isNotEmpty() ? " (" + initialiseError + ")" : juce::String()));
    }

    juce::StringArray types;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        types.add (type->getTypeName());

    lines.add ("Available types:  " + types.joinIntoString (", ")
               + (JUCE_ASIO ? "" : "   [built without ASIO]"));
    lines.add (juce::String (ffmpegVersionString()));

    infoLabel.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
}

} // namespace stemab
