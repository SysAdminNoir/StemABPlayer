// StemAB_Probe - headless Alignment + Track Null Depth report for Original / Stem File pairs.
//
//   StemAB_Probe <original> <stemfile>     one pair
//   StemAB_Probe                           the three test pairs in "Audio Files/" and "Stem Files/"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "analysis/Alignment.h"
#include "analysis/NullDepth.h"
#include "decode/Decode.h"

static_assert (std::is_same_v<stemab::StemBuffers, decltype (stemab::DecodedStems::stems)>,
               "Analysis and Decode must agree on the Stem buffer type");

namespace
{
    using Clock = std::chrono::steady_clock;

    double secondsSince (Clock::time_point t0)
    {
        return std::chrono::duration<double> (Clock::now() - t0).count();
    }

    std::string grouped (juce::int64 v)
    {
        auto digits = std::to_string (v < 0 ? -v : v);
        for (int i = (int) digits.size() - 3; i > 0; i -= 3)
            digits.insert ((size_t) i, ",");
        return (v < 0 ? "-" : "") + digits;
    }

    std::string signedGrouped (juce::int64 v)   { return (v > 0 ? "+" : "") + grouped (v); }

    std::string mmss (double seconds)
    {
        const auto whole = (long long) std::floor (std::max (0.0, seconds));
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%02lld:%02lld", whole / 60, whole % 60);
        return buf;
    }

    std::string mmssTenths (double seconds)
    {
        const auto tenths = (long long) std::llround (std::max (0.0, seconds) * 10.0);
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%02lld:%02lld.%lld", tenths / 600, (tenths / 10) % 60, tenths % 10);
        return buf;
    }

    double linearToDb (double g)   { return g > 0.0 ? 20.0 * std::log10 (g) : -999.0; }

    /** DIAGNOSTIC ONLY (not part of the Analysis API): Null Depth after low-passing A and the Residual with
        the same 16th-order Butterworth at `cutoffHz`. Filtering is linear, so LP(A) - LP(B) = LP(A - B):
        this is exactly "A and B low-passed identically". Separates codec bandwidth loss from separation error. */
    double lowPassedNullDepthDb (const juce::AudioBuffer<float>& original, const stemab::StemBuffers& aligned,
                                 double sampleRate, double cutoffHz)
    {
        auto sections = juce::dsp::FilterDesign<double>::designIIRLowpassHighOrderButterworthMethod (cutoffHz, sampleRate, 16);
        double energyA = 0.0, energyR = 0.0;

        for (int ch = 0; ch < original.getNumChannels(); ++ch)
        {
            std::vector<juce::dsp::IIR::Filter<double>> filterA, filterR;
            for (auto* c : sections)
            {
                filterA.emplace_back (c);
                filterR.emplace_back (c);
            }

            const float* a = original.getReadPointer (ch);

            for (int n = 0; n < original.getNumSamples(); ++n)
            {
                double b = 0.0;
                for (const auto& s : aligned)
                    if (ch < s.getNumChannels() && n < s.getNumSamples())
                        b += (double) s.getSample (ch, n);

                double x = (double) a[n];
                double r = x - b;

                for (size_t i = 0; i < filterA.size(); ++i)
                {
                    x = filterA[i].processSample (x);
                    r = filterR[i].processSample (r);
                }

                energyA += x * x;
                energyR += r * r;
            }
        }

        return stemab::nullDepthDbFromEnergies (energyA, energyR);
    }

