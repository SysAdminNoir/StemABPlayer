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

    constexpr std::array<float, 4> scaleSteps { 0.9f, 1.0f, 1.15f, 1.3f };
    constexpr int defaultScaleIndex = 1;
    constexpr float bridgeWidthPx = 168.0f;

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

    HeaderBar::Segment seg (const juce::String& text, juce::uint32 argb, bool mono = false)
    {
        return { text, theme::colour (argb), mono, false };
    }

    HeaderBar::Segment dot (juce::uint32 argb)
    {
        return { {}, theme::colour (argb), false, true };
    }
}

//==============================================================================
MainComponent::MainComponent()
{
    initialiseError = deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&engineCallback);
    deviceManager.addChangeListener (this);

    loader.onStatusChanged = [this] { updateStatusText(); };

    // Interface scale, remembered in %APPDATA%/StemABPlayer/settings.xml
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "settings";
        options.filenameSuffix = ".xml";
        options.folderName = "StemABPlayer";
        options.osxLibrarySubFolder = "Application Support";
        appProperties.setStorageParameters (options);
        if (auto* props = appProperties.getUserSettings())
            scaleIndex = juce::jlimit (0, (int) scaleSteps.size() - 1, props->getIntValue ("uiScaleIndex", defaultScaleIndex));
    }

    // Waveform summaries arrive on the message thread (stale ones are dropped by the builder).
    waveforms.onOriginalReady = [this] (WaveformBuilder::SummaryPtr s)
    {
        originalSummaryReady = true;
        tracks.setOriginalSummary (std::move (s));
        tracks.setOriginalState (false);
    };
    waveforms.onStemsReady = [this] (const WaveformBuilder::StemSummaries& r)
    {
        tracks.setStemSummaries (r.stems, r.residual);
    };

    // Header
    header.openOriginalButton.onClick = [this] { chooseOriginal(); };
    header.openStemButton.onClick = [this] { chooseStemFile(); };
    header.settingsButton.onClick = [this] { showAudioSettings(); };
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
    stopTimer();

    if (settingsDialog != nullptr)
        delete settingsDialog.getComponent();

    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&engineCallback);
    deviceManager.closeAudioDevice();
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

    header.setUiScale (s);
    header.setScaleLabel (juce::String (juce::roundToInt (s * 100.0f)) + "%");
    header.scaleDownButton.setEnabled (scaleIndex > 0);
    header.scaleUpButton.setEnabled (scaleIndex < (int) scaleSteps.size() - 1);
    tracks.setUiScale (s);
    bridge.setUiScale (s);
    deck.setUiScale (s);
    resized();
    repaint();

    if (auto* props = appProperties.getUserSettings())
        props->setValue ("uiScaleIndex", scaleIndex);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour (theme::groundArgb));
}

void MainComponent::resized()
{
    const float s = uiScale();
    auto r = getLocalBounds();

    header.setBounds (r.removeFromTop (header.getPreferredHeight()));
    deck.setBounds (r.removeFromBottom (deck.getPreferredHeight (r.getWidth())));
    bridge.setBounds (r.removeFromRight (theme::scaled (bridgeWidthPx, s)));
    tracks.setBounds (r);
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    loader.deviceConfigurationChanged();
    updateStatusText();
}

void MainComponent::pollSession()
{
    const auto s = loader.getPlaybackSession();

    // What the stem / residual lanes say while they have no waveform.
    const auto& st = loader.getStatus();
    const auto stemsState = s == nullptr ? TrackArea::StemsState::noSession
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
        // A new Original: its waveform, and the stem lanes start over.
        originalSummaryReady = false;
        tracks.setOriginalSummary (nullptr);
        tracks.setOriginalState (true);
        tracks.clearStemSummaries();
        waveforms.buildOriginal (s);

        const auto ext = juce::File::createFileWithoutCheckingPath (st.originalName).getFileExtension()
                             .trimCharactersAtStart (".").toUpperCase();
        tracks.setOriginalSubLabel ((ext.isNotEmpty() ? ext + " " + middleDot + " " : juce::String())
                                    + format::kiloHertz (s->sampleRate()) + " " + middleDot + " stereo");
    }

    if (s->hasStems())
    {
        // New Stems or a corrected Alignment Offset: recompute (the old lanes stay until the new ones arrive).
        waveforms.buildStems (s);
    }
    else
    {
        waveforms.cancelStems();
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

    // Transport
    deck.playButton.setPlaying (engine.isPlaying());
    if (deck.loopButton.getToggleState() != engine.isLoopEnabled())
        deck.loopButton.setToggleState (engine.isLoopEnabled(), juce::dontSendNotification);

    const auto len = engine.getSessionLength();
    const double rate = engine.getSessionSampleRate();
    const auto playhead = engine.getPlayheadSample();

    if (len > 0 && rate > 0.0)
        deck.setTime (format::minutesSecondsMillis ((double) playhead / rate),
                      "Sample: " + format::withThousands (playhead) + "\nTotal:  " + format::withThousands (len));
    else
        deck.setTime ("0:00.000", "no track loaded");

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
            if (m == MeterBridge::out)
                deck.outMeters[(size_t) c].setLevels (reading.peak, reading.rms);
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
}

