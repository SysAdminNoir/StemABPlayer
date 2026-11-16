#include "MainComponent.h"

#include <cmath>
#include <limits>

#include "Theme.h"
#include "decode/Decode.h"
#include "engine/LiveNullDepth.h"
#include "ui/Format.h"

#ifndef STEMAB_SOURCE_DIR
 #define STEMAB_SOURCE_DIR ""
#endif

namespace stemab
{

namespace
{
    constexpr double masterMinDb = -60.0;
    constexpr double masterInfDb = -61.0;     // bottom of the fader = -inf

    using theme::scaleSteps;
    using theme::defaultScaleIndex;
    using theme::C;
    constexpr float bridgeWidthPx = 150.0f;

    const juce::String middleDot (juce::CharPointer_UTF8 ("\xc2\xb7"));
    const juce::String minusSign (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    const juce::String rho (juce::CharPointer_UTF8 ("\xcf\x81"));
    const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));

    juce::String gainText (double db)
    {
        if (db <= masterMinDb - 0.5)
            return minusSign + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e dB"));
        const auto v = juce::String (std::abs (db), 1);
        return (db < 0.0 && v.getDoubleValue() != 0.0 ? minusSign : juce::String()) + v + " dB";
    }

    /** Children never take keyboard focus: the shortcuts go to MainComponent. */
    void disableFocusRecursively (juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            child->setWantsKeyboardFocus (false);
            child->setMouseClickGrabsKeyboardFocus (false);
            disableFocusRecursively (*child);
        }
    }

    HeaderBar::Segment seg (const juce::String& text, C role, bool mono = true)
    {
        return { text, theme::colour (role), mono, false, false };
    }

    HeaderBar::Segment silk (const juce::String& text, C role = C::silkDim)
    {
        return { text, theme::colour (role), false, false, true };
    }

    HeaderBar::Segment dot (C role)
    {
        return { {}, theme::colour (role), false, true, false };
    }

    /** e.g. "FLAC 44.1k stereo" for the Original strip. */
    juce::String originalSubLabel (const juce::String& fileName, double sampleRate)
    {
        const auto ext = juce::File::createFileWithoutCheckingPath (fileName).getFileExtension()
                             .trimCharactersAtStart (".").toUpperCase();
        return (ext.isNotEmpty() ? ext + " " : juce::String())
               + format::kiloHertz (sampleRate).upToFirstOccurrenceOf (" kHz", false, false) + "k stereo";
    }
}

