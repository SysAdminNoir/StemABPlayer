#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdlib>

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
