#pragma once

#include <vector>

#include "analysis/Alignment.h"

namespace stemab
{

/** Null Depth limits. A Residual that is exactly zero reads +200 dB; an Original that is silent while the
    Residual is not reads -200 dB. No Null Depth value is ever infinite or NaN. */
inline constexpr double maxNullDepthDb = 200.0;
inline constexpr double minNullDepthDb = -200.0;

/** Per-second Null Depth value for a block whose Original is too quiet to judge (A below -70 dBFS RMS).
    It lies far outside [minNullDepthDb, maxNullDepthDb], so it can never be mistaken for a measurement;
    test with isQuietBlock() and leave such blocks out of statistics and displays. */
inline constexpr float quietBlockNullDepthDb = -1000.0f;
inline constexpr double quietBlockThresholdDbfs = -70.0;

inline constexpr bool isQuietBlock (float nullDepthDb) noexcept   { return nullDepthDb <= quietBlockNullDepthDb; }

/** Null Depth in dB from energies (sums of squares): 10 * log10 (referenceEnergy / residualEnergy),
    clamped to [minNullDepthDb, maxNullDepthDb]. Residual exactly zero -> maxNullDepthDb; reference zero
    with a non-zero residual -> minNullDepthDb; NaN input -> minNullDepthDb. Always finite. */
double nullDepthDbFromEnergies (double referenceEnergy, double residualEnergy) noexcept;

/** The same from RMS levels: 20 * log10 (referenceRms / residualRms), with the same limits. */
double nullDepthDb (double referenceRms, double residualRms) noexcept;

//==============================================================================
/** Result of computeNullDepth(). */
struct NullDepthResult
{
    /** Track Null Depth: 10*log10 (energy(A) / energy(Residual)) over the whole timeline, all channels. */
    double trackNullDepthDb = 0.0;

    /** <A,B> / <B,B> with B = the Stem Sum. DIAGNOSTIC ONLY - never applied (ADR 0004 forbids level matching).
        1.0 means the Stems carry no level difference; 0 when B is silent. */
    double leastSquaresGain = 0.0;

    /** DIAGNOSTIC ONLY: the Track Null Depth B *would* reach if multiplied by leastSquaresGain. Its distance
        from trackNullDepthDb is how much of the Residual is a plain level difference. Never applied. */
    double gainMatchedNullDepthDb = 0.0;

    /** Null Depth per 1-second block of the timeline (the last block may be shorter). Blocks whose A is below
        quietBlockThresholdDbfs hold quietBlockNullDepthDb (see isQuietBlock()). Never NaN or infinite. */
    std::vector<float> perSecondNullDepthDb;
};

/** Track Null Depth of the Original against Stems already on A's timeline (alignStems()).

    Residual = A - (sum of all four aligned Stems), per channel, at the Stems' original amplitude - Mute/Solo
    never enter into it. Channel c of a Stem pairs with channel c of A; a Stem channel or sample that does not
    exist counts as zero. Sums are accumulated in double precision. `sampleRate` only sets the per-second
    block length. Offline; call it on a worker thread. */
NullDepthResult computeNullDepth (const juce::AudioBuffer<float>& original,
                                  const StemBuffers& alignedStems,
                                  double sampleRate);

} // namespace stemab
