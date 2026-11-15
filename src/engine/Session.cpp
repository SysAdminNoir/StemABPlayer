#include "engine/Session.h"

namespace stemab
{

namespace
{
    /** Returns `in` as a stereo buffer of exactly `length` samples: moved if it already is one, otherwise
        copied (mono duplicated, channels beyond 2 dropped, zero-padded or trimmed). No gain change. */
    juce::AudioBuffer<float> toStereoOfLength (juce::AudioBuffer<float>&& in, int length)
    {
        if (in.getNumChannels() == 2 && in.getNumSamples() == length)
            return std::move (in);

        juce::AudioBuffer<float> out (2, length);
        out.clear();

        const int srcChannels = in.getNumChannels();
        const int toCopy = juce::jmin (length, in.getNumSamples());

        if (srcChannels > 0 && toCopy > 0)
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, 0, in, juce::jmin (ch, srcChannels - 1), 0, toCopy);

        return out;
    }
}

std::shared_ptr<const Session> Session::create (juce::AudioBuffer<float> original,
                                                StemBuffers alignedStems,
                                                double sampleRate)
{
    if (! (sampleRate > 0.0))
        return nullptr;

    const int length = original.getNumSamples();

    std::shared_ptr<Session> s (new Session());
    s->a = toStereoOfLength (std::move (original), length);

    for (size_t i = 0; i < alignedStems.size(); ++i)
        s->stems[i] = toStereoOfLength (std::move (alignedStems[i]), length);

    s->rate = sampleRate;
    return s;
}

} // namespace stemab
