#include "analysis/WaveformSummary.h"

#include <algorithm>
#include <cmath>

namespace stemab
{

template <typename FillChunk>
WaveformSummary WaveformSummary::buildWith (int numChannels, std::int64_t length, int binSize,
                                            const CancelCheck& cancel, FillChunk&& fill)
{
    WaveformSummary s;
    if (length <= 0 || binSize <= 0 || numChannels <= 0)
        return s;

    const std::int64_t numBins = (length + binSize - 1) / binSize;
    s.length = length;
    s.binSize = binSize;
    s.peaks.resize ((size_t) numBins);
    s.meanSquares.resize ((size_t) numBins);

    std::vector<float> chunk ((size_t) binSize);

    for (std::int64_t b = 0; b < numBins; ++b)
    {
        if ((b & 255) == 0 && isCancelled (cancel))
            return {};

        const std::int64_t start = b * binSize;
        const int n = (int) std::min<std::int64_t> (binSize, length - start);

        float peak = 0.0f;
        double sumSq = 0.0;
        int channelsUsed = 0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (! fill (ch, start, n, chunk.data()))
                continue;

            ++channelsUsed;
            for (int i = 0; i < n; ++i)
            {
                const float x = chunk[(size_t) i];
                peak = std::max (peak, std::abs (x));
                sumSq += (double) x * (double) x;
            }
        }

        s.peaks[(size_t) b] = peak;
        s.meanSquares[(size_t) b] = channelsUsed > 0 ? (float) (sumSq / ((double) channelsUsed * (double) n)) : 0.0f;
    }

    return s;
}

WaveformSummary WaveformSummary::build (const float* const* channels, int numChannels, std::int64_t length,
                                        int binSize, const CancelCheck& cancel)
{
    if (channels == nullptr)
        return {};

    return buildWith (numChannels, length, binSize, cancel,
                      [channels] (int ch, std::int64_t start, int n, float* dest)
                      {
                          const float* src = channels[ch];
                          if (src == nullptr)
                              return false;
                          std::copy (src + start, src + start + n, dest);
                          return true;
                      });
}

WaveformSummary WaveformSummary::buildResidual (const float* const* original, int numChannels,
                                                const float* const* const* stems, int numStems,
                                                std::int64_t length, int binSize, const CancelCheck& cancel)
{
    if (original == nullptr)
        return {};

    return buildWith (numChannels, length, binSize, cancel,
                      [original, stems, numStems] (int ch, std::int64_t start, int n, float* dest)
                      {
                          const float* a = original[ch];
                          for (int i = 0; i < n; ++i)
                          {
                              const std::int64_t k = start + i;
                              double r = a != nullptr ? (double) a[k] : 0.0;
                              for (int st = 0; st < numStems; ++st)
                                  if (stems != nullptr && stems[st] != nullptr && stems[st][ch] != nullptr)
                                      r -= (double) stems[st][ch][k];
                              dest[i] = (float) r;
                          }
                          return true;
                      });
}

std::int64_t WaveformSummary::binSamples (int index) const noexcept
{
    const std::int64_t start = (std::int64_t) index * binSize;
    return std::min<std::int64_t> (binSize, length - start);
}

WaveformSummary::Level WaveformSummary::getBin (int index) const noexcept
{
    if (index < 0 || index >= getNumBins())
        return {};
    return { peaks[(size_t) index], std::sqrt (meanSquares[(size_t) index]) };
}

WaveformSummary::Level WaveformSummary::getLevel (std::int64_t startSample, std::int64_t endSample) const noexcept
{
    startSample = std::max<std::int64_t> (0, startSample);
    endSample = std::min (endSample, length);
    if (isEmpty() || endSample <= startSample)
        return {};

    const int b0 = (int) (startSample / binSize);
    const int b1 = (int) std::min<std::int64_t> (getNumBins(), (endSample + binSize - 1) / binSize);

    float peak = 0.0f;
    double weighted = 0.0;
    std::int64_t samples = 0;

    for (int b = b0; b < b1; ++b)
    {
        const std::int64_t n = binSamples (b);
        peak = std::max (peak, peaks[(size_t) b]);
        weighted += (double) meanSquares[(size_t) b] * (double) n;
        samples += n;
    }

    return { peak, samples > 0 ? (float) std::sqrt (weighted / (double) samples) : 0.0f };
}

} // namespace stemab
