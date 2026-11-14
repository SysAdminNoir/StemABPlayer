#include "analysis/NullDepth.h"

#include <algorithm>
#include <cmath>
#include <juce_dsp/juce_dsp.h>

namespace stemab
{

double nullDepthDb (double referenceRms, double residualRms) noexcept
{
    constexpr double tiny = 1.0e-12;   // caps a perfect null at +240 dB instead of infinity
    const auto ratio = std::max (referenceRms, tiny) / std::max (residualRms, tiny);
    return 20.0 * std::log10 (ratio);
}

} // namespace stemab
