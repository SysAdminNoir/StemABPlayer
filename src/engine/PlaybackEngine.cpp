#include "engine/PlaybackEngine.h"

#include "engine/OutputResampler.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace stemab
{

namespace
{
    /** Transport fade shape: C1-continuous, exactly 0 at 0 and exactly 1 at 1. */
    inline float fadeShape (float phase) noexcept     { return phase * phase * (3.0f - 2.0f * phase); }

    /** Single-writer "max since last read": the UI resets it with exchange (0). */
    inline void accumulatePeak (std::atomic<float>& target, float value) noexcept
    {
        float old = target.load (std::memory_order_relaxed);
        while (value > old && ! target.compare_exchange_weak (old, value, std::memory_order_relaxed)) {}
    }

    /** Stands in for "no session" so that nullptr can mean "nothing pending". Never played (length 0). */
    const std::shared_ptr<const Session>& emptySession()
    {
        static const std::shared_ptr<const Session> empty = Session::create ({}, {}, 1.0);
        return empty;
    }

    /** A session with audio (the empty stand-in and nullptr have none; their rate means nothing). */
    inline bool hasAudio (const Session* s) noexcept   { return s != nullptr && s->length() > 0; }

    inline bool sameRate (double a, double b) noexcept  { return std::abs (a - b) < 0.5; }

    /** Silent source samples that count as "fed for ever" (a fresh resampler holds nothing). */
    constexpr juce::int64 alwaysSilent = std::numeric_limits<juce::int64>::max() / 4;

    inline bool hasStems (const Session* s) noexcept   { return s != nullptr && s->hasStems(); }

    // pendingSession packs the Handover into the pointer's low bits.
    static_assert (alignof (Session) >= 4, "the Handover tag needs two free low bits");
    constexpr std::uintptr_t handoverMask = 3;

    inline std::uintptr_t pack (const Session* s, Handover h) noexcept
    {
        return reinterpret_cast<std::uintptr_t> (s) | (std::uintptr_t) h;
    }

    inline const Session* sessionOf (std::uintptr_t packed) noexcept
    {
        return reinterpret_cast<const Session*> (packed & ~handoverMask);
    }

    inline Handover handoverOf (std::uintptr_t packed) noexcept   { return (Handover) (packed & handoverMask); }
}

//==============================================================================
PlaybackEngine::PlaybackEngine()
{
    prepare (44100.0, 512);
}

PlaybackEngine::~PlaybackEngine()
{
    // The device must be stopped. keepAlive releases every session; the resamplers are freed here.
    delete resampler;
    delete standby;
    delete pendingResampler.exchange (nullptr);

    const int ready = retiredResamplerFifo.getNumReady();
    retiredResamplerFifo.read (ready).forEach ([this] (int index) { delete retiredResamplerSlots[(size_t) index]; });
}

//==============================================================================
void PlaybackEngine::prepare (double deviceSampleRate, int maximumBlockSize)
{
    deviceRate = deviceSampleRate > 0.0 ? deviceSampleRate : 44100.0;
    maxBlock = juce::jmax (256, maximumBlockSize);

    mixL.assign ((size_t) maxBlock, 0.0f);
    mixR.assign ((size_t) maxBlock, 0.0f);

    // Nothing to play: run the (silent) mix at the device rate, so nothing is resampled.
    if (! hasAudio (current))
        mixRate = deviceRate;

    setMixRate (mixRate);
    fadePos = juce::jmin (fadePos, fadeLen);

    // reset() snaps each ramp to its target, so the first block starts settled.
    updateGainTargets();
    for (auto* g : { &gainA, &gainB, &gainR, &gainMaster, &gainStemsPresent })
        g->setCurrentAndTargetValue (g->getTargetValue());
    for (auto& g : gainStem)
        g.setCurrentAndTargetValue (g.getTargetValue());
    for (auto& g : gainStemA)
        g.setCurrentAndTargetValue (g.getTargetValue());

    // Rebuild the output path for the new device (no callback runs now, so building and freeing is fine).
    delete resampler;
    resampler = nullptr;
    delete standby;
    standby = nullptr;

    if (! sameRate (mixRate, deviceRate))
        resampler = new OutputResampler (mixRate, deviceRate, maxBlock);

    // A session still waiting for its swap at another rate: its resampler too (one the UI built for the
    // previous device rate would no longer match).
    if (swapPending && hasAudio (incoming) && ! sameRate (incoming->sampleRate(), mixRate)
        && ! sameRate (incoming->sampleRate(), deviceRate))
        standby = new OutputResampler (incoming->sampleRate(), deviceRate, maxBlock);

    // The device restarted: if the transport runs, come back with a fade-in rather than a hard start.
    holdMixer = true;
    resumeMixer();
    requestOpen = false;
    silentRun = alwaysSilent;

    deviceRatePublished.store (deviceRate, std::memory_order_relaxed);
    maxBlockPublished.store (maxBlock, std::memory_order_relaxed);
    resamplingFlag.store (resampler != nullptr, std::memory_order_relaxed);
    resamplerLatency.store (resampler != nullptr ? resampler->getLatencySeconds() : 0.0, std::memory_order_relaxed);
}

//==============================================================================
// UI thread

void PlaybackEngine::offerResampler (std::unique_ptr<OutputResampler> r)
{
    if (auto* never = pendingResampler.exchange (r.release(), std::memory_order_acq_rel))
        delete never;    // the audio thread had not taken it: it never will
}

void PlaybackEngine::upgradeSession (std::shared_ptr<const Session> upgraded)
{
    releaseRetiredSessions();

    if (upgraded == nullptr)
        return;

    keepAlive.push_back (upgraded);

    // An earlier upgrade the audio thread never took is superseded: release it here.
    if (const Session* never = pendingUpgrade.exchange (upgraded.get(), std::memory_order_acq_rel))
    {
        auto it = std::find_if (keepAlive.begin(), keepAlive.end(), [never] (const auto& p) { return p.get() == never; });
        if (it != keepAlive.end())
            keepAlive.erase (it);
    }
}

void PlaybackEngine::setSession (std::shared_ptr<const Session> session, Handover handover)
{
    releaseRetiredSessions();

    if (session == nullptr)
        session = emptySession();

    // Build the output resampler this session needs now, off the audio thread, unless the active one fits.
    std::unique_ptr<OutputResampler> needed;

    if (hasAudio (session.get()))
    {
        const double dev = deviceRatePublished.load (std::memory_order_relaxed);
        const double rate = session->sampleRate();
        const bool activeFits = resamplingFlag.load (std::memory_order_relaxed)
                                && sameRate (mixRatePublished.load (std::memory_order_relaxed), rate);

        if (! sameRate (rate, dev) && ! activeFits)
            needed = std::make_unique<OutputResampler> (rate, dev, maxBlockPublished.load (std::memory_order_relaxed));
    }

    keepAlive.push_back (session);

    // If the audio thread had not yet taken the previous pending session, it never will: release it here.
    if (const Session* never = sessionOf (pendingSession.exchange (pack (session.get(), handover), std::memory_order_acq_rel)))
    {
        auto it = std::find_if (keepAlive.begin(), keepAlive.end(), [never] (const auto& p) { return p.get() == never; });
        if (it != keepAlive.end())
            keepAlive.erase (it);
    }

    // Right after the session, so the audio thread sees the session first (a resampler that arrives with
    // no session needing it is dropped; the audio thread would then ask for it again - see updateOutputPath).
    if (needed != nullptr)
        offerResampler (std::move (needed));
}

void PlaybackEngine::releaseRetiredSessions()
{
    if (const int ready = retiredFifo.getNumReady(); ready > 0)
    {
        retiredFifo.read (ready).forEach ([this] (int index)
        {
            const Session* done = retiredSlots[(size_t) index];
            auto it = std::find_if (keepAlive.begin(), keepAlive.end(), [done] (const auto& p) { return p.get() == done; });
            if (it != keepAlive.end())
                keepAlive.erase (it);      // the last owner frees it here, on the UI thread
        });
    }

    if (const int ready = retiredResamplerFifo.getNumReady(); ready > 0)
        retiredResamplerFifo.read (ready).forEach ([this] (int index) { delete retiredResamplerSlots[(size_t) index]; });

    // The audio thread asked for a resampler it has no other way to get.
    const auto serial = requestSerial.load (std::memory_order_acquire);
    if (serial != servedRequestSerial)
    {
        servedRequestSerial = serial;
        const double src = requestSourceRate.load (std::memory_order_relaxed);
        const double dst = requestDeviceRate.load (std::memory_order_relaxed);
        const int mb = requestMaxBlock.load (std::memory_order_relaxed);

        // (If setSession() served it meanwhile, the audio thread drops the spare one.)
        if (src > 0.0 && dst > 0.0 && mb > 0 && ! sameRate (src, dst))
            offerResampler (std::make_unique<OutputResampler> (src, dst, mb));
    }
}

void PlaybackEngine::play() noexcept     { transportCommand.store (1, std::memory_order_release); }
void PlaybackEngine::pause() noexcept    { transportCommand.store (2, std::memory_order_release); }

void PlaybackEngine::seek (juce::int64 sample) noexcept
{
    seekRequest.store (juce::jmax ((juce::int64) 0, sample), std::memory_order_release);
}

void PlaybackEngine::setLoopEnabled (bool shouldLoop) noexcept
{
    loopEnabled.store (shouldLoop, std::memory_order_release);
}

void PlaybackEngine::setLoopRegion (juce::int64 start, juce::int64 end) noexcept
{
    // Seqlock (single writer): odd version while writing; the audio thread retries next block on a torn read.
    const auto v = loopRegionVersion.load (std::memory_order_relaxed);
    loopRegionVersion.store (v + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    loopStartRequest.store (start, std::memory_order_relaxed);
    loopEndRequest.store (end, std::memory_order_relaxed);
    loopRegionVersion.store (v + 2, std::memory_order_release);
}

void PlaybackEngine::setCrossfade (float x) noexcept
{
    crossfadeX.store (std::isnan (x) ? 0.0f : juce::jlimit (0.0f, 1.0f, x), std::memory_order_relaxed);
}

void PlaybackEngine::setCurve (Curve c) noexcept                { curve.store ((int) c, std::memory_order_relaxed); }
void PlaybackEngine::setListenMode (ListenMode m) noexcept      { listenMode.store ((int) m, std::memory_order_relaxed); }

void PlaybackEngine::setStemMuted (int i, bool m) noexcept
{
    if (juce::isPositiveAndBelow (i, Session::numStems))
        muted[(size_t) i].store (m, std::memory_order_relaxed);
}

void PlaybackEngine::setStemSoloed (int i, bool s) noexcept
{
    if (juce::isPositiveAndBelow (i, Session::numStems))
        soloed[(size_t) i].store (s, std::memory_order_relaxed);
}

bool PlaybackEngine::isStemMuted (int i) const noexcept
{
    return juce::isPositiveAndBelow (i, Session::numStems) && muted[(size_t) i].load (std::memory_order_relaxed);
}

bool PlaybackEngine::isStemSoloed (int i) const noexcept
{
    return juce::isPositiveAndBelow (i, Session::numStems) && soloed[(size_t) i].load (std::memory_order_relaxed);
}

void PlaybackEngine::setStemAMuted (int i, bool m) noexcept
{
    if (juce::isPositiveAndBelow (i, Session::numStems))
        mutedA[(size_t) i].store (m, std::memory_order_relaxed);
}

void PlaybackEngine::setStemASoloed (int i, bool s) noexcept
{
    if (juce::isPositiveAndBelow (i, Session::numStems))
        soloedA[(size_t) i].store (s, std::memory_order_relaxed);
}

bool PlaybackEngine::isStemAMuted (int i) const noexcept
{
    return juce::isPositiveAndBelow (i, Session::numStems) && mutedA[(size_t) i].load (std::memory_order_relaxed);
}

bool PlaybackEngine::isStemASoloed (int i) const noexcept
{
    return juce::isPositiveAndBelow (i, Session::numStems) && soloedA[(size_t) i].load (std::memory_order_relaxed);
}

void PlaybackEngine::setMasterGainDb (double gainDb) noexcept
{
    if (std::isnan (gainDb))
        gainDb = -std::numeric_limits<double>::infinity();

    gainDb = juce::jmin (gainDb, 0.0);
    masterGainDb.store (gainDb, std::memory_order_relaxed);
    masterGainLinear.store (gainDb <= masterGainFloorDb ? 0.0f : (float) std::pow (10.0, gainDb / 20.0),
                            std::memory_order_relaxed);
}

std::array<bool, Session::numStems> PlaybackEngine::audibleStems (const std::array<bool, Session::numStems>& m,
                                                                  const std::array<bool, Session::numStems>& s) noexcept
{
    const bool anySolo = std::any_of (s.begin(), s.end(), [] (bool b) { return b; });

    std::array<bool, Session::numStems> audible {};
    for (size_t i = 0; i < audible.size(); ++i)
        audible[i] = anySolo ? s[i] : ! m[i];

    return audible;
}

MeterReading PlaybackEngine::readMeter (MeterTap tap, int channel) noexcept
{
    const int t = (int) tap;
    if (! juce::isPositiveAndBelow (t, numMeterTaps) || ! juce::isPositiveAndBelow (channel, 2))
        return {};

    return { meterPeak[(size_t) t][(size_t) channel].exchange (0.0f, std::memory_order_relaxed),
             meterRms [(size_t) t][(size_t) channel].load (std::memory_order_relaxed) };
}

//==============================================================================
// Audio thread

juce::int64 PlaybackEngine::effectiveEnd() const noexcept
{
    const juce::int64 len = current != nullptr ? current->length() : 0;

    if (loopOn && loopStart >= 0 && loopStart < juce::jmin (loopEnd, len))
        return juce::jmin (loopEnd, len);

    return len;
}

juce::int64 PlaybackEngine::effectiveLoopStart() const noexcept
{
    const juce::int64 len = current != nullptr ? current->length() : 0;

    if (loopOn && loopStart >= 0 && loopStart < juce::jmin (loopEnd, len))
        return loopStart;

    return 0;
}

void PlaybackEngine::retire (const Session* s) noexcept
{
    if (s == nullptr)
        return;

    // Space is guaranteed by takeCommands(), which only takes a pending session when 2 slots are free.
    retiredFifo.write (1).forEach ([this, s] (int index) { retiredSlots[(size_t) index] = s; });
}

void PlaybackEngine::beginFadeOut() noexcept
{
    fadeDir = -1;
}

void PlaybackEngine::swapInIncomingSession() noexcept
{
    retire (current);
    current = incoming;
    incoming = nullptr;
    swapPending = false;

    // The mix runs at the session's own rate; the output path follows at the next block (updateOutputPath).
    if (hasAudio (current) && ! sameRate (current->sampleRate(), mixRate))
        setMixRate (current->sampleRate());

    const bool resume = incomingHandover != Handover::pauseAtStart && incomingResumes && hasAudio (current);
    const juce::int64 len = current != nullptr ? current->length() : 0;

    pos = incomingHandover == Handover::continueAtPosition ? juce::jlimit ((juce::int64) 0, len, pos) : 0;
    running = resume;
    fadePos = 0;
    fadeDir = resume ? +1 : 0;
    pendingJump = -1;
    pendingPause = false;
    incomingHandover = Handover::pauseAtStart;
    incomingResumes = false;

    // B and the Residual enter with the transport fade-in, so their own ramp starts settled.
    gainStemsPresent.setCurrentAndTargetValue (hasStems (current) ? 1.0f : 0.0f);
}

void PlaybackEngine::takeUpgrade() noexcept
{
    // Take the upgrade first: acquiring it makes everything the UI did before upgradeSession() visible, in
    // particular a setSession() of the session it upgrades. That replacement must be taken first (the
    // upgrade may be for it), so the upgrade is held until pendingSession is empty.
    if (heldUpgrade == nullptr)
    {
        heldUpgrade = pendingUpgrade.exchange (nullptr, std::memory_order_acq_rel);
    }
    else if (retiredFifo.getFreeSpace() >= (swapPending ? 3 : 2))
    {
        if (const Session* newer = pendingUpgrade.exchange (nullptr, std::memory_order_acq_rel))
        {
            retire (heldUpgrade);       // superseded while held
            heldUpgrade = newer;
        }
    }

    // Keep a slot for the pending swap's own retire.
    if (heldUpgrade == nullptr
        || pendingSession.load (std::memory_order_acquire) != 0
        || retiredFifo.getFreeSpace() < (swapPending ? 2 : 1))
        return;

    const Session* u = heldUpgrade;
    heldUpgrade = nullptr;

    if (swapPending && incoming != nullptr && u->sharesOriginalWith (*incoming))
    {
        retire (incoming);              // still waiting for its fade-out: swap the upgraded one in instead
        incoming = u;
    }
    else if (current != nullptr && u->sharesOriginalWith (*current) && sameRate (u->sampleRate(), current->sampleRate()))
    {
        retire (current);               // same Original, same length and rate: the transport carries on
        current = u;                    // (updateGainTargets() ramps B and R in or out)
    }
    else
    {
        retire (u);                     // stale: for a session that is no longer here
    }
}

void PlaybackEngine::resolveFadeOut() noexcept
{
    fadePos = 0;
    fadeDir = 0;

    if (swapPending)
    {
        swapInIncomingSession();
        return;
    }

    if (pendingJump >= 0)
    {
        pos = pendingJump;
        pendingJump = -1;
    }

    if (pendingPause)
    {
        pendingPause = false;
        running = false;
    }
    else
    {
        fadeDir = +1;
    }
}

void PlaybackEngine::takeCommands() noexcept
{
    // 1. Session handoff (needs room to retire both the session being replaced and a superseded incoming one).
    if (retiredFifo.getFreeSpace() >= 2)
    {
        if (const auto packed = pendingSession.exchange (0, std::memory_order_acq_rel); packed != 0)
        {
            const Session* p = sessionOf (packed);

            if (swapPending)
                retire (incoming);
            else
                incomingResumes = running && ! pendingPause;   // playing (not fading into a Pause) now

            incoming = p;
            incomingHandover = handoverOf (packed);
            swapPending = true;
            pendingJump = -1;
            pendingPause = false;

            if (running && fadePos > 0 && hasAudio (current) && ! holdMixer)
                beginFadeOut();          // swapped when the fade-out completes
            else
                swapInIncomingSession();
        }
    }

    // 1b. Session upgrade (same Original, e.g. the Stems arrived): no fade, the transport carries on.
    takeUpgrade();

    // 2. Loop settings (seqlock read; a torn read keeps the previous region until the next block).
    loopOn = loopEnabled.load (std::memory_order_acquire);
    {
        const auto v1 = loopRegionVersion.load (std::memory_order_acquire);
        const auto s  = loopStartRequest.load (std::memory_order_relaxed);
        const auto e  = loopEndRequest.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        const auto v2 = loopRegionVersion.load (std::memory_order_relaxed);

        if (v1 == v2 && (v1 & 1u) == 0)
        {
            loopStart = s;
            loopEnd = e;
        }
    }

    const bool playable = hasAudio (current);

    // 3. Play / Pause (last command wins).
    switch (transportCommand.exchange (0, std::memory_order_acq_rel))
    {
        case 1:   // play
            if (swapPending)
            {
                incomingResumes = true;               // honoured by a continue* handover
                break;
            }

            if (! playable)
                break;

            if (! running)
            {
                if (pos >= effectiveEnd())
                    pos = effectiveLoopStart();       // at the end: start again

                running = true;
                fadeDir = +1;
            }
            else if (pendingPause)
            {
                pendingPause = false;                 // cancel a Pause still fading out
                if (pendingJump < 0)
                    fadeDir = +1;
            }
            break;

        case 2:   // pause
            if (swapPending)
            {
                incomingResumes = false;
            }
            else if (running)
            {
                pendingPause = true;
                beginFadeOut();                       // resolved at fade == 0 (next sample if already 0)
            }
            break;

        default:
            break;
    }

    // 4. Seek.
    const auto target = seekRequest.exchange (-1, std::memory_order_acq_rel);
    if (target >= 0 && current != nullptr && ! swapPending)
    {
        const auto clamped = juce::jlimit ((juce::int64) 0, current->length(), target);

        if (running)
        {
            pendingJump = clamped;                    // fade out -> jump -> fade in
            beginFadeOut();
        }
        else
        {
            pos = clamped;                            // paused: nothing audible to fade
        }
    }
}

void PlaybackEngine::updateGainTargets() noexcept
{
    if ((ListenMode) listenMode.load (std::memory_order_relaxed) == ListenMode::Residual)
    {
        gainA.setTargetValue (0.0f);
        gainB.setTargetValue (0.0f);
        gainR.setTargetValue (1.0f);
    }
    else
    {
        const auto g = crossfadeGains (crossfadeX.load (std::memory_order_relaxed),
                                       (Curve) curve.load (std::memory_order_relaxed));
        gainA.setTargetValue (g.gA);
        gainB.setTargetValue (g.gB);
        gainR.setTargetValue (0.0f);
    }

    std::array<bool, Session::numStems> m {}, s {};
    for (size_t i = 0; i < m.size(); ++i)
    {
        m[i] = muted[i].load (std::memory_order_relaxed);
        s[i] = soloed[i].load (std::memory_order_relaxed);
    }

    const auto audible = audibleStems (m, s);
    for (size_t i = 0; i < audible.size(); ++i)
        gainStem[i].setTargetValue (audible[i] ? 1.0f : 0.0f);

    std::array<bool, Session::numStems> mA {}, sA {};
    for (size_t i = 0; i < mA.size(); ++i)
    {
        mA[i] = mutedA[i].load (std::memory_order_relaxed);
        sA[i] = soloedA[i].load (std::memory_order_relaxed);
    }

    const auto audibleA = audibleStems (mA, sA);
    for (size_t i = 0; i < audibleA.size(); ++i)
        gainStemA[i].setTargetValue (audibleA[i] ? 1.0f : 0.0f);

    gainMaster.setTargetValue (masterGainLinear.load (std::memory_order_relaxed));
    gainStemsPresent.setTargetValue (hasStems (current) ? 1.0f : 0.0f);
}

void PlaybackEngine::publish (int numSamples) noexcept
{
    playhead.store (pos, std::memory_order_relaxed);
    playingFlag.store (running && ! pendingPause && ! swapPending, std::memory_order_relaxed);

    const bool hasSession = hasAudio (current);
    sessionLength.store (hasSession ? current->length() : 0, std::memory_order_relaxed);
    sessionRate.store (hasSession ? current->sampleRate() : 0.0, std::memory_order_relaxed);
    sessionStems.store (hasSession && current->hasStems(), std::memory_order_relaxed);
    sessionStemsA.store (hasSession && current->hasStemsA(), std::memory_order_relaxed);
    mixRatePublished.store (mixRate, std::memory_order_relaxed);
    resamplingFlag.store (resampler != nullptr && ! holdMixer, std::memory_order_relaxed);
    resamplerLatency.store (resampler != nullptr ? resampler->getLatencySeconds() : 0.0, std::memory_order_relaxed);

    // The source taps cover the source samples mixed in this block, the Output tap the device samples.
    const double invSource = sourceSamplesThisBlock > 0 ? 1.0 / (double) sourceSamplesThisBlock : 0.0;
    const double invOut = numSamples > 0 ? 1.0 / (double) numSamples : 0.0;

    for (size_t t = 0; t < (size_t) numMeterTaps; ++t)
    {
        const bool isOutput = t == (size_t) MeterTap::Output;

        for (size_t c = 0; c < 2; ++c)
        {
            accumulatePeak (meterPeak[t][c], blockPeak[t][c]);

            if (isOutput || sourceSamplesThisBlock > 0)
                meterRms[t][c].store ((float) std::sqrt (blockSumSq[t][c] * (isOutput ? invOut : invSource)),
                                      std::memory_order_relaxed);
        }
    }

    if (blockClipped)
        clipFlag.store (true, std::memory_order_relaxed);
}

//==============================================================================
// Output path (audio thread)

void PlaybackEngine::setMixRate (double rate) noexcept
{
    mixRate = rate;

    fadeLen = juce::jmax (1, (int) std::lround (transportFadeSeconds * mixRate));
    fadePos = juce::jmin (fadePos, fadeLen);
    fadeLenPublished.store (fadeLen, std::memory_order_relaxed);

    // Ramp lengths in samples follow the rate (reset() also snaps each ramp to its target).
    for (auto* g : { &gainA, &gainB, &gainR, &gainMaster })
        g->reset (mixRate, gainRampSeconds);
    for (auto& g : gainStem)
        g.reset (mixRate, gainRampSeconds);
    for (auto& g : gainStemA)
        g.reset (mixRate, gainRampSeconds);
    gainStemsPresent.reset (mixRate, stemsFadeSeconds);

    mixRatePublished.store (mixRate, std::memory_order_relaxed);
}

void PlaybackEngine::retireResampler (OutputResampler* r) noexcept
{
    if (r == nullptr)
        return;

    // Callers check for space first.
    retiredResamplerFifo.write (1).forEach ([this, r] (int index) { retiredResamplerSlots[(size_t) index] = r; });
}

void PlaybackEngine::requestResampler() noexcept
{
    if (requestOpen)
        return;

    requestSourceRate.store (mixRate, std::memory_order_relaxed);
    requestDeviceRate.store (deviceRate, std::memory_order_relaxed);
    requestMaxBlock.store (maxBlock, std::memory_order_relaxed);
    requestSerial.fetch_add (1, std::memory_order_release);
    requestOpen = true;
}

void PlaybackEngine::resumeMixer() noexcept
{
    if (! holdMixer)
        return;

    holdMixer = false;

    // The mix was held mid-stream: come back with a fade-in (or finish a pending fade-out at once).
    if (running)
    {
        fadePos = 0;
        if (fadeDir >= 0)
            fadeDir = +1;
    }
}

void PlaybackEngine::updateOutputPath() noexcept
{
    const bool wantPassthrough = sameRate (mixRate, deviceRate);

    // 1. A resampler from the UI thread. Keep it as the standby (one at a time).
    if (retiredResamplerFifo.getFreeSpace() >= 2)
    {
        if (auto* offered = pendingResampler.exchange (nullptr, std::memory_order_acq_rel))
        {
            retireResampler (standby);
            standby = offered;
            requestOpen = false;   // answered (if it does not fit, the next request goes out below)
        }
    }

    // A standby that serves neither the mix rate nor a session waiting for its swap is useless.
    if (standby != nullptr && retiredResamplerFifo.getFreeSpace() >= 1)
    {
        const bool forMix = ! wantPassthrough && resampler == nullptr && standby->matches (mixRate, deviceRate, maxBlock);
        const bool forMixSwitch = ! wantPassthrough && resampler != nullptr
                                  && ! resampler->matches (mixRate, deviceRate, maxBlock)
                                  && standby->matches (mixRate, deviceRate, maxBlock);
        const bool forIncoming = swapPending && hasAudio (incoming)
                                 && standby->matches (incoming->sampleRate(), deviceRate, maxBlock)
                                 && ! sameRate (incoming->sampleRate(), mixRate);

        if (! (forMix || forMixSwitch || forIncoming))
        {
            retireResampler (standby);
            standby = nullptr;
        }
    }

    // 2. Is the active path right for (mixRate -> deviceRate)?
    const bool fits = wantPassthrough ? resampler == nullptr
                                      : resampler != nullptr && resampler->matches (mixRate, deviceRate, maxBlock);
    if (fits)
    {
        requestOpen = false;
        resumeMixer();
        return;
    }

    // 3. Switch. Hold the mix and let the old resampler play out what it still holds, then swap paths.
    holdMixer = true;

    const bool flushed = resampler == nullptr || silentRun >= resampler->getFlushSourceSamples();
    if (! flushed || retiredResamplerFifo.getFreeSpace() < 1)
        return;

    if (wantPassthrough)
    {
        retireResampler (resampler);
        resampler = nullptr;
        resumeMixer();
        return;
    }

    if (standby != nullptr && standby->matches (mixRate, deviceRate, maxBlock))
    {
        retireResampler (resampler);
        resampler = standby;
        standby = nullptr;
        silentRun = alwaysSilent;   // fresh: holds nothing
        resumeMixer();
        return;
    }

    requestResampler();   // silence until the UI thread builds it (releaseRetiredSessions())
}

void PlaybackEngine::pullSourceThunk (void* engine, float* left, float* right, int numSamples)
{
    static_cast<PlaybackEngine*> (engine)->pullSource (left, right, numSamples);
}

void PlaybackEngine::pullSource (float* left, float* right, int numSamples) noexcept
{
    bool sound = false;

    if (holdMixer)
    {
        juce::FloatVectorOperations::clear (left, numSamples);
        juce::FloatVectorOperations::clear (right, numSamples);
    }
    else
    {
        sound = renderMix (left, right, numSamples);
    }

    silentRun = sound ? 0 : juce::jmin (alwaysSilent, silentRun + numSamples);
    sourceSamplesThisBlock += numSamples;
}

void PlaybackEngine::writeOutput (float* outL, float* outR, bool monoOut, int offset, int numSamples) noexcept
{
    constexpr size_t tOut = (size_t) MeterTap::Output;
    const float* yL = mixL.data();
    const float* yR = mixR.data();

    for (int i = 0; i < numSamples; ++i)
    {
        const float y[2] = { yL[i], yR[i] };

        for (size_t c = 0; c < 2; ++c)
        {
            blockPeak[tOut][c] = juce::jmax (blockPeak[tOut][c], std::abs (y[c]));
            blockSumSq[tOut][c] += (double) y[c] * y[c];
        }

        if (std::abs (y[0]) > 1.0f || std::abs (y[1]) > 1.0f)
            blockClipped = true;              // indicated, never hidden: the samples are written as they are

        if (monoOut)
        {
            if (outL != nullptr) outL[offset + i] = 0.5f * (y[0] + y[1]);
        }
        else
        {
            if (outL != nullptr) outL[offset + i] = y[0];
            if (outR != nullptr) outR[offset + i] = y[1];
        }
    }
}

void PlaybackEngine::process (float* const* outputChannels, int numOutputChannels, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    float* outL = nullptr;
    float* outR = nullptr;

    if (outputChannels != nullptr)
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannels[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannels[ch], numSamples);

        outL = numOutputChannels > 0 ? outputChannels[0] : nullptr;
        outR = numOutputChannels > 1 ? outputChannels[1] : nullptr;
    }

    const bool monoOut = numOutputChannels == 1;

    for (auto& tap : blockPeak)  tap = { 0.0f, 0.0f };
    for (auto& tap : blockSumSq) tap = { 0.0, 0.0 };
    blockClipped = false;
    sourceSamplesThisBlock = 0;

    takeCommands();
    updateGainTargets();
    updateOutputPath();

    // Mix at the source rate; straight to the device (Native Rate) or through the resampler. Device blocks
    // larger than prepare() announced are done in pieces.
    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (numSamples - done, maxBlock);

        if (resampler != nullptr)
            resampler->render (&PlaybackEngine::pullSourceThunk, this, mixL.data(), mixR.data(), n);
        else
            pullSource (mixL.data(), mixR.data(), n);

        writeOutput (outL, outR, monoOut, done, n);
        done += n;
    }

    publish (numSamples);
}

