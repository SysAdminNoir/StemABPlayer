#include "engine/Crossfade.h"

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

namespace stemab
{

CrossfadeGains crossfadeGains (float x, Curve curve) noexcept
{
    // Clamp to [0, 1] with exact endpoints (cos(pi/2) is not exactly 0 in floating point). NaN maps to all-A.
    if (! (x > 0.0f))
        return { 1.0f, 0.0f };

    if (x >= 1.0f)
        return { 0.0f, 1.0f };

    switch (curve)
    {
        case Curve::Linear:
            return { 1.0f - x, x };

        case Curve::EqualPower:
        {
            const auto angle = x * juce::MathConstants<float>::halfPi;
            return { std::cos (angle), std::sin (angle) };
        }
    }

    return { 1.0f - x, x };
}

} // namespace stemab
