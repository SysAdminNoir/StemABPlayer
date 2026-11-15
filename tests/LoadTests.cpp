#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "load/AlignmentCache.h"
#include "load/PairLoader.h"

using namespace stemab;

namespace
{
constexpr double fs = 44100.0;

/** A fresh folder in the temp directory, deleted at the end of the test. */
struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("StemAB_LoadTests_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));

    TempDir()   { dir.createDirectory(); }
    ~TempDir()  { dir.deleteRecursively(); }

    juce::File file (const juce::String& name, const juce::String& content = "x") const
    {
        auto f = dir.getChildFile (name);
        f.replaceWithText (content);
        return f;
    }
};

/** The owner thread's message queue, played by the test thread. */
class TaskQueue
{
public:
    PairLoader::Post poster()
    {
        return [this] (PairLoader::Task t)
        {
            const std::lock_guard lock (mutex);
            tasks.push_back (std::move (t));
        };
    }

    /** Runs every queued task (and any they queue). */
    int runAll()
    {
        int n = 0;
        for (;;)
        {
            PairLoader::Task t;
            {
                const std::lock_guard lock (mutex);
                if (tasks.empty())
                    return n;
                t = std::move (tasks.front());
                tasks.pop_front();
            }
            t();
            ++n;
        }
    }

private:
    std::mutex mutex;
    std::deque<PairLoader::Task> tasks;
};

/** Deterministic white noise, stereo. */
juce::AudioBuffer<float> noise (int length, int seed)
{
    juce::Random random (seed);
    juce::AudioBuffer<float> b (2, length);
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < length; ++n)
            b.setSample (ch, n, 0.3f * (2.0f * random.nextFloat() - 1.0f));
    return b;
}

/** Four Stems summing exactly to `a` (power-of-two weights), delayed by `delay`, 7168 samples longer. */
StemBuffers splitAndDelay (const juce::AudioBuffer<float>& a, int delay)
{
    const float weights[] = { 0.5f, 0.25f, 0.125f, 0.125f };
    const int lenB = a.getNumSamples() + 7168;
    StemBuffers stems;
    for (size_t s = 0; s < 4; ++s)
    {
        stems[s].setSize (2, lenB);
        stems[s].clear();
        for (int ch = 0; ch < 2; ++ch)
            for (int m = 0; m < lenB; ++m)
                if (const int src = m - delay; src >= 0 && src < a.getNumSamples())
                    stems[s].setSample (ch, m, weights[s] * a.getSample (ch, src));
    }
    return stems;
}

/** What a synthetic "file" decodes to: an Original of `seconds` from `seed`, Stems delayed by `delay`. */
struct FakePair { int seed; double seconds; int delay; };

/** Synthetic decoders keyed by file name. A decoder can be held at a gate (until released or cancelled). */
struct FakeBackend
{
    std::map<juce::String, FakePair> pairs;      // by Original or Stem File name
    std::atomic<bool> holdOriginal { false }, holdStems { false };
    std::atomic<int> cancelsSeen { 0 }, originalDecodes { 0 };