//==============================================================================
MainComponent::MainComponent()
{
    // The saved device setup (Preferences > Audio); a device that is gone falls back to the default one.
    {
        const auto savedDevice = settings.getAudioDeviceState();
        initialiseError = deviceManager.initialise (0, 2, savedDevice.get(), true);
    }
    deviceManager.addAudioCallback (&engineCallback);
    deviceManager.addChangeListener (this);

    loader.onStatusChanged = [this] { updateStatusText(); };

    scaleIndex = juce::jlimit (0, (int) scaleSteps.size() - 1, settings.getScaleIndex (defaultScaleIndex));

    // The saved colour palette, before anything is painted.
    theme::setPalette (settings.getColourPalette() == AppSettings::Palette::colourBlindSafe ? theme::PaletteId::colourBlindSafe
                                                                                          : theme::PaletteId::standard);
    if (auto* lf = dynamic_cast<theme::StemLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        lf->applyPalette();
    tracks.refreshPalette();    // the lanes were built (in their default colours) before this ran

    // Waveform summaries arrive on the message thread (stale ones are dropped by the builder).
    waveforms.onOriginalReady = [this] (WaveformBuilder::SummaryPtr s)
    {
        originalSummaryReady = true;
        originalPreviewShown = false;
        tracks.setOriginalSummary (std::move (s));
        tracks.setOriginalState (false);
    };
    waveforms.onStemsReady = [this] (const WaveformBuilder::StemSummaries& r)
    {
        stemsPreviewShown = false;
        tracks.setStemSummaries (r.stems, r.residual);
    };

    // Previews of a file chosen on its own. The pair's own summaries (above) win: a preview that arrives
    // after this pair's Original / Stems are in is dropped.
    previews.onOriginalReady = [this] (const FilePreview::OriginalResult& r)
    {
        if (r.file != originalFile || originalSummaryReady || loader.getStatus().original == StageState::done)
            return;

        if (r.error.isNotEmpty())
        {
            previewNotice = r.file.getFileName() + ": " + r.error;
            tracks.setOriginalState (false);
            updateStatusText();
            return;
        }

        originalPreviewShown = true;
        tracks.setOriginalSummary (r.summary);
        tracks.setOriginalState (false);
        tracks.setOriginalSubLabel (originalSubLabel (r.file.getFileName(), r.sampleRate));
        setPreviewTimeline (r.length, r.sampleRate);
    };
    previews.onStemsReady = [this] (const FilePreview::StemsResult& r)
    {
        if (r.file != stemFile || loader.getStatus().alignment == StageState::done)
            return;

        if (r.error.isNotEmpty())
        {
            previewNotice = r.file.getFileName() + ": " + r.error;
            updateStatusText();
            return;
        }

        stemsPreviewShown = true;
        tracks.setStemSummaries (r.stems, nullptr);     // unaligned; no Residual without an aligned pair
        if (! originalPreviewShown)
            setPreviewTimeline (r.length, r.sampleRate);
    };

    // Header
    header.openOriginalButton.onClick = [this] { chooseOriginal(); };
    header.openStemButton.onClick = [this] { chooseStemFile(); };
    header.settingsButton.onClick = [this] { showPreferences(); };
    header.unloadButton.onClick = [this] { unloadAll(); };
    header.scaleDownButton.onClick = [this] { setScaleIndex (scaleIndex - 1); };
    header.scaleUpButton.onClick = [this] { setScaleIndex (scaleIndex + 1); };

    // Transport
    deck.playButton.onClick = [this] { engine.togglePlayPause(); };
    deck.toStartButton.onClick = [this] { seekTo (0); };
    deck.backButton.onClick = [this] { seekBySeconds (-5.0); };
    deck.forwardButton.onClick = [this] { seekBySeconds (5.0); };
    deck.loopButton.onClick = [this]
    {
        engine.setLoopRegion (0, 0);    // whole track for now
        engine.setLoopEnabled (deck.loopButton.getToggleState());
    };

    // Seeking from the waveform column (throttled by the overlay while dragging)
    tracks.getOverlay().onSeek = [this] (juce::int64 sample) { seekTo (sample); };
    tracks.setHint ("Open an original and a stem file");

    // Crossfade
    deck.crossfadeSlider.setRange (0.0, 1.0, 0.0);
    deck.crossfadeSlider.setValue (0.0, juce::dontSendNotification);
    deck.crossfadeSlider.setDoubleClickReturnValue (true, 0.5);
    deck.crossfadeSlider.onValueChange = [this]
    {
        engine.setCrossfade ((float) deck.crossfadeSlider.getValue());
        updateMixButtons();
    };
    deck.residualButton.onClick = [this] { toggleResidual(); };
    deck.centreButton.onClick = [this] { setListenAB (0.5f); };
    deck.curveButton.onClick = [this]
    {
        engine.setCurve (engine.getCurve() == Curve::EqualPower ? Curve::Linear : Curve::EqualPower);
        updateMixButtons();
    };
    engine.setCurve (Curve::EqualPower);

    // Stems
    for (int i = 0; i < Session::numStems; ++i)
    {
        auto& h = tracks.getStemHeader (i);
        h.mute.onClick = [this, i] { toggleStemMute (i); };
        h.solo.onClick = [this, i] { toggleStemSolo (i); };
        h.mute.setTooltip ("Mute " + juce::String (stemName ((Stem) i)) + " (Shift+" + juce::String (i + 1) + ")");
        h.solo.setTooltip ("Solo " + juce::String (stemName ((Stem) i)) + " (" + juce::String (i + 1) + ")");
    }

    // Master Gain, Clip
    deck.masterSlider.setRange (masterInfDb, 0.0, 0.1);
    deck.masterSlider.setValue (0.0, juce::dontSendNotification);
    deck.masterSlider.setDoubleClickReturnValue (true, 0.0);
    deck.masterSlider.onValueChange = [this]
    {
        const double v = deck.masterSlider.getValue();
        engine.setMasterGainDb (v <= masterMinDb - 0.5 ? -std::numeric_limits<double>::infinity() : v);
        deck.setGainText (gainText (v));
    };
    deck.setGainText (gainText (0.0));
    deck.clipButton.onClick = [this] { resetClip(); };

    addAndMakeVisible (header);
    addAndMakeVisible (tracks);
    addAndMakeVisible (bridge);
    addAndMakeVisible (deck);

    // Keyboard shortcuts go to this component: no child takes focus.
    disableFocusRecursively (*this);
    setWantsKeyboardFocus (true);

    setScaleIndex (scaleIndex);
    updateMixButtons();
    updateStatusText();
    setSize (1280, 800);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    saveSessionState (true);    // the last position and mix settings (before anything is torn down)
    stopTimer();

    preferences.reset();

    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&engineCallback);
    deviceManager.closeAudioDevice();
    settings.save();
    // waveforms (cancels and joins its jobs) and loader (waits for its worker) are destroyed next.
}

//==============================================================================
float MainComponent::uiScale() const noexcept
{
    return scaleSteps[(size_t) scaleIndex];
}

