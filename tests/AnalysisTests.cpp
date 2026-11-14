#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "analysis/NullDepth.h"

TEST_CASE ("null depth is the dB ratio of reference to residual RMS", "[analysis]")
{
    using Catch::Matchers::WithinAbs;

    CHECK_THAT (stemab::nullDepthDb (1.0, 0.1),  WithinAbs (20.0, 1.0e-9));
    CHECK_THAT (stemab::nullDepthDb (1.0, 1.0),  WithinAbs (0.0,  1.0e-9));
    CHECK_THAT (stemab::nullDepthDb (1.0, 0.01), WithinAbs (40.0, 1.0e-9));
    CHECK (stemab::nullDepthDb (1.0, 0.0) > 200.0);   // perfect null is capped, not infinite
}
