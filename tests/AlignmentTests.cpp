#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>

#include "analysis/Alignment.h"
#include "analysis/NullDepth.h"
#include "decode/Decode.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double fs = 44100.0;

    /** Small windows and a +/-0.25 s search so the synthetic tests stay fast in Debug builds. */
    const stemab::AlignmentOptions fastOptions { 1 << 15, 0.25, 3 };

    juce::AudioBuffer<float> noise (int length, int seed, float amplitude = 0.3f, int channels = 2)
    {
        juce::Random random (seed);
        juce::AudioBuffer<float> b (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int n = 0; n < length; ++n)
                b.setSample (ch, n, amplitude * (2.0f * random.nextFloat() - 1.0f));
        return b;
    }

    /** Sum of sines with slow amplitude movement plus decaying noise bursts every 0.3 s, stereo. */
    juce::AudioBuffer<float> musicLike (int length, int seed)
    {
        const double freqs[] = { 55.0, 110.3, 220.7, 331.0, 440.0, 1234.5, 3456.7 };
        juce::Random random (seed);
        juce::AudioBuffer<float> b (2, length);
        const int burstPeriod = (int) (0.3 * fs);

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int n = 0; n < length; ++n)
            {
                const double t = n / fs;
                double v = 0.0;
                for (size_t k = 0; k < std::size (freqs); ++k)
                {
                    const double lfo = 0.6 + 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * (0.13 + 0.07 * (double) k) * t);
                    v += 0.08 / (1.0 + (double) k * 0.3) * lfo
                         * std::sin (2.0 * juce::MathConstants<double>::pi * freqs[k] * t + (double) (ch + k));
                }

                const int inBurst = n % burstPeriod;
                v += 0.25 * std::exp (-(double) inBurst / (0.01 * fs)) * (2.0 * random.nextDouble() - 1.0);
                b.setSample (ch, n, (float) v);
            }
        }
        return b;
    }

    /** Splits `a` into four Stems that sum to `a` exactly (power-of-two weights rotating every 0.5 s), each
        delayed by `delay` samples (B[m] = part[m - delay]) and `lengthChange` samples longer than `a`. */
    stemab::StemBuffers splitAndDelay (const juce::AudioBuffer<float>& a, juce::int64 delay, int lengthChange = 7168)
    {
        const float weights[] = { 0.5f, 0.25f, 0.125f, 0.125f };
        const int lenA = a.getNumSamples();
        const int lenB = std::max (0, lenA + lengthChange);
        const int block = (int) (0.5 * fs);
        stemab::StemBuffers stems;

        for (int s = 0; s < 4; ++s)
        {
            stems[(size_t) s].setSize (a.getNumChannels(), lenB);
            stems[(size_t) s].clear();

            for (int ch = 0; ch < a.getNumChannels(); ++ch)
                for (int m = 0; m < lenB; ++m)
                {
                    const auto src = (juce::int64) m - delay;
                    if (src >= 0 && src < lenA)
                        stems[(size_t) s].setSample (ch, m, weights[(s + (int) src / block) % 4] * a.getSample (ch, (int) src));
                }
        }
        return stems;
    }

    double energy (const juce::AudioBuffer<float>& b, int from = 0, int to = -1)
    {
        if (to < 0)
            to = b.getNumSamples();
        double e = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int n = from; n < to; ++n)
                e += (double) b.getSample (ch, n) * b.getSample (ch, n);
        return e;
    }

    void checkRecovered (const stemab::AlignmentResult& r, juce::int64 offset, int polarity)
    {
        REQUIRE (r.ok);
        CHECK (r.offsetSamples == offset);
        CHECK (r.polarity == polarity);
        CHECK_FALSE (r.lowConfidence);
        CHECK (r.confidence > 0.95);

        for (const auto& w : r.windows)
        {
            CHECK (w.used);
            CHECK (w.lagSamples == offset);
            CHECK (w.polarity == polarity);
            CHECK (std::abs (w.correlation) > 0.99);
        }
    }
} // namespace

