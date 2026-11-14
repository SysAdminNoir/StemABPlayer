#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/Crossfade.h"

using Catch::Matchers::WithinAbs;
using stemab::Curve;
using stemab::crossfadeGains;

TEST_CASE ("crossfade endpoints are exact for both curves", "[engine][crossfade]")
{
    for (auto curve : { Curve::Linear, Curve::EqualPower })
    {
        const auto allA = crossfadeGains (0.0f, curve);
        CHECK (allA.gA == 1.0f);
        CHECK (allA.gB == 0.0f);

        const auto allB = crossfadeGains (1.0f, curve);
        CHECK (allB.gA == 0.0f);
        CHECK (allB.gB == 1.0f);
    }
}

TEST_CASE ("crossfade clamps out-of-range positions", "[engine][crossfade]")
{
    for (auto curve : { Curve::Linear, Curve::EqualPower })
    {
        CHECK (crossfadeGains (-0.5f, curve).gA == 1.0f);
        CHECK (crossfadeGains (-0.5f, curve).gB == 0.0f);
        CHECK (crossfadeGains (1.5f, curve).gA == 0.0f);
        CHECK (crossfadeGains (1.5f, curve).gB == 1.0f);
    }
}

TEST_CASE ("linear crossfade gains sum to 1 (amplitude-preserving)", "[engine][crossfade]")
{
    for (float x = 0.0f; x <= 1.0f; x += 0.125f)
    {
        const auto g = crossfadeGains (x, Curve::Linear);
        CHECK_THAT (g.gA, WithinAbs (1.0f - x, 1.0e-6));
        CHECK_THAT (g.gB, WithinAbs (x, 1.0e-6));
        CHECK_THAT (g.gA + g.gB, WithinAbs (1.0, 1.0e-6));
    }
}

TEST_CASE ("equal-power crossfade has unit power at the midpoint and throughout", "[engine][crossfade]")
{
    const auto mid = crossfadeGains (0.5f, Curve::EqualPower);
    CHECK_THAT (mid.gA, WithinAbs (0.70710678, 1.0e-6));
    CHECK_THAT (mid.gB, WithinAbs (0.70710678, 1.0e-6));
    CHECK_THAT (mid.gA * mid.gA + mid.gB * mid.gB, WithinAbs (1.0, 1.0e-6));

    for (float x = 0.0f; x <= 1.0f; x += 0.0625f)
    {
        const auto g = crossfadeGains (x, Curve::EqualPower);
        CHECK_THAT (g.gA * g.gA + g.gB * g.gB, WithinAbs (1.0, 1.0e-6));
    }
}
