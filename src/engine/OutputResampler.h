#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace r8b { class CDSPResampler; }   // r8brain-free-src stays out of every public header

namespace stemab
{

/** r8brain filter parameters (see r8b::CDSPResampler). */
struct ResamplerQuality
{
    double transitionBandPercent;   // ReqTransBand
    double stopBandAttenuationDb;   // ReqAtten
    bool minimumPhase;              // ReqPhase (false: linear phase)
};

//==============================================================================
/** Realtime stereo resampler for the mixed output (ADR 0005): the engine mixes at the source rate and this
    converts the one stereo stream to the device rate.

    Two r8brain resamplers (L and R) built with identical settings, so both channels are treated identically
    (ADR 0004). The device asks for exactly N samples per callback; render() pulls source-rate audio from the
    mixer in fixed chunks of `chunkSize` until a preallocated FIFO holds >= N resampled samples, then pops N.

    Threads: construct and destroy off the audio thread (both allocate/free). render() is allocation-, lock-
    and I/O-free (r8brain's process() only touches buffers sized at construction; the constructor runs one
    warm-up chunk so that the lazily-built FFT twiddle tables are made here, not on the audio thread). */
class OutputResampler final
{
public:
    /** Source samples pulled from the mixer per r8brain process() call. */
    static constexpr int chunkSize = 64;

    /** Chosen for the realtime path (measured by the "[.resampler-measure]" test, Release):
          linear phase; 0.0000 dB at 20 kHz, -2.1 dB at 21 kHz; artifacts <= -134 dB, 23 kHz alias -144 dB;
          extra latency 9.9 ms at 44.1k -> 48k and 8.9 ms at 48k -> 44.1k.
        r8brain's 24-bit preset (2 %, 180 dB) measures 39 / 35 ms, its 16-bit preset 41 / 37 ms; the latency
        moves in steps with the FFT block size, and 4.5 % / 130 dB is the most selective setting that stays
        under ~10 ms in both directions. */
    static constexpr ResamplerQuality defaultQuality { 4.5, 130.0, false };

    /** `maxOutputBlock`: the most samples one render() call will be asked for. Allocates. */
    OutputResampler (double sourceRate, double deviceRate, int maxOutputBlock,
                     ResamplerQuality quality = defaultQuality);
    ~OutputResampler();

    double getSourceRate() const noexcept       { return srcRate; }
    double getDeviceRate() const noexcept       { return dstRate; }
    int getMaxOutputBlock() const noexcept      { return maxOut; }

    /** True if this converts `sourceRate` -> `deviceRate` and can serve blocks of `maxOutputBlock`. */
    bool matches (double sourceRate, double deviceRate, int maxOutputBlock) const noexcept;

    /** Source samples the resampler needs before its first output sample (r8brain's look-ahead). */
    int getLookaheadSourceSamples() const noexcept  { return lookaheadIn; }

    /** Nominal extra output latency in seconds: look-ahead plus half a chunk (the FIFO adds 0..1 chunk). */
    double getLatencySeconds() const noexcept;

    /** Silent source samples that must be fed before everything previously fed has come out. */
    int getFlushSourceSamples() const noexcept      { return flushIn; }

    /** Fills `left`/`right` with `numSamples` source-rate samples. Called only from render(). */
    using PullFn = void (*) (void* context, float* left, float* right, int numSamples);

    /** Audio thread: writes exactly `numSamples` (<= getMaxOutputBlock()) device-rate samples. */
    void render (PullFn pull, void* context, float* outLeft, float* outRight, int numSamples) noexcept;

    /** Totals since construction (for tests and diagnostics). */
    std::int64_t getSourceSamplesPulled() const noexcept   { return totalIn; }
    std::int64_t getOutputSamplesRendered() const noexcept { return totalOut; }

private:
    double srcRate, dstRate;
    int maxOut;
    std::unique_ptr<r8b::CDSPResampler> left, right;
    int maxOutPerChunk = 0, lookaheadIn = 0, flushIn = 0;

    std::vector<float> pullL, pullR;       // chunkSize, from the mixer
    std::vector<double> inL, inR;          // chunkSize, r8brain input
    std::vector<float> fifoL, fifoR;       // maxOut + maxOutPerChunk
    int fifoCount = 0;

    std::int64_t totalIn = 0, totalOut = 0;

    OutputResampler (const OutputResampler&) = delete;
    OutputResampler& operator= (const OutputResampler&) = delete;
};

} // namespace stemab