//==============================================================================
TEST_CASE ("alignment recovers known offsets exactly", "[analysis][alignment]")
{
    const auto offset = GENERATE (as<juce::int64> {}, 2112, -500, 7144, 0);
    const int length = (int) (5.0 * fs);

    SECTION ("white noise")
    {
        const auto a = noise (length, 1);
        const auto r = stemab::measureAlignment (a, splitAndDelay (a, offset), fs, fastOptions);
        checkRecovered (r, offset, +1);
        CHECK (r.windows.size() == 3);
    }

    SECTION ("music-like signal")
    {
        const auto a = musicLike (length, 2);
        const auto r = stemab::measureAlignment (a, splitAndDelay (a, offset), fs, fastOptions);
        checkRecovered (r, offset, +1);
        CHECK (r.windows.size() == 3);
    }
}

TEST_CASE ("alignment detects inverted polarity", "[analysis][alignment]")
{
    const auto a = musicLike ((int) (5.0 * fs), 3);
    auto stems = splitAndDelay (a, 2112);
    for (auto& s : stems)
        s.applyGain (-1.0f);

    const auto r = stemab::measureAlignment (a, stems, fs, fastOptions);
    checkRecovered (r, 2112, -1);

    const auto nd = stemab::computeNullDepth (a, stemab::alignStems (stems, r.offsetSamples, r.polarity, a.getNumSamples()), fs);
    CHECK (nd.trackNullDepthDb >= 150.0);
}

TEST_CASE ("a delayed 4-way split nulls completely after alignment", "[analysis][alignment][nulldepth]")
{
    const auto offset = GENERATE (as<juce::int64> {}, 2112, 7144);
    const auto a = musicLike ((int) (5.0 * fs), 4);
    const auto stems = splitAndDelay (a, offset);

    const auto r = stemab::measureAlignment (a, stems, fs, fastOptions);
    REQUIRE (r.ok);
    REQUIRE (r.offsetSamples == offset);

    const auto nd = stemab::computeNullDepth (a, stemab::alignStems (stems, r.offsetSamples, r.polarity, a.getNumSamples()), fs);
    CHECK (nd.trackNullDepthDb >= 150.0);
    CHECK_THAT (nd.leastSquaresGain, WithinAbs (1.0, 1.0e-9));
    CHECK (nd.perSecondNullDepthDb.size() == 5);
    for (float v : nd.perSecondNullDepthDb)
        CHECK (v >= 150.0f);

    SECTION ("misaligned by one sample it does not")
    {
        const auto off = stemab::computeNullDepth (a, stemab::alignStems (stems, r.offsetSamples + 1, r.polarity, a.getNumSamples()), fs);
        CHECK (off.trackNullDepthDb < 40.0);
    }
}

TEST_CASE ("residual noise at a known level gives the expected null depth", "[analysis][nulldepth]")
{
    const double targetDb = GENERATE (30.0, 12.0);
    const auto a = musicLike ((int) (5.0 * fs), 5);
    auto e = noise (a.getNumSamples(), 77);
    e.applyGain ((float) std::sqrt (energy (a) / energy (e) / std::pow (10.0, targetDb / 10.0)));

    // Put the noise into one Stem (Vocals) before the delay, as separation error would be.
    auto parts = splitAndDelay (a, 0, 0);
    for (int ch = 0; ch < 2; ++ch)
        parts[3].addFrom (ch, 0, e, ch, 0, e.getNumSamples());

    stemab::StemBuffers stems;
    for (size_t s = 0; s < 4; ++s)
    {
        stems[s].setSize (2, a.getNumSamples() + 7168);
        stems[s].clear();
        for (int ch = 0; ch < 2; ++ch)
            stems[s].copyFrom (ch, 2112, parts[s], ch, 0, a.getNumSamples());
    }

    const auto r = stemab::measureAlignment (a, stems, fs, fastOptions);
    REQUIRE (r.ok);
    REQUIRE (r.offsetSamples == 2112);

    const auto nd = stemab::computeNullDepth (a, stemab::alignStems (stems, r.offsetSamples, r.polarity, a.getNumSamples()), fs);
    CHECK_THAT (nd.trackNullDepthDb, WithinAbs (targetDb, 0.1));
    CHECK_THAT (nd.leastSquaresGain, WithinAbs (1.0 / (1.0 + std::pow (10.0, -targetDb / 10.0)), 0.01));
}