void MainComponent::setScaleIndex (int index)
{
    scaleIndex = juce::jlimit (0, (int) scaleSteps.size() - 1, index);
    const float s = uiScale();

    if (auto* lf = dynamic_cast<theme::StemLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        lf->setTooltipScale (s);
    header.setUiScale (s);
    header.setScaleLabel (juce::String (juce::roundToInt (s * 100.0f)) + "%");
    header.scaleDownButton.setEnabled (scaleIndex > 0);
    header.scaleUpButton.setEnabled (scaleIndex < (int) scaleSteps.size() - 1);
    tracks.setUiScale (s);
    bridge.setUiScale (s);
    deck.setUiScale (s);
    resized();
    repaint();

    if (preferences != nullptr)
    {
        preferences->setUiScale (s);
        preferences->getPanel().setScaleIndex (scaleIndex);
    }

    settings.setScaleIndex (scaleIndex);
}

void MainComponent::setPalette (AppSettings::Palette palette)
{
    settings.setColourPalette (palette);
    settings.save();

    theme::setPalette (palette == AppSettings::Palette::colourBlindSafe ? theme::PaletteId::colourBlindSafe
                                                                        : theme::PaletteId::standard);
    if (auto* lf = dynamic_cast<theme::StemLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        lf->applyPalette();

    tracks.refreshPalette();        // re-renders the waveform images in the new lane colours
    updateStatusText();
    updateMixButtons();

    if (preferences != nullptr)
    {
        preferences->getPanel().setPalette (palette);
        preferences->refreshPalette();
    }

    // Every window (and every child) picks up the LookAndFeel colours again and repaints.
    for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
        if (auto* c = juce::Desktop::getInstance().getComponent (i))
        {
            c->sendLookAndFeelChange();
            c->repaint();
        }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour (C::ground));
}

void MainComponent::resized()
{
    const float s = uiScale();
    auto r = getLocalBounds();

    header.setBounds (r.removeFromTop (header.getPreferredHeight()));
    deck.setBounds (r.removeFromBottom (deck.getPreferredHeight (r.getWidth())));

    // The screen area: padding 4 18 8 (the status line above already gives some air), the meters on the right
    r = r.withTrimmedTop (theme::scaled (4.0f, s)).withTrimmedBottom (theme::scaled (8.0f, s)).reduced (theme::scaled (18.0f, s), 0);
    bridge.setBounds (r.removeFromRight (theme::scaled (bridgeWidthPx, s)));
    r.removeFromRight (theme::scaled (14.0f, s));
    tracks.setBounds (r);
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    loader.deviceConfigurationChanged();    // the rate policy (a different device tries Native Rate again)
    saveDeviceState();
    updateStatusText();
}

void MainComponent::saveDeviceState()
{
    if (const auto xml = deviceManager.createStateXml())   // null until a setup was chosen or restored
    {
        settings.setAudioDeviceState (xml.get());
        settings.save();
    }
}

void MainComponent::pollSession()
{
    const auto s = loader.getPlaybackSession();

    // What the stem / residual lanes say while they have no waveform.
    const auto& st = loader.getStatus();
    const bool stemsPreview = stemsPreviewShown || previews.isStemsPending();
    const auto stemsState = s == nullptr ? (stemsPreview ? TrackArea::StemsState::preview : TrackArea::StemsState::noSession)
                          : s->hasStems() ? TrackArea::StemsState::ready
                          : (st.stems == StageState::failed || st.alignment == StageState::failed)
                                ? TrackArea::StemsState::noSession
                                : TrackArea::StemsState::loading;
    tracks.setStemsState (stemsState);

    if (s == shownSession)
        return;

    const auto previous = shownSession;
    shownSession = s;

    if (s == nullptr)
    {
        waveforms.cancelOriginal();
        waveforms.cancelStems();
        tracks.setOriginalSummary (nullptr);
        tracks.setOriginalState (false);
        tracks.clearStemSummaries();
        tracks.setTimeline (0, 0.0);
        return;
    }

    tracks.setTimeline (s->length(), s->sampleRate());

    if (previous == nullptr || ! s->sharesOriginalWith (*previous))
    {
        // A new Original: its waveform, and the stem lanes start over - except for previews of this pair's
        // files, which stay until the real summaries replace them.
        originalSummaryReady = false;
        if (! originalPreviewShown)
        {
            tracks.setOriginalSummary (nullptr);
            tracks.setOriginalState (true);
        }
        if (! stemsPreviewShown)
            tracks.clearStemSummaries();
        waveforms.buildOriginal (s);

        tracks.setOriginalSubLabel (originalSubLabel (st.originalName, s->sampleRate()));
    }

    if (s->hasStems())
    {
        // New Stems or a corrected Alignment Offset: recompute (the old lanes stay until the new ones arrive).
        waveforms.buildStems (s);
    }
    else
    {
        waveforms.cancelStems();
        if (! stemsPreviewShown)
            tracks.clearStemSummaries();
    }
}

void MainComponent::timerCallback()
{
    engine.releaseRetiredSessions();

    if (! focusInitialised && isShowing())
    {
        focusInitialised = true;
        grabKeyboardFocus();   // shortcuts; children never take focus, a click on the background regains it
    }

    pollSession();
    updateResumeSeek();

    // Transport
    deck.playButton.setPlaying (engine.isPlaying());
    if (deck.loopButton.getToggleState() != engine.isLoopEnabled())
        deck.loopButton.setToggleState (engine.isLoopEnabled(), juce::dontSendNotification);

    const auto len = engine.getSessionLength();
    const double rate = engine.getSessionSampleRate();
    const auto playhead = engine.getPlayheadSample();

    if (len > 0 && rate > 0.0 && loader.getPlaybackSession() != nullptr)
        deck.setTime (format::minutesSecondsMillis ((double) playhead / rate),
                      "Sample " + format::withThousands (playhead) + "\nTotal " + format::withThousands (len));
    else if (previewLength > 0 && previewRate > 0.0)
        deck.setTime ("0:00.000", "Preview only " + middleDot + " " + format::minutesSecondsMillis ((double) previewLength / previewRate)
                                      + "\nOpen the other file to play");
    else
        deck.setTime ("0:00.000", "No track loaded");

    tracks.update (playhead);

    // Meters. Peaks are read-and-reset, so every tap is read once per tick and fed to every meter that shows it.
    for (int i = 0; i < Session::numStems; ++i)
    {
        const auto tap = (MeterTap) ((int) MeterTap::Drums + i);
        const auto l = engine.readMeter (tap, 0);
        const auto r = engine.readMeter (tap, 1);
        if (auto* m = tracks.getStemHeader (i).getMeter())
            m->setLevels (juce::jmax (l.peak, r.peak), juce::jmax (l.rms, r.rms));
    }

    const MeterTap taps[3] = { MeterTap::Original, MeterTap::StemSum, MeterTap::Output };
    float aPeak = 0.0f, aRms = 0.0f;
    for (int m = 0; m < 3; ++m)
        for (int c = 0; c < 2; ++c)
        {
            const auto reading = engine.readMeter (taps[m], c);
            bridge.setLevels (m, c, reading.peak, reading.rms);
            if (m == MeterBridge::a)
            {
                aPeak = juce::jmax (aPeak, reading.peak);
                aRms = juce::jmax (aRms, reading.rms);
            }
        }
    if (auto* m = tracks.getOriginalHeader().getMeter())
        m->setLevels (aPeak, aRms);

    // Clip (latched ~1 s; a click on the LED resets it)
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (engine.readAndClearClip())
        clipShownUntilMs = now + 1000.0;

    const bool clipShown = now < clipShownUntilMs;
    if (clipShown != lastClipShown)
    {
        lastClipShown = clipShown;
        deck.clipButton.setLit (clipShown);
    }

    // B/R availability follows the session the audio thread actually plays.
    if (engine.sessionHasStems() != stemControlsEnabled)
        updateMixButtons();

    // Live Null Depth (UI thread, against the immutable Session)
    juce::String live = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 dB"));
    if (auto session = loader.getPlaybackSession(); session != nullptr && session->hasStems() && session->length() == len)
    {
        const auto nd = measureLiveNullDepth (*session, playhead);
        live = nd.quiet ? juce::String ("quiet") : format::withMinusSign (juce::String (nd.nullDepthDb, 1)) + " dB";
    }
    bridge.setLiveNullDepth (live);

    updateStatusText();
    saveSessionState (false);
}

//==============================================================================
juce::File MainComponent::builtInFolder (AppSettings::Folder folder) const
{
    const char* name = folder == AppSettings::Folder::originals ? "Audio Files" : "Stem Files";

    if (const juce::File src (juce::String (STEMAB_SOURCE_DIR)); src.isDirectory() && src.getChildFile (name).isDirectory())
        return src.getChildFile (name);

    // Fallback: walk up from the exe.
    auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (int i = 0; i < 8 && dir.exists(); ++i, dir = dir.getParentDirectory())
        if (dir.getChildFile (name).isDirectory())
            return dir.getChildFile (name);

    return juce::File::getSpecialLocation (juce::File::userMusicDirectory);
}

juce::File MainComponent::chooserFolder (AppSettings::Folder folder) const
{
    if (settings.getRememberLastFolder())
        if (const auto last = settings.getLastUsedFolder (folder); last.isDirectory())
            return last;

    if (const auto chosen = settings.getFolder (folder); chosen.isDirectory())
        return chosen;

    return builtInFolder (folder);
}

void MainComponent::chooseOriginal()
{
    chooser = std::make_unique<juce::FileChooser> ("Open original (A)", chooserFolder (AppSettings::Folder::originals),
                                                   "*.flac;*.wav;*.aif;*.aiff;*.mp3;*.m4a;*.ogg;*.opus");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (const auto f = fc.getResult(); f.existsAsFile())
                              {
                                  cancelResume();
                                  settings.setLastUsedFolder (AppSettings::Folder::originals, f.getParentDirectory());
                                  originalFile = f;
                                  previewChosenOriginal();
                                  startLoadIfReady();
                                  updateStatusText();
                              }
                          });
}

