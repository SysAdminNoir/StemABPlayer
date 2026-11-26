#pragma once

#include <array>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>

#include "analysis/Alignment.h"   // StemBuffers

namespace stemab
{

/** Everything the PlaybackEngine plays, immutable once built (CONTEXT.md terms):

      - Side A: either the Original stereo track, OR four Stems (Stem A: Drums, Bass, Other, Vocals);
      - Side B: optionally four Stems (Stem B: Drums, Bass, Other, Vocals), on A's timeline, stereo;
      - their one shared sample rate.

    A Session without Side B ("Original only" or "Stem A only") plays A; B and the Residual are silent.
    It is what the loader publishes the moment A is decoded. When Side B and the Alignment Offset arrive,
    withStems() builds the upgraded Session, which shares the very same Side A buffer(s) (not a copy), so
    the engine can swap it in without interrupting playback (PlaybackEngine::upgradeSession()).

    Stems are held as decoded and placed on A's timeline by (offset, polarity): A's sample n plays with Stem
    sample n + offset, times polarity. When that needs no shifting copy (stereo, polarity +1, offset >= 0 and
    the Stems long enough) the decoded buffers are shared as they are, so building the Session copies nothing
    and a corrected offset is just another cheap Session over the same buffers.

    Build it off the audio thread (it may allocate), hand it to the engine as std::shared_ptr<const Session>;
    the engine never frees it on the audio thread. */
class Session final
{
public:
    static constexpr int numStems = 4;

    /** A plus Stems already on A's timeline (alignStems()). Every buffer is made stereo (a mono buffer is
        duplicated, extra channels are dropped) and every Stem is zero-padded or trimmed to A's length.
        Nothing else is changed: no gain, no shift (ADR 0004). Buffers that already conform are moved, not
        copied. Returns nullptr if `sampleRate` is not positive. Allocates: never call it on the audio thread. */
    static std::shared_ptr<const Session> create (juce::AudioBuffer<float> original,
                                                  StemBuffers alignedStems,
                                                  double sampleRate);

    /** A only (made stereo as above; moved if it already is). Returns nullptr if `sampleRate` is not positive. */
    static std::shared_ptr<const Session> createOriginalOnly (juce::AudioBuffer<float> original, double sampleRate);

    /** Side A as four Stems only (zero-copy when already stereo and equal length). Returns nullptr if
        `stemsA` is null or `sampleRate` is not positive. */
    static std::shared_ptr<const Session> createStemsAOnly (std::shared_ptr<const StemBuffers> stemsA, double sampleRate);

    /** A Session without Side B Stems that shares `base`'s Side A (Original or Stem A, no copy). */
    static std::shared_ptr<const Session> originalOnlyOf (const Session& base);

    /** The upgrade: `base`'s Side A (shared, no copy) plus `stems` (Side B) - decoded, not yet aligned, at the
        same sample rate - put on A's timeline by the Alignment Offset and polarity (see the class comment; the
        same convention as alignStems()). Zero-copy when possible, otherwise one aligned stereo copy is made.
        Returns nullptr if `stems` is null. Allocates: never call it on the audio thread. */
    static std::shared_ptr<const Session> withStems (const Session& base,
                                                     std::shared_ptr<const StemBuffers> stems,
                                                     juce::int64 offsetSamples,
                                                     int polarity);

    const juce::AudioBuffer<float>& original() const noexcept           { return *a; }

    bool hasOriginal() const noexcept                                   { return a != nullptr; }
    bool hasStemsA() const noexcept                                     { return sourceA != nullptr; }
    bool hasStems() const noexcept                                      { return source != nullptr; }
    /** True if both Sessions play the very same Side A buffer(s) (one is an upgrade of the other). */
    bool sharesOriginalWith (const Session& other) const noexcept
    {
        return (a != nullptr && a == other.a) || (sourceA != nullptr && sourceA == other.sourceA);
    }

    /** The Side A Stems (null when Side A is an Original track). */
    const std::shared_ptr<const StemBuffers>& decodedStemsA() const noexcept { return sourceA; }
    /** The Side B Stems as given to withStems() (or the aligned ones given to create()), and how they are placed. */
    const std::shared_ptr<const StemBuffers>& decodedStems() const noexcept  { return source; }
    juce::int64 alignmentOffset() const noexcept    { return offset; }
    int alignmentPolarity() const noexcept          { return polarity; }

    double sampleRate() const noexcept      { return rate; }
    juce::int64 length() const noexcept     { return trackLength; }

    /** Raw read pointers for the audio thread: channel 0/1 of Original (nullptr when Side A is Stems),
        Side A Stems (nullptr when Side A is Original), and Side B Stems (nullptr when !hasStems()).
        Valid for samples [0, length()). */
    const float* originalChannel (int ch) const noexcept            { return a != nullptr ? a->getReadPointer (ch) : nullptr; }
    const float* stemAChannel (int index, int ch) const noexcept    { return stemAPointers[(size_t) index][(size_t) ch]; }
    const float* stemChannel (int index, int ch) const noexcept     { return stemPointers[(size_t) index][(size_t) ch]; }

    Session (const Session&) = delete;
    Session& operator= (const Session&) = delete;

private:
    Session() = default;

    std::shared_ptr<const juce::AudioBuffer<float>> a;   // Original (null when Side A is Stem A)
    std::shared_ptr<const StemBuffers> sourceA;          // Stem A as decoded (null when Side A is Original)
    std::shared_ptr<const StemBuffers> storageA;         // normalized stereo Stem A (== sourceA when zero-copy)
    std::array<std::array<const float*, 2>, numStems> stemAPointers {};

    std::shared_ptr<const StemBuffers> source;           // Stem B as decoded (null: Side A only)
    std::shared_ptr<const StemBuffers> storage;          // what the pointers below point into (== source when zero-copy)
    std::array<std::array<const float*, 2>, numStems> stemPointers {};
    juce::int64 offset = 0;
    int polarity = +1;
    juce::int64 trackLength = 0;
    double rate = 0.0;
};

} // namespace stemab