TEST_CASE ("a level difference shows in the least-squares gain but is never applied", "[analysis][nulldepth]")
{
    const auto a = musicLike ((int) (3.0 * fs), 6);
    auto stems = splitAndDelay (a, 0, 0);
    for (auto& s : stems)
        s.applyGain (0.5f);   // exact in float

    const auto nd = stemab::computeNullDepth (a, stems, fs);
    CHECK_THAT (nd.leastSquaresGain, WithinAbs (2.0, 1.0e-9));
    CHECK_THAT (nd.trackNullDepthDb, WithinAbs (10.0 * std::log10 (4.0), 1.0e-6));   // residual = A/2
    CHECK (nd.gainMatchedNullDepthDb > 100.0);
}

TEST_CASE ("per-second null depth marks quiet blocks and measures each block", "[analysis][nulldepth]")
{
    const int sec = (int) fs;
    auto a = noise (4 * sec, 8);
    for (int ch = 0; ch < 2; ++ch)
        a.clear (ch, 3 * sec, sec);   // last second silent

    auto stems = splitAndDelay (a, 0, 0);
    auto e = noise (sec, 9);
    e.applyGain ((float) std::sqrt (energy (a, sec, 2 * sec) / energy (e) / 100.0));   // 20 dB below block 1
    for (int ch = 0; ch < 2; ++ch)
        stems[0].addFrom (ch, sec, e, ch, 0, sec);

    const auto nd = stemab::computeNullDepth (a, stems, fs);
    REQUIRE (nd.perSecondNullDepthDb.size() == 4);
    CHECK (nd.perSecondNullDepthDb[0] >= 150.0f);
    CHECK_THAT ((double) nd.perSecondNullDepthDb[1], WithinAbs (20.0, 0.01));
    CHECK (nd.perSecondNullDepthDb[2] >= 150.0f);
    CHECK (stemab::isQuietBlock (nd.perSecondNullDepthDb[3]));
    CHECK_THAT (nd.trackNullDepthDb, WithinAbs (10.0 * std::log10 (energy (a) / energy (e)), 0.01));   // ~24.8 dB
}

TEST_CASE ("alignStems shifts, pads and trims every Stem identically", "[analysis][alignment]")
{
    stemab::StemBuffers stems;
    for (int s = 0; s < 4; ++s)
    {
        stems[(size_t) s].setSize (s == 3 ? 1 : 2, 10);   // channel counts are preserved
        for (int ch = 0; ch < stems[(size_t) s].getNumChannels(); ++ch)
            for (int n = 0; n < 10; ++n)
                stems[(size_t) s].setSample (ch, n, (float) (100 * s + 10 * ch + n + 1));
    }

    auto expectShift = [&] (juce::int64 offset, int polarity, int timeline)
    {
        const auto out = stemab::alignStems (stems, offset, polarity, timeline);
        for (int s = 0; s < 4; ++s)
        {
            const auto& o = out[(size_t) s];
            REQUIRE (o.getNumSamples() == timeline);
            REQUIRE (o.getNumChannels() == stems[(size_t) s].getNumChannels());
            for (int ch = 0; ch < o.getNumChannels(); ++ch)
                for (int n = 0; n < timeline; ++n)
                {
                    const auto src = n + offset;
                    const float expected = (src >= 0 && src < 10)
                                             ? (polarity < 0 ? -1.0f : 1.0f) * stems[(size_t) s].getSample (ch, (int) src)
                                             : 0.0f;
                    CHECK (o.getSample (ch, n) == expected);
                }
        }
    };

    SECTION ("positive offset: B is later, moved earlier, tail trimmed")  { expectShift (3, +1, 8); }
    SECTION ("negative offset: B is earlier, front zero-padded")          { expectShift (-2, +1, 12); }
    SECTION ("inverted polarity")                                          { expectShift (4, -1, 10); }
    SECTION ("offset past either end gives silence")                       { expectShift (20, +1, 10); expectShift (-20, +1, 10); }
    SECTION ("empty timeline")                                             { expectShift (0, +1, 0); }
    SECTION ("timeline longer than the Stems")                             { expectShift (0, +1, 15); }
}