//==============================================================================
juce::File MainComponent::defaultFolder (const char* name) const
{
    if (const juce::File src (juce::String (STEMAB_SOURCE_DIR)); src.isDirectory() && src.getChildFile (name).isDirectory())
        return src.getChildFile (name);

    // Fallback: walk up from the exe.
    auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (int i = 0; i < 8 && dir.exists(); ++i, dir = dir.getParentDirectory())
        if (dir.getChildFile (name).isDirectory())
            return dir.getChildFile (name);

    return juce::File::getSpecialLocation (juce::File::userMusicDirectory);
}

void MainComponent::chooseOriginal()
{
    chooser = std::make_unique<juce::FileChooser> ("Open original (A)", defaultFolder ("Audio Files"),
                                                   "*.flac;*.wav;*.aif;*.aiff;*.mp3;*.m4a;*.ogg;*.opus");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (const auto f = fc.getResult(); f.existsAsFile())
                              {
                                  originalFile = f;
                                  startLoadIfReady();
                                  updateStatusText();
                              }
                          });
}

void MainComponent::chooseStemFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Open stem file (B)", defaultFolder ("Stem Files"),
                                                   "*.stem.m4a;*.m4a;*.mp4");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (const auto f = fc.getResult(); f.existsAsFile())
                              {
                                  stemFile = f;
                                  startLoadIfReady();
                                  updateStatusText();
                              }
                          });
}

void MainComponent::loadPair (const juce::File& original, const juce::File& stems)
{
    originalFile = original;
    stemFile = stems;
    startLoadIfReady();
    updateStatusText();
}

void MainComponent::startLoadIfReady()
{
    if (originalFile.existsAsFile() && stemFile.existsAsFile())
        loader.load (originalFile, stemFile);
}

void MainComponent::showAudioSettings()
{
    if (settingsDialog != nullptr)
    {
        settingsDialog->toFront (true);
        return;
    }

    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager,
                                                                          0, 0,      // no inputs
                                                                          0, 2,      // outputs
                                                                          false, false,
                                                                          true,      // stereo pairs
                                                                          false);    // show advanced options
    selector->setSize (640, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = theme::colour (theme::panelArgb);
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.escapeKeyTriggersCloseButton = true;
    settingsDialog = options.launchAsync();
}