void MainComponent::chooseStemFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Open stem file (B)", chooserFolder (AppSettings::Folder::stemFiles),
                                                   "*.stem.m4a;*.m4a;*.mp4");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (const auto f = fc.getResult(); f.existsAsFile())
                              {
                                  cancelResume();
                                  settings.setLastUsedFolder (AppSettings::Folder::stemFiles, f.getParentDirectory());
                                  stemFile = f;
                                  previewChosenStemFile();
                                  startLoadIfReady();
                                  updateStatusText();
                              }
                          });
}

void MainComponent::loadPair (const juce::File& original, const juce::File& stems)
{
    cancelResume();
    originalFile = original;
    stemFile = stems;
    startLoadIfReady();
    updateStatusText();
}

void MainComponent::startLoadIfReady()
{
    if (originalFile.existsAsFile() && stemFile.existsAsFile())
    {
        pairOriginal = originalFile;
        pairStems = stemFile;
        loader.load (originalFile, stemFile);
    }
}

void MainComponent::previewChosenOriginal()
{
    previewNotice.clear();
    if (originalFile == pairOriginal && loader.getStatus().original == StageState::done)
        return;     // the loaded pair's own Original: the reload reuses it (and its waveform stays)

    waveforms.cancelOriginal();
    originalSummaryReady = false;
    originalPreviewShown = false;
    tracks.setOriginalSummary (nullptr);
    tracks.setOriginalState (true);                 // "Rendering waveform..." until the preview (or A) is in
    previews.previewOriginal (originalFile);        // before the pair load, which then takes its decode
}

void MainComponent::previewChosenStemFile()
{
    previewNotice.clear();
    if (stemFile == pairStems && loader.getStatus().alignment == StageState::done)
        return;

    waveforms.cancelStems();
    stemsPreviewShown = false;
    tracks.clearStemSummaries();
    previews.previewStems (stemFile);
}

