#include "SessionLoader.h"

#include <cmath>

#include "PlatformWindows.h"
#include "analysis/Alignment.h"
#include "analysis/NullDepth.h"
#include "decode/Decode.h"

namespace stemab
{

namespace
{
    bool sameRate (double a, double b) noexcept     { return std::abs (a - b) < 0.5; }

    juce::String hz (double rate)                   { return juce::String (rate, 0) + " Hz"; }

    /** JUCE's plain "Windows Audio" device type is WASAPI shared mode with Windows-side rate conversion. */
    bool isWasapiSharedWithAutoConvert (const juce::String& typeName)
    {
        return typeName == "Windows Audio";
    }
}

struct SessionLoader::SourceResult
{
    std::shared_ptr<const Session> session;
    AlignmentResult alignment;
    double trackNullDepthDb = 0.0;
    juce::StringArray notes;
};

//==============================================================================
SessionLoader::SessionLoader (juce::AudioDeviceManager& dm, PlaybackEngine& e)
    : deviceManager (dm), engine (e)
{
    weakSelf = this;   // created here, on the message thread; workers only copy it
}

SessionLoader::~SessionLoader()
{
    latestGeneration.store (-1);
    pool.removeAllJobs (true, 30000);   // decode steps are not interruptible; wait for the current one
}

template <typename Fn>
void SessionLoader::postToMessageThread (Fn&& fn)
{
    juce::MessageManager::callAsync (std::forward<Fn> (fn));
}

void SessionLoader::changed()
{
    if (onStatusChanged)
        onStatusChanged();
}

//==============================================================================
void SessionLoader::load (const juce::File& original, const juce::File& stemFile)
{
    const int gen = ++generation;
    latestGeneration.store (gen);
    pool.removeAllJobs (true, 0);       // queued jobs go; a running one sees the new generation and stops

    // Replace the previous pair: stop playing it and free its buffers before decoding the next one.
    source.reset();
    playback.reset();
    engine.setSession (nullptr);
    resampleInFlightRate = 0.0;

    status = {};
    status.originalName = original.getFileName();
    status.stemFileName = stemFile.getFileName();
    status.busy = true;
    status.progress = "Decoding original...";
    changed();

    pool.addJob ([this, gen, original, stemFile] { runDecodeJob (gen, original, stemFile); });
}

void SessionLoader::setProgress (int gen, const juce::String& text)
{
    postToMessageThread ([weak = weakSelf, gen, text]
    {
        if (auto* self = weak.get(); self != nullptr && gen == self->generation)
        {
            self->status.progress = text;
            self->changed();
        }
    });
}

void SessionLoader::fail (int gen, const juce::String& message)
{
    postToMessageThread ([weak = weakSelf, gen, message]
    {
        if (auto* self = weak.get(); self != nullptr && gen == self->generation)
        {
            self->status.busy = false;
            self->status.error = message;
            self->status.progress = "Failed";
            self->changed();
        }
    });
}

//==============================================================================
// Worker thread

void SessionLoader::runDecodeJob (int gen, juce::File originalFile, juce::File stemFile)
{
    const auto cancelled = [this, gen] { return latestGeneration.load() != gen; };
    const auto t0 = juce::Time::getMillisecondCounterHiRes();

    auto original = decodeOriginal (originalFile);
    if (cancelled()) return;
    if (! original.ok) { fail (gen, "Original: " + original.error); return; }

    setProgress (gen, "Decoding stem file...");
    auto stems = decodeStemFile (stemFile);
    if (cancelled()) return;
    if (! stems.ok) { fail (gen, "Stem file: " + stems.error); return; }

    auto result = std::make_shared<SourceResult>();
    result->notes.addArray (original.notes);
    result->notes.addArray (stems.notes);

    const double rate = original.sampleRate;

    if (! sameRate (stems.sampleRate, rate))
    {
        setProgress (gen, "Resampling Stems " + hz (stems.sampleRate) + " -> " + hz (rate) + "...");
        for (auto& s : stems.stems)
        {
            s = resample (s, stems.sampleRate, rate);
            if (cancelled()) return;
        }
        result->notes.add ("Stems resampled " + hz (stems.sampleRate) + " -> " + hz (rate) + " to match the Original");
    }

    setProgress (gen, "Measuring alignment...");
    result->alignment = measureAlignment (original.audio, stems.stems, rate);
    if (cancelled()) return;
    if (! result->alignment.ok) { fail (gen, "Alignment failed: " + result->alignment.error); return; }

    auto aligned = alignStems (stems.stems, result->alignment.offsetSamples, result->alignment.polarity,
                               original.audio.getNumSamples());
    stems.stems = {};   // free the unaligned Stems now

    setProgress (gen, "Measuring Track Null Depth...");
    result->trackNullDepthDb = computeNullDepth (original.audio, aligned, rate).trackNullDepthDb;
    if (cancelled()) return;

    result->session = Session::create (std::move (original.audio), std::move (aligned), rate);
    if (result->session == nullptr) { fail (gen, "Invalid sample rate"); return; }

    result->notes.add ("Loaded in " + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) + " s");

