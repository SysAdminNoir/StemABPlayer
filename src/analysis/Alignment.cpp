#include "analysis/Alignment.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

#include <juce_dsp/juce_dsp.h>

namespace stemab
{

namespace
{
    constexpr double silenceRms          = 0.001;   // -60 dBFS: windows quieter than this are skipped
    constexpr double minAbsCorrelation   = 0.7;     // below this |Pearson| the alignment is low-confidence
    constexpr int    peakExclusion       = 100;     // samples around the main peak ignored for peakRatio
    const double     candidateFractions[] = { 0.2, 0.5, 0.8, 0.35, 0.65, 0.1, 0.9 };
    constexpr int    numPrimaryFractions = 3;       // 20 %, 50 %, 80 %; the rest are fallbacks for quiet windows

    /** Mono downmix (average of channels) of `buffers` summed, for samples [start, start + length) of their
        timeline; samples outside a buffer are zero. */
    void monoSegment (const juce::AudioBuffer<float>* const* buffers, size_t numBuffers,
                      juce::int64 start, int length, std::vector<float>& out)
    {
        out.assign ((size_t) length, 0.0f);

        for (size_t b = 0; b < numBuffers; ++b)
        {
            const auto& buf = *buffers[b];
            const int numCh = buf.getNumChannels();
            const auto bufLen = (juce::int64) buf.getNumSamples();

            if (numCh == 0 || bufLen == 0)
                continue;

            const auto from = std::max<juce::int64> (start, 0);
            const auto to   = std::min<juce::int64> (start + length, bufLen);

            if (to <= from)
                continue;

            const float scale = 1.0f / (float) numCh;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* src = buf.getReadPointer (ch);
                float* dst = out.data() + (from - start);
                juce::FloatVectorOperations::addWithMultiply (dst, src + from, scale, (int) (to - from));
            }
        }
    }

    double rms (const std::vector<float>& x)
    {
        if (x.empty())
            return 0.0;

        double sum = 0.0;
        for (float v : x)
            sum += (double) v * v;

        return std::sqrt (sum / (double) x.size());
    }

    /** Pearson coefficient of a[n] and b[n + shift] over the n where both exist. */
    double pearson (const std::vector<float>& a, const std::vector<float>& b, juce::int64 shift)
    {
        const auto nFrom = std::max<juce::int64> (0, -shift);
        const auto nTo   = std::min<juce::int64> ((juce::int64) a.size(), (juce::int64) b.size() - shift);

        if (nTo - nFrom < 2)
            return 0.0;

        double sa = 0.0, sb = 0.0, saa = 0.0, sbb = 0.0, sab = 0.0;

        for (auto n = nFrom; n < nTo; ++n)
        {
            const double x = a[(size_t) n];
            const double y = b[(size_t) (n + shift)];
            sa += x;  sb += y;  saa += x * x;  sbb += y * y;  sab += x * y;
        }

        const double count = (double) (nTo - nFrom);
        const double cov = sab - sa * sb / count;
        const double va  = saa - sa * sa / count;
        const double vb  = sbb - sb * sb / count;

        if (! (va > 0.0) || ! (vb > 0.0))
            return 0.0;

        return std::clamp (cov / std::sqrt (va * vb), -1.0, 1.0);
    }

