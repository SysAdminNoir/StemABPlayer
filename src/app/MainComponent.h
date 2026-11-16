#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <memory>
#include <optional>

#include "AppSettings.h"
#include "FilePreview.h"
#include "SessionLoader.h"
#include "SessionState.h"
#include "WaveformBuilder.h"
#include "engine/PlaybackEngine.h"
#include "load/DecodeStash.h"
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

    /** Same as choosing both files in the dialogs (used for "Stem AB Player.exe <original> <stem file>"). B is
        a Stem File (a juce::File converts) or a Stem Set. */
    void loadPair (const juce::File& original, const StemSource& stems);

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
    /** Open B: one Stem File, or 1-4 separate stem files (a Stem Set, mapped to the Stems by their names). */
    void chooseStemFile();
    /** The files chosen in the Open B dialog, as a Stem File or a Stem Set; an empty source and `error` set if
        they cannot be used. */
    static StemSource stemSourceFromChoice (const juce::Array<juce::File>&, juce::String& error);
    /** The lanes and Mute / Solo of Stems without a file (a Stem Set with fewer than four). */
    void applyStemPresence();
    void startLoadIfReady();
    /** A file chosen in a dialog: its lane(s) start over and its waveform preview starts at once (before the
        pair is complete). Not for the file the loaded pair already uses (the reload reuses that one). */
    void previewChosenOriginal();
    void previewChosenStemFile();
    /** Back to the empty state: transport paused, both files cleared, the engine's session dropped, every
        load / preview / waveform job cancelled, lanes and readouts reset, the saved resume pair forgotten. */
    void unloadAll();
    void setPreviewTimeline (juce::int64 length, double sampleRate);
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
    /** Saves and applies a colour palette: the theme, the LookAndFeel, the waveform images, every window. */
    void setPalette (AppSettings::Palette);

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

    // Destruction order matters (reverse of declaration): the previews, the waveform jobs and the loader go
    // before the device, and the device before the engine. The settings go last (their destructor writes the
    // file). The decode stash is shared by the previews and the loader's decoders (it outlives both).
    AppSettings settings;
    PlaybackEngine engine;
    juce::AudioDeviceManager deviceManager;
    EngineCallback engineCallback { engine };
    std::shared_ptr<DecodeStash> decodeStash = std::make_shared<DecodeStash>();
    SessionLoader loader { deviceManager, engine, DecodeStash::wrap (decodeStash) };
    WaveformBuilder waveforms;
    FilePreview previews { decodeStash };
    juce::String initialiseError;

    juce::File originalFile;
    StemSource stemSource;                          // B: a Stem File or a Stem Set
    juce::File pairOriginal;                        // the pair last handed to loader.load()
    StemSource pairStems;
    juce::String openNotice;                        // why the last Open B choice was not used
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<PreferencesWindow> preferences;     // before the device's destruction (the selector listens to it)
    juce::TooltipWindow tooltips { nullptr, 600 };

    HeaderBar header;
    TrackArea tracks;
    MeterBridge bridge;
    ControlDeck deck;

    std::shared_ptr<const Session> shownSession;    // what the lanes show (polled from the loader)
    bool originalSummaryReady = false;

    // Previews of a file chosen on its own (FilePreview); the pair's own summaries replace them.
    bool originalPreviewShown = false, stemsPreviewShown = false;
    juce::int64 previewLength = 0;                  // the timeline while no session exists (0: none)
    double previewRate = 0.0;
    juce::String previewNotice;                     // e.g. a chosen file that could not be decoded

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
