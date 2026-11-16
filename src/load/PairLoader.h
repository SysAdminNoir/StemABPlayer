#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include <juce_core/juce_core.h>

#include "analysis/Alignment.h"
#include "decode/Decode.h"
#include "engine/Session.h"
#include "load/AlignmentCache.h"

namespace stemab
{

/** The stages of loading a pair, in the order the listener can use them. */
enum class LoadStage { original, stems, alignment, trackNullDepth };

/** Loads an Original / Stem File pair so that it is usable as early as possible, in stages:

      1. decodeOriginal and decodeStemFile start at once, on two worker threads (the Stems themselves
         decode on four more, see decodeStemFile()).
      2. As soon as A is decoded: onOriginalReady with an Original-only Session - play it now. If the new
         Original is the very file (path, size, mtime) of the last one delivered, it is not decoded again:
         its buffer is reused (sameOriginal = true).
      3. When A and the Stems are both in: measure the Alignment Offset (or take it from the AlignmentCache),
         build the upgraded Session (Session::withStems(), sharing A) -> onStemsReady.
         On a cache hit the cached offset is delivered at once, then measured anyway; if the measurement
         differs, onAlignmentCorrected brings a corrected Session (and the cache is updated).
      4. Track Null Depth, off the critical path -> onTrackNullDepth.

    Every load() starts a new generation. Work for an older generation is cancelled (its decode and
    alignment loops poll a cancel flag) and its results are never delivered: every callback runs through
    `post` on the owner thread and is dropped there unless its generation is still the current one.

    Construct, call and destroy on the owner thread (the message thread in the app); `post` must run its
    tasks on that thread, in order. Callbacks are invoked only from tasks run by `post`. */
class PairLoader final
{
public:
    using Task = std::function<void()>;
    using Post = std::function<void (Task)>;

    /** Injectable for tests (synthetic decoders); defaults are the real FFmpeg decoders. */
    struct Backend
    {
        std::function<DecodedOriginal (const juce::File&, const DecodeOptions&)> decodeOriginal;
        std::function<DecodedStems (const juce::File&, const DecodeOptions&)> decodeStems;
        AlignmentOptions alignmentOptions;
    };

    static Backend defaultBackend();

    //==============================================================================
    struct OriginalReady
    {
        std::shared_ptr<const Session> session;   // Original only
        bool sameOriginal = false;                // reused the previous pair's A buffer (no decode)
        double secondsSinceLoad = 0.0;            // time to A playable
        double decodeSeconds = 0.0;               // 0 when reused
        juce::StringArray notes;
    };

    struct StemsReady
    {
        std::shared_ptr<const Session> session;   // with Stems; shares OriginalReady's A
        AlignmentResult alignment;                // when fromCache: offset/polarity/confidence from the cache, no windows
        bool fromCache = false;
        double secondsSinceLoad = 0.0;            // time to B ready
        double decodeSeconds = 0.0;               // decodeStemFile alone
        double alignmentSeconds = 0.0;            // 0 when fromCache
        juce::StringArray notes;
    };

    struct AlignmentCorrected
    {
        std::shared_ptr<const Session> session;   // the same Stems at the measured offset
        CachedAlignment cached;                   // what was delivered first
        AlignmentResult measured;
        double secondsSinceLoad = 0.0;
    };

    struct TrackNullDepth
    {
        double trackNullDepthDb = 0.0;
        double seconds = 0.0;
        double secondsSinceLoad = 0.0;
    };

    std::function<void (const OriginalReady&)> onOriginalReady;
    /** Progress only: the Stem File is decoded (alignment follows once A is in too). */
    std::function<void (double decodeSeconds)> onStemsDecoded;
    std::function<void (const StemsReady&)> onStemsReady;
    std::function<void (const AlignmentCorrected&)> onAlignmentCorrected;
    /** Also sent (with the same generation rule) after a correction, for the corrected offset. */
    std::function<void (const TrackNullDepth&)> onTrackNullDepth;
    std::function<void (LoadStage, const juce::String& message)> onFailed;

    //==============================================================================
    /** `cache` may be null (no caching); it must outlive the loader. */
    PairLoader (Post post, AlignmentCache* cache, Backend backend = defaultBackend());
    ~PairLoader();   // cancels and waits for the workers

    /** Starts loading a pair: cancels any load in progress (its results are never delivered). Returns the
        new generation. */
    int load (const juce::File& original, const juce::File& stemFile);

    /** Cancels the load in progress without starting another. */
    void cancel();

    /** cancel(), and forgets the last delivered Original (no reuse, its buffer is released here unless a
        Session still holds it): the state of a new loader. */
    void reset();

    int getGeneration() const noexcept      { return ownerGeneration; }

    /** Blocks until no worker job is running or queued (posted tasks may still be waiting), or the timeout
        passes. For tests and the probe. */
    bool waitUntilIdle (int timeoutMs);

private:
    struct Job;

    void startOriginal (std::shared_ptr<Job>);
    void startStems (std::shared_ptr<Job>);
    void maybeRunStemsStage (const std::shared_ptr<Job>&);
    void runStemsStage (const std::shared_ptr<Job>&);
    void deliver (int generation, Task task);
    void addJob (Task task);

    Post post;
    AlignmentCache* cache;
    Backend backend;

    std::atomic<int> generation { 0 };      // workers: cancel when it moves on
    int ownerGeneration = 0;                // owner thread: what may still be delivered
    std::atomic<int> runningJobs { 0 };

    // Owner thread: the last delivered Original, reused when the next pair has the same Original file.
    FileIdentity lastOriginalIdentity;
    std::shared_ptr<const Session> lastOriginalSession;

    struct Token { PairLoader* self; };
    std::shared_ptr<Token> token;           // owner thread: reset by the destructor
    const std::weak_ptr<Token> weakToken;   // never reassigned, so workers may copy it: dead loader -> dropped

    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("PairLoader").withNumberOfThreads (4) };

    JUCE_DECLARE_NON_COPYABLE (PairLoader)
};

} // namespace stemab
