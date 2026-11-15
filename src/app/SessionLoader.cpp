#include "SessionLoader.h"

#include <cmath>

#include "PlatformWindows.h"

namespace stemab
{

namespace
{
    bool sameRate (double a, double b) noexcept     { return std::abs (a - b) < 0.5; }

    juce::String hz (double rate)                   { return juce::String (rate, 0) + " Hz"; }

    juce::String arrow()                            { return juce::CharPointer_UTF8 (" \xe2\x86\x92 "); }   // U+2192

    /** JUCE's plain "Windows Audio" device type is WASAPI shared mode with Windows-side rate conversion. */
    bool isWasapiSharedWithAutoConvert (const juce::String& typeName)
    {
        return typeName == "Windows Audio";
    }

    juce::String signedSamples (juce::int64 v)      { return (v > 0 ? "+" : "") + juce::String (v); }
}

//==============================================================================
SessionLoader::SessionLoader (juce::AudioDeviceManager& dm, PlaybackEngine& e)
    : deviceManager (dm),
      engine (e),
      pairLoader ([] (PairLoader::Task task) { juce::MessageManager::callAsync (std::move (task)); }, &cache)
{
    pairLoader.onOriginalReady      = [this] (const auto& ev) { originalReady (ev); };
    pairLoader.onStemsDecoded       = [this] (double seconds) { stemsDecoded (seconds); };
    pairLoader.onStemsReady         = [this] (const auto& ev) { stemsReady (ev); };
    pairLoader.onAlignmentCorrected = [this] (const auto& ev) { alignmentCorrected (ev); };
    pairLoader.onTrackNullDepth     = [this] (const auto& ev) { trackNullDepthReady (ev); };
    pairLoader.onFailed             = [this] (LoadStage stage, const juce::String& message) { failed (stage, message); };
}

SessionLoader::~SessionLoader() = default;   // the PairLoader cancels and waits for its workers

void SessionLoader::changed()
{
    if (onStatusChanged)
        onStatusChanged();
}

//==============================================================================
void SessionLoader::load (const juce::File& original, const juce::File& stemFile)
{
    // Instant feedback: names and stage states now, before any audio exists. The current pair keeps
    // playing until the new Original is ready (originalReady() hands over with a fade).
    const auto keepRate = status;
    status = {};
    status.originalName = original.getFileName();
    status.stemFileName = stemFile.getFileName();
    status.busy = true;
    status.original = StageState::running;
    status.stems = StageState::running;
    status.alignment = StageState::idle;
    status.trackNullDepth = StageState::idle;
    status.sourceRate = keepRate.sourceRate;
    status.deviceRate = keepRate.deviceRate;
    status.rateState = keepRate.rateState;
    status.rateNote = keepRate.rateNote;
    updateProgress();
    changed();

    pairLoader.load (original, stemFile);
}

void SessionLoader::updateProgress()
{
    if (status.error.isNotEmpty() && status.original != StageState::done)
        status.progress = "Failed";
    else if (status.original != StageState::done)
        status.progress = "Decoding...";
    else if (status.stems == StageState::failed || status.alignment == StageState::failed)
        status.progress = "Original only (B unavailable)";
    else if (status.alignment != StageState::done)
        status.progress = "Original playable - Stems on the way...";
    else
        status.progress = "Ready";

    status.busy = status.original == StageState::running || status.stems == StageState::running
               || status.alignment == StageState::running;
}

//==============================================================================
// PairLoader callbacks (message thread, current generation only)

void SessionLoader::originalReady (const PairLoader::OriginalReady& ev)
{
    session = ev.session;

    status.original = StageState::done;
    status.originalReused = ev.sameOriginal;
    status.timeToOriginal = ev.secondsSinceLoad;
    status.decodeOriginalSeconds = ev.decodeSeconds;
    status.sourceRate = session->sampleRate();
    if (status.stems == StageState::done)
        status.alignment = StageState::running;

    // Native Rate first (the device restarts at the source rate if it accepts it), then hand the Session
    // over: fade out the old pair, swap, fade in - at 0 for a new Original, in place for the same one.
    lastDeviceIdentity = deviceIdentity();
    applyRatePolicy (session->sampleRate());
    engine.setSession (session, ev.sameOriginal ? Handover::continueAtPosition : Handover::continueFromStart);

    updateRateStatus();
    updateProgress();
    changed();
}

void SessionLoader::stemsDecoded (double seconds)
{
    status.stems = StageState::done;
    status.decodeStemsSeconds = seconds;
    status.alignment = status.original == StageState::done ? StageState::running : StageState::idle;
    updateProgress();
    changed();
}

void SessionLoader::setAlignment (const AlignmentResult& al)
{
    status.hasAlignment = true;
    status.offsetSamples = al.offsetSamples;
    status.polarity = al.polarity;
    status.confidence = al.confidence;
    status.lowConfidence = al.lowConfidence;
}

void SessionLoader::stemsReady (const PairLoader::StemsReady& ev)
{
    if (session == nullptr || ! ev.session->sharesOriginalWith (*session))
        return;   // not for the Original that is playing (cannot happen within one generation)

    session = ev.session;
    engine.upgradeSession (session);   // B and the Residual fade in; the playhead carries on

    setAlignment (ev.alignment);
    status.alignmentFromCache = ev.fromCache;
    status.stems = StageState::done;
    status.alignment = StageState::done;
    status.trackNullDepth = StageState::running;
    status.timeToStems = ev.secondsSinceLoad;
    status.decodeStemsSeconds = ev.decodeSeconds;
    status.alignmentSeconds = ev.alignmentSeconds;

    updateProgress();
    changed();
}

void SessionLoader::alignmentCorrected (const PairLoader::AlignmentCorrected& ev)
{
    if (session == nullptr || ! ev.session->sharesOriginalWith (*session))
        return;

    // A different offset is a jump in B: fade out, swap, fade back in at the same position.
    session = ev.session;
    engine.setSession (session, Handover::continueAtPosition);

    setAlignment (ev.measured);
    status.alignmentFromCache = false;
    status.notice = "Cached Alignment Offset " + signedSamples (ev.cached.offsetSamples)
                  + (ev.cached.polarity < 0 ? " (inverted)" : "") + " corrected to "
                  + signedSamples (ev.measured.offsetSamples) + (ev.measured.polarity < 0 ? " (inverted)" : "")
                  + " by re-measuring";

    updateProgress();
    changed();
}

void SessionLoader::trackNullDepthReady (const PairLoader::TrackNullDepth& ev)
{
    status.hasTrackNullDepth = true;
    status.trackNullDepthDb = ev.trackNullDepthDb;
    status.trackNullDepth = StageState::done;
    status.timeToNullDepth = ev.secondsSinceLoad;
    status.nullDepthSeconds = ev.seconds;

    updateProgress();
    changed();
}

void SessionLoader::failed (LoadStage stage, const juce::String& message)
{
    status.error = message;

    switch (stage)
    {
        case LoadStage::original:
            status.original = StageState::failed;
            status.stems = status.stems == StageState::running ? StageState::idle : status.stems;
            status.error << "  (the previous pair, if any, keeps playing)";
            break;
        case LoadStage::stems:          status.stems = StageState::failed; break;
        case LoadStage::alignment:      status.alignment = StageState::failed; break;
        case LoadStage::trackNullDepth: status.trackNullDepth = StageState::failed; break;
    }

    updateProgress();
    changed();
}

juce::String SessionLoader::deviceIdentity() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return deviceManager.getCurrentAudioDeviceType() + "|" + d->getName();

