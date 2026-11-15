#include "MainComponent.h"

#include <cmath>
#include <limits>

#include "Theme.h"
#include "decode/Decode.h"
#include "engine/LiveNullDepth.h"

#ifndef STEMAB_SOURCE_DIR
 #define STEMAB_SOURCE_DIR ""
#endif

namespace stemab
{

namespace
{
    constexpr double masterMinDb = -60.0;
    constexpr double masterInfDb = -61.0;     // bottom of the slider = -inf

    const juce::Colour accent   { theme::accentArgb };
    const juce::Colour dimText  { theme::dimTextArgb };
    const juce::Colour clipRed  { 0xffe5484d };
    const juce::Colour muteCol  { 0xffe0913a };
    const juce::Colour soloCol  { 0xffe5c84a };

    juce::String withThousands (juce::int64 v)
    {
        auto digits = juce::String (std::abs (v));
        juce::String out;
        for (int i = 0; i < digits.length(); ++i)
        {
            if (i > 0 && (digits.length() - i) % 3 == 0)
                out << ",";
            out << digits[i];
        }
        return (v < 0 ? "-" : "+") + out;
    }

    void styleToggle (juce::TextButton& b, juce::Colour on)
    {
        b.setColour (juce::TextButton::buttonOnColourId, on);
        b.setColour (juce::TextButton::textColourOnId, juce::Colour (theme::windowArgb));
    }
}

//==============================================================================
void MeterBar::setLevels (float peak, float rms)
{
    // Peak falls back ~20 dB/s at 30 fps; RMS follows directly.
    shownPeak = juce::jmax (peak, shownPeak * 0.86f);
    shownRms = rms;
    repaint();
}

void MeterBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (theme::windowArgb));
    g.fillRect (r);

    const auto toX = [&r] (float linear)
    {
        const float db = linear > 0.0f ? 20.0f * std::log10 (linear) : -100.0f;
        return r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };

    const float rmsX = toX (shownRms);
    g.setColour (shownRms > 1.0f ? clipRed : accent.withAlpha (0.75f));
    g.fillRect (r.withRight (rmsX));

    if (shownPeak > 0.0f)
    {
        const float peakX = juce::jmin (toX (shownPeak), r.getRight() - 2.0f);
        g.setColour (shownPeak > 1.0f ? clipRed : juce::Colour (theme::textArgb));
        g.fillRect (juce::Rectangle<float> (peakX, r.getY(), 2.0f, r.getHeight()));
    }

    g.setColour (juce::Colour (theme::outlineArgb));
    g.drawRect (r, 1.0f);
}

