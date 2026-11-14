#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "decode/Decode.h"

namespace
{
    constexpr double pi = 3.14159265358979323846;

    juce::AudioBuffer<float> makeSine (double rate, double freq, double seconds, float amp = 0.5f, int channels = 2)
    {
        const int n = (int) std::llround (rate * seconds);
        juce::AudioBuffer<float> b (channels, n);

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, amp * (float) std::sin (2.0 * pi * freq * (double) i / rate));

        return b;
    }

    int expectedLength (int n, double from, double to)
    {
        return (int) std::llround ((double) n * to / from);
    }

    double rmsRange (const juce::AudioBuffer<float>& b, int ch, int start, int end)
    {
        double sum = 0.0;

        for (int i = start; i < end; ++i)
            sum += (double) b.getSample (ch, i) * (double) b.getSample (ch, i);

        return std::sqrt (sum / (double) (end - start));
    }

    /** Frequency estimate from linearly-interpolated positive-going zero crossings in [start, end). */
    double crossingFrequency (const juce::AudioBuffer<float>& b, int ch, double rate, int start, int end)
    {
        std::vector<double> times;

        for (int i = start; i + 1 < end; ++i)
        {
            const double a = b.getSample (ch, i), c = b.getSample (ch, i + 1);

            if (a < 0.0 && c >= 0.0)
                times.push_back (((double) i + (-a) / (c - a)) / rate);
        }

        REQUIRE (times.size() > 10);
        return (double) (times.size() - 1) / (times.back() - times.front());
    }
}

TEST_CASE ("resample with equal rates is an exact copy", "[resample]")
{
    const auto in = makeSine (44100.0, 1000.0, 0.1);
    const auto out = stemab::resample (in, 44100.0, 44100.0);

    REQUIRE (out.getNumChannels() == in.getNumChannels());
    REQUIRE (out.getNumSamples() == in.getNumSamples());

    for (int ch = 0; ch < in.getNumChannels(); ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            REQUIRE (out.getSample (ch, i) == in.getSample (ch, i));
}

TEST_CASE ("resample output length is round(n * to / from)", "[resample]")
{
    const std::pair<double, double> rates[] = { { 44100.0, 48000.0 }, { 48000.0, 44100.0 },
                                                { 44100.0, 96000.0 }, { 96000.0, 44100.0 },
                                                { 44100.0, 88200.0 } };

    for (auto [from, to] : rates)
        for (int n : { 1, 2, 100, 4410, 44100, 44101, 12345, 100003 })
        {
            INFO (from << " -> " << to << ", n = " << n);
            juce::AudioBuffer<float> in (2, n);
            in.clear();
            const auto out = stemab::resample (in, from, to);
            CHECK (out.getNumChannels() == 2);
            CHECK (out.getNumSamples() == expectedLength (n, from, to));
        }

    CHECK (stemab::resample (juce::AudioBuffer<float> (2, 0), 44100.0, 48000.0).getNumSamples() == 0);
    CHECK (stemab::resample (juce::AudioBuffer<float> (2, 100), 0.0, 48000.0).getNumSamples() == 0);
}

TEST_CASE ("resampling keeps a 1 kHz sine's frequency and amplitude", "[resample]")
{
    const std::pair<double, double> rates[] = { { 44100.0, 48000.0 }, { 48000.0, 44100.0 } };

    for (auto [from, to] : rates)
    {
        INFO (from << " -> " << to);
        const auto in = makeSine (from, 1000.0, 2.0, 0.5f);
        const auto out = stemab::resample (in, from, to);
        REQUIRE (out.getNumSamples() == expectedLength (in.getNumSamples(), from, to));

        const int start = (int) (0.25 * to), end = (int) (1.75 * to);
        const double expectedRms = 0.5 / std::sqrt (2.0);

        for (int ch = 0; ch < 2; ++ch)
        {
            const double db = 20.0 * std::log10 (rmsRange (out, ch, start, end) / expectedRms);
            CHECK (std::abs (db) < 0.05);
            CHECK_THAT (crossingFrequency (out, ch, to, start, end), Catch::Matchers::WithinAbs (1000.0, 0.05));
        }
    }
}

TEST_CASE ("resampled sine matches the ideal sine sample for sample (zero latency)", "[resample]")
{
    const std::pair<double, double> rates[] = { { 44100.0, 48000.0 }, { 48000.0, 44100.0 }, { 44100.0, 96000.0 } };

    for (auto [from, to] : rates)
    {
        INFO (from << " -> " << to);
        const auto in = makeSine (from, 1000.0, 1.0, 0.5f, 1);
        const auto out = stemab::resample (in, from, to);

        double maxErr = 0.0;

        for (int i = (int) (0.1 * to); i < (int) (0.9 * to); ++i)
        {
            const double ideal = 0.5 * std::sin (2.0 * pi * 1000.0 * (double) i / to);
            maxErr = std::max (maxErr, std::abs ((double) out.getSample (0, i) - ideal));
        }

        CHECK (maxErr < 1.0e-3);
    }
}

TEST_CASE ("resample 44.1k -> 48k -> 44.1k round trip has no latency (cross-correlation peak at lag 0)", "[resample]")
{
    // Broadband noise so the correlation peak is sharp.
    const int n = 44100;
    juce::AudioBuffer<float> in (1, n);
    std::mt19937 rng (12345);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);

    for (int i = 0; i < n; ++i)
        in.setSample (0, i, dist (rng));

    const auto back = stemab::resample (stemab::resample (in, 44100.0, 48000.0), 48000.0, 44100.0);
    REQUIRE (back.getNumSamples() == n);

    int bestLag = 0;
    double bestValue = -1.0e30;

    for (int lag = -8; lag <= 8; ++lag)
    {
        double sum = 0.0;

        for (int i = 2000; i < n - 2000; ++i)
            sum += (double) in.getSample (0, i) * (double) back.getSample (0, i + lag);

        if (sum > bestValue)
        {
            bestValue = sum;
            bestLag = lag;
        }
    }

    CHECK (std::abs (bestLag) <= 1);
    CHECK (bestLag == 0);
}

TEST_CASE ("every channel is resampled identically", "[resample]")
{
    const auto mono = makeSine (44100.0, 3000.0, 0.5, 0.4f, 1);
    juce::AudioBuffer<float> stereo (2, mono.getNumSamples());
    stereo.copyFrom (0, 0, mono, 0, 0, mono.getNumSamples());
    stereo.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples());

    const auto out = stemab::resample (stereo, 44100.0, 48000.0);

    for (int i = 0; i < out.getNumSamples(); ++i)
        REQUIRE (out.getSample (0, i) == out.getSample (1, i));
}