/** Mixes `numSamples` source-rate samples into left/right (silence when not running). Returns true if the
    transport ran in this piece (i.e. it may contain sound). */
bool PlaybackEngine::renderMix (float* left, float* right, int numSamples) noexcept
{
    juce::FloatVectorOperations::clear (left, numSamples);
    juce::FloatVectorOperations::clear (right, numSamples);

    if (! running)
    {
        for (auto* g : { &gainA, &gainB, &gainR, &gainMaster, &gainStemsPresent })
            g->skip (numSamples);
        for (auto& g : gainStem)
            g.skip (numSamples);
        for (auto& g : gainStemA)
            g.skip (numSamples);

        return false;
    }

    const Session* s = current;
    const juce::int64 len = s->length();
    const bool hasOrig = s->hasOriginal();
    const bool hasStemsA = s->hasStemsA();
    const bool stemsPresent = s->hasStems();   // false: Side A only - B and the Residual are silent
    const float* aL = hasOrig ? s->originalChannel (0) : nullptr;
    const float* aR = hasOrig ? s->originalChannel (1) : nullptr;
    std::array<const float*, Session::numStems> sAL {}, sAR {};
    if (hasStemsA)
    {
        for (int k = 0; k < Session::numStems; ++k)
        {
            sAL[(size_t) k] = s->stemAChannel (k, 0);
            sAR[(size_t) k] = s->stemAChannel (k, 1);
        }
    }
    std::array<const float*, Session::numStems> sL {}, sR {};
    for (int k = 0; k < Session::numStems; ++k)
    {
        sL[(size_t) k] = s->stemChannel (k, 0);
        sR[(size_t) k] = s->stemChannel (k, 1);
    }

    constexpr size_t tA = (size_t) MeterTap::Original, tB = (size_t) MeterTap::StemSum,
                     tStem0 = (size_t) MeterTap::Drums, tStemA0 = (size_t) MeterTap::DrumsA;

    for (int i = 0; i < numSamples; ++i)
    {
        const float ga = gainA.getNextValue();
        const float gb = gainB.getNextValue();
        const float gr = gainR.getNextValue();
        const float gm = gainMaster.getNextValue();
        const float gp = gainStemsPresent.getNextValue();
        std::array<float, Session::numStems> gs {}, gsA {};
        for (size_t k = 0; k < gs.size(); ++k)
        {
            gs[k] = gainStem[k].getNextValue() * gp;   // exact when gp == 1 (the steady state)
            gsA[k] = gainStemA[k].getNextValue();
        }

        if (! running || current != s)
            continue;   // paused or swapped earlier in this piece: the rest stays silent (the new session
                        // starts with the next piece, at fade 0)

        // A fade-out has reached zero: apply what it was for (swap / jump / pause).
        if (fadeDir < 0 && fadePos <= 0)
        {
            resolveFadeOut();

            if (current != s || ! running)
                continue;   // swapped (now paused at 0) or paused: silent from here on
        }

        const juce::int64 end = effectiveEnd();

        if (pos >= end)
        {
            if (fadePos <= 0)
            {
                if (loopOn)
                {
                    pos = effectiveLoopStart();   // sample-accurate wrap; the fade has already reached zero
                    fadeDir = +1;
                }
                else
                {
                    pos = len;                    // end of track: hold at the end, paused
                    running = false;
                    fadeDir = 0;
                    continue;
                }
            }
            else if (fadeDir >= 0)
            {
                // Only after the Loop Region moved behind the playhead: fade out, then go to its start.
                pendingJump = loopOn ? effectiveLoopStart() : len;
                beginFadeOut();
            }
        }

        // Transport fade: advance, then cap so it reaches exactly zero on the last sample before `end`.
        if (fadeDir > 0)
        {
            if (++fadePos >= fadeLen) { fadePos = fadeLen; fadeDir = 0; }
        }
        else if (fadeDir < 0)
        {
            fadePos = juce::jmax (0, fadePos - 1);
        }

        if (pos < end)
            fadePos = (int) juce::jmin ((juce::int64) fadePos, end - 1 - pos);

        const float env = fadePos >= fadeLen ? 1.0f : fadeShape ((float) fadePos / (float) fadeLen);

        // Read (zeros past the buffer - never read beyond it).
        float a[2] = { 0.0f, 0.0f };      // audible Side A (Original or audible Stem A)
        float allA[2] = { 0.0f, 0.0f };   // full Side A (Original or all four Stem A), for the Residual
        float b[2] = { 0.0f, 0.0f };      // audible Side B Stems (Mute/Solo)
        float all[2] = { 0.0f, 0.0f };    // all four Side B Stems, for the Residual
        std::array<std::array<float, 2>, Session::numStems> stemAOut {}, stemOut {};

        if (pos >= 0 && pos < len)
        {
            const auto n = (size_t) pos;
            if (hasOrig)
            {
                a[0] = allA[0] = aL[n];
                a[1] = allA[1] = aR[n];
            }
            else if (hasStemsA)
            {
                for (size_t k = 0; k < (size_t) Session::numStems; ++k)
                {
                    const float l = sAL[k] != nullptr ? sAL[k][n] : 0.0f;
                    const float r = sAR[k] != nullptr ? sAR[k][n] : 0.0f;
                    allA[0] += l;
                    allA[1] += r;
                    stemAOut[k] = { gsA[k] * l, gsA[k] * r };
                    a[0] += stemAOut[k][0];
                    a[1] += stemAOut[k][1];
                }
            }

            if (stemsPresent)
            {
                for (size_t k = 0; k < (size_t) Session::numStems; ++k)
                {
                    const float l = sL[k] != nullptr ? sL[k][n] : 0.0f;
                    const float r = sR[k] != nullptr ? sR[k][n] : 0.0f;
                    all[0] += l;
                    all[1] += r;
                    stemOut[k] = { gs[k] * l, gs[k] * r };
                    b[0] += stemOut[k][0];
                    b[1] += stemOut[k][1];
                }
            }
        }

        // Without Stems the Residual is silent too (not A); while they fade in it follows gp.
        const float grp = gr * gp;

        float y[2];
        for (size_t c = 0; c < 2; ++c)
        {
            const float residual = (stemsPresent && (hasOrig || hasStemsA)) ? allA[c] - all[c] : 0.0f;
            y[c] = (ga * a[c] + gb * b[c] + grp * residual) * gm * env;

            const float ya = a[c] * env, yb = b[c] * env;
            blockPeak[tA][c] = juce::jmax (blockPeak[tA][c], std::abs (ya));
            blockPeak[tB][c] = juce::jmax (blockPeak[tB][c], std::abs (yb));
            blockSumSq[tA][c] += (double) ya * ya;
            blockSumSq[tB][c] += (double) yb * yb;

            for (size_t k = 0; k < (size_t) Session::numStems; ++k)
            {
                const float ys = stemOut[k][c] * env;
                blockPeak[tStem0 + k][c] = juce::jmax (blockPeak[tStem0 + k][c], std::abs (ys));
                blockSumSq[tStem0 + k][c] += (double) ys * ys;

                const float ysA = stemAOut[k][c] * env;
                blockPeak[tStemA0 + k][c] = juce::jmax (blockPeak[tStemA0 + k][c], std::abs (ysA));
                blockSumSq[tStemA0 + k][c] += (double) ysA * ysA;
            }
        }

        left[i] = y[0];
        right[i] = y[1];

        ++pos;
    }

    return true;
}

} // namespace stemab
