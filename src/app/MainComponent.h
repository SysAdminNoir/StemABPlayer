#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <memory>
#include <optional>

#include "AppSettings.h"
#include "SessionLoader.h"
#include "SessionState.h"
#include "WaveformBuilder.h"
#include "engine/PlaybackEngine.h"
#include "ui/ControlDeck.h"
#include "ui/HeaderBar.h"
#include "ui/MeterBridge.h"
#include "ui/PreferencesPanel.h"
#include "ui/TrackArea.h"

namespace stemab
{

/** The main panel and coordinator: header bar, track area (A / Stems / R waveform lanes with one playhead),
    meter bridge and the bottom control deck. It owns the engine, the device, the SessionLoader and the
    WaveformBuilder, and wires the widgets to them. The audio callback calls only PlaybackEngine::process;
    everything else polls the engine from a 30 fps timer.

    Two kinds of resizing: the window is freely resizable (lanes grow, the track area scrolls when even the
    minimum lane height does not fit), and the interface scale (A- / A+, Ctrl+- / Ctrl+= / Ctrl+0) steps
    90 / 100 / 115 / 130 %, multiplying every font size and metric through one uiScale factor.

    Settings (AppSettings, %APPDATA%/StemABPlayer/settings.xml): the interface scale, the Open-dialog folders,
    the audio device setup and - with "Resume last session on launch" - the last pair, playhead and mix
    settings (SessionState), saved at most about once a second while they change, and on shutdown. */
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    /** Smallest client-area size; below it the track area scrolls (and at 115 / 130 % the deck may wrap). */
    static constexpr int minimumWidth = 1100;
    static constexpr int minimumHeight = 720;

    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Same as choosing both files in the dialogs (used for "Stem AB Player.exe <original> <stem file>"). */
    void loadPair (const juce::File& original, const juce::File& stems);

    /** At launch, without files on the command line: if "Resume last session on launch" is on and the saved
        pair still exists, applies the saved mix settings, loads the pair and (once A is playable) moves the
        playhead to the saved position, paused. Returns false if nothing was resumed (a missing file is
        reported in the header status). */
    bool resumeLastSession();

    AppSettings& getSettings() noexcept         { return settings; }

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

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void chooseOriginal();
    void chooseStemFile();
    void startLoadIfReady();
    void showPreferences();
    void updateStatusText();
    void updateMixButtons();
    void pollSession();
    void seekBySeconds (double seconds);
    void seekTo (juce::int64 sample);
    void toggleStemSolo (int i);
    void toggleStemMute (int i);
    void setListenAB (float x);
    void toggleResidual();
    void toggleLoop();
    void resetClip();

    void setScaleIndex (int index);
    float uiScale() const noexcept;

    /** The repo's "Audio Files" / "Stem Files" when present (next to the sources or above the exe), else the
        user's Music folder. */
    juce::File builtInFolder (AppSettings::Folder) const;
    /** Where an Open dialog starts: the last-used folder (if that option is on), the folder chosen in
        Preferences, or builtInFolder(). */
    juce::File chooserFolder (AppSettings::Folder) const;

    // Session resume / save (see SessionState)
    std::optional<SessionState> captureSessionState() const;
    void applySessionState (const SessionState&);
    void saveSessionState (bool force);
    void updateResumeSeek();
    void cancelResume();
    void saveDeviceState();

    // Destruction order matters (reverse of declaration): the waveform jobs and the loader go before the
    // device, and the device before the engine. The settings go last (their destructor writes the file).
    AppSettings settings;
    PlaybackEngine engine;
    juce::AudioDeviceManager deviceManager;
    EngineCallback engineCallback { engine };
    SessionLoader loader { deviceManager, engine };
    WaveformBuilder waveforms;
    juce::String initialiseError;

    juce::File originalFile, stemFile;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<PreferencesWindow> preferences;     // before the device's destruction (the selector listens to it)
    juce::TooltipWindow tooltips { nullptr, 600 };

    HeaderBar header;
    TrackArea tracks;
    MeterBridge bridge;
    ControlDeck deck;

    std::shared_ptr<const Session> shownSession;    // what the lanes show (polled from the loader)
    bool originalSummaryReady = false;

    int scaleIndex = 1;                             // into scaleSteps (100 %)
    double clipShownUntilMs = 0.0;
    bool lastClipShown = false;
    bool stemControlsEnabled = false;   // the playing session has Stems (B, R, Crossfade, Mute/Solo usable)
    bool focusInitialised = false;

    // Resume: the saved playhead waits for the Original-only Session to be live (-1 = nothing pending).
    juce::int64 resumeSeekTarget = -1;
    int resumeSeekAttempts = 0;
    bool resumeSeekIssued = false;
    juce::String resumeNotice;                      // e.g. a saved file that no longer exists
    std::optional<SessionState> lastSavedSession;
    double lastSessionSaveMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace stemab
