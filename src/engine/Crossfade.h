#pragma once

namespace stemab
{

/** Crossfade law between A (original) and B (stem sum). See CONTEXT.md: "Crossfade". */
enum class Curve
{
    Linear,     // gA = 1 - x,        gB = x
    EqualPower  // gA = cos(x*pi/2),  gB = sin(x*pi/2)
};

struct CrossfadeGains
{
    float gA;
    float gB;
};

/** Gains for crossfade position x (0 = all A, 1 = all B). x is clamped to [0, 1]. Real-time safe. */
CrossfadeGains crossfadeGains (float x, Curve curve) noexcept;

} // namespace stemab
