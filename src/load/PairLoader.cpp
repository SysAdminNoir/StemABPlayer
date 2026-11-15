#include "load/PairLoader.h"

#include <cmath>
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

    CachedAlignment toCached (const AlignmentResult& r)
    {
        return { r.offsetSamples, r.polarity, r.confidence, r.lowConfidence };
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
}

//==============================================================================
/** One load (one generation). Shared by its worker jobs; `mutex` guards the hand-over fields. */
struct PairLoader::Job
{
    int generation = 0;
    juce::File originalFile, stemFile;
    FileIdentity originalId, stemId;
    std::optional<CachedAlignment> cached;
    double startMs = 0.0;
    CancelCheck cancel;

    std::mutex mutex;
    std::shared_ptr<const Session> original;      // Original only: decoded or reused
    std::shared_ptr<StemBuffers> stems;           // as decoded
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
    return b;
}

PairLoader::PairLoader (Post p, AlignmentCache* c, Backend b)
    : post (std::move (p)), cache (c), backend (std::move (b)), token (std::make_shared<Token> (Token { this })), weakToken (token)
{
    if (! backend.decodeOriginal || ! backend.decodeStems)
    {
        const auto d = defaultBackend();
        if (! backend.decodeOriginal) backend.decodeOriginal = d.decodeOriginal;
        if (! backend.decodeStems)    backend.decodeStems = d.decodeStems;
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
int PairLoader::load (const juce::File& originalFile, const juce::File& stemFile)
{
    const int gen = ++ownerGeneration;
    generation.store (gen);              // older jobs see this and stop

    auto job = std::make_shared<Job>();
    job->generation = gen;
    job->originalFile = originalFile;
    job->stemFile = stemFile;
    job->originalId = FileIdentity::of (originalFile);
    job->stemId = FileIdentity::of (stemFile);
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

//==============================================================================
// Workers

void PairLoader::startOriginal (std::shared_ptr<Job> job)
{
    addJob ([this, job]
    {
        const double t = nowMs();
        auto decoded = backend.decodeOriginal (job->originalFile, DecodeOptions { job->cancel, true });

        if (isCancelled (job->cancel))
            return;

        if (! decoded.ok)
        {
            deliver (job->generation, [this, e = decoded.error] { if (onFailed) onFailed (LoadStage::original, "Original: " + e); });
            return;
        }

        auto session = Session::createOriginalOnly (std::move (decoded.audio), decoded.sampleRate);

        if (session == nullptr)
        {
            deliver (job->generation, [this] { if (onFailed) onFailed (LoadStage::original, "Original: invalid sample rate"); });
            return;
        }

        OriginalReady ev;
        ev.session = session;
        ev.decodeSeconds = secondsSince (t);
        ev.secondsSinceLoad = secondsSince (job->startMs);
        ev.notes = decoded.notes;

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
        auto decoded = backend.decodeStems (job->stemFile, DecodeOptions { job->cancel, true });

        if (isCancelled (job->cancel))
            return;

        if (! decoded.ok)
        {
            deliver (job->generation, [this, e = decoded.error] { if (onFailed) onFailed (LoadStage::stems, "Stem file: " + e); });
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

void PairLoader::runStemsStage (const std::shared_ptr<Job>& job)
{
    const int gen = job->generation;
    const auto& cancel = job->cancel;
    const auto base = job->original;
    auto stems = job->stems;
    juce::StringArray notes = job->stemsNotes;

    if (isCancelled (cancel))
        return;

    const double rate = base->sampleRate();

    if (! sameRate (job->stemsRate, rate))
    {
        for (auto& s : *stems)
        {
            s = resample (s, job->stemsRate, rate);
            if (isCancelled (cancel))
                return;
        }

        notes.add ("Stems resampled " + hz (job->stemsRate) + " -> " + hz (rate) + " to match the Original");
    }

    const std::shared_ptr<const StemBuffers> constStems = stems;
    const auto& original = base->original();

    const auto deliverStems = [&] (const AlignmentResult& al, bool fromCache, double alignSeconds)
    {
        StemsReady ev;
        ev.session = Session::withStems (*base, constStems, al.offsetSamples, al.polarity);
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
        finalAlignment = fromCached (*job->cached);
        deliverStems (finalAlignment, true, 0.0);

        const auto measured = measureAlignment (original, *stems, rate, backend.alignmentOptions, cancel);

        if (isCancelled (cancel))
            return;

        if (measured.ok)
        {
            if (! toCached (measured).sameAlignment (*job->cached))
            {
                AlignmentCorrected ev;
                ev.session = Session::withStems (*base, constStems, measured.offsetSamples, measured.polarity);
                ev.cached = *job->cached;
                ev.measured = measured;
                ev.secondsSinceLoad = secondsSince (job->startMs);
                deliver (gen, [this, ev] { if (onAlignmentCorrected) onAlignmentCorrected (ev); });
                finalAlignment = measured;
            }

            if (cache != nullptr)
                cache->store (job->originalId, job->stemId, toCached (measured));
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
            cache->store (job->originalId, job->stemId, toCached (finalAlignment));
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
