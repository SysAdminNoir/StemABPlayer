#pragma once

#include <array>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>

#include "analysis/Alignment.h"   // StemBuffers

namespace stemab
{

/** Everything the PlaybackEngine plays, immutable once built (CONTEXT.md terms):

      - the Original (A), stereo;
      - the four Stems (Drums, Bass, Other, Vocals), already on A's timeline (alignStems()), stereo;
      - their one shared sample rate.

    All five buffers have exactly A's length. Build it off the audio thread (it allocates), hand it to the
    engine as std::shared_ptr<const Session>; the engine never frees it on the audio thread. */
class Session final
{
public:
    static constexpr int numStems = 4;

    /** Builds a Session. Every buffer is made stereo (a mono buffer is duplicated, extra channels are dropped)
        and every Stem is zero-padded or trimmed to A's length. Nothing else is changed: no gain, no shift
        (ADR 0004). Buffers that already conform are moved, not copied. Returns nullptr if `sampleRate` is not
        positive. Allocates: never call it on the audio thread. */
    static std::shared_ptr<const Session> create (juce::AudioBuffer<float> original,
                                                  StemBuffers alignedStems,
                                                  double sampleRate);

    const juce::AudioBuffer<float>& original() const noexcept           { return a; }
    const juce::AudioBuffer<float>& stem (int index) const noexcept     { return stems[(size_t) index]; }
    const StemBuffers& allStems() const noexcept                        { return stems; }

    double sampleRate() const noexcept      { return rate; }
    juce::int64 length() const noexcept     { return (juce::int64) a.getNumSamples(); }

    /** Raw read pointers for the audio thread: channel 0/1 of A and of each Stem. */
    const float* originalChannel (int ch) const noexcept            { return a.getReadPointer (ch); }
    const float* stemChannel (int index, int ch) const noexcept     { return stems[(size_t) index].getReadPointer (ch); }

    Session (const Session&) = delete;
    Session& operator= (const Session&) = delete;

private:
    Session() = default;

    juce::AudioBuffer<float> a;
    StemBuffers stems;
    double rate = 0.0;
};

} // namespace stemab
