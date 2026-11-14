#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include "analysis/NullDepth.h"

TEST_CASE ("null depth is the dB ratio of reference to residual RMS", "[analysis]")
{
    using Catch::Matchers::WithinAbs;

    CHECK_THAT (stemab::nullDepthDb (1.0, 0.1),  WithinAbs (20.0, 1.0e-9));
    CHECK_THAT (stemab::nullDepthDb (1.0, 1.0),  WithinAbs (0.0,  1.0e-9));
    CHECK_THAT (stemab::nullDepthDb (1.0, 0.01), WithinAbs (40.0, 1.0e-9));
    CHECK (stemab::nullDepthDb (1.0, 0.0) == stemab::maxNullDepthDb);   // perfect null is capped, not infinite
    CHECK (stemab::maxNullDepthDb >= 200.0);
}

TEST_CASE ("null depth from energies is always finite and clamped", "[analysis]")
{
    using Catch::Matchers::WithinAbs;

    CHECK_THAT (stemab::nullDepthDbFromEnergies (1.0, 0.001), WithinAbs (30.0, 1.0e-9));
    CHECK_THAT (stemab::nullDepthDbFromEnergies (4.0, 1.0),   WithinAbs (10.0 * std::log10 (4.0), 1.0e-12));
    CHECK (stemab::nullDepthDbFromEnergies (1.0, 0.0) == stemab::maxNullDepthDb);   // residual exactly zero
    CHECK (stemab::nullDepthDbFromEnergies (0.0, 0.0) == stemab::maxNullDepthDb);   // both silent: nothing left over
    CHECK (stemab::nullDepthDbFromEnergies (0.0, 1.0) == stemab::minNullDepthDb);   // silent A, audible residual
    CHECK (stemab::nullDepthDbFromEnergies (1.0e300, 1.0e-300) == stemab::maxNullDepthDb);
    CHECK (std::isfinite (stemab::nullDepthDbFromEnergies (std::nan (""), 1.0)));
    CHECK (std::isfinite (stemab::nullDepthDbFromEnergies (1.0, std::nan (""))));
    CHECK (stemab::isQuietBlock (stemab::quietBlockNullDepthDb));
    CHECK_FALSE (stemab::isQuietBlock ((float) stemab::minNullDepthDb));
}
