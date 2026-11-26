#include "load/PairLoader.h"

#include <cmath>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <thread>

#include "analysis/NullDepth.h"

namespace stemab
{

namespace
{
    double nowMs() noexcept                         { return juce::Time::getMillisecondCounterHiRes(); }
    double secondsSince (double startMs) noexcept   { return (nowMs() - startMs) / 1000.0; }
    bool sameRate (double a, double b) noexcept     { return std::abs (a - b) < 0.5; }
    juce::String hz (double rate)                   { return juce::String (rate, 0) + " Hz"; }

    /** `measuredRate` is the sample rate the offset was measured at (the session rate). */
    CachedAlignment toCached (const AlignmentResult& r, double measuredRate)
    {
        return { r.offsetSamples, r.polarity, r.confidence, r.lowConfidence, measuredRate };
    }

    AlignmentResult fromCached (const CachedAlignment& c)
    {
        AlignmentResult r;
        r.ok = true;
        r.offsetSamples = c.offsetSamples;
        r.polarity = c.polarity;
        r.confidence = c.confidence;
        r.lowConfidence = c.lowConfidence;
        return r;
    }

    juce::AudioBuffer<float> sumStemsToStereo (const StemBuffers& stems)
    {
        int maxSamples = 0;
        for (const auto& s : stems)
            maxSamples = juce::jmax (maxSamples, s.getNumSamples());

        juce::AudioBuffer<float> sum (2, maxSamples);
        sum.clear();

        for (const auto& s : stems)
        {
            const int n = s.getNumSamples();
            const int chs = s.getNumChannels();
            if (n <= 0 || chs <= 0)
                continue;

            for (int ch = 0; ch < 2; ++ch)
                sum.addFrom (ch, 0, s, juce::jmin (ch, chs - 1), 0, n);
        }

        return sum;
    }
}

//==============================================================================
/** One load (one generation). Shared by its worker jobs; `mutex` guards the hand-over fields. */
struct PairLoader::Job
{
    int generation = 0;
    bool isStemsA = false;
    juce::File originalFile;
    StemSource stemSourceA;
    StemSource stemSource;
    FileIdentity originalId, stemId;
    std::optional<CachedAlignment> cached;
    double startMs = 0.0;
    CancelCheck cancel;

