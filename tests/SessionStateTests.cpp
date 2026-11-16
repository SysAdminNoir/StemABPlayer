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

TEST_CASE ("session state round-trips a Stem Set (one path per Stem, empty = no file)", "[app][session][stemset]")
{
    auto s = sample();
    s.stemFilePath.clear();
    s.stemSetPaths = { juce::String ("C:/Sep/03 Heresy/drums.wav"), juce::String(),
                       juce::String (juce::CharPointer_UTF8 ("C:/Sep/03 HÃ©rÃ©sy/other & \"x\".wav")),
                       juce::String ("C:/Sep/03 Heresy/vocals.wav") };
    REQUIRE (s.hasStemSet());

    const auto back = roundTripThroughText (s);
    REQUIRE (back.has_value());
    CHECK (*back == s);
    CHECK (back->hasStemSet());
    CHECK (back->stemFilePath.isEmpty());
    CHECK (back->stemSetPaths[1].isEmpty());

    // A set with every slot empty is no pair at all.
    SessionState empty;
    empty.originalPath = "a.flac";
    CHECK_FALSE (empty.hasStemSet());
    CHECK_FALSE (roundTripThroughText (empty).has_value());
}

TEST_CASE ("session state: an old single Stem File path still loads", "[app][session][stemset]")
{
    // As older versions wrote it: no per-Stem attributes.
    juce::XmlElement xml (SessionState::xmlTag);
    xml.setAttribute ("original", "C:/Music/a.flac");
    xml.setAttribute ("stemFile", "C:/Music/b.stem.m4a");
    xml.setAttribute ("playhead", "4410");

    const auto s = SessionState::fromXml (&xml);
    REQUIRE (s.has_value());
    CHECK (s->stemFilePath == "C:/Music/b.stem.m4a");
    CHECK_FALSE (s->hasStemSet());
    CHECK (s->stemSetPaths == std::array<juce::String, 4> {});
    CHECK (s->playheadSample == 4410);

    // ...and a Stem File keeps precedence over stray per-Stem attributes.
    xml.setAttribute ("stemDrums", "C:/Music/drums.wav");
    const auto t = SessionState::fromXml (&xml);
    REQUIRE (t.has_value());
    CHECK (t->stemFilePath == "C:/Music/b.stem.m4a");
    CHECK_FALSE (t->hasStemSet());

    // A Stem Set written by this version: stemFile is empty, the per-Stem attributes carry the paths.
    juce::XmlElement set (SessionState::xmlTag);
    set.setAttribute ("original", "C:/Music/a.flac");
    set.setAttribute ("stemFile", "");
    set.setAttribute ("stemVocals", "C:/Music/vocals.wav");
    const auto u = SessionState::fromXml (&set);
    REQUIRE (u.has_value());
    CHECK (u->hasStemSet());
    CHECK (u->stemSetPaths[3] == "C:/Music/vocals.wav");
}