    return {};
}

double SessionLoader::applyRatePolicy (double sourceRate)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    policyNote = {};

    if (device == nullptr)
        return 0.0;

    double wanted = sourceRate;   // Native Rate preferred

    if (isWasapiSharedWithAutoConvert (deviceManager.getCurrentAudioDeviceType()))
    {
        // Shared mode "accepts" any rate and lets Windows convert to the endpoint's mix rate. That would be
        // Resampled anyway, just by Windows instead of r8brain: run the device at the mix rate instead.
        const double mixRate = sharedModeMixRate (device->getName().toWideCharPointer());

        if (mixRate > 0.0)
            wanted = mixRate;
    }

    if (! sameRate (device->getCurrentSampleRate(), wanted)
        && device->getAvailableSampleRates().contains (wanted))
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.sampleRate = wanted;
        const auto err = deviceManager.setAudioDeviceSetup (setup, true);

        if (err.isNotEmpty())
            policyNote = "Device refused " + hz (wanted) + ": " + err;
    }

    device = deviceManager.getCurrentAudioDevice();
    return device != nullptr ? device->getCurrentSampleRate() : 0.0;
}

void SessionLoader::deviceConfigurationChanged()
{
    if (session == nullptr)
        return;   // nothing loaded yet (a load in progress applies the policy when its decode finishes)

    const auto identity = deviceIdentity();

    if (identity.isNotEmpty() && identity != lastDeviceIdentity)
    {
        lastDeviceIdentity = identity;
        applyRatePolicy (session->sampleRate());   // a different device: try Native Rate again
    }

    updateRateStatus();   // whatever rate the device ends up at, the engine adapts (realtime resampling)
    changed();
}

void SessionLoader::updateRateStatus()
{
    if (session == nullptr)
        return;

    auto* device = deviceManager.getCurrentAudioDevice();
    const double deviceRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    const double srcRate = session->sampleRate();

    status.deviceRate = deviceRate;
    status.rateNote = policyNote;

    if (device != nullptr && isWasapiSharedWithAutoConvert (deviceManager.getCurrentAudioDeviceType()))
    {
        const double mixRate = sharedModeMixRate (device->getName().toWideCharPointer());
        juce::String note;

        if (mixRate <= 0.0)
            note = "WASAPI shared: endpoint mix rate unknown, Windows may convert";
        else if (! sameRate (mixRate, deviceRate))
            note = "WASAPI shared: Windows converts " + hz (deviceRate) + " -> " + hz (mixRate) + " (not true Native Rate)";
        else
            note = "WASAPI shared: endpoint mix rate " + hz (mixRate)
                   + (sameRate (mixRate, srcRate) ? juce::String() : juce::String (" - Exclusive Mode or ASIO may allow Native Rate"));

        status.rateNote = status.rateNote.isEmpty() ? note : status.rateNote + "; " + note;
    }

    if (deviceRate <= 0.0)
        status.rateState = "No audio device";
    else if (sameRate (deviceRate, srcRate))
        status.rateState = "Native Rate " + hz (srcRate);
    else
        status.rateState = "Resampled " + juce::String (srcRate, 0) + arrow() + hz (deviceRate) + " (realtime)";
}

} // namespace stemab