    std::mutex mutex;
    std::shared_ptr<const Session> original;      // Original only or Stem A only: decoded or reused
    std::shared_ptr<StemBuffers> stems;           // Side B as decoded
    double stemsRate = 0.0;
    double stemsDecodeSeconds = 0.0;
    juce::StringArray stemsNotes;
    bool stemsStageStarted = false;
};

//==============================================================================
PairLoader::Backend PairLoader::defaultBackend()
{
    Backend b;
    b.decodeOriginal = [] (const juce::File& f, const DecodeOptions& o) { return decodeOriginal (f, o); };
    b.decodeStems    = [] (const juce::File& f, const DecodeOptions& o) { return decodeStemFile (f, o); };
    b.decodeStemSet  = [] (const StemSetFiles& f, const DecodeOptions& o) { return decodeStemSet (f, o); };
    return b;
}

PairLoader::PairLoader (Post p, AlignmentCache* c, Backend b)
    : post (std::move (p)), cache (c), backend (std::move (b)), token (std::make_shared<Token> (Token { this })), weakToken (token)
{
    if (! backend.decodeOriginal || ! backend.decodeStems || ! backend.decodeStemSet)
    {
        const auto d = defaultBackend();
        if (! backend.decodeOriginal) backend.decodeOriginal = d.decodeOriginal;
        if (! backend.decodeStems)    backend.decodeStems = d.decodeStems;
        if (! backend.decodeStemSet)  backend.decodeStemSet = d.decodeStemSet;
    }
}

PairLoader::~PairLoader()
{
    token.reset();                       // queued deliveries are dropped
    generation.store (-1);               // running work stops at its next cancel check
    pool.removeAllJobs (true, 30000);
}

void PairLoader::cancel()
{
    generation.store (++ownerGeneration);
}

void PairLoader::reset()
{
    cancel();
    lastOriginalIdentity = {};
    lastOriginalSession = nullptr;
}

bool PairLoader::waitUntilIdle (int timeoutMs)
{
    const auto deadline = nowMs() + timeoutMs;

    while (runningJobs.load() > 0)
    {
        if (nowMs() > deadline)
            return false;

        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    return true;
}

void PairLoader::addJob (Task task)
{
    runningJobs.fetch_add (1);
    pool.addJob ([this, task = std::move (task)]
    {
        task();
        runningJobs.fetch_sub (1);
    });
}

void PairLoader::deliver (int gen, Task task)
{
    post ([weak = weakToken, gen, task = std::move (task)]
    {
        // Owner thread: a result from an older load (or for a destroyed loader) is never delivered.
        if (const auto t = weak.lock(); t != nullptr && t->self->ownerGeneration == gen)
            task();
    });
}

//==============================================================================
int PairLoader::load (const juce::File& originalFile, const StemSource& stems)
{
    const int gen = ++ownerGeneration;
    generation.store (gen);              // older jobs see this and stop

    auto job = std::make_shared<Job>();
    job->generation = gen;
    job->originalFile = originalFile;
    job->stemSource = stems;
    job->originalId = FileIdentity::of (originalFile);
    job->stemId = FileIdentity::of (stems);
    job->startMs = nowMs();
    job->cancel = [this, gen] { return generation.load (std::memory_order_relaxed) != gen; };

    if (cache != nullptr)
        job->cached = cache->find (job->originalId, job->stemId);

    if (lastOriginalSession != nullptr && job->originalId.isValid() && job->originalId == lastOriginalIdentity)
    {
        // Same Original file as the one already decoded: share its buffer, no decode.
        job->original = Session::originalOnlyOf (*lastOriginalSession);

        OriginalReady ev;
        ev.session = job->original;
        ev.isStemsA = job->original->hasStemsA();
        ev.sameOriginal = true;
        ev.secondsSinceLoad = secondsSince (job->startMs);
        ev.notes.add ("Original unchanged: reused without decoding");

        deliver (gen, [this, ev] { if (onOriginalReady) onOriginalReady (ev); });
    }
    else
    {
        startOriginal (job);
    }

    startStems (job);
    return gen;
}

int PairLoader::loadStemsPair (const StemSource& stemsA, const StemSource& stemsB)
{
    const int gen = ++ownerGeneration;
    generation.store (gen);

    auto job = std::make_shared<Job>();
    job->generation = gen;
    job->isStemsA = true;
    job->stemSourceA = stemsA;
    job->stemSource = stemsB;
    job->originalId = FileIdentity::of (stemsA);
    job->stemId = FileIdentity::of (stemsB);
    job->startMs = nowMs();
    job->cancel = [this, gen] { return generation.load (std::memory_order_relaxed) != gen; };

    if (cache != nullptr)
        job->cached = cache->find (job->originalId, job->stemId);

    if (lastOriginalSession != nullptr && job->originalId.isValid() && job->originalId == lastOriginalIdentity)
    {
        job->original = Session::originalOnlyOf (*lastOriginalSession);

        OriginalReady ev;
        ev.session = job->original;
        ev.isStemsA = true;
        ev.sameOriginal = true;
        ev.secondsSinceLoad = secondsSince (job->startMs);
        ev.notes.add ("Stem A unchanged: reused without decoding");

        deliver (gen, [this, ev] { if (onOriginalReady) onOriginalReady (ev); });
    }
    else
    {
        startOriginal (job);
    }

    startStems (job);
    return gen;
}

//==============================================================================
// Workers

void PairLoader::startOriginal (std::shared_ptr<Job> job)
{
    addJob ([this, job]
    {
        const double t = nowMs();
        const DecodeOptions options { job->cancel, true };
        std::shared_ptr<const Session> session;
        juce::StringArray notes;

        if (job->isStemsA)
        {
            const bool isSet = job->stemSourceA.isSet();
            auto decoded = isSet ? backend.decodeStemSet (job->stemSourceA.setFiles, options)
                                 : backend.decodeStems (job->stemSourceA.stemFile, options);

            if (isCancelled (job->cancel))
                return;

            if (! decoded.ok)
            {
                const juce::String what = isSet ? "Stem A files: " : "Stem A file: ";
                deliver (job->generation, [this, e = what + decoded.error] { if (onFailed) onFailed (LoadStage::original, e); });
                return;
            }

            auto bufPtr = std::make_shared<const StemBuffers> (std::move (decoded.stems));
            session = Session::createStemsAOnly (bufPtr, decoded.sampleRate);
            notes = std::move (decoded.notes);
        }
        else
        {
            auto decoded = backend.decodeOriginal (job->originalFile, options);

            if (isCancelled (job->cancel))
                return;

            if (! decoded.ok)
            {
                deliver (job->generation, [this, e = decoded.error] { if (onFailed) onFailed (LoadStage::original, "Original: " + e); });
                return;
            }

            session = Session::createOriginalOnly (std::move (decoded.audio), decoded.sampleRate);
            notes = std::move (decoded.notes);
        }

        if (session == nullptr)
        {
            deliver (job->generation, [this] { if (onFailed) onFailed (LoadStage::original, "Side A: invalid sample rate"); });
            return;
        }

        OriginalReady ev;
        ev.session = session;
        ev.isStemsA = job->isStemsA;
        ev.decodeSeconds = secondsSince (t);
        ev.secondsSinceLoad = secondsSince (job->startMs);
        ev.notes = notes;

        // Posted before the hand-over below, so onOriginalReady always precedes onStemsReady.
        deliver (job->generation, [this, ev, id = job->originalId]
        {
            lastOriginalIdentity = id;
            lastOriginalSession = ev.session;
            if (onOriginalReady) onOriginalReady (ev);
        });

        {
            const std::lock_guard lock (job->mutex);
            job->original = std::move (session);
        }

        maybeRunStemsStage (job);
    });
}

void PairLoader::startStems (std::shared_ptr<Job> job)
{
    addJob ([this, job]
    {
        const double t = nowMs();
        const bool isSet = job->stemSource.isSet();
        const DecodeOptions options { job->cancel, true };
        auto decoded = isSet ? backend.decodeStemSet (job->stemSource.setFiles, options)
                             : backend.decodeStems (job->stemSource.stemFile, options);

        if (isCancelled (job->cancel))
            return;

        if (! decoded.ok)
        {
            const juce::String what = isSet ? "Stem files: " : "Stem file: ";
            deliver (job->generation, [this, e = what + decoded.error] { if (onFailed) onFailed (LoadStage::stems, e); });
            return;
        }

        {
            const std::lock_guard lock (job->mutex);
            job->stems = std::make_shared<StemBuffers> (std::move (decoded.stems));
            job->stemsRate = decoded.sampleRate;
            job->stemsDecodeSeconds = secondsSince (t);
            job->stemsNotes = decoded.notes;
        }

        deliver (job->generation, [this, s = job->stemsDecodeSeconds] { if (onStemsDecoded) onStemsDecoded (s); });
        maybeRunStemsStage (job);
    });
}

void PairLoader::maybeRunStemsStage (const std::shared_ptr<Job>& job)
{
    bool go = false;

    {
        const std::lock_guard lock (job->mutex);
        go = job->original != nullptr && job->stems != nullptr && ! job->stemsStageStarted;
        job->stemsStageStarted = job->stemsStageStarted || go;
    }

    // Runs on whichever worker delivered the second half (A or the Stems).
    if (go)
        runStemsStage (job);
}

void PairLoader::runStemsStage (const std::shared_ptr<Job>& job, std::optional<RateMismatchChoice> forcedRateChoice)
{
    const int gen = job->generation;
    const auto& cancel = job->cancel;
    auto base = job->original;
    auto stems = job->stems;
    juce::StringArray notes = job->stemsNotes;

    if (isCancelled (cancel))
        return;

    double rate = base->sampleRate();
    const double nativeRateA = rate;    // before any resampleA swap; legacy cache entries were measured at this rate

    if (! sameRate (job->stemsRate, rate))
    {
        if (! forcedRateChoice.has_value() && onRateMismatch)
        {
            const double rateA = rate;
            const double rateB = job->stemsRate;
            deliver (gen, [this, job, gen, rateA, rateB]
            {
                if (! onRateMismatch)
                {
                    addJob ([this, job] { runStemsStage (job, RateMismatchChoice::resampleB); });
                    return;
                }

                onRateMismatch (rateA, rateB, [this, job, gen] (RateMismatchChoice choice)
                {
                    if (ownerGeneration != gen)
                        return;

                    if (choice == RateMismatchChoice::cancel)
                    {
                        this->cancel();
                        if (onFailed)
                            onFailed (LoadStage::stems, "Cancelled (sample rate mismatch).");
                        return;
                    }

                    addJob ([this, job, choice] { runStemsStage (job, choice); });
                });
            });
            return;
        }

        const auto choice = forcedRateChoice.value_or (RateMismatchChoice::resampleB);

        if (choice == RateMismatchChoice::resampleA)
        {
            const double targetRate = job->stemsRate;

            if (base->hasOriginal())
            {
                auto resampledOrig = resample (base->original(), rate, targetRate);
                if (isCancelled (cancel))
                    return;
                base = Session::createOriginalOnly (std::move (resampledOrig), targetRate);
            }
            else if (base->hasStemsA())
            {
                StemBuffers resampledA;
                const auto& srcA = *base->decodedStemsA();
                for (size_t k = 0; k < (size_t) Session::numStems; ++k)
                {
                    resampledA[k] = resample (srcA[k], rate, targetRate);
                    if (isCancelled (cancel))
                        return;
                }
                base = Session::createStemsAOnly (std::make_shared<const StemBuffers> (std::move (resampledA)), targetRate);
            }

            {
                const std::lock_guard lock (job->mutex);
                job->original = base;
            }

            notes.add ("Side A resampled " + hz (rate) + " -> " + hz (targetRate) + " to match Side B");
            rate = targetRate;

            OriginalReady origEv;
            origEv.session = base;
            origEv.isStemsA = job->isStemsA;
            origEv.secondsSinceLoad = secondsSince (job->startMs);
            origEv.notes = notes;
            deliver (gen, [this, origEv]
            {
                lastOriginalSession = origEv.session;
                if (onOriginalReady) onOriginalReady (origEv);
            });
        }
        else
        {
            for (auto& s : *stems)
            {
                s = resample (s, job->stemsRate, rate);
                if (isCancelled (cancel))
                    return;
            }

            notes.add ("Side B resampled " + hz (job->stemsRate) + " -> " + hz (rate) + " to match Side A");
        }
    }

    const std::shared_ptr<const StemBuffers> constStems = stems;
    const juce::AudioBuffer<float> summedA = base->hasOriginal()
                                                 ? juce::AudioBuffer<float>()
                                                 : sumStemsToStereo (*base->decodedStemsA());
    const juce::AudioBuffer<float>& original = base->hasOriginal() ? base->original() : summedA;

    const auto deliverStems = [&] (const AlignmentResult& al, bool fromCache, double alignSeconds)
    {
        StemsReady ev;
        ev.session = Session::withStems (*base, constStems, al.offsetSamples, al.polarity);
        ev.isStemVsStem = job->isStemsA;
        ev.alignment = al;
        ev.fromCache = fromCache;
        ev.decodeSeconds = job->stemsDecodeSeconds;
        ev.alignmentSeconds = alignSeconds;
        ev.secondsSinceLoad = secondsSince (job->startMs);
        ev.notes = notes;
        deliver (gen, [this, ev] { if (onStemsReady) onStemsReady (ev); });
    };

    AlignmentResult finalAlignment;

    if (job->cached.has_value())
    {
        // Cache hit: B is ready right now with the cached offset; measure anyway and correct if it moved.
        // A cached offset is in samples at the rate it was measured at; bring it to the session rate.
        auto cachedAtRate = *job->cached;
        const double cachedRate = cachedAtRate.sampleRate > 0.0 ? cachedAtRate.sampleRate : nativeRateA;
        const bool converted = ! sameRate (cachedRate, rate);

        if (converted)
            cachedAtRate.offsetSamples = (juce::int64) std::llround ((double) cachedAtRate.offsetSamples * rate / cachedRate);

        cachedAtRate.sampleRate = rate;

        finalAlignment = fromCached (cachedAtRate);
        deliverStems (finalAlignment, true, 0.0);

        const auto measured = measureAlignment (original, *stems, rate, backend.alignmentOptions, cancel);

        if (isCancelled (cancel))
            return;

        if (measured.ok)
        {
            // Converting between rates rounds, so allow a sample of slack then (never otherwise).
            const bool same = converted ? (measured.polarity == cachedAtRate.polarity
                                           && std::abs (measured.offsetSamples - cachedAtRate.offsetSamples) <= 1)
                                        : toCached (measured, rate).sameAlignment (cachedAtRate);

            if (! same)
            {
                AlignmentCorrected ev;
                ev.session = Session::withStems (*base, constStems, measured.offsetSamples, measured.polarity);
                ev.cached = cachedAtRate;
                ev.measured = measured;
                ev.secondsSinceLoad = secondsSince (job->startMs);
                deliver (gen, [this, ev] { if (onAlignmentCorrected) onAlignmentCorrected (ev); });
                finalAlignment = measured;
            }

            if (cache != nullptr)
                cache->store (job->originalId, job->stemId, toCached (measured, rate));
        }
    }
    else
    {
        const double t = nowMs();
        finalAlignment = measureAlignment (original, *stems, rate, backend.alignmentOptions, cancel);

        if (isCancelled (cancel))
            return;

        if (! finalAlignment.ok)
        {
            deliver (gen, [this, e = finalAlignment.error] { if (onFailed) onFailed (LoadStage::alignment, "Alignment failed: " + e); });
            return;
        }

        deliverStems (finalAlignment, false, secondsSince (t));

        if (cache != nullptr)
            cache->store (job->originalId, job->stemId, toCached (finalAlignment, rate));
    }

    // Off the critical path: B is already playing.
    const double t = nowMs();
    const auto nd = computeNullDepth (original, *stems, finalAlignment.offsetSamples, finalAlignment.polarity, rate, cancel);

    if (isCancelled (cancel))
        return;

    TrackNullDepth ev;
    ev.trackNullDepthDb = nd.trackNullDepthDb;
    ev.seconds = secondsSince (t);
    ev.secondsSinceLoad = secondsSince (job->startMs);
    deliver (gen, [this, ev] { if (onTrackNullDepth) onTrackNullDepth (ev); });
}

} // namespace stemab
