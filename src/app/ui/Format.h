#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

namespace stemab::format
{

/** 1234567 -> "1,234,567" (no sign for non-negative values unless `forceSign`). */
inline juce::String withThousands (juce::int64 v, bool forceSign = false)
{
    const auto digits = juce::String (std::llabs (v));
    juce::String out;
    for (int i = 0; i < digits.length(); ++i)
    {
        if (i > 0 && (digits.length() - i) % 3 == 0)
            out << ",";
        out << digits[i];
    }
    if (v < 0)
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + out;
    return (forceSign ? "+" : "") + out;
}

/** "m:ss" (ruler, playhead bubble). */
inline juce::String minutesSeconds (double seconds)
{
    const auto total = (juce::int64) std::floor (juce::jmax (0.0, seconds));
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

/** "m:ss.mmm" (transport readout). */
inline juce::String minutesSecondsMillis (double seconds)
{
    const auto ms = (juce::int64) std::floor (juce::jmax (0.0, seconds) * 1000.0);
    return juce::String (ms / 60000) + ":" + juce::String ((ms / 1000) % 60).paddedLeft ('0', 2)
           + "." + juce::String (ms % 1000).paddedLeft ('0', 3);
}

/** Parses what the user types into the transport time readout. Returns {seconds, relative}:
      "1:23.456", "83.5", "0:05", "1:02:03", ".5", "1,5"  -> an absolute time (never negative);
      "+5", "-2.5", "-0:30" (also U+2212 for the minus)    -> a relative offset, sign applied.
    Forms: ss, ss.fff, m:ss, m:ss.fff, h:mm:ss(.fff). Fields after the first colon must be < 60; only the
    last field may have a fraction. Anything else (empty, letters, 4 colon parts, "1::2", "--1") is rejected. */
inline std::optional<std::pair<double, bool>> parseTimeInput (const juce::String& input)
{
    auto s = input.trim();
    if (s.isEmpty())
        return std::nullopt;

    bool relative = false;
    double sign = 1.0;
    const auto first = s[0];
    if (first == '+' || first == '-' || first == (juce::juce_wchar) 0x2212)
    {
        relative = true;
        sign = first == '+' ? 1.0 : -1.0;
        s = s.substring (1).trim();
    }
    s = s.replaceCharacter (',', '.');

    // Split on ':' by hand (juce::StringArray::fromTokens would drop empty fields such as the one in "1::2").
    std::vector<juce::String> parts;
    for (int start = 0;;)
    {
        const int colon = s.indexOfChar (start, ':');
        parts.push_back (colon < 0 ? s.substring (start) : s.substring (start, colon));
        if (colon < 0)
            break;
        start = colon + 1;
    }
    if (parts.size() > 3)
        return std::nullopt;

    constexpr int maxFieldLength = 12;      // keeps the integer fields far from overflow
    std::vector<double> values;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const auto& p = parts[i];
        if (p.isEmpty() || p.length() > maxFieldLength)
            return std::nullopt;

        const bool last = i + 1 == parts.size();
        int digits = 0, dots = 0;
        for (auto it = p.getCharPointer(); ! it.isEmpty(); ++it)
        {
            const auto c = *it;
            if (c >= '0' && c <= '9')
                ++digits;
            else if (c == '.' && last)
                ++dots;
            else
                return std::nullopt;
        }
        if (digits == 0 || dots > 1)
            return std::nullopt;

        const double v = p.getDoubleValue();
        if (! std::isfinite (v) || v < 0.0)
            return std::nullopt;
        if (i > 0 && v >= 60.0)
            return std::nullopt;
        values.push_back (v);
    }

    double total = 0.0;
    for (const double v : values)
        total = total * 60.0 + v;
    if (! std::isfinite (total))
        return std::nullopt;

    return std::make_pair (total * sign, relative);
}

/** 44100 -> "44.1 kHz", 48000 -> "48 kHz". */
inline juce::String kiloHertz (double rate)
{
    if (rate <= 0.0)
        return "-";
    const double k = rate / 1000.0;
    return (std::abs (k - std::round (k)) < 1.0e-6 ? juce::String ((int) std::round (k)) : juce::String (k, 1)) + " kHz";
}

/** A real minus sign instead of '-'. */
inline juce::String withMinusSign (const juce::String& s)
{
    return s.replace ("-", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")));
}

} // namespace stemab::format