TEST_CASE ("uncorrelated noise is flagged low-confidence", "[analysis][alignment]")
{
    const auto a = noise ((int) (5.0 * fs), 10);
    const auto r = stemab::measureAlignment (a, splitAndDelay (noise ((int) (5.0 * fs), 11), 0), fs, fastOptions);
    REQUIRE (r.ok);
    CHECK (r.lowConfidence);
    CHECK (r.confidence < 0.3);
}

TEST_CASE ("silence and empty input don't crash and stay finite", "[analysis][alignment][nulldepth]")
{
    const int length = (int) (3.0 * fs);
    juce::AudioBuffer<float> silent (2, length);
    silent.clear();

    SECTION ("both silent")
    {
        const auto stems = splitAndDelay (silent, 0);
        const auto r = stemab::measureAlignment (silent, stems, fs, fastOptions);
        CHECK_FALSE (r.ok);
        CHECK (r.lowConfidence);
        CHECK (r.offsetSamples == 0);
        CHECK (r.polarity == 1);
        CHECK (r.error.isNotEmpty());

        const auto nd = stemab::computeNullDepth (silent, stemab::alignStems (stems, 0, 1, length), fs);
        CHECK (nd.trackNullDepthDb == stemab::maxNullDepthDb);
        CHECK (nd.leastSquaresGain == 0.0);
        for (float v : nd.perSecondNullDepthDb)
            CHECK (stemab::isQuietBlock (v));
    }

    SECTION ("silent Original, audible Stems")
    {
        const auto stems = splitAndDelay (noise (length, 12), 0);
        CHECK_FALSE (stemab::measureAlignment (silent, stems, fs, fastOptions).ok);

        const auto nd = stemab::computeNullDepth (silent, stems, fs);
        CHECK (nd.trackNullDepthDb == stemab::minNullDepthDb);
        CHECK (std::isfinite (nd.leastSquaresGain));
        CHECK (std::isfinite (nd.gainMatchedNullDepthDb));
    }

    SECTION ("empty buffers")
    {
        juce::AudioBuffer<float> empty;
        stemab::StemBuffers noStems;
        CHECK_FALSE (stemab::measureAlignment (empty, splitAndDelay (silent, 0), fs).ok);
        CHECK_FALSE (stemab::measureAlignment (noise (1000, 13), noStems, fs).ok);
        CHECK_FALSE (stemab::measureAlignment (noise (1000, 13), splitAndDelay (noise (1000, 13), 0), 0.0).ok);

        const auto nd = stemab::computeNullDepth (empty, noStems, fs);
        CHECK (nd.perSecondNullDepthDb.empty());
        CHECK (std::isfinite (nd.trackNullDepthDb));
    }
}

TEST_CASE ("alignment copes with short files, mismatched lengths and quiet windows", "[analysis][alignment]")
{
    SECTION ("very short file: one shrunken window, default options")
    {
        const auto a = noise (1000, 14);
        const auto r = stemab::measureAlignment (a, splitAndDelay (a, 37, 100), fs);
        REQUIRE (r.ok);
        CHECK (r.offsetSamples == 37);
        CHECK (r.windows.size() == 1);
        CHECK (r.windows[0].length == 1000);
        CHECK_FALSE (r.lowConfidence);
    }

    SECTION ("Stems shorter than the Original")
    {
        const auto a = musicLike ((int) (5.0 * fs), 15);
        const auto r = stemab::measureAlignment (a, splitAndDelay (a, 100, -3000), fs, fastOptions);
        checkRecovered (r, 100, +1);
    }

    SECTION ("a silent passage at 20 % is skipped and replaced by another window")
    {
        auto a = musicLike ((int) (10.0 * fs), 16);
        for (int ch = 0; ch < 2; ++ch)
            a.clear (ch, (int) (1.0 * fs), (int) (2.0 * fs));   // 1 s .. 3 s silent, around the 20 % window

        const auto r = stemab::measureAlignment (a, splitAndDelay (a, 2112), fs, fastOptions);
        REQUIRE (r.ok);
        CHECK (r.offsetSamples == 2112);
        CHECK_FALSE (r.lowConfidence);
        CHECK_FALSE (r.windows[0].used);

        int used = 0;
        for (const auto& w : r.windows)
            used += w.used ? 1 : 0;
        CHECK (used == 3);
    }
}

