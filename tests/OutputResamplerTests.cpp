#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <random>
#include <vector>

#include "decode/Decode.h"
#include "engine/OutputResampler.h"
#include "engine/PlaybackEngine.h"

using Catch::Matchers::WithinAbs;
using namespace stemab;

#if defined (_MSC_VER) && defined (_DEBUG)
 #include <crtdbg.h>
#endif

//==============================================================================
// Allocation counter (the audio-thread code under test must not allocate), active while `countAllocations`
// is set: every operator new in this test exe, and in Debug builds also every CRT heap allocation (malloc,
// realloc - r8brain allocates with ::malloc) through _CrtSetAllocHook.
namespace
{
    std::atomic<bool> countAllocations { false };
    std::atomic<long> allocationCount { 0 };

   #if defined (_MSC_VER) && defined (_DEBUG)
    int crtAllocHook (int type, void*, size_t, int, long, const unsigned char*, int)
    {
        if ((type == _HOOK_ALLOC || type == _HOOK_REALLOC) && countAllocations.load (std::memory_order_relaxed))
            allocationCount.fetch_add (1, std::memory_order_relaxed);
        return 1;
    }
    constexpr bool crtHookAvailable = true;
   #else
    constexpr bool crtHookAvailable = false;
   #endif

    void* countedAlloc (std::size_t size)
    {
        if (countAllocations.load (std::memory_order_relaxed))
            allocationCount.fetch_add (1, std::memory_order_relaxed);

        if (void* p = std::malloc (size == 0 ? 1 : size))
            return p;

        throw std::bad_alloc();
    }
}