    bool wait (const std::atomic<bool>& hold, const DecodeOptions& o)
    {
        while (hold.load())
        {
            if (isCancelled (o.cancel))
            {
                cancelsSeen.fetch_add (1);
                return false;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        return ! isCancelled (o.cancel);
    }

    PairLoader::Backend backend()
    {
        PairLoader::Backend b;
        b.decodeOriginal = [this] (const juce::File& f, const DecodeOptions& o)
        {
            DecodedOriginal r;
            originalDecodes.fetch_add (1);
            if (! wait (holdOriginal, o)) { r.error = "Cancelled."; return r; }
            const auto& p = pairs.at (f.getFileName());
            r.audio = noise ((int) (p.seconds * fs), p.seed);
            r.sampleRate = fs;
            r.ok = true;
            return r;
        };
        b.decodeStems = [this] (const juce::File& f, const DecodeOptions& o)
        {
            DecodedStems r;
            if (! wait (holdStems, o)) { r.error = "Cancelled."; return r; }
            const auto& p = pairs.at (f.getFileName());
            r.stems = splitAndDelay (noise ((int) (p.seconds * fs), p.seed), p.delay);
            r.sampleRate = fs;
            r.ok = true;
            return r;
        };
        return b;
    }
};

/** Everything a PairLoader delivered, in order. */
struct Events
{
    std::vector<std::string> order;
    std::shared_ptr<const Session> original, upgraded, corrected;
    PairLoader::OriginalReady originalEvent;
    PairLoader::StemsReady stemsEvent;
    double trackNullDepthDb = 0.0;
    juce::String error;

    void attach (PairLoader& loader)
    {
        loader.onOriginalReady = [this] (const auto& ev) { order.push_back ("A"); original = ev.session; originalEvent = ev; };
        loader.onStemsDecoded = [this] (double) { order.push_back ("decoded"); };
        loader.onStemsReady = [this] (const auto& ev) { order.push_back ("B"); upgraded = ev.session; stemsEvent = ev; };
        loader.onAlignmentCorrected = [this] (const auto& ev) { order.push_back ("corrected"); corrected = ev.session; };
        loader.onTrackNullDepth = [this] (const auto& ev) { order.push_back ("null"); trackNullDepthDb = ev.trackNullDepthDb; };
        loader.onFailed = [this] (LoadStage, const juce::String& m) { order.push_back ("failed"); error = m; };
    }

    int count (const std::string& what) const   { return (int) std::count (order.begin(), order.end(), what); }

    int indexOf (const std::string& what) const
    {
        const auto it = std::find (order.begin(), order.end(), what);
        return it == order.end() ? -1 : (int) (it - order.begin());
    }
};

void finish (PairLoader& loader, TaskQueue& queue)
{
    REQUIRE (loader.waitUntilIdle (30000));
    queue.runAll();
}
} // namespace

//==============================================================================
TEST_CASE ("AlignmentCache: hit, miss, invalidation by size or modification time, persistence", "[load][cache]")
{
    TempDir tmp;
    const auto original = tmp.file ("a.flac", "original");
    const auto stems = tmp.file ("b.stem.m4a", "stems");
    const auto json = tmp.dir.getChildFile ("sub").getChildFile ("alignment-cache.json");
    const CachedAlignment measured { 6522, +1, 0.9999, false };

    {
        AlignmentCache cache (json);
        CHECK_FALSE (cache.find (FileIdentity::of (original), FileIdentity::of (stems)).has_value());   // miss
        cache.store (FileIdentity::of (original), FileIdentity::of (stems), measured);
        CHECK (json.existsAsFile());

        const auto hit = cache.find (FileIdentity::of (original), FileIdentity::of (stems));
        REQUIRE (hit.has_value());
        CHECK (hit->offsetSamples == 6522);
        CHECK (hit->polarity == 1);
        CHECK (hit->confidence == 0.9999);
        CHECK_FALSE (hit->lowConfidence);

        // Swapped roles are another pair.
        CHECK_FALSE (cache.find (FileIdentity::of (stems), FileIdentity::of (original)).has_value());
    }

    SECTION ("persists across instances (a JSON file in the app data folder)")
    {
        AlignmentCache reopened (json);
        CHECK (reopened.find (FileIdentity::of (original), FileIdentity::of (stems)).has_value());
        CHECK (AlignmentCache::defaultFile().getFullPathName().contains ("StemABPlayer"));
    }

    SECTION ("a changed size invalidates")
    {
        stems.appendText ("more");
        AlignmentCache reopened (json);
        CHECK_FALSE (reopened.find (FileIdentity::of (original), FileIdentity::of (stems)).has_value());

        // Storing the new identity replaces the stale entry for the same paths.
        reopened.store (FileIdentity::of (original), FileIdentity::of (stems), { 100, -1, 0.5, true });
        CHECK (reopened.size() == 1);
        const auto hit = reopened.find (FileIdentity::of (original), FileIdentity::of (stems));
        REQUIRE (hit.has_value());
        CHECK (hit->offsetSamples == 100);
        CHECK (hit->polarity == -1);
        CHECK (hit->lowConfidence);
    }

    SECTION ("a changed modification time invalidates")
    {
        REQUIRE (original.setLastModificationTime (original.getLastModificationTime() + juce::RelativeTime::seconds (10.0)));
        AlignmentCache reopened (json);
        CHECK_FALSE (reopened.find (FileIdentity::of (original), FileIdentity::of (stems)).has_value());
    }

    SECTION ("a missing file is a miss; a corrupt cache file is an empty cache")
    {
        AlignmentCache reopened (json);
        CHECK_FALSE (reopened.find (FileIdentity::of (tmp.dir.getChildFile ("gone.flac")), FileIdentity::of (stems)).has_value());

        json.replaceWithText ("{ this is not json");
        AlignmentCache corrupt (json);
        CHECK (corrupt.size() == 0);
        CHECK_FALSE (corrupt.find (FileIdentity::of (original), FileIdentity::of (stems)).has_value());
    }
}

//==============================================================================
TEST_CASE ("PairLoader: Original first, then the upgraded Session, then Track Null Depth", "[load][loader]")
{
    TempDir tmp;
    FakeBackend fake;
    fake.pairs["a1.flac"] = fake.pairs["b1.stem.m4a"] = { 1, 5.0, 2112 };
    TaskQueue queue;
    PairLoader loader (queue.poster(), nullptr, fake.backend());
    Events ev;
    ev.attach (loader);

    // Hold the Stems so the Original is certainly first, as it is on real files.
    fake.holdStems = true;
    loader.load (tmp.file ("a1.flac"), tmp.file ("b1.stem.m4a"));

    for (int i = 0; i < 5000 && ev.original == nullptr; ++i)
    {
        queue.runAll();
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    REQUIRE (ev.original != nullptr);
    CHECK_FALSE (ev.original->hasStems());
    CHECK (ev.original->length() == (juce::int64) (5.0 * fs));
    CHECK (ev.upgraded == nullptr);

    fake.holdStems = false;
    finish (loader, queue);

    CHECK (ev.error.isEmpty());
    REQUIRE (ev.upgraded != nullptr);
    CHECK (ev.indexOf ("A") < ev.indexOf ("B"));
    CHECK (ev.indexOf ("B") < ev.indexOf ("null"));
    CHECK (ev.upgraded->hasStems());
    CHECK (ev.upgraded->sharesOriginalWith (*ev.original));    // the same A buffer: an in-place upgrade
    CHECK (ev.upgraded->alignmentOffset() == 2112);
    CHECK (ev.stemsEvent.alignment.offsetSamples == 2112);
    CHECK_FALSE (ev.stemsEvent.alignment.lowConfidence);
    CHECK_FALSE (ev.stemsEvent.fromCache);
    CHECK (ev.trackNullDepthDb >= 150.0);                        // an exact split nulls completely
    CHECK (ev.stemsEvent.secondsSinceLoad >= ev.originalEvent.secondsSinceLoad);
}

TEST_CASE ("PairLoader: stale load results are never delivered", "[load][loader][stale]")
{
    TempDir tmp;
    FakeBackend fake;
    fake.pairs["a1.flac"] = fake.pairs["b1.stem.m4a"] = { 1, 5.0, 2112 };
    fake.pairs["a2.flac"] = fake.pairs["b2.stem.m4a"] = { 2, 4.0, 500 };
    const auto a1 = tmp.file ("a1.flac"), b1 = tmp.file ("b1.stem.m4a");
    const auto a2 = tmp.file ("a2.flac"), b2 = tmp.file ("b2.stem.m4a");

    TaskQueue queue;
    PairLoader loader (queue.poster(), nullptr, fake.backend());
    Events ev;
    ev.attach (loader);

    SECTION ("the first load finished but was not delivered yet when the second started")
    {
        loader.load (a1, b1);
        REQUIRE (loader.waitUntilIdle (30000));   // all of load 1's results are posted, none run yet
        const int second = loader.load (a2, b2);
        CHECK (second == 2);
        finish (loader, queue);

        CHECK (ev.count ("A") == 1);
        CHECK (ev.count ("B") == 1);
        CHECK (ev.count ("null") == 1);
        CHECK (ev.count ("failed") == 0);
        REQUIRE (ev.original != nullptr);
        CHECK (ev.original->length() == (juce::int64) (4.0 * fs));   // pair 2 only
        CHECK (ev.upgraded->alignmentOffset() == 500);
    }

    SECTION ("the first load is cancelled while decoding")
    {
        fake.holdOriginal = true;
        fake.holdStems = true;
        loader.load (a1, b1);
        std::this_thread::sleep_for (std::chrono::milliseconds (20));   // both decoders are waiting at the gate

        // Release the gate only for the second load: the first one's decoders see the cancel and give up.
        loader.load (a2, b2);
        for (int i = 0; i < 5000 && fake.cancelsSeen.load() < 2; ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        CHECK (fake.cancelsSeen.load() == 2);

        fake.holdOriginal = false;
        fake.holdStems = false;
        finish (loader, queue);

        CHECK (ev.count ("A") == 1);
        CHECK (ev.count ("B") == 1);
        CHECK (ev.count ("failed") == 0);   // a cancelled load does not even report a failure
        REQUIRE (ev.original != nullptr);
        CHECK (ev.original->length() == (juce::int64) (4.0 * fs));
        CHECK (ev.upgraded->alignmentOffset() == 500);
    }

    SECTION ("cancel() drops everything in flight")
    {
        loader.load (a1, b1);
        loader.cancel();
        finish (loader, queue);
        CHECK (ev.order.empty());
    }
}

TEST_CASE ("PairLoader: the same Original is reused, not decoded again", "[load][loader]")
{
    TempDir tmp;
    FakeBackend fake;
    fake.pairs["a1.flac"] = fake.pairs["b1.stem.m4a"] = { 1, 5.0, 2112 };
    fake.pairs["b1-other.stem.m4a"] = { 1, 5.0, 700 };
    const auto a1 = tmp.file ("a1.flac");

    TaskQueue queue;
    PairLoader loader (queue.poster(), nullptr, fake.backend());
    Events first;
    first.attach (loader);
    loader.load (a1, tmp.file ("b1.stem.m4a"));
    finish (loader, queue);
    REQUIRE (first.upgraded != nullptr);
    CHECK (fake.originalDecodes.load() == 1);

    Events second;
    second.attach (loader);
    loader.load (a1, tmp.file ("b1-other.stem.m4a"));
    finish (loader, queue);

    CHECK (fake.originalDecodes.load() == 1);
    REQUIRE (second.original != nullptr);
    CHECK (second.originalEvent.sameOriginal);
    CHECK (second.original->sharesOriginalWith (*first.original));
    REQUIRE (second.upgraded != nullptr);
    CHECK (second.upgraded->alignmentOffset() == 700);
}

TEST_CASE ("PairLoader + AlignmentCache: miss measures, hit is instant and re-measured, a wrong entry is corrected", "[load][loader][cache]")
{
    TempDir tmp;
    FakeBackend fake;
    fake.pairs["a1.flac"] = fake.pairs["b1.stem.m4a"] = { 1, 5.0, 2112 };
    const auto a1 = tmp.file ("a1.flac"), b1 = tmp.file ("b1.stem.m4a");
    AlignmentCache cache (tmp.dir.getChildFile ("cache.json"));

    // Each run uses a fresh loader, as after an app restart.
    const auto run = [&]
    {
        TaskQueue queue;
        PairLoader loader (queue.poster(), &cache, fake.backend());
        Events ev;
        ev.attach (loader);
        loader.load (a1, b1);
        finish (loader, queue);
        return ev;
    };

    const auto miss = run();
    REQUIRE (miss.upgraded != nullptr);
    CHECK_FALSE (miss.stemsEvent.fromCache);
    CHECK (miss.count ("corrected") == 0);
    const auto stored = cache.find (FileIdentity::of (a1), FileIdentity::of (b1));
    REQUIRE (stored.has_value());
    CHECK (stored->offsetSamples == 2112);

    const auto hit = run();
    REQUIRE (hit.upgraded != nullptr);
    CHECK (hit.stemsEvent.fromCache);
    CHECK (hit.upgraded->alignmentOffset() == 2112);
    CHECK (hit.count ("corrected") == 0);

    // A wrong cached offset is used at once, then corrected by the re-measurement (and the cache fixed).
    cache.store (FileIdentity::of (a1), FileIdentity::of (b1), { 2000, +1, 0.99, false });
    const auto wrong = run();
    REQUIRE (wrong.upgraded != nullptr);
    CHECK (wrong.stemsEvent.fromCache);
    CHECK (wrong.upgraded->alignmentOffset() == 2000);
    REQUIRE (wrong.corrected != nullptr);
    CHECK (wrong.indexOf ("B") < wrong.indexOf ("corrected"));
    CHECK (wrong.corrected->alignmentOffset() == 2112);
    CHECK (wrong.corrected->sharesOriginalWith (*wrong.original));
    CHECK (wrong.corrected->decodedStems() == wrong.upgraded->decodedStems());   // same Stems, no copy
    CHECK (wrong.trackNullDepthDb >= 150.0);                                       // computed at the corrected offset
    CHECK (cache.find (FileIdentity::of (a1), FileIdentity::of (b1))->offsetSamples == 2112);
}

TEST_CASE ("PairLoader on the real pairs: A playable first, then B at +6522", "[load][loader][realfiles]")
{
    const auto dir = juce::File (juce::String::fromUTF8 (STEMAB_TEST_DATA_DIR));
    const auto originalFile = dir.getChildFile ("Audio Files").getChildFile ("03 Heresy.flac");
    const auto stemFile = dir.getChildFile ("Stem Files").getChildFile ("03 Nine Inch Nails - Heresy.stem.m4a");

    if (! originalFile.existsAsFile() || ! stemFile.existsAsFile())
        SKIP ("test files not present");

    TaskQueue queue;
    PairLoader loader (queue.poster(), nullptr);
    Events ev;
    ev.attach (loader);
    loader.load (originalFile, stemFile);
    finish (loader, queue);

    CHECK (ev.error.isEmpty());
    REQUIRE (ev.upgraded != nullptr);
    CHECK (ev.indexOf ("A") < ev.indexOf ("B"));
    CHECK (ev.upgraded->alignmentOffset() == 6522);
    CHECK (ev.upgraded->alignmentPolarity() == 1);
    CHECK (ev.upgraded->decodedStems() != nullptr);
    // Zero-copy: the Stems play straight out of the decoded buffers, shifted by the offset.
    CHECK (ev.upgraded->stemChannel (0, 0) == (*ev.upgraded->decodedStems())[0].getReadPointer (0) + 6522);
    WARN ("A playable after " << ev.originalEvent.secondsSinceLoad << " s, B after " << ev.stemsEvent.secondsSinceLoad << " s");
}
