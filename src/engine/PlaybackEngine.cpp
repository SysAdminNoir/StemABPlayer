#include "engine/PlaybackEngine.h"

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

    inline bool isPlayable (const Session* s, double deviceRate) noexcept
    {
        return s != nullptr && s->length() > 0 && std::abs (s->sampleRate() - deviceRate) < 0.5;
    }
}

//==============================================================================
PlaybackEngine::PlaybackEngine()
{
    prepare (44100.0, 512);
}

PlaybackEngine::~PlaybackEngine() = default;   // keepAlive releases every session (the device must be stopped)

//==============================================================================
void PlaybackEngine::prepare (double deviceSampleRate, int /*maximumBlockSize*/) noexcept
{
    deviceRate = deviceSampleRate > 0.0 ? deviceSampleRate : 44100.0;

    const int fadeSamples = juce::jmax (1, (int) std::lround (transportFadeSeconds * deviceRate));
    fadeLen = fadeSamples;
    fadePos = juce::jmin (fadePos, fadeLen);
    fadeLenPublished.store (fadeLen, std::memory_order_relaxed);

    // reset() snaps each ramp to its target, so the first block starts settled.
    updateGainTargets();
    for (auto* g : { &gainA, &gainB, &gainR, &gainMaster })
        g->reset (deviceRate, gainRampSeconds);
    for (auto& g : gainStem)
        g.reset (deviceRate, gainRampSeconds);

    rateMismatch.store (current != nullptr && current->length() > 0 && ! isPlayable (current, deviceRate),
                        std::memory_order_relaxed);
}

//==============================================================================
// UI thread

void PlaybackEngine::setSession (std::shared_ptr<const Session> session)
{
    releaseRetiredSessions();

    if (session == nullptr)
        session = emptySession();

    keepAlive.push_back (session);

    // If the audio thread had not yet taken the previous pending session, it never will: release it here.
    if (const Session* never = pendingSession.exchange (session.get(), std::memory_order_acq_rel))
    {
        auto it = std::find_if (keepAlive.begin(), keepAlive.end(), [never] (const auto& p) { return p.get() == never; });
        if (it != keepAlive.end())
            keepAlive.erase (it);
    }
}

void PlaybackEngine::releaseRetiredSessions()
{
    const int ready = retiredFifo.getNumReady();
    if (ready <= 0)
        return;

    retiredFifo.read (ready).forEach ([this] (int index)
    {
        const Session* done = retiredSlots[(size_t) index];
        auto it = std::find_if (keepAlive.begin(), keepAlive.end(), [done] (const auto& p) { return p.get() == done; });
        if (it != keepAlive.end())
            keepAlive.erase (it);      // the last owner frees it here, on the UI thread
    });
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

    pos = 0;
    running = false;
    fadePos = 0;
    fadeDir = 0;
    pendingJump = -1;
    pendingPause = false;
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
        if (const Session* p = pendingSession.exchange (nullptr, std::memory_order_acq_rel))
        {
            if (swapPending)
                retire (incoming);

            incoming = p;
            swapPending = true;
            pendingJump = -1;
            pendingPause = false;

            if (running && fadePos > 0 && isPlayable (current, deviceRate))
                beginFadeOut();          // swapped when the fade-out completes
            else
                swapInIncomingSession();
        }
    }

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

    const bool playable = isPlayable (current, deviceRate);

    // 3. Play / Pause (last command wins).
    switch (transportCommand.exchange (0, std::memory_order_acq_rel))
    {
        case 1:   // play
            if (swapPending || ! playable)
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
            if (running && ! swapPending)
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

    gainMaster.setTargetValue (masterGainLinear.load (std::memory_order_relaxed));
}