//==============================================================================
MainComponent::MainComponent()
{
    initialiseError = deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&engineCallback);
    deviceManager.addChangeListener (this);

    loader.onStatusChanged = [this] { updateStatusText(); };

    // Files / settings
    openOriginalButton.onClick = [this] { chooseOriginal(); };
    openStemButton.onClick = [this] { chooseStemFile(); };
    settingsButton.onClick = [this] { showAudioSettings(); };

    for (auto* l : { &statusLabel, &deviceLabel })
    {
        l->setJustificationType (juce::Justification::topLeft);
        l->setFont (juce::Font (juce::FontOptions (14.0f)));
        l->setMinimumHorizontalScale (1.0f);
    }
    deviceLabel.setColour (juce::Label::textColourId, dimText);

    // Transport
    playButton.onClick = [this] { engine.togglePlayPause(); };
    loopButton.setClickingTogglesState (true);
    styleToggle (loopButton, accent);
    loopButton.onClick = [this]
    {
        engine.setLoopRegion (0, 0);    // whole track for now
        engine.setLoopEnabled (loopButton.getToggleState());
    };

    positionSlider.setRange (0.0, 1.0, 1.0);
    positionSlider.onValueChange = [this]
    {
        if (positionSlider.isMouseButtonDown())
            engine.seek ((juce::int64) positionSlider.getValue());
    };
    positionSlider.onDragEnd = [this] { engine.seek ((juce::int64) positionSlider.getValue()); };

    timeLabel.setJustificationType (juce::Justification::centredRight);
    timeLabel.setFont (juce::Font (juce::FontOptions (15.0f)));

    // Mix
    crossfadeSlider.setRange (0.0, 1.0, 0.0);
    crossfadeSlider.setValue (0.0, juce::dontSendNotification);
    crossfadeSlider.onValueChange = [this] { engine.setCrossfade ((float) crossfadeSlider.getValue()); updateMixButtons(); };
    crossfadeSlider.setDoubleClickReturnValue (true, 0.5);

    aButton.onClick = [this] { setListenAB (0.0f); };
    bButton.onClick = [this] { setListenAB (1.0f); };
    rButton.onClick = [this] { toggleResidual(); };
    for (auto* b : { &aButton, &bButton, &rButton })
        styleToggle (*b, accent);

    curveButton.setClickingTogglesState (true);
    curveButton.setToggleState (true, juce::dontSendNotification);
    styleToggle (curveButton, juce::Colour (theme::panelArgb).brighter (0.4f));
    curveButton.setColour (juce::TextButton::textColourOnId, juce::Colour (theme::textArgb));
    curveButton.onClick = [this]
    {
        const bool eq = curveButton.getToggleState();
        curveButton.setButtonText (eq ? "Equal power" : "Linear");
        engine.setCurve (eq ? Curve::EqualPower : Curve::Linear);
    };
    engine.setCurve (Curve::EqualPower);

    liveNullLabel.setFont (juce::Font (juce::FontOptions (15.0f)));

    // Stems
    for (int i = 0; i < Session::numStems; ++i)
    {
        auto& row = stemRows[(size_t) i];
        row.name.setText (stemName ((Stem) i), juce::dontSendNotification);
        styleToggle (row.mute, muteCol);
        styleToggle (row.solo, soloCol);
        row.mute.onClick = [this, i] { toggleStemMute (i); };
        row.solo.onClick = [this, i] { toggleStemSolo (i); };
        addAndMakeVisible (row.name);
        addAndMakeVisible (row.mute);
        addAndMakeVisible (row.solo);
        addAndMakeVisible (row.meter);
    }

    // Meters, Clip, Master
    const char* meterNames[] = { "A", "B", "Out" };
    for (int m = 0; m < 3; ++m)
    {
        meterLabels[m].setText (meterNames[m], juce::dontSendNotification);
        meterLabels[m].setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (meterLabels[m]);
        addAndMakeVisible (meters[m][0]);
        addAndMakeVisible (meters[m][1]);
    }

    clipLabel.setText ("CLIP", juce::dontSendNotification);
    clipLabel.setJustificationType (juce::Justification::centred);
    clipLabel.setColour (juce::Label::textColourId, dimText);
    clipLabel.setColour (juce::Label::outlineColourId, juce::Colour (theme::outlineArgb));

    masterLabel.setText ("Master Gain", juce::dontSendNotification);
    masterSlider.setRange (masterInfDb, 0.0, 0.1);
    masterSlider.setValue (0.0, juce::dontSendNotification);
    masterSlider.setDoubleClickReturnValue (true, 0.0);
    masterSlider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 80, 24);
    masterSlider.textFromValueFunction = [] (double v)
    {
        return v <= masterMinDb - 0.5 ? juce::String ("-inf dB") : juce::String (v, 1) + " dB";
    };
    masterSlider.onValueChange = [this]
    {
        const double v = masterSlider.getValue();
        engine.setMasterGainDb (v <= masterMinDb - 0.5 ? -std::numeric_limits<double>::infinity() : v);
    };

    for (auto* c : std::initializer_list<juce::Component*> {
             &openOriginalButton, &openStemButton, &settingsButton, &statusLabel, &deviceLabel,
             &playButton, &loopButton, &positionSlider, &timeLabel,
             &aButton, &crossfadeSlider, &bButton, &rButton, &curveButton, &liveNullLabel,
             &clipLabel, &masterLabel, &masterSlider })
        addAndMakeVisible (c);

    // Keyboard shortcuts go to this component: no child takes focus.
    for (auto* c : getChildren())
    {
        c->setWantsKeyboardFocus (false);
        c->setMouseClickGrabsKeyboardFocus (false);
    }
    setWantsKeyboardFocus (true);

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
    // loader (declared after the device manager) is destroyed first, waiting for its worker.
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (theme::windowArgb));

    // Section separators.
    g.setColour (juce::Colour (theme::outlineArgb));
    for (int y : { positionSlider.getY() - 10, crossfadeSlider.getY() - 10, stemRows[0].name.getY() - 10 })
        g.drawHorizontalLine (y, 16.0f, (float) getWidth() - 16.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto top = area.removeFromTop (32);
    openOriginalButton.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (8);
    openStemButton.setBounds (top.removeFromLeft (150));
    settingsButton.setBounds (top.removeFromRight (150));

    area.removeFromTop (8);
    statusLabel.setBounds (area.removeFromTop (120));
    deviceLabel.setBounds (area.removeFromTop (40));
    area.removeFromTop (20);

    auto transport = area.removeFromTop (36);
    playButton.setBounds (transport.removeFromLeft (90));
    transport.removeFromLeft (8);
    loopButton.setBounds (transport.removeFromLeft (70));
    transport.removeFromLeft (8);
    timeLabel.setBounds (transport.removeFromRight (190));
    positionSlider.setBounds (transport.reduced (4, 0));

    area.removeFromTop (20);
    auto mix = area.removeFromTop (36);
    aButton.setBounds (mix.removeFromLeft (44));
    mix.removeFromLeft (8);
    liveNullLabel.setBounds (mix.removeFromRight (220));
    mix.removeFromRight (8);
    curveButton.setBounds (mix.removeFromRight (110));
    mix.removeFromRight (16);
    rButton.setBounds (mix.removeFromRight (44));
    mix.removeFromRight (8);
    bButton.setBounds (mix.removeFromRight (44));
    mix.removeFromRight (8);
    crossfadeSlider.setBounds (mix);

    area.removeFromTop (20);
    auto lower = area;
    auto left = lower.removeFromLeft (lower.getWidth() / 2).reduced (0, 0);
    auto right = lower.withTrimmedLeft (24);

    for (auto& row : stemRows)
    {
        auto r = left.removeFromTop (28);
        row.name.setBounds (r.removeFromLeft (80));
        row.mute.setBounds (r.removeFromLeft (34).reduced (0, 2));
        r.removeFromLeft (4);
        row.solo.setBounds (r.removeFromLeft (34).reduced (0, 2));
        r.removeFromLeft (10);
        row.meter.setBounds (r.reduced (0, 7));
        left.removeFromTop (6);
    }

    for (int m = 0; m < 3; ++m)
    {
        auto r = right.removeFromTop (30);
        meterLabels[m].setBounds (r.removeFromLeft (40));
        r.removeFromLeft (8);
        auto bars = r.reduced (0, 3);
        meters[m][0].setBounds (bars.removeFromTop (bars.getHeight() / 2).withTrimmedBottom (1));
        meters[m][1].setBounds (bars.withTrimmedTop (1));
        right.removeFromTop (6);
    }

    right.removeFromTop (8);
    auto masterRow = right.removeFromTop (30);
    clipLabel.setBounds (masterRow.removeFromRight (60));
    masterRow.removeFromRight (12);
    masterLabel.setBounds (masterRow.removeFromLeft (90));
    masterSlider.setBounds (masterRow);
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    loader.deviceConfigurationChanged();
    updateStatusText();
}

