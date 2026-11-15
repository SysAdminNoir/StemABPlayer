#include "engine/OutputResampler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

#ifndef NOMINMAX
 #define NOMINMAX            // r8brain includes <windows.h>: keep its min/max macros away from std::min/max
#endif
#include "CDSPResampler.h"   // r8brain-free-src

namespace stemab
{

OutputResampler::OutputResampler (double sourceRate, double deviceRate, int maxOutputBlock, ResamplerQuality q)
    : srcRate (sourceRate), dstRate (deviceRate), maxOut (std::max (1, maxOutputBlock))
{
    assert (sourceRate > 0.0 && deviceRate > 0.0);

    const auto make = [&]
    {
        return std::make_unique<r8b::CDSPResampler> (srcRate, dstRate, chunkSize, q.transitionBandPercent,
                                                     q.stopBandAttenuationDb,
                                                     q.minimumPhase ? r8b::fprMinPhase : r8b::fprLinearPhase);
    };

    // Identical construction for both channels (ADR 0004).
    left = make();
    right = make();

    maxOutPerChunk = std::max (left->getMaxOutLen (chunkSize), 1);
    lookaheadIn = left->getInputRequiredForOutput (1);

    // Everything fed so far has come out once the look-ahead plus whatever the FIFO can hold (in source
    // samples) has been pushed through behind it; two chunks of margin for the chunk granularity.
    const int fifoCapacity = maxOut + maxOutPerChunk;
    flushIn = lookaheadIn + (int) std::ceil ((double) fifoCapacity * srcRate / dstRate) + 2 * chunkSize;

    pullL.assign ((size_t) chunkSize, 0.0f);
    pullR.assign ((size_t) chunkSize, 0.0f);
    inL.assign ((size_t) chunkSize, 0.0);
    inR.assign ((size_t) chunkSize, 0.0);
    fifoL.assign ((size_t) fifoCapacity, 0.0f);
    fifoR.assign ((size_t) fifoCapacity, 0.0f);

    // Warm-up: the FFT objects build their twiddle tables lazily on first use (writes into preallocated
    // memory, but costs sin/cos time). Push enough silence through every stage here, then reset.
    for (int done = 0; done < lookaheadIn + 4 * chunkSize; done += chunkSize)
    {
        double* op = nullptr;
        left->process (inL.data(), chunkSize, op);
        right->process (inR.data(), chunkSize, op);
    }

    left->clear();
    right->clear();
}

OutputResampler::~OutputResampler() = default;

bool OutputResampler::matches (double sourceRate, double deviceRate, int maxOutputBlock) const noexcept
{
    return std::abs (srcRate - sourceRate) < 0.5 && std::abs (dstRate - deviceRate) < 0.5 && maxOutputBlock <= maxOut;
}

double OutputResampler::getLatencySeconds() const noexcept
{
    return ((double) lookaheadIn + 0.5 * chunkSize) / srcRate;
}

void OutputResampler::render (PullFn pull, void* context, float* outLeft, float* outRight, int numSamples) noexcept
{
    numSamples = std::clamp (numSamples, 0, maxOut);
    const int capacity = (int) fifoL.size();

    while (fifoCount < numSamples)
    {
        pull (context, pullL.data(), pullR.data(), chunkSize);

        for (size_t i = 0; i < (size_t) chunkSize; ++i)
        {
            inL[i] = (double) pullL[i];
            inR[i] = (double) pullR[i];
        }

        double* opL = nullptr;
        double* opR = nullptr;
        const int nL = left->process (inL.data(), chunkSize, opL);
        const int nR = right->process (inR.data(), chunkSize, opR);
        assert (nL == nR);   // identical settings: identical counts

        // fifoCount < numSamples <= maxOut, so there is always room for maxOutPerChunk more.
        const int n = std::min ({ nL, nR, capacity - fifoCount });

        for (int i = 0; i < n; ++i)
        {
            fifoL[(size_t) (fifoCount + i)] = (float) opL[i];
            fifoR[(size_t) (fifoCount + i)] = (float) opR[i];
        }

        fifoCount += n;
        totalIn += chunkSize;
    }

    if (outLeft != nullptr)  std::memcpy (outLeft,  fifoL.data(), sizeof (float) * (size_t) numSamples);
    if (outRight != nullptr) std::memcpy (outRight, fifoR.data(), sizeof (float) * (size_t) numSamples);

    const int rest = fifoCount - numSamples;
    if (rest > 0)
    {
        std::memmove (fifoL.data(), fifoL.data() + numSamples, sizeof (float) * (size_t) rest);
        std::memmove (fifoR.data(), fifoR.data() + numSamples, sizeof (float) * (size_t) rest);
    }

    fifoCount = rest;
    totalOut += numSamples;
}

} // namespace stemab