void* operator new (std::size_t size)                                   { return countedAlloc (size); }
void* operator new[] (std::size_t size)                                 { return countedAlloc (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept   { try { return countedAlloc (size); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { try { return countedAlloc (size); } catch (...) { return nullptr; } }
void operator delete (void* p) noexcept                                 { std::free (p); }
void operator delete[] (void* p) noexcept                               { std::free (p); }
void operator delete (void* p, std::size_t) noexcept                    { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept                  { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept          { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept        { std::free (p); }

namespace
{
constexpr double pi = 3.14159265358979323846;

using Gen = std::function<float (int channel, juce::int64 n)>;

Gen sineAt (double freq, double sampleRate, double amp, double phase = 0.0)
{
    return [=] (int ch, juce::int64 n)
    {
        return (float) (amp * std::sin (2.0 * pi * freq * (double) n / sampleRate + phase + ch * 0.3));
    };
}

/** A pull source for OutputResampler::render() that plays a generator from sample 0. */
struct GenSource
{
    Gen gen;
    juce::int64 n = 0;

    static void pull (void* ctx, float* l, float* r, int num)
    {
        auto* s = static_cast<GenSource*> (ctx);
        for (int i = 0; i < num; ++i)
        {
            l[i] = s->gen (0, s->n + i);
            r[i] = s->gen (1, s->n + i);
        }
        s->n += num;
    }
};

std::shared_ptr<const Session> makeSession (int length, double sampleRate, Gen a)
{
    juce::AudioBuffer<float> A (2, length);
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < length; ++n)
            A.setSample (ch, n, a (ch, n));

    StemBuffers S;
    for (auto& s : S)
    {
        s.setSize (2, length);
        s.clear();
    }

    return Session::create (std::move (A), std::move (S), sampleRate);
}

struct Out
{
    std::vector<float> l, r;
};

void renderInto (PlaybackEngine& e, Out& o, int numSamples, int blockSize)
{
    std::vector<float> L ((size_t) blockSize), R ((size_t) blockSize);
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (blockSize, numSamples - done);
        float* chans[2] = { L.data(), R.data() };
        e.process (chans, 2, n);
        o.l.insert (o.l.end(), L.begin(), L.begin() + n);
        o.r.insert (o.r.end(), R.begin(), R.begin() + n);
        done += n;
    }
}

Out render (PlaybackEngine& e, int numSamples, int blockSize = 512)
{
    Out o;
    renderInto (e, o, numSamples, blockSize);
    return o;
}

/** Least-squares fit of a sine at `freq` over x[start, start + len): amplitude, phase and the RMS of what is
    left over. `len` should hold a whole number of cycles. */
struct SineFit { double amplitude = 0.0, phase = 0.0, residualRms = 0.0; };

SineFit fitSine (const std::vector<float>& x, size_t start, size_t len, double freq, double sampleRate)
{
    double a = 0.0, b = 0.0;
    for (size_t i = 0; i < len; ++i)
    {
        const double w = 2.0 * pi * freq * (double) (start + i) / sampleRate;
        a += x[start + i] * std::cos (w);
        b += x[start + i] * std::sin (w);
    }
    a *= 2.0 / (double) len;
    b *= 2.0 / (double) len;

    double sq = 0.0;
    for (size_t i = 0; i < len; ++i)
    {
        const double w = 2.0 * pi * freq * (double) (start + i) / sampleRate;
        const double d = x[start + i] - (a * std::cos (w) + b * std::sin (w));
        sq += d * d;
    }

    return { std::hypot (a, b), std::atan2 (a, b), std::sqrt (sq / (double) len) };
}

double dB (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1e-30)); }

float maxJump (const std::vector<float>& x, size_t from)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t> (from, 1); i < x.size(); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

std::vector<float> streamDirect (OutputResampler& r, GenSource& src, size_t total, unsigned seed, int maxBlock)
{
    std::vector<float> L, R ((size_t) maxBlock), tmp ((size_t) maxBlock);
    std::mt19937 rng (seed);
    std::uniform_int_distribution<int> sizes (1, maxBlock);

    while (L.size() < total)
    {
        const int n = (int) std::min<size_t> ((size_t) sizes (rng), total - L.size());
        r.render (&GenSource::pull, &src, tmp.data(), R.data(), n);
        L.insert (L.end(), tmp.begin(), tmp.begin() + n);
    }
    return L;
}
} // namespace

//==============================================================================
TEST_CASE ("Native Rate: no resampler, the device gets the mixer's samples bit for bit", "[resampler][passthrough]")
{
    constexpr double rate = 44100.0;
    auto s = makeSession (100000, rate, sineAt (997.0, rate, 0.8));

    PlaybackEngine e;
    e.prepare (rate, 256);
    e.setSession (s);
    e.play();

    // Device blocks larger than prepare() announced (rendered in pieces) must not change anything either.
    const auto o = render (e, 20000, 1000);
    CHECK_FALSE (e.isResampling());
    CHECK (e.getResamplerLatencySeconds() == 0.0);

    // x = 0 (Equal Power): y = (1*a + 0*b + 0*r) * 1 * env == a * env exactly, env = smoothstep over the fade.
    const int fadeLen = e.getTransportFadeSamples();
    int bad = 0;
    for (int i = 0; i < 20000; ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            float env = 1.0f;
            if (i + 1 < fadeLen)
            {
                const float phase = (float) (i + 1) / (float) fadeLen;
                env = phase * phase * (3.0f - 2.0f * phase);
            }

            const float want = s->original().getSample (ch, i) * env;
            const float got = ch == 0 ? o.l[(size_t) i] : o.r[(size_t) i];
            if (got != want)
                ++bad;
        }
    CHECK (bad == 0);
}

TEST_CASE ("Realtime resampling 44.1k <-> 48k with random device blocks: exact count, clean 1 kHz sine",
           "[resampler][stream]")
{
    struct Case { double src, dst; };
    for (auto c : { Case { 44100.0, 48000.0 }, Case { 48000.0, 44100.0 } })
    {
        DYNAMIC_SECTION (c.src << " -> " << c.dst)
        {
            constexpr double amp = 0.5;
            const int seconds = 6;
            auto s = makeSession ((int) c.src * (seconds + 1), c.src, sineAt (1000.0, c.src, amp));

            PlaybackEngine e;
            e.prepare (c.dst, 2048);
            e.setSession (s);
            e.play();

            Out o;
            std::mt19937 rng (1234);
            std::uniform_int_distribution<int> sizes (1, 2048);
            const size_t total = (size_t) c.dst * (size_t) seconds;
            juce::int64 requested = 0;

            while (o.l.size() < total)
            {
                const int n = (int) std::min<size_t> ((size_t) sizes (rng), total - o.l.size());
                renderInto (e, o, n, n);
                requested += n;
            }

            CHECK (e.isResampling());
            CHECK (e.isPlaying());
            CHECK ((juce::int64) o.l.size() == requested);   // exactly what the device asked for

            // No drift: the mixer is ahead of the device by the resampler's look-ahead (plus < 1 chunk + FIFO).
            const double expectedPlayhead = (double) requested * c.src / c.dst;
            const double ahead = (double) e.getPlayheadSample() - expectedPlayhead;
            CHECK (ahead >= 0.0);
            CHECK (ahead <= e.getResamplerLatencySeconds() * c.src + 2.0 * OutputResampler::chunkSize);

            // Skip the fade-in and look-ahead, then check every 100 ms window.
            const size_t win = (size_t) (c.dst / 10.0);   // 100 cycles of 1 kHz
            double firstPhase = 0.0;
            float worstJump = 0.0f;

            for (size_t w = 1; (w + 1) * win <= total; ++w)
            {
                for (const auto* ch : { &o.l, &o.r })
                {
                    const auto f = fitSine (*ch, w * win, win, 1000.0, c.dst);
                    CHECK_THAT (dB (f.amplitude / amp), WithinAbs (0.0, 0.05));
                    CHECK (dB (f.residualRms / (amp / std::sqrt (2.0))) < -90.0);   // no glitch, no distortion

                    if (ch == &o.l)
                    {
                        if (w == 1) firstPhase = f.phase;
                        CHECK_THAT (f.phase, WithinAbs (firstPhase, 1e-4));            // frequency kept exactly
                    }
                }
            }

            worstJump = std::max (maxJump (o.l, win), maxJump (o.r, win));
            // A clean sine's largest step: 2*pi*f/fs * amp (plus a hair for rounding).
            CHECK (worstJump <= (float) (2.0 * pi * 1000.0 / c.dst * amp * 1.001));
        }
    }
}

TEST_CASE ("Streaming output matches the offline resample() of the same signal", "[resampler][stream]")
{
    struct Case { double src, dst; };
    for (auto c : { Case { 44100.0, 48000.0 }, Case { 48000.0, 44100.0 } })
    {
        DYNAMIC_SECTION (c.src << " -> " << c.dst)
        {
            // Content well inside both filters' pass bands (the offline one is steeper, ADR 0004 resampler).
            const Gen gen = [c] (int, juce::int64 n)
            {
                const double t = (double) n / c.src;
                return (float) (0.25 * std::sin (2.0 * pi * 440.0 * t + 0.1)
                              + 0.20 * std::sin (2.0 * pi * 3150.0 * t + 1.3)
                              + 0.15 * std::sin (2.0 * pi * 11025.0 * t + 2.2));
            };

            const int numIn = (int) c.src * 2;
            juce::AudioBuffer<float> in (1, numIn);
            for (int n = 0; n < numIn; ++n)
                in.setSample (0, n, gen (0, n));

            const auto offline = resample (in, c.src, c.dst);

            OutputResampler r (c.src, c.dst, 2048);
            GenSource src { gen };
            const auto streamed = streamDirect (r, src, (size_t) offline.getNumSamples() - 4096, 99, 2048);

            // r8brain compensates its own latency: output sample k is input time k / dst in both modes.
            // Find the best integer alignment (expected: 0) and compare away from the edges.
            const size_t from = 2048, to = streamed.size() - 2048;
            int bestLag = 0;
            double bestErr = 1e9;
            for (int lag = -8; lag <= 8; ++lag)
            {
                double err = 0.0;
                for (size_t i = from; i < to; ++i)
                    err = std::max (err, (double) std::abs (streamed[i] - offline.getSample (0, (int) ((juce::int64) i + lag))));
                if (err < bestErr) { bestErr = err; bestLag = lag; }
            }

            CHECK (bestLag == 0);
            // The filters differ only near the band edge; here the difference is at float rounding level
            // (measured 2.4e-7). 1e-5 is -96 dB of the 0.6 peak.
            CHECK (bestErr < 1.0e-5);
        }
    }
}

TEST_CASE ("Clip is detected after resampling: inter-sample overs count", "[resampler][clip]")
{
    // fs/4 at 45 degrees: every source sample is +-0.954 (<= 1.0), but the waveform peaks at 1.35 between them.
    constexpr double src = 44100.0;
    const Gen overs = [] (int, juce::int64 n)
    {
        return (float) (1.35 * std::sin (2.0 * pi * 0.25 * (double) n + pi / 4.0));
    };
    auto s = makeSession (88200, src, overs);

    for (int n = 0; n < 88200; ++n)
        REQUIRE (std::abs (s->original().getSample (0, n)) <= 1.0f);

    SECTION ("Native Rate: no sample above 1.0, no Clip")
    {
        PlaybackEngine e;
        e.prepare (src, 512);
        e.setSession (s);
        e.play();
        render (e, 22050);
        CHECK_FALSE (e.readAndClearClip());
        CHECK (e.readMeter (MeterTap::Output, 0).peak <= 1.0f);
    }

    SECTION ("Resampled to 48 kHz: the device gets overs and Clip is set")
    {
        PlaybackEngine e;
        e.prepare (48000.0, 512);
        e.setSession (s);
        e.play();
        const auto o = render (e, 24000);

        float peak = 0.0f;
        for (float x : o.l)
            peak = std::max (peak, std::abs (x));
        CHECK (peak > 1.2f);
        CHECK (e.readAndClearClip());
        CHECK (e.readMeter (MeterTap::Output, 0).peak > 1.2f);     // Output meter: after the resampler
        CHECK (e.readMeter (MeterTap::Original, 0).peak <= 1.0f);  // source taps: before it
    }
}

TEST_CASE ("Device rate changes mid-stream: re-prepare, no crash, output resumes", "[resampler][device]")
{
    constexpr double src = 44100.0;
    auto s = makeSession ((int) src * 20, src, sineAt (1000.0, src, 0.5));

    PlaybackEngine e;
    e.prepare (48000.0, 512);
    e.setSession (s);
    e.play();
    render (e, 9600);
    REQUIRE (e.isResampling());

    for (const auto [dev, block] : { std::pair { 96000.0, 1024 }, std::pair { 44100.0, 480 },
                                     std::pair { 48000.0, 128 }, std::pair { 22050.0, 64 }, std::pair { 48000.0, 4096 } })
    {
        CAPTURE (dev, block);
        const auto before = e.getPlayheadSample();

        e.prepare (dev, block);   // the device restarted (no callback runs meanwhile)
        CHECK (e.isResampling() == (dev != src));

        const auto o = render (e, (int) dev / 2, block);   // 0.5 s
        CHECK (e.isPlaying());
        CHECK (e.getPlayheadSample() > before + (juce::int64) (0.45 * src));
        CHECK (std::all_of (o.l.begin(), o.l.end(), [] (float x) { return std::isfinite (x) && std::abs (x) <= 0.51f; }));

        // The 1 kHz tone is back at full level after the restart.
        const size_t win = (size_t) (dev / 10.0);
        const auto f = fitSine (o.l, o.l.size() - win, win, 1000.0, dev);
        CHECK_THAT (dB (f.amplitude / 0.5), WithinAbs (0.0, 0.05));
    }
}

TEST_CASE ("Session swap to another rate re-prepares the output path off the audio thread", "[resampler][session]")
{
    PlaybackEngine e;
    e.prepare (48000.0, 512);

    auto s441 = makeSession (44100 * 4, 44100.0, sineAt (1000.0, 44100.0, 0.5));
    auto s48 = makeSession (48000 * 4, 48000.0, sineAt (1000.0, 48000.0, 0.5));

    e.setSession (s441);
    e.play();
    render (e, 24000);
    CHECK (e.isResampling());
    CHECK (e.getTransportFadeSamples() == 441);

    // To the device's own rate: the old resampler plays out its tail, then the path becomes passthrough.
    e.setSession (s48);
    render (e, 9600);
    e.releaseRetiredSessions();
    CHECK_FALSE (e.isPlaying());
    e.play();
    auto o = render (e, 24000);
    CHECK_FALSE (e.isResampling());
    CHECK (e.isPlaying());
    CHECK (e.getTransportFadeSamples() == 480);
    CHECK_THAT (dB (fitSine (o.l, 12000, 4800, 1000.0, 48000.0).amplitude / 0.5), WithinAbs (0.0, 0.05));

    // And back: a resampler built by setSession() on this thread.
    e.setSession (s441);
    render (e, 4800);
    e.play();
    o = render (e, 24000);
    CHECK (e.isResampling());
    CHECK (e.isPlaying());
    CHECK_THAT (dB (fitSine (o.l, 12000, 4800, 1000.0, 48000.0).amplitude / 0.5), WithinAbs (0.0, 0.05));

    // A clean stop: no click when the swap fades out through the resampler.
    e.setSession (s48);
    o = render (e, 9600);
    CHECK (maxJump (o.l, 0) <= (float) (2.0 * pi * 1000.0 / 48000.0 * 0.5 * 1.01));
    e.releaseRetiredSessions();
}

TEST_CASE ("A resampler the audio thread lacks is requested and built on the UI thread", "[resampler][session]")
{
    PlaybackEngine e;
    e.prepare (48000.0, 512);
    e.setSession (makeSession (44100 * 4, 44100.0, sineAt (1000.0, 44100.0, 0.5)));   // builds 44.1k -> 48k

    // The device restarts at another rate before the audio thread took the session: that resampler is stale.
    e.prepare (96000.0, 512);
    e.play();
    auto o = render (e, 4096);
    CHECK (std::all_of (o.l.begin(), o.l.end(), [] (float x) { return x == 0.0f; }));   // waiting: silence

    e.releaseRetiredSessions();   // the UI timer: builds 44.1k -> 96k
    o = render (e, 48000);
    CHECK (e.isResampling());
    CHECK (e.isPlaying());
    CHECK_THAT (dB (fitSine (o.l, 38400, 9600, 1000.0, 96000.0).amplitude / 0.5), WithinAbs (0.0, 0.05));
}

TEST_CASE ("The audio thread never allocates, at the Native Rate or resampling", "[resampler][realtime]")
{
   #if defined (_MSC_VER) && defined (_DEBUG)
    auto* previousHook = _CrtSetAllocHook (crtAllocHook);
   #endif

    // The counter itself works (malloc only seen through the CRT hook, i.e. in Debug).
    {
        allocationCount = 0;
        countAllocations = true;
        auto* p = new int (1);
        void* m = std::malloc (16);
        countAllocations = false;
        delete p;
        std::free (m);
        CHECK (allocationCount.load() == (crtHookAvailable ? 3 : 1));
    }

    for (const double dev : { 44100.0, 48000.0, 96000.0 })
    {
        CAPTURE (dev);
        PlaybackEngine e;
        e.prepare (dev, 512);
        e.setSession (makeSession (44100 * 3, 44100.0, sineAt (1000.0, 44100.0, 0.5)));
        e.play();
        render (e, 2048);   // takes the session and the resampler

        std::vector<float> L (512), R (512);
        float* chans[2] = { L.data(), R.data() };

        allocationCount = 0;
        countAllocations = true;
        for (int i = 0; i < 400; ++i)
        {
            if (i == 50)  e.seek (1000);
            if (i == 100) { e.setLoopRegion (500, 30000); e.setLoopEnabled (true); }
            if (i == 150) e.pause();
            if (i == 200) e.play();
            if (i == 250) e.setCrossfade (1.0f);
            if (i == 300) e.setMasterGainDb (-6.0);
            e.process (chans, 2, 1 + (i * 37) % 512);
        }
        countAllocations = false;

        CHECK (allocationCount.load() == 0);

        // A continue* handover and a Session upgrade (the Stems arrive) are taken on the audio thread, too.
        {
            const auto full = makeSession (44100 * 3, 44100.0, sineAt (1000.0, 44100.0, 0.5));
            const auto aOnly = Session::originalOnlyOf (*full);
            const auto upgraded = Session::withStems (*aOnly, full->decodedStems(), 0, +1);

            e.setSession (aOnly, Handover::continueFromStart);
            allocationCount = 0;
            countAllocations = true;
            for (int i = 0; i < 50; ++i)
                e.process (chans, 2, 1 + (i * 53) % 512);
            countAllocations = false;
            CHECK (allocationCount.load() == 0);
            CHECK (e.isPlaying());

            e.upgradeSession (upgraded);
            allocationCount = 0;
            countAllocations = true;
            for (int i = 0; i < 50; ++i)
                e.process (chans, 2, 1 + (i * 53) % 512);
            countAllocations = false;
            CHECK (allocationCount.load() == 0);
            CHECK (e.sessionHasStems());
            e.releaseRetiredSessions();
        }
    }

   #if defined (_MSC_VER) && defined (_DEBUG)
    _CrtSetAllocHook (previousHook);
   #endif
}

//==============================================================================
// Not a test: the numbers behind OutputResampler::defaultQuality. Run with
//   StemAB_Tests "[.resampler-measure]" -s
TEST_CASE ("Measure resampler configurations: latency, pass band, stop band, CPU", "[.resampler-measure]")
{
    struct Config { const char* name; ResamplerQuality q; };
    const Config configs[] = {
        { "r8b 24-bit default (tb 2, 180.15 dB)", { 2.0, 180.15, false } },
        { "r8b 16-bit default (tb 2, 136.45 dB)", { 2.0, 136.45, false } },
        { "tb 3, 136 dB",  { 3.0, 136.0, false } },
        { "tb 4, 120 dB",  { 4.0, 120.0, false } },
        { "tb 4, 125 dB",  { 4.0, 125.0, false } },
        { "tb 4, 130 dB",  { 4.0, 130.0, false } },
        { "tb 4.5, 120 dB", { 4.5, 120.0, false } },
        { "tb 4.5, 125 dB", { 4.5, 125.0, false } },
        { "tb 4.5, 130 dB", { 4.5, 130.0, false } },
        { "tb 5, 120 dB",  { 5.0, 120.0, false } },
        { "tb 5, 125 dB",  { 5.0, 125.0, false } },
        { "tb 5, 130 dB",  { 5.0, 130.0, false } },
        { "tb 5, 136 dB",  { 5.0, 136.0, false } },
        { "tb 6, 120 dB",  { 6.0, 120.0, false } },
        { "tb 8, 120 dB",  { 8.0, 120.0, false } },
        { "tb 8, 110 dB",  { 8.0, 110.0, false } },
        { "tb 6, 136 dB",  { 6.0, 136.0, false } },
        { "tb 4, 136 dB min-phase", { 4.0, 136.0, true } },
        { "tb 2, 180 dB min-phase", { 2.0, 180.15, true } },
    };

    struct Dir { double src, dst; };
    for (auto d : { Dir { 44100.0, 48000.0 }, Dir { 48000.0, 44100.0 } })
    {
        std::printf ("\n%.0f -> %.0f\n", d.src, d.dst);
        std::printf ("%-38s %8s %8s %8s %8s %9s %9s %9s %9s %9s %9s %8s\n", "config", "lookahd", "nom ms", "mean ms", "max ms",
                     "20k dB", "21k dB", "art 1k", "art 10k", "art 20k", "alias", "us/blk");

        for (const auto& c : configs)
        {
            const auto measure = [&] (double freq, double amp, SineFit& fit, double& rmsOut)
            {
                OutputResampler r (d.src, d.dst, 512, c.q);
                GenSource g { sineAt (freq, d.src, amp) };
                const auto y = streamDirect (r, g, (size_t) d.dst * 2, 7, 512);
                const size_t start = (size_t) d.dst / 2, len = (size_t) d.dst;   // 1 s: whole cycles
                fit = fitSine (y, start, len, freq, d.dst);
                double sq = 0.0;
                for (size_t i = start; i < start + len; ++i) sq += (double) y[i] * y[i];
                rmsOut = std::sqrt (sq / (double) len);
            };

            SineFit f1, f10, f20, f21, fa;
            double rms = 0.0, aliasRms = 0.0;
            measure (1000.0, 0.5, f1, rms);
            measure (10000.0, 0.5, f10, rms);
            measure (20000.0, 0.5, f20, rms);
            measure (21000.0, 0.5, f21, rms);

            // Above the lower Nyquist (22.05k): must be removed when going down; when going up, 21.5k's image
            // (at 22.6k) shows in the residual of the 21k fit instead.
            double aliasDb = 0.0;
            if (d.dst < d.src)
            {
                measure (23000.0, 0.5, fa, aliasRms);
                aliasDb = dB (aliasRms / (0.5 / std::sqrt (2.0)));
            }

            const auto art = [] (const SineFit& f) { return dB (f.residualRms / (f.amplitude / std::sqrt (2.0))); };

            // CPU: 10 s of output in 512-sample device blocks (both channels), per block. Also the measured
            // extra latency: mixed-but-not-yet-heard audio at the end of each callback, averaged.
            OutputResampler r (d.src, d.dst, 512, c.q);
            GenSource g { sineAt (1000.0, d.src, 0.5) };
            std::vector<float> L (512), R (512);
            const int blocks = (int) (d.dst * 10.0 / 512.0);
            double us = 0.0, aheadSum = 0.0, aheadMax = 0.0;
            for (int b = 0; b < blocks; ++b)
            {
                const auto t0 = std::chrono::steady_clock::now();
                r.render (&GenSource::pull, &g, L.data(), R.data(), 512);
                us += std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();

                const double ahead = (double) r.getSourceSamplesPulled() / d.src - (double) r.getOutputSamplesRendered() / d.dst;
                aheadSum += ahead;
                aheadMax = std::max (aheadMax, ahead);
            }
            us /= blocks;

            std::printf ("%-38s %8d %8.2f %8.2f %8.2f %9.4f %9.4f %9.1f %9.1f %9.1f %9.1f %8.1f\n", c.name,
                         r.getLookaheadSourceSamples(), r.getLatencySeconds() * 1000.0,
                         aheadSum / blocks * 1000.0, aheadMax * 1000.0,
                         dB (f20.amplitude / 0.5), dB (f21.amplitude / 0.5), art (f1), art (f10), art (f20),
                         aliasDb, us);
        }
    }

    // The generator itself (sin per sample) is part of the CPU column; measure it alone for reference.
    {
        GenSource g { sineAt (1000.0, 44100.0, 0.5) };
        std::vector<float> L (64), R (64);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 10000; ++b)
            GenSource::pull (&g, L.data(), R.data(), 64);
        std::printf ("\ngenerator alone: %.2f us per 64 stereo samples\n",
                     std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / 10000.0);
    }
    SUCCEED();
}