    /** Measures one window: cross-correlates A[start, start+len) with B[start-maxLag, start+len+maxLag). */
    WindowMeasure measureWindow (const juce::AudioBuffer<float>& original,
                                 const juce::AudioBuffer<float>* const* stems, size_t numStems,
                                 juce::int64 start, int length, int maxLag,
                                 const juce::dsp::FFT& fft,
                                 std::vector<float>& a, std::vector<float>& b,
                                 std::vector<float>& fa, std::vector<float>& fb)
    {
        WindowMeasure w;
        w.start  = start;
        w.length = length;

        const juce::AudioBuffer<float>* originalList[] = { &original };
        monoSegment (originalList, 1, start, length, a);
        monoSegment (stems, numStems, start - maxLag, length + 2 * maxLag, b);

        if (rms (a) < silenceRms || rms (b) < silenceRms)
            return w;   // near-silent: not used

        w.used = true;

        const int n = fft.getSize();
        fa.assign ((size_t) n * 2, 0.0f);
        fb.assign ((size_t) n * 2, 0.0f);
        std::copy (a.begin(), a.end(), fa.begin());
        std::copy (b.begin(), b.end(), fb.begin());

        fft.performRealOnlyForwardTransform (fa.data(), true);
        fft.performRealOnlyForwardTransform (fb.data(), true);

        // c[j] = sum_n a[n] * b[n + j]  <=>  C = conj(FA) * FB. No circular wrap for j in [0, 2*maxLag]
        // because n + j < length + 2*maxLag <= fftSize.
        auto* ca = reinterpret_cast<std::complex<float>*> (fa.data());
        auto* cb = reinterpret_cast<std::complex<float>*> (fb.data());

        for (int k = 0; k <= n / 2; ++k)
            ca[k] = std::conj (ca[k]) * cb[k];

        fft.performRealOnlyInverseTransform (fa.data());
        const float* c = fa.data();   // c[j], j = lag + maxLag

        const int numLags = 2 * maxLag + 1;
        int peak = 0;
        float peakAbs = -1.0f;

        for (int j = 0; j < numLags; ++j)
        {
            const float v = std::abs (c[j]);
            if (v > peakAbs)
            {
                peakAbs = v;
                peak = j;
            }
        }

        float secondAbs = -1.0f;
        for (int j = 0; j < numLags; ++j)
            if (std::abs (j - peak) > peakExclusion)
                secondAbs = std::max (secondAbs, std::abs (c[j]));

        w.lagSamples = (juce::int64) (peak - maxLag);
        w.polarity   = c[peak] < 0.0f ? -1 : +1;
        w.peakRatio  = secondAbs < 0.0f ? 0.0
                                        : (secondAbs > 0.0f ? std::min (1.0e6, (double) peakAbs / secondAbs) : 1.0e6);

        w.subSampleLag = (double) w.lagSamples;
        if (peak > 0 && peak < numLags - 1)
        {
            const double y0 = std::abs (c[peak - 1]), y1 = peakAbs, y2 = std::abs (c[peak + 1]);
            const double denom = y0 - 2.0 * y1 + y2;
            if (denom < 0.0)
                w.subSampleLag += std::clamp (0.5 * (y0 - y2) / denom, -0.5, 0.5);
        }

        // b[m] is B at (start - maxLag + m), so B[start + i + lag] is b[i + lag + maxLag].
        w.correlation = pearson (a, b, w.lagSamples + maxLag);
        return w;
    }

    double median (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;

        std::sort (v.begin(), v.end());
        const auto mid = v.size() / 2;
        return v.size() % 2 == 1 ? v[mid] : 0.5 * (v[mid - 1] + v[mid]);
    }
} // namespace

