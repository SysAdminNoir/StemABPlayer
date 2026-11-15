#include "analysis/Alignment.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <memory>

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

    /** Scratch buffers and the FFT for one measureAlignment() call (reused across its windows). */
    struct Workspace
    {
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> a, b;            // full-rate mono window of A and the Stem Sum segment
        std::vector<float> ad, bd;          // the same, decimated (coarse pass)
        std::vector<float> fa, fb;          // FFT work areas
        std::vector<double> exact;          // exact correlation per lag, NaN = not computed yet
    };

    /** c[j] = sum_n x[n] * y[n + j] for j in [0, y.size() - x.size()], left in fx[j]. fft.getSize() must be
        >= y.size(); then n + j < y.size() <= fftSize, so nothing wraps around. */
    void fftCorrelate (const juce::dsp::FFT& fft, const std::vector<float>& x, const std::vector<float>& y,
                       std::vector<float>& fx, std::vector<float>& fy)
    {
        const int n = fft.getSize();
        fx.assign ((size_t) n * 2, 0.0f);
        fy.assign ((size_t) n * 2, 0.0f);
        std::copy (x.begin(), x.end(), fx.begin());
        std::copy (y.begin(), y.end(), fy.begin());

        fft.performRealOnlyForwardTransform (fx.data(), true);
        fft.performRealOnlyForwardTransform (fy.data(), true);

        // C = conj(FX) * FY
        auto* cx = reinterpret_cast<std::complex<float>*> (fx.data());
        auto* cy = reinterpret_cast<std::complex<float>*> (fy.data());

        for (int k = 0; k <= n / 2; ++k)
            cx[k] = std::conj (cx[k]) * cy[k];

        fft.performRealOnlyInverseTransform (fx.data());
    }

    /** sum x[i] * y[i], i < n, in double precision. */
    double dot (const float* x, const float* y, int n) noexcept
    {
        double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
        int i = 0;

        for (; i + 4 <= n; i += 4)
        {
            s0 += (double) x[i]     * (double) y[i];
            s1 += (double) x[i + 1] * (double) y[i + 1];
            s2 += (double) x[i + 2] * (double) y[i + 2];
            s3 += (double) x[i + 3] * (double) y[i + 3];
        }

        for (; i < n; ++i)
            s0 += (double) x[i] * (double) y[i];

        return (s0 + s1) + (s2 + s3);
    }

    /** out[i] = sum of x[i*factor .. i*factor + factor - 1] (a box filter; A and B get exactly the same one). */
    void decimateInto (const std::vector<float>& x, int factor, std::vector<float>& out)
    {
        const size_t n = x.size() / (size_t) factor;
        out.resize (n);

        for (size_t i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (size_t t = 0; t < (size_t) factor; ++t)
                s += x[i * (size_t) factor + t];
            out[i] = s;
        }
    }

    double ratioOf (double peakAbs, double secondAbs) noexcept
    {
        return secondAbs < 0.0 ? 0.0 : (secondAbs > 0.0 ? std::min (1.0e6, peakAbs / secondAbs) : 1.0e6);
    }

    /** Measures one window: cross-correlates A[start, start+len) with B[start-maxLag, start+len+maxLag). */
    WindowMeasure measureWindow (const juce::AudioBuffer<float>& original,
                                 const juce::AudioBuffer<float>* const* stems, size_t numStems,
                                 juce::int64 start, int length, int maxLag, int decimation,
                                 Workspace& ws)
    {
        WindowMeasure w;
        w.start  = start;
        w.length = length;

        auto& a = ws.a;
        auto& b = ws.b;
        const juce::AudioBuffer<float>* originalList[] = { &original };
        monoSegment (originalList, 1, start, length, a);
        monoSegment (stems, numStems, start - maxLag, length + 2 * maxLag, b);

        if (rms (a) < silenceRms || rms (b) < silenceRms)
            return w;   // near-silent: not used

        w.used = true;

        // c[j] = sum_n a[n] * b[n + j], j = lag + maxLag in [0, numLags).
        const int numLags = 2 * maxLag + 1;
        int peak = 0;
        double peakValue = 0.0;
        double neighbours[2] = { 0.0, 0.0 };   // |c| at peak - 1 and peak + 1 (when they exist)

        if (decimation <= 1)
        {
            fftCorrelate (*ws.fft, a, b, ws.fa, ws.fb);
            const float* c = ws.fa.data();

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

            peakValue = c[peak];
            w.peakRatio = ratioOf (peakAbs, secondAbs);

            if (peak > 0 && peak < numLags - 1)
            {
                neighbours[0] = std::abs (c[peak - 1]);
                neighbours[1] = std::abs (c[peak + 1]);
            }
        }
        else
        {
            // Coarse pass: the same correlation on box-filtered, decimated audio, over the whole lag range.
            const int d = decimation;
            decimateInto (a, d, ws.ad);
            decimateInto (b, d, ws.bd);
            fftCorrelate (*ws.fft, ws.ad, ws.bd, ws.fa, ws.fb);
            const float* cc = ws.fa.data();
            const int numCoarse = (2 * maxLag) / d + 1;   // coarse k <-> full-rate j = k * d

            // The best few coarse peaks (at least 2 coarse samples apart), highest first.
            constexpr int maxCandidates = 3;
            int candidates[maxCandidates] = {};
            int numCandidates = 0;

            for (int c = 0; c < maxCandidates; ++c)
            {
                int best = -1;
                float bestAbs = -1.0f;

                for (int k = 0; k < numCoarse; ++k)
                {
                    bool taken = false;
                    for (int i = 0; i < numCandidates; ++i)
                        taken = taken || std::abs (k - candidates[i]) <= 2;

                    if (! taken && std::abs (cc[k]) > bestAbs)
                    {
                        bestAbs = std::abs (cc[k]);
                        best = k;
                    }
                }

                if (best < 0)
                    break;

                candidates[numCandidates++] = best;
            }

            float secondAbs = -1.0f;
            const int coarseExclusion = std::max (1, peakExclusion / d);
            for (int k = 0; k < numCoarse; ++k)
                if (std::abs (k - candidates[0]) > coarseExclusion)
                    secondAbs = std::max (secondAbs, std::abs (cc[k]));

            w.peakRatio = ratioOf (std::abs (cc[candidates[0]]), secondAbs);

            // Exact pass: the full-rate correlation around each candidate, following the slope if the maximum
            // sits on the edge of the region (so a coarse estimate that is off by more than expected still
            // lands on the true local peak). The largest |c| over all candidates wins.
            ws.exact.assign ((size_t) numLags, std::numeric_limits<double>::quiet_NaN());

            const auto exactAt = [&] (int j)
            {
                auto& v = ws.exact[(size_t) j];
                if (std::isnan (v))
                    v = dot (a.data(), b.data() + j, length);
                return v;
            };

            const int radius = 2 * d;
            double bestAbs = -1.0;

            for (int i = 0; i < numCandidates; ++i)
            {
                int centre = candidates[i] * d;
                int localPeak = std::clamp (centre, 0, numLags - 1);

                for (int iteration = 0; iteration < 64; ++iteration)
                {
                    const int lo = std::max (0, centre - radius);
                    const int hi = std::min (numLags - 1, centre + radius);
                    double localAbs = -1.0;

                    for (int j = lo; j <= hi; ++j)
                    {
                        const double v = std::abs (exactAt (j));
                        if (v > localAbs)
                        {
                            localAbs = v;
                            localPeak = j;
                        }
                    }

                    const bool onInnerEdge = (localPeak == lo && lo > 0) || (localPeak == hi && hi < numLags - 1);
                    if (! onInnerEdge)
                        break;

                    centre = localPeak;
                }

                if (std::abs (exactAt (localPeak)) > bestAbs)
                {
                    bestAbs = std::abs (exactAt (localPeak));
                    peak = localPeak;
                }
            }

            peakValue = exactAt (peak);

            if (peak > 0 && peak < numLags - 1)
            {
                neighbours[0] = std::abs (exactAt (peak - 1));
                neighbours[1] = std::abs (exactAt (peak + 1));
            }
        }

        w.lagSamples = (juce::int64) (peak - maxLag);
        w.polarity   = peakValue < 0.0 ? -1 : +1;

        w.subSampleLag = (double) w.lagSamples;
        if (peak > 0 && peak < numLags - 1)
        {
            const double y0 = neighbours[0], y1 = std::abs (peakValue), y2 = neighbours[1];
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
                                  const AlignmentOptions& options,
                                  const CancelCheck& cancel)
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

    // Coarse pass only when the decimated window is still long enough to correlate meaningfully.
    const int decimation = std::max (1, options.decimation) > 1 && windowLen / std::max (1, options.decimation) >= 256
                               ? options.decimation : 1;

    // The FFT must hold the (decimated) Stem Sum segment: windowLen + 2 * maxLag samples.
    const juce::int64 segment = ((juce::int64) windowLen + 2 * (juce::int64) maxLag) / decimation;
    int order = 1;
    while ((juce::int64 (1) << order) < segment)
        ++order;

    Workspace ws;
    ws.fft = std::make_unique<juce::dsp::FFT> (order);

    const juce::AudioBuffer<float>* stemList[] = { &stems[0], &stems[1], &stems[2], &stems[3] };
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

        if (isCancelled (cancel))
        {
            result = {};
            result.error = "Cancelled.";
            return result;
        }

        starts.push_back (start);
        auto w = measureWindow (original, stemList, std::size (stemList), start, windowLen, maxLag,
                                decimation, ws);
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
