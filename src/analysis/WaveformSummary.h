#pragma once

#include <cstdint>
#include <vector>

#include "common/Cancel.h"

namespace stemab
{

/** A fixed-resolution overview of a (stereo) signal, for drawing waveforms at any width.

    The signal is cut into bins of `binSize` samples (the last bin may be shorter). Per bin it keeps the
    peak (max |x| over every channel) and the mean square (over every channel and sample of the bin). A query
    for any sample range combines the bins it touches: the peak is their maximum, the RMS is the square root
    of their sample-weighted mean square. So a pixel column covering many bins is exact, and one narrower
    than a bin shows that bin.

    Pure and allocation-only (no JUCE, no threads): build it on a worker thread, read it on the message
    thread once handed over. Display only - nothing in the listening path uses it. */
class WaveformSummary final
{
public:
    static constexpr int defaultBinSize = 256;

    struct Level
    {
        float peak = 0.0f;   // linear, max |x|
        float rms = 0.0f;    // linear
    };

    WaveformSummary() = default;

    /** Summary of `numChannels` channels (nullptr entries are skipped) of `length` samples each. An empty
        summary is returned if `length` <= 0, `binSize` <= 0, or `cancel` fires. */
    static WaveformSummary build (const float* const* channels, int numChannels, std::int64_t length,
                                  int binSize = defaultBinSize, const CancelCheck& cancel = {});

    /** Summary of the Residual: original[ch][i] - sum over stems of stems[s][ch][i], for `numChannels`
        channels and `numStems` stems (`stems[s]` points at that stem's `numChannels` channel pointers; a
        nullptr stem channel counts as silence). Accumulated in double precision per sample. */
    static WaveformSummary buildResidual (const float* const* original, int numChannels,
                                          const float* const* const* stems, int numStems,
                                          std::int64_t length, int binSize = defaultBinSize,
                                          const CancelCheck& cancel = {});

    bool isEmpty() const noexcept               { return peaks.empty(); }
    std::int64_t getLength() const noexcept     { return length; }
    int getBinSize() const noexcept             { return binSize; }
    int getNumBins() const noexcept             { return (int) peaks.size(); }

    /** Peak and RMS of the samples [startSample, endSample), clamped to the signal; combines whole bins, so
        the result covers every bin the range touches. An empty range (after clamping) gives zeros. */
    Level getLevel (std::int64_t startSample, std::int64_t endSample) const noexcept;

    /** One bin's values (for tests / diagnostics). */
    Level getBin (int index) const noexcept;

private:
    template <typename FillChunk>
    static WaveformSummary buildWith (int numChannels, std::int64_t length, int binSize,
                                      const CancelCheck& cancel, FillChunk&& fill);

    std::int64_t binSamples (int index) const noexcept;

    std::vector<float> peaks;        // per bin
    std::vector<float> meanSquares;  // per bin, over all channels
    std::int64_t length = 0;
    int binSize = defaultBinSize;
};

} // namespace stemab