//==============================================================================
AlignmentResult measureAlignment (const juce::AudioBuffer<float>& original,
                                  const StemBuffers& stems,
                                  double sampleRate,
                                  const AlignmentOptions& options)
{
    AlignmentResult result;

    const auto lenA = (juce::int64) original.getNumSamples();
    juce::int64 lenB = 0;
    for (const auto& s : stems)
        lenB = std::max (lenB, (juce::int64) s.getNumSamples());

    if (original.getNumChannels() == 0 || lenA == 0)
    {
        result.error = "The Original has no audio.";
        return result;
    }

    if (lenB == 0)
    {
        result.error = "The Stems have no audio.";
        return result;
    }

    if (! (sampleRate > 0.0) || ! (options.maxLagSeconds >= 0.0) || options.windowSamples < 1)
    {
        result.error = "Invalid sample rate or alignment options.";
        return result;
    }

    const int maxLag = (int) std::min (std::round (options.maxLagSeconds * sampleRate), 1.0e7);
    const int windowLen = (int) std::min<juce::int64> (options.windowSamples, lenA);

    int order = 1;
    while ((juce::int64 (1) << order) < (juce::int64) windowLen + 2 * (juce::int64) maxLag)
        ++order;

    const juce::dsp::FFT fft (order);

    const juce::AudioBuffer<float>* stemList[] = { &stems[0], &stems[1], &stems[2], &stems[3] };
    std::vector<float> a, b, fa, fb;
    std::vector<juce::int64> starts;
    int usedCount = 0;
    const int wanted = std::max (1, options.numWindows);

    for (size_t i = 0; i < std::size (candidateFractions); ++i)
    {
        // Fallback fractions only while fewer than `wanted` usable windows exist.
        if ((int) i >= numPrimaryFractions && usedCount >= wanted)
            break;

        const double centre = candidateFractions[i] * (double) lenA;
        const auto start = std::clamp ((juce::int64) std::llround (centre - windowLen / 2.0),
                                       juce::int64 (0), lenA - windowLen);

        if (std::find (starts.begin(), starts.end(), start) != starts.end())
            continue;   // short files: several fractions collapse onto the same window

        starts.push_back (start);
        auto w = measureWindow (original, stemList, std::size (stemList), start, windowLen, maxLag,
                                fft, a, b, fa, fb);
        usedCount += w.used ? 1 : 0;
        result.windows.push_back (w);
    }

    std::vector<juce::int64> lags;
    for (const auto& w : result.windows)
        if (w.used)
            lags.push_back (w.lagSamples);

    if (lags.empty())
    {
        result.error = "The Original or the Stem Sum is near-silent (< -60 dBFS) everywhere; nothing to align.";
        return result;
    }

    // Consensus: most common lag; ties go to the lag nearest the median.
    std::vector<juce::int64> sorted (lags);
    std::sort (sorted.begin(), sorted.end());
    const double medianLag = sorted.size() % 2 == 1 ? (double) sorted[sorted.size() / 2]
                                                    : 0.5 * (double) (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]);
    juce::int64 best = sorted.front();
    long bestCount = 0;

    for (auto lag : sorted)
    {
        const auto count = (long) std::count (sorted.begin(), sorted.end(), lag);
        if (count > bestCount
            || (count == bestCount && std::abs ((double) lag - medianLag) < std::abs ((double) best - medianLag)))
        {
            best = lag;
            bestCount = count;
        }
    }

    std::vector<double> agreeingCorrelations;
    int positive = 0, negative = 0;

    for (const auto& w : result.windows)
    {
        if (w.used && w.lagSamples == best)
        {
            agreeingCorrelations.push_back (std::abs (w.correlation));
            (w.polarity > 0 ? positive : negative) += 1;
        }
    }

    const double agreement = (double) agreeingCorrelations.size() / (double) lags.size();
    const double medianCorrelation = median (agreeingCorrelations);

    result.ok = true;
    result.offsetSamples = best;
    result.polarity = negative > positive ? -1 : +1;
    result.confidence = std::clamp (agreement * medianCorrelation, 0.0, 1.0);
    result.lowConfidence = agreement < 1.0
                        || (positive > 0 && negative > 0)
                        || medianCorrelation < minAbsCorrelation
                        || (maxLag > 0 && std::abs (best) >= maxLag);
    return result;
}

//==============================================================================
StemBuffers alignStems (const StemBuffers& stems,
                        juce::int64 offsetSamples,
                        int polarity,
                        juce::int64 timelineLength)
{
    const float gain = polarity < 0 ? -1.0f : 1.0f;
    const auto length = (int) std::clamp<juce::int64> (timelineLength, 0, std::numeric_limits<int>::max());
    StemBuffers out;

    for (size_t s = 0; s < stems.size(); ++s)
    {
        const auto& src = stems[s];
        auto& dst = out[s];
        dst.setSize (src.getNumChannels(), length);
        dst.clear();

        const auto srcLen = (juce::int64) src.getNumSamples();
        // out[n] = gain * src[n + offset] for n in [nFrom, nTo).
        const auto nFrom = std::max<juce::int64> (0, -offsetSamples);
        const auto nTo   = std::min<juce::int64> (length, srcLen - offsetSamples);

        if (nTo <= nFrom)
            continue;

        for (int ch = 0; ch < src.getNumChannels(); ++ch)
            juce::FloatVectorOperations::copyWithMultiply (dst.getWritePointer (ch) + nFrom,
                                                           src.getReadPointer (ch) + nFrom + offsetSamples,
                                                           gain, (int) (nTo - nFrom));
    }

    return out;
}

} // namespace stemab