void PlaybackEngine::publish (int numSamples) noexcept
{
    playhead.store (pos, std::memory_order_relaxed);
    playingFlag.store (running && ! pendingPause && ! swapPending, std::memory_order_relaxed);

    const bool hasSession = current != nullptr && current->length() > 0;
    sessionLength.store (hasSession ? current->length() : 0, std::memory_order_relaxed);
    sessionRate.store (hasSession ? current->sampleRate() : 0.0, std::memory_order_relaxed);
    rateMismatch.store (hasSession && ! isPlayable (current, deviceRate), std::memory_order_relaxed);

    const double invN = numSamples > 0 ? 1.0 / (double) numSamples : 0.0;

    for (size_t t = 0; t < (size_t) numMeterTaps; ++t)
        for (size_t c = 0; c < 2; ++c)
        {
            accumulatePeak (meterPeak[t][c], blockPeak[t][c]);
            meterRms[t][c].store ((float) std::sqrt (blockSumSq[t][c] * invN), std::memory_order_relaxed);
        }

    if (blockClipped)
        clipFlag.store (true, std::memory_order_relaxed);
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

    takeCommands();
    updateGainTargets();

    // A session whose rate is not the device's is never played (it would be at the wrong speed/pitch).
    if (running && ! isPlayable (current, deviceRate))
    {
        running = false;
        fadePos = 0;
        fadeDir = 0;
        pendingJump = -1;
        pendingPause = false;
    }

    if (! running)
    {
        for (auto* g : { &gainA, &gainB, &gainR, &gainMaster })
            g->skip (numSamples);
        for (auto& g : gainStem)
            g.skip (numSamples);

        publish (numSamples);
        return;
    }

    const Session* s = current;
    const juce::int64 len = s->length();
    const float* aL = s->originalChannel (0);
    const float* aR = s->originalChannel (1);
    std::array<const float*, Session::numStems> sL {}, sR {};
    for (int k = 0; k < Session::numStems; ++k)
    {
        sL[(size_t) k] = s->stemChannel (k, 0);
        sR[(size_t) k] = s->stemChannel (k, 1);
    }

    constexpr size_t tA = (size_t) MeterTap::Original, tB = (size_t) MeterTap::StemSum,
                     tOut = (size_t) MeterTap::Output, tStem0 = (size_t) MeterTap::Drums;

    for (int i = 0; i < numSamples; ++i)
    {
        const float ga = gainA.getNextValue();
        const float gb = gainB.getNextValue();
        const float gr = gainR.getNextValue();
        const float gm = gainMaster.getNextValue();
        std::array<float, Session::numStems> gs {};
        for (size_t k = 0; k < gs.size(); ++k)
            gs[k] = gainStem[k].getNextValue();

        if (! running)
            continue;   // paused (or swapped) earlier in this block: the rest stays silent

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
        float a[2] = { 0.0f, 0.0f };
        float b[2] = { 0.0f, 0.0f };      // audible Stems (Mute/Solo)
        float all[2] = { 0.0f, 0.0f };    // all four Stems, for the Residual
        std::array<std::array<float, 2>, Session::numStems> stemOut {};

        if (pos >= 0 && pos < len)
        {
            const auto n = (size_t) pos;
            a[0] = aL[n];
            a[1] = aR[n];

            for (size_t k = 0; k < (size_t) Session::numStems; ++k)
            {
                const float l = sL[k][n], r = sR[k][n];
                all[0] += l;
                all[1] += r;
                stemOut[k] = { gs[k] * l, gs[k] * r };
                b[0] += stemOut[k][0];
                b[1] += stemOut[k][1];
            }
        }

        float y[2];
        for (size_t c = 0; c < 2; ++c)
        {
            const float residual = a[c] - all[c];
            y[c] = (ga * a[c] + gb * b[c] + gr * residual) * gm * env;

            const float ya = a[c] * env, yb = b[c] * env;
            blockPeak[tA][c] = juce::jmax (blockPeak[tA][c], std::abs (ya));
            blockPeak[tB][c] = juce::jmax (blockPeak[tB][c], std::abs (yb));
            blockPeak[tOut][c] = juce::jmax (blockPeak[tOut][c], std::abs (y[c]));
            blockSumSq[tA][c] += (double) ya * ya;
            blockSumSq[tB][c] += (double) yb * yb;
            blockSumSq[tOut][c] += (double) y[c] * y[c];

            for (size_t k = 0; k < (size_t) Session::numStems; ++k)
            {
                const float ys = stemOut[k][c] * env;
                blockPeak[tStem0 + k][c] = juce::jmax (blockPeak[tStem0 + k][c], std::abs (ys));
                blockSumSq[tStem0 + k][c] += (double) ys * ys;
            }
        }

        if (std::abs (y[0]) > 1.0f || std::abs (y[1]) > 1.0f)
            blockClipped = true;              // indicated, never hidden: the samples are written as they are

        if (monoOut)
        {
            if (outL != nullptr) outL[i] = 0.5f * (y[0] + y[1]);
        }
        else
        {
            if (outL != nullptr) outL[i] = y[0];
            if (outR != nullptr) outR[i] = y[1];
        }

        ++pos;
    }

    publish (numSamples);
}

} // namespace stemab
