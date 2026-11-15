#include "app/SessionState.h"

#include <cmath>
#include <limits>

namespace stemab
{

namespace
{
    constexpr double masterFloorDb = -60.0;     // below the fader's last dB step: silent

    juce::String flagsToString (const std::array<bool, SessionState::numStems>& flags)
    {
        juce::String s;
        for (const bool f : flags)
            s << (f ? "1" : "0");
        return s;
    }

    std::array<bool, SessionState::numStems> flagsFromString (const juce::String& s)
    {
        std::array<bool, SessionState::numStems> flags {};
        for (int i = 0; i < SessionState::numStems && i < s.length(); ++i)
            flags[(size_t) i] = s[i] == '1';
        return flags;
    }

    juce::String gainToString (double db)
    {
        return std::isfinite (db) ? juce::String (db, 2) : juce::String ("-inf");
    }

    double gainFromString (const juce::String& s)
    {
        const auto t = s.trim();
        if (t.isEmpty())
            return 0.0;
        if (t.equalsIgnoreCase ("-inf"))
            return -std::numeric_limits<double>::infinity();

        const double db = t.getDoubleValue();
        if (std::isnan (db))
            return 0.0;
        if (db < masterFloorDb - 0.5)
            return -std::numeric_limits<double>::infinity();
        return juce::jmin (0.0, db);
    }
}

std::unique_ptr<juce::XmlElement> SessionState::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (xmlTag);
    xml->setAttribute ("original", originalPath);
    xml->setAttribute ("stemFile", stemFilePath);
    xml->setAttribute ("playhead", juce::String (playheadSample));
    xml->setAttribute ("crossfade", juce::String (crossfade, 4));
    xml->setAttribute ("curve", equalPowerCurve ? "equalPower" : "linear");
    xml->setAttribute ("listen", residualListen ? "residual" : "ab");
    xml->setAttribute ("mute", flagsToString (muted));
    xml->setAttribute ("solo", flagsToString (soloed));
    xml->setAttribute ("masterDb", gainToString (masterGainDb));
    xml->setAttribute ("loop", loopEnabled);
    xml->setAttribute ("residualView", residualViewAmplified ? "+24dB" : "0dB");
    return xml;
}

std::optional<SessionState> SessionState::fromXml (const juce::XmlElement* xml)
{
    if (xml == nullptr || ! xml->hasTagName (xmlTag))
        return std::nullopt;

    SessionState s;
    s.originalPath = xml->getStringAttribute ("original").trim();
    s.stemFilePath = xml->getStringAttribute ("stemFile").trim();
    if (s.originalPath.isEmpty() || s.stemFilePath.isEmpty())
        return std::nullopt;

    s.playheadSample = juce::jmax ((juce::int64) 0, xml->getStringAttribute ("playhead", "0").getLargeIntValue());

    const auto x = (float) xml->getDoubleAttribute ("crossfade", 0.0);
    s.crossfade = std::isfinite (x) ? juce::jlimit (0.0f, 1.0f, x) : 0.0f;

    s.equalPowerCurve = ! xml->getStringAttribute ("curve", "equalPower").equalsIgnoreCase ("linear");
    s.residualListen = xml->getStringAttribute ("listen", "ab").equalsIgnoreCase ("residual");
    s.muted = flagsFromString (xml->getStringAttribute ("mute"));
    s.soloed = flagsFromString (xml->getStringAttribute ("solo"));
    s.masterGainDb = gainFromString (xml->getStringAttribute ("masterDb", "0"));
    s.loopEnabled = xml->getBoolAttribute ("loop", false);
    s.residualViewAmplified = ! xml->getStringAttribute ("residualView", "+24dB").equalsIgnoreCase ("0dB");
    return s;
}

} // namespace stemab