//==============================================================================
void MainComponent::updateStatusText()
{
    const auto& st = loader.getStatus();
    juce::Array<HeaderBar::Segment> left, right;
    juce::StringArray lines;   // the full detail, for the tooltip

    header.setFileNames (originalFile.existsAsFile() ? originalFile.getFileName() : juce::String(),
                         stemFile.existsAsFile() ? stemFile.getFileName() : juce::String());

    lines.add ("Original:  " + (originalFile.existsAsFile() ? originalFile.getFileName() : juce::String ("(none)"))
               + "        Stem file:  " + (stemFile.existsAsFile() ? stemFile.getFileName() : juce::String ("(none)")));

    // Left: errors, progress, notices
    if (st.error.isNotEmpty())
    {
        left.add (seg ("ERROR: " + st.error, theme::meterClipArgb));
        lines.add ("ERROR: " + st.error);
    }

    if (st.original == StageState::idle && st.progress.isEmpty())
    {
        if (st.error.isEmpty())
            left.add (seg (originalFile.existsAsFile() || stemFile.existsAsFile()
                               ? juce::String ("Choose the other file to start")
                               : juce::String ("Choose an original and a stem file"), theme::mutedTextArgb));
        lines.add ("Status:  choose an original and a stem file");
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
            left.add (seg (st.progress, st.busy ? theme::textArgb : theme::mutedTextArgb));
        if (timing.isNotEmpty())
            left.add (seg ("   " + timing, theme::mutedTextArgb));

        lines.add ("Status:  " + st.progress + (timing.isNotEmpty() ? "    " + timing : juce::String()));
        lines.add ("         " + stages);
    }

    if (st.notice.isNotEmpty())
    {
        left.add (seg ("   Note: " + st.notice, theme::amberArgb));
        lines.add ("Note:  " + st.notice);
    }

    // Right: Alignment Offset, Track Null Depth, rate, device
    const auto sep = [&right] { right.add (seg ("  " + middleDot + "  ", theme::mutedTextArgb)); };

    if (st.hasAlignment)
    {
        right.add (dot (st.lowConfidence ? theme::amberArgb : theme::goodArgb));
        right.add (seg ("Aligned ", theme::mutedTextArgb));
        right.add (seg (format::withThousands (st.offsetSamples, true) + " smp", theme::textArgb, true));
        right.add (seg (" " + middleDot + " " + rho + " ", theme::mutedTextArgb));
        right.add (seg (juce::String (st.confidence, 4), theme::textArgb, true));
        if (st.polarity < 0)
            right.add (seg (" " + middleDot + " polarity " + minusSign + "1", theme::amberArgb));
        if (st.lowConfidence)
            right.add (seg ("  LOW CONFIDENCE", theme::amberArgb));

        lines.add ("Alignment Offset:  " + format::withThousands (st.offsetSamples, true) + " samples,  polarity "
                   + (st.polarity < 0 ? "-1 (inverted)" : "+1") + ",  confidence " + juce::String (st.confidence, 4)
                   + (st.alignmentFromCache ? "  (cached, re-measuring)" : "")
                   + (st.lowConfidence ? "    LOW CONFIDENCE - check by ear" : ""));
    }

    if (st.hasTrackNullDepth)
    {
        if (! right.isEmpty())
            sep();
        right.add (seg ("Track Null Depth ", theme::mutedTextArgb));
        right.add (seg (format::withMinusSign (juce::String (st.trackNullDepthDb, 1)) + " dB", theme::textArgb, true));
        lines.add ("Track Null Depth:  " + juce::String (st.trackNullDepthDb, 1) + " dB");
    }

    if (st.rateState.isNotEmpty())
        lines.add ("Playback:  " + st.rateState + (st.rateNote.isNotEmpty() ? "    [" + st.rateNote + "]" : juce::String()));

    juce::String dev;
    if (auto* d = deviceManager.getCurrentAudioDevice())
    {
        if (! right.isEmpty())
            sep();
        right.add (seg (d->getName() + " " + middleDot + " ", theme::mutedTextArgb));
        right.add (seg (format::kiloHertz (d->getCurrentSampleRate()), theme::textArgb, true));
        if (engine.isResampling())
            right.add (seg (" (resampled)", theme::mutedTextArgb));

        dev << "Device:  " << deviceManager.getCurrentAudioDeviceType() << "  /  " << d->getName()
            << "   " << juce::String (d->getCurrentSampleRate(), 0) << " Hz,  buffer "
            << d->getCurrentBufferSizeSamples() << " samples,  output latency " << d->getOutputLatencyInSamples() << " samples";
    }
    else
    {
        if (! right.isEmpty())
            sep();
        right.add (seg ("No audio device open", theme::meterClipArgb));
        dev << "No audio device open" << (initialiseError.isNotEmpty() ? " (" + initialiseError + ")" : juce::String());
    }

    lines.add (dev);
    lines.add (juce::String (ffmpegVersionString()) + (JUCE_ASIO ? "   |   ASIO built in" : "   |   built without ASIO"));

    header.setStatus (left, right);
    header.setTooltip (lines.joinIntoString ("\n"));

    tracks.setHint (st.busy && shownSession == nullptr ? "Decoding the original" + ellipsis
                                                       : juce::String ("Open an original and a stem file"));
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