void MainComponent::setPreviewTimeline (juce::int64 length, double sampleRate)
{
    previewLength = length;
    previewRate = sampleRate;
    if (shownSession == nullptr)
        tracks.setTimeline (length, sampleRate);    // a session's own timeline wins once it exists
}

void MainComponent::unloadAll()
{
    cancelResume();
    engine.pause();                 // (setSession (nullptr) below fades a playing transport out anyway)

    // Every job: previews (and the decode stash), waveform summaries, the pair load. The loader drops the
    // engine's session; the audio thread retires it and releaseRetiredSessions() (timer) frees it here.
    previews.cancelAll();
    waveforms.cancelOriginal();
    waveforms.cancelStems();
    loader.unload();

    originalFile = stemFile = pairOriginal = pairStems = juce::File();
    shownSession = nullptr;
    originalSummaryReady = originalPreviewShown = stemsPreviewShown = false;
    previewLength = 0;
    previewRate = 0.0;
    previewNotice.clear();

    tracks.setOriginalSummary (nullptr);
    tracks.setOriginalState (false);
    tracks.clearStemSummaries();
    tracks.setStemsState (TrackArea::StemsState::noSession);
    tracks.setOriginalSubLabel ({});
    tracks.setTimeline (0, 0.0);
    tracks.update (0);
    resetClip();

    // Nothing to resume on the next launch (the mix settings stay as they are).
    settings.clearSession();
    settings.save();
    lastSavedSession.reset();

    updateMixButtons();
    updateStatusText();
}

void MainComponent::showPreferences()
{
    if (preferences == nullptr)
    {
        PreferencesPanel::Callbacks callbacks;
        callbacks.builtInFolder = [this] (AppSettings::Folder f) { return builtInFolder (f); };
        callbacks.setScaleIndex = [this] (int index) { setScaleIndex (index); };
        callbacks.setPalette = [this] (AppSettings::Palette p) { setPalette (p); };

        preferences = std::make_unique<PreferencesWindow> (settings, deviceManager, std::move (callbacks));
        preferences->onClosed = [this]
        {
            if (isShowing())
                grabKeyboardFocus();   // the shortcuts again
        };
        preferences->setUiScale (uiScale());
        preferences->getPanel().setScaleIndex (scaleIndex);
        preferences->centreAroundComponent (this, preferences->getWidth(), preferences->getHeight());
    }

    preferences->setVisible (true);
    preferences->toFront (true);
}

//==============================================================================
bool MainComponent::resumeLastSession()
{
    if (! settings.getResumeSession())
        return false;

    const auto saved = settings.getSession();
    if (! saved.has_value())
        return false;

    const juce::File original (saved->originalPath), stems (saved->stemFilePath);
    if (! original.existsAsFile() || ! stems.existsAsFile())
    {
        const auto& missing = original.existsAsFile() ? stems : original;
        resumeNotice = "last session not resumed (" + missing.getFileName() + " not found)";
        updateStatusText();
        return false;
    }

    applySessionState (*saved);
    loadPair (original, stems);     // (clears any earlier resume state)

    // The playhead is on A's timeline: it is applied once the Original-only Session is live, and the Stems
    // upgrade keeps the position (PlaybackEngine::upgradeSession). The transport stays paused.
    resumeSeekTarget = saved->playheadSample > 0 ? saved->playheadSample : -1;
    resumeSeekAttempts = 0;
    resumeSeekIssued = false;
    lastSavedSession = saved;       // nothing new to write until something changes
    lastSessionSaveMs = juce::Time::getMillisecondCounterHiRes();
    return true;
}

void MainComponent::cancelResume()
{
    resumeSeekTarget = -1;
    resumeNotice.clear();
}

void MainComponent::updateResumeSeek()
{
    if (resumeSeekTarget < 0)
        return;

    const auto& st = loader.getStatus();
    if (st.original == StageState::failed)
    {
        resumeSeekTarget = -1;
        return;
    }

    // Wait until the audio thread plays this load's Original-only (or upgraded) Session.
    const auto s = loader.getPlaybackSession();
    const auto len = engine.getSessionLength();
    if (st.original != StageState::done || s == nullptr || len <= 0 || len != s->length())
        return;

    const auto target = juce::jlimit ((juce::int64) 0, len, resumeSeekTarget);
    const auto tolerance = (juce::int64) (engine.getSessionSampleRate() * 0.5);

    if (resumeSeekIssued && std::abs (engine.getPlayheadSample() - target) <= tolerance)
    {
        resumeSeekTarget = -1;      // done (paused: the playhead sits exactly on it)
        return;
    }

    // A seek that meets a Session swap (e.g. a corrected Alignment Offset) is dropped by the engine: retry.
    if (++resumeSeekAttempts > 60)
    {
        resumeSeekTarget = -1;
        return;
    }

    engine.seek (target);
    resumeSeekIssued = true;
}

