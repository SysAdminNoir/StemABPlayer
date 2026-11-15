#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "analysis/WaveformSummary.h"

using stemab::WaveformSummary;
using Catch::Matchers::WithinAbs;

namespace
{
    std::vector<float> sine (int n, double cyclesPerSample, float amplitude = 1.0f)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amplitude * (float) std::sin (2.0 * 3.14159265358979323846 * cyclesPerSample * (i + 0.25 / cyclesPerSample));
        return v;
    }
}

TEST_CASE ("waveform summary of silence is all zeros", "[waveform]")
{
    std::vector<float> l (1000, 0.0f), r (1000, 0.0f);
    const float* ch[] = { l.data(), r.data() };
    const auto s = WaveformSummary::build (ch, 2, 1000, 256);

    REQUIRE (s.getNumBins() == 4);
    for (int b = 0; b < s.getNumBins(); ++b)
    {
        CHECK (s.getBin (b).peak == 0.0f);
        CHECK (s.getBin (b).rms == 0.0f);
    }
    const auto all = s.getLevel (0, 1000);
    CHECK (all.peak == 0.0f);
    CHECK (all.rms == 0.0f);
}

TEST_CASE ("waveform summary of a full-scale sine: peak 1, rms 0.707", "[waveform]")
{
    // 64 samples per cycle, so every 256-sample bin holds whole cycles and its peak sample is exactly 1.
    const int n = 256 * 40;
    const auto l = sine (n, 1.0 / 64.0), r = sine (n, 1.0 / 64.0);
    const float* ch[] = { l.data(), r.data() };
    const auto s = WaveformSummary::build (ch, 2, n, 256);

    REQUIRE (s.getNumBins() == 40);
    const auto all = s.getLevel (0, n);
    CHECK_THAT (all.peak, WithinAbs (1.0, 1.0e-4));
    CHECK_THAT (all.rms, WithinAbs (std::sqrt (0.5), 1.0e-4));

    const auto oneBin = s.getBin (7);
    CHECK_THAT (oneBin.peak, WithinAbs (1.0, 1.0e-4));
    CHECK_THAT (oneBin.rms, WithinAbs (std::sqrt (0.5), 1.0e-4));
}

TEST_CASE ("waveform summary bins: boundaries and the last partial bin", "[waveform]")
{
    // 600 samples, bin 256 -> bins of 256, 256, 88. A single spike in each bin, at its edges.
    std::vector<float> x (600, 0.0f);
    x[255] = 0.5f;     // last sample of bin 0
    x[256] = -0.25f;   // first sample of bin 1
    x[599] = 0.75f;    // last sample of the partial bin 2
    const float* ch[] = { x.data() };
    const auto s = WaveformSummary::build (ch, 1, 600, 256);

    REQUIRE (s.getNumBins() == 3);
    CHECK (s.getLength() == 600);
    CHECK (s.getBin (0).peak == 0.5f);
    CHECK (s.getBin (1).peak == 0.25f);
    CHECK (s.getBin (2).peak == 0.75f);

    // The partial bin's mean square is over its own 88 samples, not 256.
    CHECK_THAT (s.getBin (2).rms, WithinAbs (std::sqrt (0.75 * 0.75 / 88.0), 1.0e-6));
    CHECK_THAT (s.getBin (0).rms, WithinAbs (std::sqrt (0.5 * 0.5 / 256.0), 1.0e-6));

    // Out-of-range / empty queries.
    CHECK (s.getLevel (600, 700).peak == 0.0f);
    CHECK (s.getLevel (100, 100).peak == 0.0f);
    CHECK (s.getLevel (-50, 10).peak == 0.5f);   // clamped to [0, 10) -> bin 0
    CHECK (s.getBin (3).peak == 0.0f);
}

TEST_CASE ("waveform summary query combines bins (peak = max, rms = weighted)", "[waveform]")
{
    // bin 0 constant 0.5, bin 1 constant 1.0, bin 2 (partial, 128 samples) constant 0.25
    std::vector<float> x (256 + 256 + 128);
    std::fill (x.begin(), x.begin() + 256, 0.5f);
    std::fill (x.begin() + 256, x.begin() + 512, 1.0f);
    std::fill (x.begin() + 512, x.end(), 0.25f);
    const float* ch[] = { x.data(), x.data() };
    const auto s = WaveformSummary::build (ch, 2, (std::int64_t) x.size(), 256);

    const auto first2 = s.getLevel (0, 512);
    CHECK (first2.peak == 1.0f);
    CHECK_THAT (first2.rms, WithinAbs (std::sqrt ((0.25 + 1.0) / 2.0), 1.0e-6));

    const auto all = s.getLevel (0, (std::int64_t) x.size());
    CHECK (all.peak == 1.0f);
    const double ms = (256 * 0.25 + 256 * 1.0 + 128 * 0.0625) / 640.0;
    CHECK_THAT (all.rms, WithinAbs (std::sqrt (ms), 1.0e-6));

    // A range inside one bin reports that whole bin; a range straddling a boundary covers both bins.
    CHECK (s.getLevel (10, 20).peak == 0.5f);
    CHECK (s.getLevel (250, 260).peak == 1.0f);
    CHECK (s.getLevel (600, 640).peak == 0.25f);
}

TEST_CASE ("waveform summary of the residual of identical signals is zero", "[waveform]")
{
    const int n = 5000;
    const auto a = sine (n, 1.0 / 100.0, 0.8f);

    // Four stems that sum exactly to A: A/2, A/4, A/4 and silence.
    std::vector<float> s0 (a), s1 (a), s2 (a), s3 ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        s0[(size_t) i] = a[(size_t) i] * 0.5f;
        s1[(size_t) i] = a[(size_t) i] * 0.25f;
        s2[(size_t) i] = a[(size_t) i] * 0.25f;
    }

    const float* orig[] = { a.data(), a.data() };
    const float* st0[] = { s0.data(), s0.data() };
    const float* st1[] = { s1.data(), s1.data() };
    const float* st2[] = { s2.data(), s2.data() };
    const float* st3[] = { s3.data(), s3.data() };
    const float* const* stems[] = { st0, st1, st2, st3 };

    const auto r = WaveformSummary::buildResidual (orig, 2, stems, 4, n, 256);
    REQUIRE (r.getNumBins() == (n + 255) / 256);
    const auto all = r.getLevel (0, n);
    CHECK (all.peak < 1.0e-6f);
    CHECK (all.rms < 1.0e-6f);

    // And with one stem missing, the residual is that stem.
    const float* const* missing[] = { st0, st1, nullptr, st3 };
    const auto r2 = WaveformSummary::buildResidual (orig, 2, missing, 4, n, 256);
    CHECK_THAT (r2.getLevel (0, n).peak, WithinAbs (0.8 * 0.25, 1.0e-4));
}

TEST_CASE ("waveform summary: degenerate inputs and cancellation give an empty summary", "[waveform]")
{
    std::vector<float> x (1000, 0.1f);
    const float* ch[] = { x.data() };

    CHECK (WaveformSummary::build (ch, 1, 0).isEmpty());
    CHECK (WaveformSummary::build (ch, 1, 1000, 0).isEmpty());
    CHECK (WaveformSummary::build (nullptr, 1, 1000).isEmpty());
    CHECK (WaveformSummary::build (ch, 1, 1000, 256, [] { return true; }).isEmpty());
    CHECK (WaveformSummary().getLevel (0, 100).peak == 0.0f);
}