//==============================================================================
namespace
{
    struct RealPair { const char* original; const char* stemFile; };

    const RealPair realPairs[] =
    {
        { "03 Heresy.flac", "03 Nine Inch Nails - Heresy.stem.m4a" },
        { "And One - Bodypop - 06 Body Company.flac", "And One - Bodypop - 06 Body Company.stem.m4a" },
        { "Boy Harsher - Careful - 02 Face the Fire.flac", "Boy Harsher - Careful - 02 Face the Fire.stem.m4a" },
    };

    juce::File testDataFile (const char* folder, const char* name)
    {
        return juce::File (juce::String::fromUTF8 (STEMAB_TEST_DATA_DIR))
                   .getChildFile (folder)
                   .getChildFile (juce::String::fromUTF8 (name));
    }
} // namespace

TEST_CASE ("each real pair aligns with high confidence", "[analysis][alignment][realfiles]")
{
    const auto& pair = realPairs[(size_t) GENERATE (0, 1, 2)];
    const auto originalFile = testDataFile ("Audio Files", pair.original);
    const auto stemFile = testDataFile ("Stem Files", pair.stemFile);

    if (! originalFile.existsAsFile() || ! stemFile.existsAsFile())
        SKIP ("test files not present: " << pair.original);

    const auto original = stemab::decodeOriginal (originalFile);
    auto decoded = stemab::decodeStemFile (stemFile);
    REQUIRE (original.ok);
    REQUIRE (decoded.ok);
    REQUIRE (decoded.sampleRate == original.sampleRate);

    const auto r = stemab::measureAlignment (original.audio, decoded.stems, original.sampleRate);
    REQUIRE (r.ok);
    CHECK_FALSE (r.lowConfidence);

    // ADR 0003: every test pair aligns at exactly +6,522 samples, polarity +1, all windows agreeing.
    CHECK (r.offsetSamples == 6522);
    CHECK (r.polarity == 1);

    int used = 0;
    for (const auto& w : r.windows)
    {
        if (! w.used)
            continue;
        ++used;
        CHECK (w.lagSamples == r.offsetSamples);
        CHECK (w.polarity == r.polarity);
        CHECK (w.correlation > 0.999);
    }
    CHECK (used == 3);

    // The previous full-rate reference (3 x 2^20 windows, +/-2 s, one FFT each) agrees.
    const auto reference = stemab::measureAlignment (original.audio, decoded.stems, original.sampleRate, { 1 << 20, 2.0, 3, 1 });
    REQUIRE (reference.ok);
    CHECK (reference.offsetSamples == r.offsetSamples);
    CHECK (reference.polarity == r.polarity);

    const auto aligned = stemab::alignStems (decoded.stems, r.offsetSamples, r.polarity, original.audio.getNumSamples());
    const auto nd = stemab::computeNullDepth (original.audio, aligned, original.sampleRate);
    CHECK (std::isfinite (nd.trackNullDepthDb));

    WARN (pair.original << ": offset " << r.offsetSamples << ", polarity " << r.polarity
                        << ", confidence " << r.confidence << ", Track Null Depth " << nd.trackNullDepthDb
                        << " dB, least-squares gain " << nd.leastSquaresGain);
}

