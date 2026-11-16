#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <memory>
#include <optional>

namespace stemab
{

/** What "Resume last session on launch" brings back: the pair, the playhead and the mix settings.

    A plain value (no engine or GUI types) so it can be serialised and tested without a device or a window.
    The playhead is in samples on A's timeline (the Session's source rate). The Master Gain is in dB with
    -infinity meaning silent (the bottom of the fader).

    B is either one Stem File (stemFilePath) or a Stem Set (stemSetPaths: one path per Stem, empty = no file;
    used only when stemFilePath is empty). Older settings files have only the Stem File path and still load.

    Stored as one XML element in the settings file (AppSettings::Keys::session). Parsing is forgiving: missing
    attributes take their defaults and out-of-range values are clamped; only a missing pair makes it invalid. */
struct SessionState
{
    static constexpr int numStems = 4;

    juce::String originalPath, stemFilePath;
    std::array<juce::String, numStems> stemSetPaths {};    // Drums, Bass, Other, Vocals (a Stem Set)
    juce::int64 playheadSample = 0;
    float crossfade = 0.0f;                     // 0 = all A, 1 = all B
    bool equalPowerCurve = true;
    bool residualListen = false;                // Listen mode Residual (else A/B)
    std::array<bool, numStems> muted {}, soloed {};
    double masterGainDb = 0.0;                  // <= 0; -infinity = silent
    bool loopEnabled = false;
    bool residualViewAmplified = true;          // the R lane's +24 dB view (else 0 dB, the true level)

    bool operator== (const SessionState&) const = default;

    /** B is a Stem Set: no Stem File path and at least one Stem Set path. */
    bool hasStemSet() const;

    static constexpr const char* xmlTag = "SESSION";

    std::unique_ptr<juce::XmlElement> toXml() const;

    /** nullopt for nullptr, another tag, or no pair. */
    static std::optional<SessionState> fromXml (const juce::XmlElement*);
};

} // namespace stemab
