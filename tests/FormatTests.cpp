#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "app/ui/Format.h"

using Catch::Matchers::WithinAbs;
using stemab::format::parseTimeInput;

namespace
{
    const juce::String unicodeMinus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));

    void requireAbsolute (const juce::String& text, double seconds)
    {
        INFO ("input: \"" << text << "\"");
        const auto r = parseTimeInput (text);
        REQUIRE (r.has_value());
        CHECK_FALSE (r->second);
        CHECK_THAT (r->first, WithinAbs (seconds, 1.0e-9));
    }

    void requireRelative (const juce::String& text, double seconds)
    {
        INFO ("input: \"" << text << "\"");
        const auto r = parseTimeInput (text);
        REQUIRE (r.has_value());
        CHECK (r->second);
        CHECK_THAT (r->first, WithinAbs (seconds, 1.0e-9));
    }

    void requireRejected (const juce::String& text)
    {
        INFO ("input: \"" << text << "\"");
        CHECK_FALSE (parseTimeInput (text).has_value());
    }
}

TEST_CASE ("parseTimeInput: absolute times", "[format]")
{
    requireAbsolute ("1:23.456", 83.456);
    requireAbsolute ("83.5", 83.5);
    requireAbsolute ("0:05", 5.0);
    requireAbsolute ("1:02:03", 3723.0);
    requireAbsolute ("1:02:03.5", 3723.5);
    requireAbsolute ("90", 90.0);
    requireAbsolute ("0", 0.0);
    requireAbsolute ("0:00.000", 0.0);
    requireAbsolute ("12:34", 754.0);
    requireAbsolute ("1,5", 1.5);
    requireAbsolute ("0:01,25", 1.25);
    requireAbsolute (".5", 0.5);
    requireAbsolute ("  1:23.456  ", 83.456);
    requireAbsolute ("\t2:00\n", 120.0);
}

TEST_CASE ("parseTimeInput: relative offsets", "[format]")
{
    requireRelative ("+5", 5.0);
    requireRelative ("-2.5", -2.5);
    requireRelative ("+1:30", 90.0);
    requireRelative ("-1:30", -90.0);
    requireRelative (" - 3 ", -3.0);
    requireRelative ("+.5", 0.5);
    requireRelative (unicodeMinus + "2.5", -2.5);
    requireRelative (unicodeMinus + "0:10", -10.0);
}

TEST_CASE ("parseTimeInput: rejects malformed input", "[format]")
{
    requireRejected ("");
    requireRejected ("   ");
    requireRejected ("abc");
    requireRejected ("1:60");
    requireRejected ("1:2:3:4");
    requireRejected ("1::2");
    requireRejected ("--1");
    requireRejected ("1:-5");
    requireRejected ("+");
    requireRejected ("-");
    requireRejected (".");
    requireRejected (":");
    requireRejected ("1:");
    requireRejected (":30");
    requireRejected ("1:00:60");
    requireRejected ("1:60:00");
    requireRejected ("1.5:30");
    requireRejected ("1.2.3");
    requireRejected ("1e3");
    requireRejected ("nan");
    requireRejected ("inf");
    requireRejected ("1 2");
    requireRejected ("12345678901234567890");
}
