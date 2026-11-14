#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "decode/Decode.h"

TEST_CASE ("FFmpeg is linked and reports a version", "[decode]")
{
    const auto version = stemab::ffmpegVersionString();
    CHECK_FALSE (version.empty());
    CHECK_THAT (version, Catch::Matchers::ContainsSubstring ("FFmpeg"));
    CHECK_THAT (version, Catch::Matchers::ContainsSubstring ("libavformat"));
    CHECK_THAT (version, Catch::Matchers::ContainsSubstring ("libavcodec"));
}
