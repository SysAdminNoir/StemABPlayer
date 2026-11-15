#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "app/SessionState.h"

using stemab::SessionState;

namespace
{
    SessionState sample()
    {
        SessionState s;
        s.originalPath = "C:/Music/Audio Files/03 Heresy.flac";
        s.stemFilePath = juce::String (juce::CharPointer_UTF8 ("C:/Music/Stem Files/03 H\xc3\xa9r\xc3\xa9sy & \"Co\" <x>.stem.m4a"));
        s.playheadSample = 12'345'678'901LL;
        s.crossfade = 0.25f;
        s.equalPowerCurve = false;
        s.residualListen = true;
        s.muted = { true, false, true, false };
        s.soloed = { false, false, false, true };
        s.masterGainDb = -12.5;
        s.loopEnabled = true;
        s.residualViewAmplified = false;
        return s;
    }

    /** Through text, as the settings file stores it. */
    std::optional<SessionState> roundTripThroughText (const SessionState& s)
    {
        const auto text = s.toXml()->toString();
        const auto parsed = juce::parseXML (text);
        return SessionState::fromXml (parsed.get());
    }
}

TEST_CASE ("session state round-trips through XML text", "[app][session]")
{
    const auto s = sample();
    const auto back = roundTripThroughText (s);
    REQUIRE (back.has_value());
    CHECK (*back == s);
}

TEST_CASE ("session state defaults round-trip", "[app][session]")
{
    SessionState s;
    s.originalPath = "a.flac";
    s.stemFilePath = "b.stem.m4a";
    const auto back = roundTripThroughText (s);
    REQUIRE (back.has_value());
    CHECK (*back == s);
    CHECK (back->equalPowerCurve);
    CHECK (back->residualViewAmplified);
    CHECK (back->masterGainDb == 0.0);
}

TEST_CASE ("session state keeps a silent (-inf) Master Gain", "[app][session]")
{
    auto s = sample();
    s.masterGainDb = -std::numeric_limits<double>::infinity();
    const auto back = roundTripThroughText (s);
    REQUIRE (back.has_value());
    CHECK (std::isinf (back->masterGainDb));
    CHECK (back->masterGainDb < 0.0);
}

TEST_CASE ("session state is invalid without a pair or with another tag", "[app][session]")
{
    CHECK_FALSE (SessionState::fromXml (nullptr).has_value());

    juce::XmlElement other ("SOMETHING");
    other.setAttribute ("original", "a.flac");
    other.setAttribute ("stemFile", "b.stem.m4a");
    CHECK_FALSE (SessionState::fromXml (&other).has_value());

    juce::XmlElement noStems (SessionState::xmlTag);
    noStems.setAttribute ("original", "a.flac");
    CHECK_FALSE (SessionState::fromXml (&noStems).has_value());

    juce::XmlElement blankOriginal (SessionState::xmlTag);
    blankOriginal.setAttribute ("original", "   ");
    blankOriginal.setAttribute ("stemFile", "b.stem.m4a");
    CHECK_FALSE (SessionState::fromXml (&blankOriginal).has_value());
}

TEST_CASE ("session state: missing attributes take their defaults", "[app][session]")
{
    juce::XmlElement xml (SessionState::xmlTag);
    xml.setAttribute ("original", "a.flac");
    xml.setAttribute ("stemFile", "b.stem.m4a");

    const auto s = SessionState::fromXml (&xml);
    REQUIRE (s.has_value());

    SessionState expected;
    expected.originalPath = "a.flac";
    expected.stemFilePath = "b.stem.m4a";
    CHECK (*s == expected);
}

TEST_CASE ("session state: out-of-range and garbage values are clamped", "[app][session]")
{
    juce::XmlElement xml (SessionState::xmlTag);
    xml.setAttribute ("original", "a.flac");
    xml.setAttribute ("stemFile", "b.stem.m4a");
    xml.setAttribute ("playhead", "-500");
    xml.setAttribute ("crossfade", "7.5");
    xml.setAttribute ("masterDb", "+6");
    xml.setAttribute ("mute", "11");          // short: the rest stay off
    xml.setAttribute ("solo", "x1y1z");       // only '1' counts
    xml.setAttribute ("curve", "nonsense");   // anything but "linear" is equal-power

    auto s = SessionState::fromXml (&xml);
    REQUIRE (s.has_value());
    CHECK (s->playheadSample == 0);
    CHECK (s->crossfade == 1.0f);
    CHECK (s->masterGainDb == 0.0);
    CHECK (s->muted == std::array<bool, 4> { true, true, false, false });
    CHECK (s->soloed == std::array<bool, 4> { false, true, false, true });
    CHECK (s->equalPowerCurve);

    xml.setAttribute ("crossfade", "-1");
    xml.setAttribute ("masterDb", "-75");     // below the fader's -60 dB: silent
    s = SessionState::fromXml (&xml);
    REQUIRE (s.has_value());
    CHECK (s->crossfade == 0.0f);
    CHECK (std::isinf (s->masterGainDb));

    xml.setAttribute ("masterDb", "-60");
    s = SessionState::fromXml (&xml);
    REQUIRE (s.has_value());
    CHECK (s->masterGainDb == -60.0);
}