std::optional<SessionState> MainComponent::captureSessionState() const
{
    if (! originalFile.existsAsFile() || ! stemFile.existsAsFile())
        return std::nullopt;

    // Only once this pair's Original plays (until then the engine still reports the previous pair).
    const auto s = loader.getPlaybackSession();
    const auto len = engine.getSessionLength();
    if (loader.getStatus().original != StageState::done || s == nullptr || len <= 0 || len != s->length())
        return std::nullopt;

    SessionState st;
    st.originalPath = originalFile.getFullPathName();
    st.stemFilePath = stemFile.getFullPathName();
    st.playheadSample = engine.getPlayheadSample();
    st.crossfade = engine.getCrossfade();
    st.equalPowerCurve = engine.getCurve() == Curve::EqualPower;
    st.residualListen = engine.getListenMode() == ListenMode::Residual;
    for (int i = 0; i < Session::numStems; ++i)
    {
        st.muted[(size_t) i] = engine.isStemMuted (i);
        st.soloed[(size_t) i] = engine.isStemSoloed (i);
    }
    const double v = deck.masterSlider.getValue();
    st.masterGainDb = v <= masterMinDb - 0.5 ? -std::numeric_limits<double>::infinity() : v;
    st.loopEnabled = engine.isLoopEnabled();
    st.residualViewAmplified = tracks.isResidualAmplified();
    return st;
}

void MainComponent::applySessionState (const SessionState& st)
{
    engine.setCurve (st.equalPowerCurve ? Curve::EqualPower : Curve::Linear);
    engine.setListenMode (st.residualListen ? ListenMode::Residual : ListenMode::AB);
    engine.setCrossfade (st.crossfade);
    deck.crossfadeSlider.setValue (st.crossfade, juce::dontSendNotification);

    for (int i = 0; i < Session::numStems; ++i)
    {
        engine.setStemMuted (i, st.muted[(size_t) i]);
        engine.setStemSoloed (i, st.soloed[(size_t) i]);
    }

    deck.masterSlider.setValue (std::isfinite (st.masterGainDb) ? juce::jlimit (masterMinDb, 0.0, st.masterGainDb) : masterInfDb,
                                juce::dontSendNotification);
    deck.masterSlider.onValueChange();     // the engine and the readout

    engine.setLoopRegion (0, 0);    // whole track
    engine.setLoopEnabled (st.loopEnabled);
    deck.loopButton.setToggleState (st.loopEnabled, juce::dontSendNotification);

    tracks.setResidualAmplified (st.residualViewAmplified);
    updateMixButtons();
}

void MainComponent::saveSessionState (bool force)
{
    // Not while a resumed position is still on its way (the saved one would be overwritten with 0).
    if (! settings.getResumeSession() || resumeSeekTarget >= 0)
        return;

    const auto state = captureSessionState();
    if (! state.has_value() || state == lastSavedSession)
        return;

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! force)
    {
        // At most once a second; while playing, a moving playhead alone only every 5 s.
        bool onlyPlayhead = false;
        if (lastSavedSession.has_value())
        {
            auto withNewPlayhead = *lastSavedSession;
            withNewPlayhead.playheadSample = state->playheadSample;
            onlyPlayhead = withNewPlayhead == *state;
        }

        const double interval = onlyPlayhead && engine.isPlaying() ? 5000.0 : 1000.0;
        if (now - lastSessionSaveMs < interval)
            return;
    }

    settings.setSession (*state);
    settings.save();
    lastSavedSession = state;
    lastSessionSaveMs = now;
}