void MainComponent::timerCallback()
{
    engine.releaseRetiredSessions();

    if (! focusInitialised && isShowing())
    {
        focusInitialised = true;
        grabKeyboardFocus();   // shortcuts; children never take focus, a click on the background regains it
    }

    // Transport
    const bool playing = engine.isPlaying();
    playButton.setButtonText (playing ? "Pause" : "Play");

    const auto len = engine.getSessionLength();
    const double rate = engine.getSessionSampleRate();
    const auto playhead = engine.getPlayheadSample();

    if (len > 0)
    {
        if (positionSlider.getMaximum() != (double) len)
            positionSlider.setRange (0.0, (double) len, 1.0);
        if (! positionSlider.isMouseButtonDown())
            positionSlider.setValue ((double) playhead, juce::dontSendNotification);
    }
    positionSlider.setEnabled (len > 0);

    timeLabel.setText (rate > 0.0 ? formatTime ((double) playhead / rate) + " / " + formatTime ((double) len / rate)
                                  : juce::String ("--:--.- / --:--.-"),
                       juce::dontSendNotification);

    // Meters
    for (int i = 0; i < Session::numStems; ++i)
    {
        const auto l = engine.readMeter ((MeterTap) ((int) MeterTap::Drums + i), 0);
        const auto r = engine.readMeter ((MeterTap) ((int) MeterTap::Drums + i), 1);
        stemRows[(size_t) i].meter.setLevels (juce::jmax (l.peak, r.peak), juce::jmax (l.rms, r.rms));
    }

    const MeterTap taps[3] = { MeterTap::Original, MeterTap::StemSum, MeterTap::Output };
    for (int m = 0; m < 3; ++m)
        for (int c = 0; c < 2; ++c)
        {
            const auto reading = engine.readMeter (taps[m], c);
            meters[m][c].setLevels (reading.peak, reading.rms);
        }

    // Clip (latched ~1 s)
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (engine.readAndClearClip())
        clipShownUntilMs = now + 1000.0;

    const bool clipShown = now < clipShownUntilMs;
    if (clipShown != lastClipShown)
    {
        lastClipShown = clipShown;
        clipLabel.setColour (juce::Label::backgroundColourId, clipShown ? clipRed : juce::Colours::transparentBlack);
        clipLabel.setColour (juce::Label::textColourId, clipShown ? juce::Colours::white : dimText);
    }

    // Live Null Depth (UI thread, against the immutable Session)
    juce::String live = "Live Null Depth: -";
    if (auto session = loader.getPlaybackSession(); session != nullptr && session->length() == len)
    {
        const auto nd = measureLiveNullDepth (*session, playhead);
        live = nd.quiet ? juce::String ("Live Null Depth: quiet")
                        : "Live Null Depth: " + juce::String (nd.nullDepthDb, 1) + " dB";
    }
    liveNullLabel.setText (live, juce::dontSendNotification);

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
    options.dialogBackgroundColour = juce::Colour (theme::windowArgb);
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.escapeKeyTriggersCloseButton = true;
    settingsDialog = options.launchAsync();
}

