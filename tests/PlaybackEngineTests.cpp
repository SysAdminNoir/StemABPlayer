#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <thread>
#include <vector>

#include "analysis/NullDepth.h"
#include "engine/LiveNullDepth.h"
#include "engine/PlaybackEngine.h"

using Catch::Matchers::WithinAbs;
using namespace stemab;

namespace
{
constexpr double rate = 44100.0;
constexpr int block = 512;

using Gen = std::function<float (int channel, int n)>;

std::shared_ptr<const Session> makeSession (int length, Gen a, std::array<Gen, 4> stems, double sr = rate)
{
    juce::AudioBuffer<float> A (2, length);
    StemBuffers S;
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < length; ++n)
            A.setSample (ch, n, a (ch, n));

    for (size_t k = 0; k < 4; ++k)
    {
        S[k].setSize (2, length);
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < length; ++n)
                S[k].setSample (ch, n, stems[k] ? stems[k] (ch, n) : 0.0f);
    }

    return Session::create (std::move (A), std::move (S), sr);
}

/** Deterministic noise in [-0.25, 0.25): every sample distinct, so content identifies position. */
Gen noise (int seed)
{
    return [seed] (int ch, int n)
    {
        juce::uint32 x = (juce::uint32) (n * 2 + ch) * 2654435761u ^ (juce::uint32) seed * 40503u;
        x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15;
        return ((float) (x & 0xffffff) / (float) 0x1000000 - 0.5f) * 0.5f;
    };
}

Gen sine (double freq, float amp)
{
    return [freq, amp] (int ch, int n)
    {
        return amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * n / rate + ch * 0.3);
    };
}

std::shared_ptr<const Session> noiseSession (int length = 100000)
{
    return makeSession (length, noise (1), { noise (2), noise (3), noise (4), noise (5) });
}

struct Out
{
    std::vector<float> l, r;
    size_t size() const { return l.size(); }
};

Out render (PlaybackEngine& e, int numSamples, int blockSize = block)
{
    Out o;
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
    return o;
}