//==============================================================================
void MainComponent::updateStatusText()
{
    const auto& st = loader.getStatus();
    juce::Array<HeaderBar::Segment> left, right;
    juce::StringArray lines;   // the full detail, for the tooltip

    header.setFiles (originalFile.existsAsFile() ? originalFile : juce::File(),
                     stemFile.existsAsFile() ? stemFile : juce::File());

    lines.add ("Original:  " + (originalFile.existsAsFile() ? originalFile.getFileName() : juce::String ("(none)"))
               + "        Stem file:  " + (stemFile.existsAsFile() ? stemFile.getFileName() : juce::String ("(none)")));

    // Left: errors, progress, notices
    if (st.error.isNotEmpty())
    {
        left.add (seg ("ERROR: " + st.error, C::meterRed));
        lines.add ("ERROR: " + st.error);
    }

    if (st.original == StageState::idle && st.progress.isEmpty())
    {
        const bool hasA = originalFile.existsAsFile(), hasB = stemFile.existsAsFile();
        const juce::String idle = hasA && ! hasB ? "Original chosen - waiting for a stem file"
                                : hasB && ! hasA ? "Stem file chosen - waiting for an original"
                                                 : "Choose an original and a stem file";
        if (st.error.isEmpty())
            left.add (seg (idle, C::muted));
        if (previews.isOriginalPending() || previews.isStemsPending())
            left.add (seg ("   Rendering preview" + ellipsis, C::text));
        lines.add ("Status:  " + idle);
    }
    else
    {
        // Per-stage progress with the time each stage took / was reached (seconds since the pair was chosen).
        const auto secs = [] (double s) { return juce::String (s, 2) + " s"; };
        const auto mark = [] (StageState s)
        {
            switch (s)
            {
                case StageState::done:    return juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93"));   // check mark
                case StageState::running: return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));   // ellipsis
                case StageState::failed:  return juce::String ("failed");
                case StageState::idle:    break;
            }
            return juce::String ("-");
        };

        juce::String stages;
        stages << "Original " << mark (st.original)
               << (st.original == StageState::done ? (st.originalReused ? " (reused)" : " " + secs (st.decodeOriginalSeconds)) : juce::String())
               << "    Stems " << mark (st.stems)
               << (st.stems == StageState::done ? " " + secs (st.decodeStemsSeconds) : juce::String())
               << "    Alignment " << mark (st.alignment)
               << (st.alignment == StageState::done ? (st.alignmentFromCache ? " (cached)" : " " + secs (st.alignmentSeconds)) : juce::String())
               << "    Track Null Depth " << mark (st.trackNullDepth)
               << (st.trackNullDepth == StageState::done ? " " + secs (st.nullDepthSeconds) : juce::String());

        juce::String timing;
        if (st.timeToOriginal >= 0.0)
            timing << "A playable after " << secs (st.timeToOriginal);
        if (st.timeToStems >= 0.0)
            timing << ", B after " << secs (st.timeToStems);

        if (st.progress.isNotEmpty())
            left.add (seg (st.progress, st.busy ? C::text : C::muted));
        if (timing.isNotEmpty())
            left.add (seg ("   " + timing, C::muted));

        lines.add ("Status:  " + st.progress + (timing.isNotEmpty() ? "    " + timing : juce::String()));
        lines.add ("         " + stages);
    }

    if (st.notice.isNotEmpty())
    {
        left.add (seg ("   Note: " + st.notice, C::amber));
        lines.add ("Note:  " + st.notice);
    }

    if (previewNotice.isNotEmpty())
    {
        left.add (seg ("   Note: " + previewNotice, C::amber));
        lines.add ("Note:  " + previewNotice);
    }

    if (resumeNotice.isNotEmpty())
    {
        left.add (seg ("   Note: " + resumeNotice, C::amber));
        lines.add ("Note:  " + resumeNotice);
    }

    // Right: Alignment Offset, Track Null Depth, rate, device
    const auto sep = [&right] { right.add (seg ("  " + middleDot + "  ", C::silkDim)); };

    if (st.hasAlignment)
    {
        right.add (dot (st.lowConfidence ? C::amber : C::go));
        right.add (silk ("Aligned"));
        right.add (seg (" " + format::withThousands (st.offsetSamples, true) + " smp", C::muted));
        right.add (seg ("  " + rho + " ", C::silkDim));
        right.add (seg (juce::String (st.confidence, 4), C::muted));
        if (st.polarity < 0)
            right.add (seg (" " + middleDot + " polarity " + minusSign + "1", C::amber));
        if (st.lowConfidence)
            right.add (seg ("  ", C::amber));
        if (st.lowConfidence)
            right.add (silk ("Low confidence", C::amber));

        lines.add ("Alignment Offset:  " + format::withThousands (st.offsetSamples, true) + " samples,  polarity "
                   + (st.polarity < 0 ? "-1 (inverted)" : "+1") + ",  confidence " + juce::String (st.confidence, 4)
                   + (st.alignmentFromCache ? "  (cached, re-measuring)" : "")
                   + (st.lowConfidence ? "    LOW CONFIDENCE - check by ear" : ""));
    }

    if (st.hasTrackNullDepth)
    {
        if (! right.isEmpty())
            sep();
        right.add (silk ("Track null depth"));
        right.add (seg (" " + format::withMinusSign (juce::String (st.trackNullDepthDb, 1)) + " dB", C::muted));
        lines.add ("Track Null Depth:  " + juce::String (st.trackNullDepthDb, 1) + " dB");
    }

    if (st.rateState.isNotEmpty())
        lines.add ("Playback:  " + st.rateState + (st.rateNote.isNotEmpty() ? "    [" + st.rateNote + "]" : juce::String()));

    juce::String dev;
    if (auto* d = deviceManager.getCurrentAudioDevice())
    {
        if (! right.isEmpty())
            sep();
        right.add (silk ("Device"));
        right.add (seg (" " + d->getName() + " " + middleDot + " " + format::kiloHertz (d->getCurrentSampleRate()), C::muted));
        if (engine.isResampling())
            right.add (seg (" (resampled)", C::muted));

        dev << "Device:  " << deviceManager.getCurrentAudioDeviceType() << "  /  " << d->getName()
            << "   " << juce::String (d->getCurrentSampleRate(), 0) << " Hz,  buffer "
            << d->getCurrentBufferSizeSamples() << " samples,  output latency " << d->getOutputLatencyInSamples() << " samples";
    }
    else
    {
        if (! right.isEmpty())
            sep();
        right.add (seg ("No audio device open", C::meterRed));
        dev << "No audio device open" << (initialiseError.isNotEmpty() ? " (" + initialiseError + ")" : juce::String());
    }

    lines.add (dev);
    lines.add (juce::String (ffmpegVersionString()) + (JUCE_ASIO ? "   |   ASIO built in" : "   |   built without ASIO"));

    header.setStatus (left, right);
    header.setTooltip (lines.joinIntoString ("\n"));

    // Shown only while the timeline is empty. With one file chosen its lanes say what is happening.
    const bool anyFile = originalFile != juce::File() || stemFile != juce::File();
    tracks.setHint (st.busy && shownSession == nullptr && previewLength <= 0 ? "Decoding the original" + ellipsis
                    : anyFile ? juce::String()
                              : juce::String ("Open an original and a stem file"));

    header.unloadButton.setEnabled (anyFile || st.busy || loader.getPlaybackSession() != nullptr);
}