    bool runPair (const juce::File& originalFile, const juce::File& stemFile)
    {
        std::printf ("\n=== %s  <->  %s\n", originalFile.getFileName().toRawUTF8(), stemFile.getFileName().toRawUTF8());

        auto t0 = Clock::now();
        auto original = stemab::decodeOriginal (originalFile);
        const double tOriginal = secondsSince (t0);

        if (! original.ok)
        {
            std::printf ("Original: FAILED: %s\n", original.error.toRawUTF8());
            return false;
        }

        t0 = Clock::now();
        auto decoded = stemab::decodeStemFile (stemFile);
        const double tStems = secondsSince (t0);

        if (! decoded.ok)
        {
            std::printf ("Stem File: FAILED: %s\n", decoded.error.toRawUTF8());
            return false;
        }

        const double fs = original.sampleRate;
        const auto lenA = (juce::int64) original.audio.getNumSamples();

        std::printf ("Original : %s samples @ %.0f Hz, %d ch (%s)   decode %.2f s\n",
                     grouped (lenA).c_str(), fs, original.audio.getNumChannels(),
                     mmssTenths ((double) lenA / fs).c_str(), tOriginal);

        auto stems = std::move (decoded.stems);
        const auto lenStemsNative = (juce::int64) stems[0].getNumSamples();
        std::printf ("Stems    : %s samples @ %.0f Hz, %d ch (%s vs Original)   decode %.2f s\n",
                     grouped (lenStemsNative).c_str(), decoded.sampleRate, stems[0].getNumChannels(),
                     signedGrouped (lenStemsNative - lenA).c_str(), tStems);

        if (decoded.sampleRate != fs)
        {
            t0 = Clock::now();
            for (auto& s : stems)
                s = stemab::resample (s, decoded.sampleRate, fs);
            std::printf ("           rates differ: Stems resampled %.0f -> %.0f Hz (%s samples)   %.2f s\n",
                         decoded.sampleRate, fs, grouped (stems[0].getNumSamples()).c_str(), secondsSince (t0));
        }

        const auto lenB = (juce::int64) stems[0].getNumSamples();

        // --- Alignment -------------------------------------------------------------------------------
        t0 = Clock::now();
        const auto align = stemab::measureAlignment (original.audio, stems, fs);
        const double tAlign = secondsSince (t0);

        if (! align.ok)
        {
            std::printf ("Alignment: FAILED: %s\n", align.error.toRawUTF8());
            return false;
        }

        std::printf ("Alignment Offset: %s samples (%.2f ms), polarity %+d, confidence %.4f  %s   (%.2f s)\n",
                     signedGrouped (align.offsetSamples).c_str(), 1000.0 * (double) align.offsetSamples / fs,
                     align.polarity, align.confidence, align.lowConfidence ? "[LOW CONFIDENCE]" : "[ok]", tAlign);
        std::printf ("  window  start      length     lag      rho       peak/2nd  sub-sample lag (diag)\n");

        int index = 1;
        for (const auto& w : align.windows)
        {
            if (! w.used)
                std::printf ("  %-6d  %-9s  %-9s  skipped (near-silent)\n", index++,
                             mmssTenths ((double) w.start / fs).c_str(), grouped (w.length).c_str());
            else
                std::printf ("  %-6d  %-9s  %-9s  %+6lld   %+.5f  %8.3f  %+.3f\n", index++,
                             mmssTenths ((double) w.start / fs).c_str(), grouped (w.length).c_str(),
                             (long long) w.lagSamples, w.correlation, w.peakRatio, w.subSampleLag);
        }

        std::printf ("  after alignment the Stems' last %s samples fall past A's end; A's first %s samples have no Stem audio\n",
                     grouped (std::max<juce::int64> (0, lenB - align.offsetSamples - lenA)).c_str(),
                     grouped (std::max<juce::int64> (0, -align.offsetSamples)).c_str());

        // --- Track Null Depth ---------------------------------------------------------------------------
        t0 = Clock::now();
        const auto aligned = stemab::alignStems (stems, align.offsetSamples, align.polarity, lenA);
        const auto nd = stemab::computeNullDepth (original.audio, aligned, fs);
        const double tNull = secondsSince (t0);

        std::printf ("Track Null Depth: %.2f dB   (align + null %.2f s)\n", nd.trackNullDepthDb, tNull);
        std::printf ("Least-squares gain <A,B>/<B,B> (diagnostic, NOT applied): %.5f (%+.3f dB); "
                     "gain-matched Null Depth would be %.2f dB\n",
                     nd.leastSquaresGain, linearToDb (nd.leastSquaresGain), nd.gainMatchedNullDepthDb);

        std::vector<std::pair<float, size_t>> blocks;
        for (size_t i = 0; i < nd.perSecondNullDepthDb.size(); ++i)
            if (! stemab::isQuietBlock (nd.perSecondNullDepthDb[i]))
                blocks.emplace_back (nd.perSecondNullDepthDb[i], i);

        std::sort (blocks.begin(), blocks.end());
        const auto numQuiet = nd.perSecondNullDepthDb.size() - blocks.size();

        if (blocks.empty())
        {
            std::printf ("Per-second Null Depth: all %zu blocks quiet\n", nd.perSecondNullDepthDb.size());
        }
        else
        {
            const auto mid = blocks.size() / 2;
            const double median = blocks.size() % 2 == 1 ? blocks[mid].first
                                                         : 0.5 * ((double) blocks[mid - 1].first + blocks[mid].first);
            std::printf ("Per-second Null Depth (%zu blocks, %zu quiet < -70 dBFS excluded): min %.2f / median %.2f / max %.2f dB\n",
                         nd.perSecondNullDepthDb.size(), numQuiet, (double) blocks.front().first, median,
                         (double) blocks.back().first);
            std::printf ("  worst seconds:");
            for (size_t i = 0; i < std::min<size_t> (3, blocks.size()); ++i)
                std::printf ("%s %s %.2f dB", i == 0 ? "" : ",", mmss ((double) blocks[i].second).c_str(),
                             (double) blocks[i].first);
            std::printf ("\n");
        }

        // --- Diagnostic only -----------------------------------------------------------------------------
        t0 = Clock::now();
        const double lp = lowPassedNullDepthDb (original.audio, aligned, fs, 15000.0);
        std::printf ("[DIAGNOSTIC ONLY, not part of the API] Null Depth with A and B both low-passed at 15 kHz "
                     "(identical 16th-order Butterworth): %.2f dB   (%.2f s)\n", lp, secondsSince (t0));
        return true;
    }
} // namespace

int main (int argc, char* argv[])
{
    std::printf ("StemAB_Probe - %s\n", stemab::ffmpegVersionString().c_str());

    if (argc == 3)
        return runPair (juce::File (juce::String::fromUTF8 (argv[1])), juce::File (juce::String::fromUTF8 (argv[2]))) ? 0 : 1;

    if (argc != 1)
    {
        std::printf ("usage: StemAB_Probe <original> <stemfile>\n"
                     "       StemAB_Probe              (runs the three test pairs from the repo folders)\n");
        return 2;
    }

    const char* pairs[][2] =
    {
        { "03 Heresy.flac", "03 Nine Inch Nails - Heresy.stem.m4a" },
        { "And One - Bodypop - 06 Body Company.flac", "And One - Bodypop - 06 Body Company.stem.m4a" },
        { "Boy Harsher - Careful - 02 Face the Fire.flac", "Boy Harsher - Careful - 02 Face the Fire.stem.m4a" },
    };

    const juce::File repo (juce::String::fromUTF8 (STEMAB_REPO_DIR));
    int failures = 0;

    for (const auto& p : pairs)
        if (! runPair (repo.getChildFile ("Audio Files").getChildFile (juce::String::fromUTF8 (p[0])),
                       repo.getChildFile ("Stem Files").getChildFile (juce::String::fromUTF8 (p[1]))))
            ++failures;

    return failures == 0 ? 0 : 1;
}