float maxJump (const std::vector<float>& x)
{
    float m = 0.0f;
    for (size_t i = 1; i < x.size(); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

/** Engine prepared, session set, playing, all ramps and the fade-in settled. */
void startSettled (PlaybackEngine& e, std::shared_ptr<const Session> s)
{
    e.prepare (rate, block);
    e.setSession (std::move (s));
    e.play();
    render (e, 4096);
}

float A (const Session& s, int ch, juce::int64 n)          { return s.original().getSample (ch, (int) n); }
float St (const Session& s, int k, int ch, juce::int64 n)  { return s.stemChannel (k, ch)[n]; }

/** Sum of the given Stems, in the engine's order (Drums, Bass, Other, Vocals). */
float stemSum (const Session& s, std::array<bool, 4> audible, int ch, juce::int64 n)
{
    float b = 0.0f;
    for (int k = 0; k < 4; ++k)
        if (audible[(size_t) k])
            b += St (s, k, ch, n);
    return b;
}

/** Renders `count` samples and checks out[i] == expected(ch, playhead + i) within tol. */
void checkSteady (PlaybackEngine& e, const std::function<float (int, juce::int64)>& expected, float tol, int count = 4096)
{
    const auto p0 = e.getPlayheadSample();
    const auto o = render (e, count);
    int bad = 0;
    for (int i = 0; i < count; ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const float got = ch == 0 ? o.l[(size_t) i] : o.r[(size_t) i];
            const float want = expected (ch, p0 + i);
            if (tol == 0.0f ? got != want : std::abs (got - want) > tol)
                ++bad;
        }
    CHECK (bad == 0);
}
} // namespace

//==============================================================================
TEST_CASE ("engine crossfade: x=0 is exactly A, x=1 exactly B, curves in between", "[engine][mix]")
{
    PlaybackEngine e;
    auto s = noiseSession();
    const Session& S = *s;
    startSettled (e, s);

    SECTION ("x = 0 -> A exactly")
    {
        e.setCrossfade (0.0f);
        render (e, 2048);
        checkSteady (e, [&] (int ch, juce::int64 n) { return A (S, ch, n); }, 0.0f);
    }

    SECTION ("x = 1 -> B (sum of all Stems) exactly")
    {
        e.setCrossfade (1.0f);
        render (e, 2048);
        checkSteady (e, [&] (int ch, juce::int64 n) { return stemSum (S, { true, true, true, true }, ch, n); }, 0.0f);
    }

    SECTION ("equal power at 0.5 -> cos/sin gains")
    {
        e.setCurve (Curve::EqualPower);
        e.setCrossfade (0.5f);
        render (e, 2048);
        const float g = std::cos (juce::MathConstants<float>::pi * 0.25f);
        checkSteady (e, [&] (int ch, juce::int64 n)
                     { return g * A (S, ch, n) + g * stemSum (S, { true, true, true, true }, ch, n); }, 1.0e-6f);
    }

    SECTION ("linear at 0.25 -> 0.75 A + 0.25 B")
    {
        e.setCurve (Curve::Linear);
        e.setCrossfade (0.25f);
        render (e, 2048);
        checkSteady (e, [&] (int ch, juce::int64 n)
                     { return 0.75f * A (S, ch, n) + 0.25f * stemSum (S, { true, true, true, true }, ch, n); }, 1.0e-6f);
    }

}

TEST_CASE ("gain changes are ramped, not stepped", "[engine][mix]")
{
    // A = DC 1, Stems silent: moving the crossfade 0 -> 1 (linear) ramps the output 1 -> 0 over ~10 ms.
    PlaybackEngine e;
    startSettled (e, makeSession (50000, [] (int, int) { return 1.0f; }, {}));
    e.setCurve (Curve::Linear);
    e.setCrossfade (1.0f);
    const auto o = render (e, 2048);
    CHECK (maxJump (o.l) <= 1.0f / 441.0f + 1.0e-6f);
    CHECK (o.l[100] > 0.5f);
    CHECK (o.l[1000] == 0.0f);

    // Master Gain and Mute too.
    e.setCrossfade (0.0f);
    render (e, 2048);
    e.setMasterGainDb (-std::numeric_limits<double>::infinity());
    const auto m = render (e, 2048);
    CHECK (maxJump (m.l) <= 1.0f / 441.0f + 1.0e-6f);
    CHECK (m.l[1000] == 0.0f);
}

TEST_CASE ("engine Residual: A minus all four Stems, ignoring Crossfade and Mute/Solo", "[engine][mix]")
{
    PlaybackEngine e;
    auto s = noiseSession();
    const Session& S = *s;
    startSettled (e, s);

    e.setCrossfade (0.3f);
    e.setStemMuted (1, true);
    e.setStemSoloed (2, true);
    e.setListenMode (ListenMode::Residual);
    render (e, 2048);

    const auto residual = [&] (int ch, juce::int64 n)
    {
        return A (S, ch, n) - (((0.0f + St (S, 0, ch, n)) + St (S, 1, ch, n) + St (S, 2, ch, n)) + St (S, 3, ch, n));
    };
    checkSteady (e, residual, 1.0e-7f);

    // Mute/Solo changes do not change it either.
    e.setStemSoloed (2, false);
    e.setStemMuted (0, true);
    e.setStemMuted (3, true);
    render (e, 2048);
    checkSteady (e, residual, 1.0e-7f);

    // Back to A/B: the crossfade applies again.
    e.setListenMode (ListenMode::AB);
    e.setCrossfade (0.0f);
    render (e, 2048);
    checkSteady (e, [&] (int ch, juce::int64 n) { return A (S, ch, n); }, 0.0f);
}

TEST_CASE ("Mute/Solo resolution table", "[engine][mix]")
{
    using B4 = std::array<bool, 4>;
    struct Row { B4 muted, soloed, audible; };

    const Row rows[] = {
        { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 1, 1, 1, 1 } },   // none
        { { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 1, 0, 1, 0 } },   // mute some
        { { 1, 1, 1, 1 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },   // mute all
        { { 0, 0, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 1, 0 } },   // solo one
        { { 0, 0, 0, 0 }, { 1, 0, 0, 1 }, { 1, 0, 0, 1 } },   // solo some
        { { 1, 1, 0, 0 }, { 1, 0, 0, 0 }, { 1, 0, 0, 0 } },   // solo + mute: solo wins for the soloed Stem
        { { 0, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 1, 0, 1 } },   // solo + mute on the same Stem: soloed set
    };

    for (const auto& row : rows)
        CHECK (PlaybackEngine::audibleStems (row.muted, row.soloed) == row.audible);

    // Through the engine at x = 1.
    PlaybackEngine e;
    auto s = noiseSession();
    const Session& S = *s;
    startSettled (e, s);
    e.setCrossfade (1.0f);

    for (const auto& row : rows)
    {
        for (int k = 0; k < 4; ++k)
        {
            e.setStemMuted (k, row.muted[(size_t) k]);
            e.setStemSoloed (k, row.soloed[(size_t) k]);
        }
        render (e, 2048);
        checkSteady (e, [&] (int ch, juce::int64 n) { return stemSum (S, row.audible, ch, n); }, 0.0f, 1024);
    }
}

TEST_CASE ("Master Gain is one plain scalar", "[engine][mix]")
{
    PlaybackEngine e;
    auto s = noiseSession();
    const Session& S = *s;
    startSettled (e, s);

    SECTION ("-6.0206 dB halves the output")
    {
        e.setMasterGainDb (-6.0206);
        render (e, 2048);
        checkSteady (e, [&] (int ch, juce::int64 n) { return 0.5f * A (S, ch, n); }, 2.0e-6f);
    }

    SECTION ("-inf is silence")
    {
        e.setMasterGainDb (-std::numeric_limits<double>::infinity());
        render (e, 2048);
        checkSteady (e, [] (int, juce::int64) { return 0.0f; }, 0.0f);
    }

    SECTION ("above 0 dB is clamped to 0 dB")
    {
        e.setMasterGainDb (6.0);
        CHECK (e.getMasterGainDb() == 0.0);
        render (e, 2048);
        checkSteady (e, [&] (int ch, juce::int64 n) { return A (S, ch, n); }, 0.0f);
    }
}

TEST_CASE ("Pause fades out within 10 ms, without a click, and holds the playhead", "[engine][transport]")
{
    PlaybackEngine e;
    startSettled (e, makeSession (200000, sine (1000.0, 1.0f), {}));
    const int fadeLen = e.getTransportFadeSamples();
    CHECK (fadeLen == 441);

    const auto before = render (e, block);
    e.pause();
    const auto o = render (e, 4096);
    CHECK_FALSE (e.isPlaying());

    // Last non-zero sample is within the fade (<= 10 ms).
    int lastNonZero = -1;
    for (int i = 0; i < (int) o.size(); ++i)
        if (o.l[(size_t) i] != 0.0f || o.r[(size_t) i] != 0.0f)
            lastNonZero = i;
    CHECK (lastNonZero < fadeLen);
    CHECK (lastNonZero > fadeLen / 2);

    // No step larger than the full-scale 1 kHz sine's own (2*pi*1000/44100 = 0.1425) plus a small margin.
    std::vector<float> joined (before.l);
    joined.insert (joined.end(), o.l.begin(), o.l.end());
    CHECK (maxJump (joined) < 0.15f);

    const auto held = e.getPlayheadSample();
    render (e, 4096);
    CHECK (e.getPlayheadSample() == held);

    // Play resumes from the held position with a fade-in (no click).
    e.play();
    const auto resumed = render (e, 2048);
    CHECK (e.isPlaying());
    CHECK (maxJump (resumed.l) < 0.15f);
    CHECK (e.getPlayheadSample() == held + 2048);
}

TEST_CASE ("Seek lands on the exact sample, with fades", "[engine][transport]")
{
    SECTION ("content after the fade-in is A from the target")
    {
        PlaybackEngine e;
        auto s = noiseSession();
        const Session& S = *s;
        startSettled (e, s);
        const int fadeLen = e.getTransportFadeSamples();

        const juce::int64 target = 54321;
        e.seek (target);
        const auto o = render (e, 4096);

        // Fade-out takes fadeLen samples ending on a zero-gain sample; the next sample is the target.
        const int jumpIndex = fadeLen;
        int bad = 0;
        for (int i = jumpIndex + fadeLen; i < (int) o.size(); ++i)
            if (o.l[(size_t) i] != A (S, 0, target + (i - jumpIndex)) || o.r[(size_t) i] != A (S, 1, target + (i - jumpIndex)))
                ++bad;
        CHECK (bad == 0);
        CHECK (o.l[(size_t) (jumpIndex - 1)] == 0.0f);
        CHECK (e.getPlayheadSample() == target + (4096 - jumpIndex));
    }

    SECTION ("no click on a full-scale sine")
    {
        PlaybackEngine e;
        startSettled (e, makeSession (200000, sine (1000.0, 1.0f), {}));
        e.seek (100011);   // arbitrary phase
        const auto o = render (e, 4096);
        CHECK (maxJump (o.l) < 0.15f);
        CHECK (maxJump (o.r) < 0.15f);
    }

    SECTION ("while paused the playhead just moves")
    {
        PlaybackEngine e;
        e.prepare (rate, block);
        e.setSession (noiseSession());
        render (e, block);
        e.seek (777);
        render (e, block);
        CHECK (e.getPlayheadSample() == 777);
        CHECK_FALSE (e.isPlaying());
    }
}

TEST_CASE ("Loop wraps at the exact sample with fades", "[engine][transport]")
{
    SECTION ("content")
    {
        PlaybackEngine e;
        auto s = noiseSession();
        const Session& S = *s;
        e.prepare (rate, block);
        e.setSession (s);
        e.setLoopRegion (10000, 20000);
        e.setLoopEnabled (true);
        render (e, block);
        e.seek (18000);
        render (e, block);   // paused: lands at 18000
        e.play();
        const auto o = render (e, 6000);
        const int fadeLen = e.getTransportFadeSamples();

        // 18000..19999 are indices 0..1999; the fade reaches zero on 19999; index 2000 is 10000.
        CHECK (o.l[1999] == 0.0f);
        int bad = 0;
        for (int i = fadeLen; i < 2000 - fadeLen; ++i)
            if (o.l[(size_t) i] != A (S, 0, 18000 + i)) ++bad;
        for (int i = 2000 + fadeLen; i < 6000; ++i)
            if (o.l[(size_t) i] != A (S, 0, 10000 + (i - 2000)) || o.r[(size_t) i] != A (S, 1, 10000 + (i - 2000))) ++bad;
        CHECK (bad == 0);
        CHECK (e.isPlaying());
        CHECK (e.getPlayheadSample() == 10000 + 4000);
    }

    SECTION ("no click on a full-scale sine, several wraps")
    {
        PlaybackEngine e;
        startSettled (e, makeSession (60000, sine (1000.0, 1.0f), {}));
        e.setLoopRegion (5003, 9871);
        e.setLoopEnabled (true);
        e.seek (5003);
        const auto o = render (e, 30000);
        CHECK (maxJump (o.l) < 0.15f);
        CHECK (e.getPlayheadSample() >= 5003);
        CHECK (e.getPlayheadSample() < 9871);
    }

    SECTION ("empty region loops the whole track")
    {
        PlaybackEngine e;
        auto s = noiseSession (30000);
        const Session& S = *s;
        startSettled (e, s);   // playhead 4096
        e.setLoopRegion (0, 0);
        e.setLoopEnabled (true);
        const auto o = render (e, 30000);   // 4096 + 30000 wraps once at 30000
        CHECK (e.isPlaying());
        CHECK (e.getPlayheadSample() == 4096);
        const int wrapIndex = 30000 - 4096;
        CHECK (o.l[(size_t) wrapIndex - 1] == 0.0f);
        CHECK (o.l[(size_t) wrapIndex + 1000] == A (S, 0, 1000));
    }
}

TEST_CASE ("End of track without loop: fades and pauses at the end", "[engine][transport]")
{
    PlaybackEngine e;
    auto s = noiseSession (30000);
    e.prepare (rate, block);
    e.setSession (s);
    render (e, block);
    e.seek (29000);
    render (e, block);
    e.play();
    const auto o = render (e, 3000);

    CHECK_FALSE (e.isPlaying());
    CHECK (e.getPlayheadSample() == 30000);
    CHECK (o.l[999] == 0.0f);
    for (size_t i = 1000; i < o.size(); ++i)
        REQUIRE (o.l[i] == 0.0f);

    // Play again from the end restarts at the beginning.
    e.play();
    render (e, 1000);
    CHECK (e.isPlaying());
    CHECK (e.getPlayheadSample() == 1000);
}

TEST_CASE ("Clip is indicated, never hidden", "[engine][clip]")
{
    SECTION ("over 1.0 sets the flag and the samples pass unlimited")
    {
        PlaybackEngine e;
        startSettled (e, makeSession (50000, [] (int, int) { return 1.25f; }, {}));
        (void) e.readAndClearClip();
        const auto o = render (e, 1024);
        CHECK (e.readAndClearClip());
        CHECK_FALSE (e.readAndClearClip());   // read-and-clear
        CHECK (o.l[100] == 1.25f);
        CHECK (o.r[100] == 1.25f);
    }

    SECTION ("B over 1.0 clips too (summed Stems)")
    {
        PlaybackEngine e;
        startSettled (e, makeSession (50000, [] (int, int) { return 0.5f; },
                                      { [] (int, int) { return 0.4f; }, [] (int, int) { return 0.4f; },
                                        [] (int, int) { return 0.4f; }, nullptr }));
        e.setCrossfade (1.0f);
        render (e, 2048);
        (void) e.readAndClearClip();
        const auto o = render (e, 1024);
        CHECK (e.readAndClearClip());
        CHECK_THAT (o.l[10], WithinAbs (1.2, 1.0e-6));
    }

    SECTION ("at or below 1.0 does not")
    {
        PlaybackEngine e;
        startSettled (e, makeSession (50000, sine (440.0, 1.0f), {}));
        render (e, 4096);
        CHECK_FALSE (e.readAndClearClip());
    }
}

TEST_CASE ("Meters: peak and RMS per tap", "[engine][meters]")
{
    PlaybackEngine e;
    startSettled (e, makeSession (50000, [] (int ch, int) { return ch == 0 ? 0.5f : -0.25f; },
                                  { [] (int, int) { return 0.1f; }, [] (int, int) { return 0.2f; }, nullptr, nullptr }));
    e.setStemMuted (1, true);
    render (e, 2048);
    for (int t = 0; t < numMeterTaps; ++t)
        for (int c = 0; c < 2; ++c)
            (void) e.readMeter ((MeterTap) t, c);
    render (e, block);

    CHECK_THAT (e.readMeter (MeterTap::Original, 0).peak, WithinAbs (0.5, 1e-6));
    CHECK_THAT (e.readMeter (MeterTap::Original, 1).rms, WithinAbs (0.25, 1e-6));
    CHECK_THAT (e.readMeter (MeterTap::StemSum, 0).peak, WithinAbs (0.1, 1e-6));   // Bass muted
    CHECK_THAT (e.readMeter (MeterTap::Drums, 0).peak, WithinAbs (0.1, 1e-6));
    CHECK (e.readMeter (MeterTap::Bass, 0).peak == 0.0f);
    CHECK_THAT (e.readMeter (MeterTap::Output, 0).peak, WithinAbs (0.5, 1e-6));   // x = 0: A
    CHECK (e.readMeter (MeterTap::Original, 0).peak == 0.0f);                      // read-and-reset
}

TEST_CASE ("Live Null Depth over a window around the playhead", "[engine][nulldepth]")
{
    SECTION ("identical A and B -> capped maximum")
    {
        auto s = makeSession (44100, noise (7), { noise (7), nullptr, nullptr, nullptr });
        const auto r = measureLiveNullDepth (*s, 20000);
        CHECK (r.nullDepthDb == maxNullDepthDb);
        CHECK_FALSE (r.quiet);
    }

    SECTION ("known residual -> expected dB")
    {
        auto a = sine (440.0, 0.5f);
        auto s = makeSession (88200, a, { [a] (int ch, int n) { return 0.9f * a (ch, n); }, nullptr, nullptr, nullptr });
        CHECK_THAT (measureLiveNullDepth (*s, 44100).nullDepthDb, WithinAbs (20.0, 0.1));

        // Mute/Solo never enter: the Residual uses all four Stems regardless (a pure function of the Session).
        auto s2 = makeSession (88200, a, { [a] (int ch, int n) { return 0.45f * a (ch, n); },
                                           [a] (int ch, int n) { return 0.54f * a (ch, n); }, nullptr, nullptr });
        CHECK_THAT (measureLiveNullDepth (*s2, 44100).nullDepthDb, WithinAbs (40.0, 0.1));   // residual 0.01 A
    }

    SECTION ("window near the edges stays inside the track; silence is quiet")
    {
        auto s = makeSession (10000, [] (int, int) { return 0.0f; }, {});
        const auto r0 = measureLiveNullDepth (*s, 0);
        const auto r1 = measureLiveNullDepth (*s, 1000000);
        CHECK (r0.quiet);
        CHECK (r1.quiet);
        CHECK (std::isfinite (r0.nullDepthDb));
    }
}

TEST_CASE ("Session swap: playhead resets, transport pauses, old session released off the audio thread", "[engine][session]")
{
    SECTION ("single-threaded: swap while playing fades out without a click")
    {
        PlaybackEngine e;
        auto first = makeSession (200000, sine (1000.0, 1.0f), {});
        std::weak_ptr<const Session> weakFirst = first;
        startSettled (e, std::move (first));
        CHECK (e.isPlaying());

        e.setSession (makeSession (50000, sine (500.0, 1.0f), {}));
        const auto o = render (e, 2048);
        CHECK_FALSE (e.isPlaying());
        CHECK (e.getPlayheadSample() == 0);
        CHECK (e.getSessionLength() == 50000);
        CHECK (maxJump (o.l) < 0.15f);
        CHECK (o.l.back() == 0.0f);

        CHECK_FALSE (weakFirst.expired());   // retired, but only the UI thread frees it...
        e.releaseRetiredSessions();
        CHECK (weakFirst.expired());          // ...here
    }

    SECTION ("a pending session replaced before the audio thread took it is released at once")
    {
        PlaybackEngine e;
        auto a = noiseSession (1000);
        std::weak_ptr<const Session> weakA = a;
        e.setSession (std::move (a));
        e.setSession (noiseSession (2000));
        CHECK (weakA.expired());
        render (e, block);
        CHECK (e.getSessionLength() == 2000);
    }

    SECTION ("nullptr clears the session: silence")
    {
        PlaybackEngine e;
        startSettled (e, noiseSession());
        e.setSession (nullptr);
        render (e, 2048);
        const auto o = render (e, 2048);
        CHECK (e.getSessionLength() == 0);
        CHECK (std::all_of (o.l.begin(), o.l.end(), [] (float x) { return x == 0.0f; }));
        e.play();
        render (e, block);
        CHECK_FALSE (e.isPlaying());
    }

    SECTION ("unload while playing a session with Stems (Unload all): fade out, empty engine, freed on the UI thread")
    {
        PlaybackEngine e;
        auto s = makeSession (200000, sine (1000.0, 1.0f), { sine (1000.0, 0.5f), nullptr, nullptr, nullptr });
        std::weak_ptr<const Session> weak = s;
        startSettled (e, std::move (s));
        REQUIRE (e.isPlaying());
        REQUIRE (e.sessionHasStems());

        e.pause();
        e.setSession (nullptr);
        const auto o = render (e, 2048);
        CHECK (maxJump (o.l) < 0.15f);                  // faded, no click
        CHECK (o.l.back() == 0.0f);
        CHECK_FALSE (e.isPlaying());
        CHECK (e.getSessionLength() == 0);
        CHECK (e.getSessionSampleRate() == 0.0);
        CHECK_FALSE (e.sessionHasStems());
        CHECK (e.getPlayheadSample() == 0);

        e.readMeter (MeterTap::Output, 0);       // (peaks are read-and-reset: drop the fade-out's)
        const auto silent = render (e, 4096);
        CHECK (std::all_of (silent.l.begin(), silent.l.end(), [] (float x) { return x == 0.0f; }));
        CHECK (std::all_of (silent.r.begin(), silent.r.end(), [] (float x) { return x == 0.0f; }));
        CHECK (e.readMeter (MeterTap::Output, 0).peak == 0.0f);

        CHECK_FALSE (weak.expired());           // retired by the audio thread, never freed there...
        e.releaseRetiredSessions();
        CHECK (weak.expired());                 // ...but here, on the UI thread
    }

    SECTION ("threaded: audio callback loop while the UI swaps, plays, seeks")
    {
        PlaybackEngine e;
        e.prepare (rate, 256);
        std::atomic<bool> stop { false };
        std::atomic<int> blocks { 0 };

        std::thread audio ([&]
        {
            std::vector<float> L (256), R (256), X (256);
            float* chans[3] = { L.data(), R.data(), X.data() };
            while (! stop.load())
            {
                e.process (chans, 3, 256);
                blocks.fetch_add (1);
            }
        });

        std::vector<std::shared_ptr<const Session>> pool;
        for (int i = 0; i < 4; ++i)
            pool.push_back (makeSession (5000 + 1000 * i, noise (i), { noise (10 + i), nullptr, nullptr, nullptr }));

        for (int i = 0; i < 400; ++i)
        {
            e.setSession (pool[(size_t) i % pool.size()]);
            e.play();
            if (i % 3 == 0) e.seek (i * 7);
            if (i % 5 == 0) e.setLoopRegion (i, i + 2000);
            e.setLoopEnabled (i % 2 == 0);
            e.releaseRetiredSessions();
            const int target = blocks.load() + 2;
            while (blocks.load() < target) std::this_thread::yield();
        }

        // Final swap: a fresh session; once the audio thread has taken it, the playhead is 0 and paused.
        auto last = makeSession (12345, noise (99), {});
        e.setSession (last);
        const int target = blocks.load() + 20;
        while (blocks.load() < target) std::this_thread::yield();
        stop = true;
        audio.join();

        CHECK (e.getSessionLength() == 12345);
        CHECK (e.getPlayheadSample() == 0);
        CHECK_FALSE (e.isPlaying());
        e.releaseRetiredSessions();
        pool.clear();
    }
}

TEST_CASE ("Device edge cases: extra channels, no session, null pointers, rate mismatch", "[engine][device]")
{
    SECTION ("extra output channels are zeroed")
    {
        PlaybackEngine e;
        startSettled (e, noiseSession());
        std::vector<std::vector<float>> bufs (4, std::vector<float> (block, 0.75f));
        float* chans[4] = { bufs[0].data(), bufs[1].data(), bufs[2].data(), bufs[3].data() };
        e.process (chans, 4, block);
        CHECK (std::all_of (bufs[2].begin(), bufs[2].end(), [] (float x) { return x == 0.0f; }));
        CHECK (std::all_of (bufs[3].begin(), bufs[3].end(), [] (float x) { return x == 0.0f; }));
        CHECK (std::any_of (bufs[0].begin(), bufs[0].end(), [] (float x) { return x != 0.0f && x != 0.75f; }));
    }

    SECTION ("no session -> silence; nullptr buffers do not crash")
    {
        PlaybackEngine e;
        e.prepare (rate, block);
        e.play();
        std::vector<float> L (block, 0.5f), R (block, 0.5f);
        float* chans[2] = { L.data(), R.data() };
        e.process (chans, 2, block);
        CHECK (std::all_of (L.begin(), L.end(), [] (float x) { return x == 0.0f; }));
        CHECK_FALSE (e.isPlaying());

        e.process (nullptr, 2, block);
        float* nulls[2] = { nullptr, R.data() };
        e.setSession (noiseSession());
        e.play();
        e.process (nulls, 2, block);
        e.process (chans, 0, block);
        e.process (chans, 2, 0);
        SUCCEED();
    }

    SECTION ("a session at another rate than the device plays through the realtime resampler")
    {
        PlaybackEngine e;
        e.prepare (48000.0, block);
        e.setSession (noiseSession());   // 44.1 kHz
        e.play();
        const auto o = render (e, 4800);
        CHECK (e.isResampling());
        CHECK (e.isPlaying());
        CHECK (e.getDeviceSampleRate() == 48000.0);
        CHECK (e.getSessionSampleRate() == rate);
        CHECK (std::any_of (o.l.begin(), o.l.end(), [] (float x) { return x != 0.0f; }));
        // 4800 device samples at 48 kHz = 100 ms = about 4410 source samples (plus the resampler's look-ahead).
        CHECK (e.getPlayheadSample() >= 4410);
        CHECK (e.getPlayheadSample() < 4410 + (juce::int64) std::ceil (e.getResamplerLatencySeconds() * rate) + 200);
    }
}

//==============================================================================
namespace
{
/** An Original-only session and its upgrade with `stems` (aligned, offset 0): both share one A buffer. */
std::pair<std::shared_ptr<const Session>, std::shared_ptr<const Session>> originalThenUpgrade (int length, Gen a, std::array<Gen, 4> stems)
{
    const auto full = makeSession (length, std::move (a), std::move (stems));
    const auto aOnly = Session::originalOnlyOf (*full);
    return { aOnly, Session::withStems (*aOnly, full->decodedStems(), 0, +1) };
}
} // namespace

TEST_CASE ("Session without Stems: A plays, B and the Residual are silent", "[engine][session][upgrade]")
{
    const auto sessions = originalThenUpgrade (100000, sine (440.0, 0.5f), { sine (440.0, 0.25f), nullptr, nullptr, nullptr });
    const auto aOnly = sessions.first;
    PlaybackEngine e;
    startSettled (e, aOnly);
    CHECK_FALSE (e.sessionHasStems());

    SECTION ("A (x = 0) is exactly A")
    {
        const Session& S = *aOnly;
        checkSteady (e, [&] (int ch, juce::int64 n) { return A (S, ch, n); }, 0.0f);
    }

    SECTION ("B (x = 1) is silence, the transport still runs")
    {
        e.setCrossfade (1.0f);
        render (e, 2048);
        const auto p0 = e.getPlayheadSample();
        const auto o = render (e, 4096);
        CHECK (std::all_of (o.l.begin(), o.l.end(), [] (float x) { return x == 0.0f; }));
        CHECK (e.isPlaying());
        CHECK (e.getPlayheadSample() == p0 + 4096);
    }

    SECTION ("the Residual is silence, not A")
    {
        e.setListenMode (ListenMode::Residual);
        render (e, 2048);
        const auto o = render (e, 4096);
        CHECK (std::all_of (o.l.begin(), o.l.end(), [] (float x) { return x == 0.0f; }));
    }
}

TEST_CASE ("Session upgrade: playhead and play state carry on, B and R fade in without a click", "[engine][session][upgrade]")
{
    // A = 0.5 sine; one Stem = 0.25 of the same sine, so B = 0.25 sine and the Residual = 0.25 sine.
    auto sessions = originalThenUpgrade (200000, sine (440.0, 0.5f), { sine (440.0, 0.25f), nullptr, nullptr, nullptr });
    const auto full = sessions.second;
    const Session& S = *full;

    PlaybackEngine e;
    std::weak_ptr<const Session> weakAOnly = sessions.first;
    startSettled (e, std::move (sessions.first));

    SECTION ("listening to A: not a single sample changes across the upgrade")
    {
        const auto p0 = e.getPlayheadSample();
        e.upgradeSession (full);
        checkSteady (e, [&] (int ch, juce::int64 n) { return A (S, ch, n); }, 0.0f, 8192);
        CHECK (e.getPlayheadSample() == p0 + 8192);
        CHECK (e.isPlaying());
        CHECK (e.sessionHasStems());
    }

    for (const bool residual : { false, true })
    {
        DYNAMIC_SECTION ((residual ? "listening to R" : "listening to B") << ": silence, then a click-free fade-in")
        {
            if (residual)
                e.setListenMode (ListenMode::Residual);
            else
                e.setCrossfade (1.0f);

            render (e, 2048);   // gain ramps settle; still silent without Stems
            const auto p0 = e.getPlayheadSample();
            const auto before = render (e, 1024);
            CHECK (std::all_of (before.l.begin(), before.l.end(), [] (float x) { return x == 0.0f; }));

            e.upgradeSession (full);
            const auto fade = render (e, 4096);

            CHECK (e.isPlaying());
            CHECK (e.getPlayheadSample() == p0 + 1024 + 4096);   // no jump, no restart
            CHECK (std::abs (fade.l.front()) < 0.01f);              // starts from silence...
            CHECK (maxJump (fade.l) < 0.02f);                       // ...and ramps without a step (sine slope ~0.016)
            CHECK (maxJump (fade.r) < 0.02f);

            // After stemsFadeSeconds the output is exactly B (or exactly the Residual): 0.25 sine.
            const int fadeLen = (int) std::lround (PlaybackEngine::stemsFadeSeconds * rate);
            const auto p1 = p0 + 1024;
            int bad = 0;
            for (int i = fadeLen + 1; i < 4096; ++i)
            {
                const float want = residual ? A (S, 0, p1 + i) - St (S, 0, 0, p1 + i) : St (S, 0, 0, p1 + i);
                bad += fade.l[(size_t) i] == want ? 0 : 1;
            }
            CHECK (bad == 0);

            // Halfway through the fade it is about half way up.
            const int mid = fadeLen / 2;
            const float full0 = residual ? A (S, 0, p1 + mid) - St (S, 0, 0, p1 + mid) : St (S, 0, 0, p1 + mid);
            CHECK_THAT (fade.l[(size_t) mid], WithinAbs (0.5f * full0, 0.02));
        }
    }

    SECTION ("paused: the upgrade keeps it paused at the same sample")
    {
        e.pause();
        render (e, 2048);
        const auto p0 = e.getPlayheadSample();
        e.upgradeSession (full);
        render (e, 2048);
        CHECK_FALSE (e.isPlaying());
        CHECK (e.getPlayheadSample() == p0);
        CHECK (e.sessionHasStems());
    }

    // The Original-only session is retired and freed on the UI thread only.
    render (e, block);
    e.releaseRetiredSessions();
    CHECK (weakAOnly.expired());
}

TEST_CASE ("Session upgrade edge cases: pending swap, stale upgrade", "[engine][session][upgrade]")
{
    SECTION ("an upgrade that arrives while the swap is still pending applies to the incoming session")
    {
        PlaybackEngine e;
        startSettled (e, makeSession (100000, sine (1000.0, 0.5f), {}));
        const auto sessions = originalThenUpgrade (50000, noise (1), { noise (2), nullptr, nullptr, nullptr });
        e.setSession (sessions.first, Handover::continueFromStart);
        e.upgradeSession (sessions.second);
        render (e, 4096);
        CHECK (e.getSessionLength() == 50000);
        CHECK (e.sessionHasStems());
        CHECK (e.isPlaying());
    }

    SECTION ("threaded: replace + upgrade back to back, many times, while the audio thread runs")
    {
        PlaybackEngine e;
        e.prepare (rate, 256);
        std::atomic<bool> stop { false };
        std::atomic<int> blocks { 0 };

        std::thread audio ([&]
        {
            std::vector<float> L (256), R (256);
            float* chans[2] = { L.data(), R.data() };
            while (! stop.load())
            {
                e.process (chans, 2, 256);
                blocks.fetch_add (1);
            }
        });

        std::vector<std::pair<std::shared_ptr<const Session>, std::shared_ptr<const Session>>> pairs;
        for (int i = 0; i < 4; ++i)
            pairs.push_back (originalThenUpgrade (3000 + 1000 * i, noise (i), { noise (20 + i), nullptr, nullptr, nullptr }));

        e.play();
        for (int i = 0; i < 300; ++i)
        {
            const auto& p = pairs[(size_t) i % pairs.size()];
            e.setSession (p.first, Handover::continueFromStart);
            e.upgradeSession (p.second);      // right behind it: must never be dropped as stale
            e.releaseRetiredSessions();
            const int target = blocks.load() + (i % 3);
            while (blocks.load() < target) std::this_thread::yield();
        }

        const int target = blocks.load() + 20;
        while (blocks.load() < target) std::this_thread::yield();
        stop = true;
        audio.join();

        CHECK (e.getSessionLength() == 3000 + 1000 * (299 % 4));
        CHECK (e.sessionHasStems());
        e.releaseRetiredSessions();
    }

    SECTION ("an upgrade for another Original is dropped (and released)")
    {
        PlaybackEngine e;
        const auto sessions = originalThenUpgrade (50000, noise (1), { noise (2), nullptr, nullptr, nullptr });
        startSettled (e, sessions.first);

        auto other = originalThenUpgrade (50000, noise (3), { noise (4), nullptr, nullptr, nullptr });
        std::weak_ptr<const Session> weakOther = other.second;
        e.upgradeSession (std::move (other.second));
        other.first.reset();
        const auto p0 = e.getPlayheadSample();
        render (e, 1024);
        CHECK_FALSE (e.sessionHasStems());
        CHECK (e.getPlayheadSample() == p0 + 1024);
        e.releaseRetiredSessions();
        CHECK (weakOther.expired());
    }
}

TEST_CASE ("Session handover: continue from 0 or at the same position, with a fade", "[engine][session][handover]")
{
    SECTION ("new Original while playing: fade out, swap, keep playing from 0")
    {
        PlaybackEngine e;
        startSettled (e, makeSession (200000, sine (1000.0, 1.0f), {}));
        render (e, 10000);

        e.setSession (makeSession (50000, sine (500.0, 1.0f), {}), Handover::continueFromStart);
        const auto o = render (e, 4096);
        CHECK (e.isPlaying());
        CHECK (e.getSessionLength() == 50000);
        CHECK (e.getPlayheadSample() > 2048);
        CHECK (e.getPlayheadSample() < 4096);
        CHECK (maxJump (o.l) < 0.15f);
    }

    SECTION ("same Original: keeps playing at the same position")
    {
        PlaybackEngine e;
        auto first = makeSession (200000, sine (1000.0, 1.0f), { sine (1000.0, 0.5f), nullptr, nullptr, nullptr });
        startSettled (e, first);
        render (e, 10000);
        const auto p0 = e.getPlayheadSample();

        e.setSession (Session::originalOnlyOf (*first), Handover::continueAtPosition);
        const auto o = render (e, 4096);
        CHECK (e.isPlaying());
        CHECK_FALSE (e.sessionHasStems());
        CHECK (e.getPlayheadSample() >= p0 + 4096 - 2 * e.getTransportFadeSamples() - block);
        CHECK (e.getPlayheadSample() <= p0 + 4096);
        CHECK (maxJump (o.l) < 0.15f);
    }

    SECTION ("paused: stays paused (at 0 for a new Original)")
    {
        PlaybackEngine e;
        startSettled (e, makeSession (200000, sine (1000.0, 1.0f), {}));
        e.pause();
        render (e, 2048);
        e.setSession (makeSession (50000, sine (500.0, 1.0f), {}), Handover::continueFromStart);
        render (e, 2048);
        CHECK_FALSE (e.isPlaying());
        CHECK (e.getPlayheadSample() == 0);
    }

    SECTION ("Pause pressed during the swap fade is honoured")
    {
        PlaybackEngine e;
        startSettled (e, makeSession (200000, sine (1000.0, 1.0f), {}));
        e.setSession (makeSession (50000, sine (500.0, 1.0f), {}), Handover::continueFromStart);
        render (e, 64);    // the swap fade has begun
        e.pause();
        render (e, 4096);
        CHECK_FALSE (e.isPlaying());
        CHECK (e.getPlayheadSample() == 0);
    }
}