void MainComponent::updateMixButtons()
{
    const bool residual = engine.getListenMode() == ListenMode::Residual;
    const float x = engine.getCrossfade();

    // Until the Stems and the Alignment arrive only A exists: B, R, the Crossfade and Mute/Solo are disabled
    // (the engine keeps them silent, then fades them in when the Session is upgraded).
    const bool stems = engine.sessionHasStems();
    stemControlsEnabled = stems;

    deck.residualButton.setToggleState (residual, juce::dontSendNotification);
    deck.residualButton.setEnabled (stems);
    deck.crossfadeSlider.setEnabled (stems && ! residual);
    deck.centreButton.setEnabled (stems && ! residual);
    deck.setCrossfadeDimmed (! stems || residual);
    deck.curveButton.setToggleState (engine.getCurve() == Curve::EqualPower, juce::dontSendNotification);

    const int bPercent = juce::roundToInt (x * 100.0f);
    deck.setCrossfadeReadout ("A " + juce::String (100 - bPercent) + " " + middleDot + " B " + juce::String (bPercent));

    std::array<bool, Session::numStems> muted {}, soloed {};
    for (int i = 0; i < Session::numStems; ++i)
    {
        auto& h = tracks.getStemHeader (i);
        muted[(size_t) i] = engine.isStemMuted (i);
        soloed[(size_t) i] = engine.isStemSoloed (i);
        h.mute.setToggleState (muted[(size_t) i], juce::dontSendNotification);
        h.solo.setToggleState (soloed[(size_t) i], juce::dontSendNotification);
        h.mute.setEnabled (stems);
        h.solo.setEnabled (stems);
    }
    tracks.setAudibleStems (PlaybackEngine::audibleStems (muted, soloed));
}

void MainComponent::setListenAB (float x)
{
    engine.setListenMode (ListenMode::AB);
    engine.setCrossfade (x);
    deck.crossfadeSlider.setValue (x, juce::dontSendNotification);
    updateMixButtons();
}

void MainComponent::toggleResidual()
{
    engine.setListenMode (engine.getListenMode() == ListenMode::Residual ? ListenMode::AB : ListenMode::Residual);
    updateMixButtons();
}

void MainComponent::toggleStemSolo (int i)
{
    engine.setStemSoloed (i, ! engine.isStemSoloed (i));
    updateMixButtons();
}

void MainComponent::toggleStemMute (int i)
{
    engine.setStemMuted (i, ! engine.isStemMuted (i));
    updateMixButtons();
}

void MainComponent::toggleLoop()
{
    deck.loopButton.setToggleState (! deck.loopButton.getToggleState(), juce::dontSendNotification);
    deck.loopButton.onClick();
}

void MainComponent::resetClip()
{
    engine.readAndClearClip();
    clipShownUntilMs = 0.0;
    lastClipShown = false;
    deck.clipButton.setLit (false);
}

void MainComponent::seekTo (juce::int64 sample)
{
    resumeSeekTarget = -1;      // the user's seek wins over a resumed position still on its way
    const auto len = engine.getSessionLength();
    if (len <= 0)
        return;
    engine.seek (juce::jlimit ((juce::int64) 0, len, sample));
}

void MainComponent::seekBySeconds (double seconds)
{
    const auto len = engine.getSessionLength();
    const double rate = engine.getSessionSampleRate();
    if (len <= 0 || rate <= 0.0)
        return;

    seekTo (engine.getPlayheadSample() + (juce::int64) std::llround (seconds * rate));
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();

    // Interface scale: Ctrl+= / Ctrl+- / Ctrl+0
    if (mods.isCtrlDown() || mods.isCommandDown())
    {
        if (mods.isAltDown())
            return false;
        if (code == '=' || code == '+' || code == juce::KeyPress::numberPadAdd)          { setScaleIndex (scaleIndex + 1); return true; }
        if (code == '-' || code == '_' || code == juce::KeyPress::numberPadSubtract)     { setScaleIndex (scaleIndex - 1); return true; }
        if (code == '0' || code == juce::KeyPress::numberPad0)                            { setScaleIndex (defaultScaleIndex); return true; }
        if (code == ',' || code == '<')                                                   { showPreferences(); return true; }
        return false;
    }

    if (mods.isAltDown())
        return false;

    if (code == juce::KeyPress::spaceKey)      { engine.togglePlayPause(); return true; }
    if (code == juce::KeyPress::leftKey)       { seekBySeconds (mods.isShiftDown() ? -5.0 : -1.0); return true; }
    if (code == juce::KeyPress::rightKey)      { seekBySeconds (mods.isShiftDown() ? 5.0 : 1.0); return true; }
    if (code == juce::KeyPress::homeKey)       { seekTo (0); return true; }

    const auto c = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);

    // B, R, the Crossfade and Mute/Solo wait for the Stems (see updateMixButtons()).
    if (! stemControlsEnabled && (c == 'B' || c == 'R' || c == 'C' || (c >= '1' && c <= '4')
                                  || juce::String ("!@#$").indexOfChar (c) >= 0))
        return true;

    switch (c)
    {
        case 'A': setListenAB (0.0f); return true;
        case 'B': setListenAB (1.0f); return true;
        case 'R': toggleResidual(); return true;
        case 'C': setListenAB (0.5f); return true;
        case 'L': toggleLoop(); return true;
        default: break;
    }

    // 1-4 solo, Shift+1-4 mute. JUCE on Windows reports the unshifted digit; the shifted US symbols are a fallback.
    int stem = -1;
    if (c >= '1' && c <= '4')
        stem = (int) (c - '1');
    else if (const int idx = juce::String ("!@#$").indexOfChar (c); idx >= 0)
        stem = idx;

    if (stem >= 0)
    {
        if (mods.isShiftDown()) toggleStemMute (stem);
        else                    toggleStemSolo (stem);
        return true;
    }

    return false;
}

} // namespace stemab