//==============================================================================
void MainComponent::updateStatusText()
{
    const auto& st = loader.getStatus();
    juce::StringArray lines;

    lines.add ("Original:  " + (originalFile.existsAsFile() ? originalFile.getFileName() : juce::String ("(none)"))
               + "        Stem file:  " + (stemFile.existsAsFile() ? stemFile.getFileName() : juce::String ("(none)")));

    if (st.error.isNotEmpty())
        lines.add ("ERROR: " + st.error);
    else if (st.progress.isNotEmpty())
        lines.add ("Status:  " + st.progress);
    else
        lines.add ("Status:  choose an original and a stem file");

    if (st.hasAlignment)
        lines.add ("Alignment Offset:  " + withThousands (st.offsetSamples) + " samples,  polarity "
                   + (st.polarity < 0 ? "-1 (inverted)" : "+1") + ",  confidence " + juce::String (st.confidence, 4)
                   + (st.lowConfidence ? "    LOW CONFIDENCE - check by ear" : ""));

    if (st.hasTrackNullDepth)
        lines.add ("Track Null Depth:  " + juce::String (st.trackNullDepthDb, 1) + " dB");

    if (st.rateState.isNotEmpty())
        lines.add ("Playback:  " + st.rateState + (st.rateNote.isNotEmpty() ? "    [" + st.rateNote + "]" : juce::String()));

    if (engine.isRateMismatched())
        lines.add ("(waiting for a session at the device rate - output silent)");

    statusLabel.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, st.error.isNotEmpty() || st.lowConfidence ? soloCol
                                                                                               : juce::Colour (theme::textArgb));

    juce::String dev;
    if (auto* d = deviceManager.getCurrentAudioDevice())
        dev << "Device:  " << deviceManager.getCurrentAudioDeviceType() << "  /  " << d->getName()
            << "   " << juce::String (d->getCurrentSampleRate(), 0) << " Hz,  buffer "
            << d->getCurrentBufferSizeSamples() << " samples,  output latency " << d->getOutputLatencyInSamples() << " samples";
    else
        dev << "No audio device open" << (initialiseError.isNotEmpty() ? " (" + initialiseError + ")" : juce::String());

    dev << "\n" << juce::String (ffmpegVersionString()) << (JUCE_ASIO ? "   |   ASIO built in" : "   |   built without ASIO");
    deviceLabel.setText (dev, juce::dontSendNotification);
}

