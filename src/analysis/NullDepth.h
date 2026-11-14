#pragma once

namespace stemab
{

/** Null Depth in dB: how far the residual RMS sits below the reference (A) RMS. Higher is better.
    Placeholder for checkpoint 1; real analysis arrives in checkpoint 3. */
double nullDepthDb (double referenceRms, double residualRms) noexcept;

} // namespace stemab