    postToMessageThread ([weak = weakSelf, gen, result]
    {
        if (auto* self = weak.get())
            self->sourceReady (gen, result);
    });
}

//==============================================================================
// Message thread

void SessionLoader::sourceReady (int gen, std::shared_ptr<SourceResult> result)
{
    if (gen != generation)
        return;

    source = result->session;

    const auto& al = result->alignment;
    status.hasAlignment = true;
    status.offsetSamples = al.offsetSamples;
    status.polarity = al.polarity;
    status.confidence = al.confidence;
    status.lowConfidence = al.lowConfidence;
    status.hasTrackNullDepth = true;
    status.trackNullDepthDb = result->trackNullDepthDb;
    status.sourceRate = source->sampleRate();

    lastDeviceIdentity = deviceIdentity();
    applyRatePolicy (source->sampleRate());
    ensureSessionForDeviceRate();
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
    if (source == nullptr)
        return;   // nothing loaded yet (a load in progress applies the policy when its decode finishes)

    const auto identity = deviceIdentity();

    if (identity.isNotEmpty() && identity != lastDeviceIdentity)
    {
        lastDeviceIdentity = identity;
        applyRatePolicy (source->sampleRate());   // a different device: try Native Rate again
    }

    ensureSessionForDeviceRate();
    changed();
}

void SessionLoader::ensureSessionForDeviceRate()
{
    if (source == nullptr)
        return;

    auto* device = deviceManager.getCurrentAudioDevice();
    const double deviceRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    const double srcRate = source->sampleRate();

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

    if (deviceRate <= 0.0 || sameRate (deviceRate, srcRate))
    {
        // Native Rate (or no device yet: prepare at the source rate; re-checked when a device opens).
        resampleInFlightRate = 0.0;
        ++resampleSerial;

        if (playback != source)
            handToEngine (source);

        status.busy = false;
        status.progress = "Ready";
        status.rateState = deviceRate > 0.0 ? "Native Rate " + hz (srcRate) : "No audio device";
        return;
    }

    if (playback != nullptr && sameRate (playback->sampleRate(), deviceRate))
    {
        status.rateState = "Resampled " + hz (srcRate) + " -> " + hz (deviceRate);
        return;   // already playing at this rate
    }

    if (sameRate (resampleInFlightRate, deviceRate))
        return;   // already being made

    startResample (deviceRate);
}

void SessionLoader::startResample (double toRate)
{
    const int gen = generation;
    const int serial = ++resampleSerial;
    resampleInFlightRate = toRate;

    status.busy = true;
    status.progress = "Resampling to the device rate...";
    status.rateState = "Resampling " + hz (source->sampleRate()) + " -> " + hz (toRate) + "...";

    pool.addJob ([this, gen, serial, toRate, src = source, weak = weakSelf]
    {
        if (latestGeneration.load() != gen)
            return;

        // A and all four Stems through the same resampler with the same settings (ADR 0004).
        const double from = src->sampleRate();
        auto a = resample (src->original(), from, toRate);
        StemBuffers s;
        for (int k = 0; k < Session::numStems; ++k)
            s[(size_t) k] = resample (src->stem (k), from, toRate);

        auto session = Session::create (std::move (a), std::move (s), toRate);

        postToMessageThread ([weak, gen, serial, session]
        {
            if (auto* self = weak.get())
                self->resampleReady (gen, serial, session);
        });
    });
}

void SessionLoader::resampleReady (int gen, int serial, std::shared_ptr<const Session> session)
{
    if (gen != generation || serial != resampleSerial || session == nullptr || source == nullptr)
        return;

    resampleInFlightRate = 0.0;

    auto* device = deviceManager.getCurrentAudioDevice();
    const double deviceRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;

    if (! sameRate (deviceRate, session->sampleRate()))
    {
        ensureSessionForDeviceRate();   // the device changed again meanwhile
        changed();
        return;
    }

    handToEngine (std::move (session));
    status.busy = false;
    status.progress = "Ready";
    status.rateState = "Resampled " + hz (source->sampleRate()) + " -> " + hz (deviceRate);
    changed();
}

void SessionLoader::handToEngine (std::shared_ptr<const Session> s)
{
    playback = std::move (s);
    status.playbackRate = playback != nullptr ? playback->sampleRate() : 0.0;
    engine.setSession (playback);
}

} // namespace stemab