void MainComponent::updateMixButtons()
{
    const bool residual = engine.getListenMode() == ListenMode::Residual;
    const float x = engine.getCrossfade();

    rButton.setToggleState (residual, juce::dontSendNotification);
    aButton.setToggleState (! residual && x <= 0.0f, juce::dontSendNotification);
    bButton.setToggleState (! residual && x >= 1.0f, juce::dontSendNotification);
    crossfadeSlider.setEnabled (! residual);
    crossfadeSlider.setAlpha (residual ? 0.4f : 1.0f);

    for (int i = 0; i < Session::numStems; ++i)
    {
        stemRows[(size_t) i].mute.setToggleState (engine.isStemMuted (i), juce::dontSendNotification);
        stemRows[(size_t) i].solo.setToggleState (engine.isStemSoloed (i), juce::dontSendNotification);
    }
}

void MainComponent::setListenAB (float x)
{
    engine.setListenMode (ListenMode::AB);
    engine.setCrossfade (x);
    crossfadeSlider.setValue (x, juce::dontSendNotification);
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

void MainComponent::seekBySeconds (double seconds)
{
    const auto len = engine.getSessionLength();
    const double rate = engine.getSessionSampleRate();
    if (len <= 0 || rate <= 0.0)
        return;

    const auto target = engine.getPlayheadSample() + (juce::int64) std::llround (seconds * rate);
    engine.seek (juce::jlimit ((juce::int64) 0, len, target));
}

juce::String MainComponent::formatTime (double seconds)
{
    seconds = juce::jmax (0.0, seconds);
    const int tenths = (int) std::floor (seconds * 10.0);
    const int mins = tenths / 600;
    const int secs = (tenths / 10) % 60;
    return juce::String (mins).paddedLeft ('0', 2) + ":" + juce::String (secs).paddedLeft ('0', 2) + "." + juce::String (tenths % 10);
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown())
        return false;

    const int code = key.getKeyCode();

    if (code == juce::KeyPress::spaceKey)      { engine.togglePlayPause(); return true; }
    if (code == juce::KeyPress::leftKey)       { seekBySeconds (mods.isShiftDown() ? -5.0 : -1.0); return true; }
    if (code == juce::KeyPress::rightKey)      { seekBySeconds (mods.isShiftDown() ? 5.0 : 1.0); return true; }
    if (code == juce::KeyPress::homeKey)       { engine.seek (0); return true; }

    const auto c = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);

    switch (c)
    {
        case 'A': setListenAB (0.0f); return true;
        case 'B': setListenAB (1.0f); return true;
        case 'R': toggleResidual(); return true;
        case 'C': setListenAB (0.5f); return true;
        case 'L':
            loopButton.setToggleState (! loopButton.getToggleState(), juce::dontSendNotification);
            loopButton.onClick();
            return true;
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
