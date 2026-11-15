#include "analysis/NullDepth.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace stemab
{

double nullDepthDbFromEnergies (double referenceEnergy, double residualEnergy) noexcept
{
    if (std::isnan (referenceEnergy) || std::isnan (residualEnergy))
        return minNullDepthDb;

    if (! (residualEnergy > 0.0))
        return maxNullDepthDb;      // the Residual is exactly zero: a perfect null, capped

    if (! (referenceEnergy > 0.0))
        return minNullDepthDb;      // silent reference, audible residual

    const double db = 10.0 * std::log10 (referenceEnergy / residualEnergy);
    return std::isfinite (db) ? std::clamp (db, minNullDepthDb, maxNullDepthDb)
                              : (db > 0.0 ? maxNullDepthDb : minNullDepthDb);
}

double nullDepthDb (double referenceRms, double residualRms) noexcept
{
    return nullDepthDbFromEnergies (referenceRms * referenceRms, residualRms * residualRms);
}

//==============================================================================
NullDepthResult computeNullDepth (const juce::AudioBuffer<float>& original,
                                  const StemBuffers& alignedStems,
                                  double sampleRate)
{
    return computeNullDepth (original, alignedStems, 0, +1, sampleRate);
}

NullDepthResult computeNullDepth (const juce::AudioBuffer<float>& original,
                                  const StemBuffers& stems,
                                  juce::int64 offsetSamples,
                                  int polarity,
                                  double sampleRate,
                                  const CancelCheck& cancel)
{
    NullDepthResult result;
    const float gain = polarity < 0 ? -1.0f : 1.0f;

    const int numCh  = original.getNumChannels();
    const int length = original.getNumSamples();
    const int blockLen = std::max (1, (int) std::lround (sampleRate > 0.0 ? sampleRate : 1.0));
    const double quietMeanSquare = std::pow (10.0, quietBlockThresholdDbfs / 10.0);

    double energyA = 0.0, energyR = 0.0, crossAB = 0.0, energyB = 0.0;

    // Stem s pairs its sample n + offset with A's sample n, for n in [from[s], to[s]) (zero elsewhere).
    std::array<juce::int64, 4> from {}, to {};
    for (size_t s = 0; s < stems.size(); ++s)
    {
        from[s] = std::max<juce::int64> (0, -offsetSamples);
        to[s]   = std::max (from[s], (juce::int64) stems[s].getNumSamples() - offsetSamples);
    }

    for (int blockStart = 0; blockStart < length; blockStart += blockLen)
    {
        if (isCancelled (cancel))
        {
            result = {};
            return result;
        }

        const int blockEnd = std::min (length, blockStart + blockLen);
        double blockA = 0.0, blockR = 0.0;

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* a = original.getReadPointer (ch);
            const float* stem[4] = {};

            for (size_t s = 0; s < stems.size(); ++s)
                if (ch < stems[s].getNumChannels())
                    stem[s] = stems[s].getReadPointer (ch);

            for (int n = blockStart; n < blockEnd; ++n)
            {
                double bv = 0.0;
                for (size_t s = 0; s < 4; ++s)
                    if (stem[s] != nullptr && n >= from[s] && n < to[s])
                        bv += (double) (gain * stem[s][n + offsetSamples]);

                const double av = (double) a[n];
                const double rv = av - bv;
                blockA  += av * av;
                blockR  += rv * rv;
                crossAB += av * bv;
                energyB += bv * bv;
            }
        }

        energyA += blockA;
        energyR += blockR;

        const double meanSquare = blockA / ((double) (blockEnd - blockStart) * (double) std::max (1, numCh));
        result.perSecondNullDepthDb.push_back (meanSquare < quietMeanSquare
                                                   ? quietBlockNullDepthDb
                                                   : (float) nullDepthDbFromEnergies (blockA, blockR));
    }

    result.trackNullDepthDb = nullDepthDbFromEnergies (energyA, energyR);
    result.leastSquaresGain = energyB > 0.0 ? crossAB / energyB : 0.0;

    // Residual energy left after the best gain g: |A - gB|^2 = E_A - <A,B>^2 / E_B.
    const double matchedResidual = energyB > 0.0 ? std::max (0.0, energyA - crossAB * crossAB / energyB) : energyA;
    result.gainMatchedNullDepthDb = nullDepthDbFromEnergies (energyA, matchedResidual);
    return result;
}

} // namespace stemab