//==============================================================================
TEST_CASE ("the coarse pass + exact refine finds what the full-rate FFT finds", "[analysis][alignment]")
{
    const auto offset = GENERATE (as<juce::int64> {}, 2112, -500, 7144, 0, 1, -3);
    const int length = (int) (5.0 * fs);
    const bool music = GENERATE (false, true);
    const auto a = music ? musicLike (length, 20) : noise (length, 21);
    const auto stems = splitAndDelay (a, offset);

    auto exactOptions = fastOptions;
    exactOptions.decimation = 1;
    auto coarseOptions = fastOptions;
    coarseOptions.decimation = 4;

    const auto exact = stemab::measureAlignment (a, stems, fs, exactOptions);
    const auto coarse = stemab::measureAlignment (a, stems, fs, coarseOptions);
    checkRecovered (exact, offset, +1);
    checkRecovered (coarse, offset, +1);

    REQUIRE (exact.windows.size() == coarse.windows.size());
    for (size_t i = 0; i < exact.windows.size(); ++i)
    {
        CHECK (coarse.windows[i].lagSamples == exact.windows[i].lagSamples);
        CHECK (coarse.windows[i].polarity == exact.windows[i].polarity);
        CHECK_THAT (coarse.windows[i].correlation, WithinAbs (exact.windows[i].correlation, 1.0e-9));
    }
}

TEST_CASE ("default options: +/-0.5 s search, 2^16 windows, coarse decimation 4", "[analysis][alignment]")
{
    const stemab::AlignmentOptions defaults;
    CHECK (defaults.windowSamples == 1 << 16);
    CHECK (defaults.maxLagSeconds == 0.5);
    CHECK (defaults.numWindows == 3);
    CHECK (defaults.decimation == 4);

    // A delay near the edge of the +/-0.5 s range is still found exactly; beyond it is flagged.
    const auto a = musicLike ((int) (8.0 * fs), 22);
    checkRecovered (stemab::measureAlignment (a, splitAndDelay (a, 22000), fs), 22000, +1);
    checkRecovered (stemab::measureAlignment (a, splitAndDelay (a, -21000), fs), -21000, +1);
}

TEST_CASE ("alignment stops when cancelled", "[analysis][alignment]")
{
    const auto a = noise ((int) (5.0 * fs), 23);
    const auto r = stemab::measureAlignment (a, splitAndDelay (a, 100), fs, fastOptions, [] { return true; });
    CHECK_FALSE (r.ok);
    CHECK (r.error == "Cancelled.");
}

TEST_CASE ("Track Null Depth of unaligned Stems + offset equals that of aligned Stems, bit for bit", "[analysis][nulldepth]")
{
    const auto offset = GENERATE (as<juce::int64> {}, 2112, -500, 0);
    const int polarity = GENERATE (1, -1);
    const auto a = musicLike ((int) (3.0 * fs), 24);
    auto stems = splitAndDelay (noise ((int) (3.0 * fs), 25), offset, 3000);   // a residual that is not zero

    const auto viaCopies = stemab::computeNullDepth (a, stemab::alignStems (stems, offset, polarity, a.getNumSamples()), fs);
    const auto direct = stemab::computeNullDepth (a, stems, offset, polarity, fs);

    CHECK (direct.trackNullDepthDb == viaCopies.trackNullDepthDb);
    CHECK (direct.leastSquaresGain == viaCopies.leastSquaresGain);
    CHECK (direct.gainMatchedNullDepthDb == viaCopies.gainMatchedNullDepthDb);
    CHECK (direct.perSecondNullDepthDb == viaCopies.perSecondNullDepthDb);
}

// Hidden: run in Release with  StemAB_Tests "[.timing]"
TEST_CASE ("alignment of a real pair takes at most 50 ms", "[.timing][realfiles]")
{
    const auto& pair = realPairs[0];
    const auto originalFile = testDataFile ("Audio Files", pair.original);
    const auto stemFile = testDataFile ("Stem Files", pair.stemFile);

    if (! originalFile.existsAsFile() || ! stemFile.existsAsFile())
        SKIP ("test files not present: " << pair.original);

    const auto original = stemab::decodeOriginal (originalFile);
    const auto decoded = stemab::decodeStemFile (stemFile);
    REQUIRE (original.ok);
    REQUIRE (decoded.ok);

    double best = 1.0e9;
    for (int i = 0; i < 5; ++i)
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto r = stemab::measureAlignment (original.audio, decoded.stems, original.sampleRate);
        best = std::min (best, juce::Time::getMillisecondCounterHiRes() - t0);
        REQUIRE (r.offsetSamples == 6522);
    }

    WARN ("measureAlignment: " << best << " ms");
    CHECK (best <= 50.0);
}
