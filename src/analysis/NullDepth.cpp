#include "analysis/NullDepth.h"

#include <algorithm>
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
    NullDepthResult result;

    const int numCh  = original.getNumChannels();
    const int length = original.getNumSamples();
    const int blockLen = std::max (1, (int) std::lround (sampleRate > 0.0 ? sampleRate : 1.0));
    const double quietMeanSquare = std::pow (10.0, quietBlockThresholdDbfs / 10.0);

    double energyA = 0.0, energyR = 0.0, crossAB = 0.0, energyB = 0.0;

    for (int blockStart = 0; blockStart < length; blockStart += blockLen)
    {
        const int blockEnd = std::min (length, blockStart + blockLen);
        double blockA = 0.0, blockR = 0.0;

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* a = original.getReadPointer (ch);
            const float* stem[4] = {};

            for (size_t s = 0; s < alignedStems.size(); ++s)
                if (ch < alignedStems[s].getNumChannels())
                    stem[s] = alignedStems[s].getReadPointer (ch);

            for (int n = blockStart; n < blockEnd; ++n)
            {
                double bv = 0.0;
                for (size_t s = 0; s < 4; ++s)
                    if (stem[s] != nullptr && n < alignedStems[s].getNumSamples())
                        bv += (double) stem[s][n];

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
