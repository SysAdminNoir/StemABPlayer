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

    /** Can `stems` be played in place: A's sample n -> Stem sample n + offset for every n < length? */
    bool usableInPlace (const StemBuffers& stems, juce::int64 offset, int polarity, juce::int64 length) noexcept
    {
        if (polarity < 0 || offset < 0)
            return false;

        for (const auto& s : stems)
            if (s.getNumChannels() != 2 || (juce::int64) s.getNumSamples() < offset + length)
                return false;

        return true;
    }
}

std::shared_ptr<const Session> Session::create (juce::AudioBuffer<float> original,
                                                StemBuffers alignedStems,
                                                double sampleRate)
{
    auto s = createOriginalOnly (std::move (original), sampleRate);

    if (s == nullptr)
        return nullptr;

    const int length = (int) s->length();

    for (auto& stem : alignedStems)
        stem = toStereoOfLength (std::move (stem), length);

    return withStems (*s, std::make_shared<const StemBuffers> (std::move (alignedStems)), 0, +1);
}

std::shared_ptr<const Session> Session::createOriginalOnly (juce::AudioBuffer<float> original, double sampleRate)
{
    if (! (sampleRate > 0.0))
        return nullptr;

    const int length = original.getNumSamples();

    std::shared_ptr<Session> s (new Session());
    s->a = std::make_shared<const juce::AudioBuffer<float>> (toStereoOfLength (std::move (original), length));
    s->trackLength = length;
    s->rate = sampleRate;
    return s;
}

std::shared_ptr<const Session> Session::createStemsAOnly (std::shared_ptr<const StemBuffers> stemsA, double sampleRate)
{
    if (stemsA == nullptr || ! (sampleRate > 0.0))
        return nullptr;

    int length = 0;
    bool allConform = true;
    for (const auto& stem : *stemsA)
        length = juce::jmax (length, stem.getNumSamples());

    for (const auto& stem : *stemsA)
        if (stem.getNumChannels() != 2 || stem.getNumSamples() != length)
            allConform = false;

    std::shared_ptr<Session> s (new Session());
    s->sourceA = stemsA;
    s->trackLength = length;
    s->rate = sampleRate;

    if (allConform)
    {
        s->storageA = stemsA;
    }
    else
    {
        StemBuffers normalized;
        for (size_t k = 0; k < (size_t) numStems; ++k)
        {
            juce::AudioBuffer<float> copy ((*stemsA)[k]);
            normalized[k] = toStereoOfLength (std::move (copy), length);
        }
        s->storageA = std::make_shared<const StemBuffers> (std::move (normalized));
    }

    for (size_t k = 0; k < (size_t) numStems; ++k)
        for (int ch = 0; ch < 2; ++ch)
            s->stemAPointers[k][(size_t) ch] = (*s->storageA)[k].getNumSamples() > 0
                                                   ? (*s->storageA)[k].getReadPointer (ch)
                                                   : nullptr;

    return s;
}

std::shared_ptr<const Session> Session::originalOnlyOf (const Session& base)
{
    std::shared_ptr<Session> s (new Session());
    s->a = base.a;
    s->sourceA = base.sourceA;
    s->storageA = base.storageA;
    s->stemAPointers = base.stemAPointers;
    s->trackLength = base.trackLength;
    s->rate = base.rate;
    return s;
}

std::shared_ptr<const Session> Session::withStems (const Session& base,
                                                   std::shared_ptr<const StemBuffers> stems,
                                                   juce::int64 offsetSamples,
                                                   int polarity)
{
    if (stems == nullptr)
        return nullptr;

    std::shared_ptr<Session> s (new Session());
    s->a = base.a;
    s->sourceA = base.sourceA;
    s->storageA = base.storageA;
    s->stemAPointers = base.stemAPointers;
    s->trackLength = base.trackLength;
    s->rate = base.rate;
    s->source = stems;
    s->offset = offsetSamples;
    s->polarity = polarity < 0 ? -1 : +1;

    const juce::int64 length = base.length();
    juce::int64 start = 0;

    if (usableInPlace (*stems, offsetSamples, s->polarity, length))
    {
        s->storage = stems;
        start = offsetSamples;
    }
    else
    {
        // Shift (and/or invert) once into stereo copies of A's length.
        auto aligned = alignStems (*stems, offsetSamples, s->polarity, length);

        for (auto& stem : aligned)
            stem = toStereoOfLength (std::move (stem), (int) length);

        s->storage = std::make_shared<const StemBuffers> (std::move (aligned));
    }

    for (size_t k = 0; k < (size_t) numStems; ++k)
        for (int ch = 0; ch < 2; ++ch)
            s->stemPointers[k][(size_t) ch] = (*s->storage)[k].getNumSamples() > 0
                                                  ? (*s->storage)[k].getReadPointer (ch) + start
                                                  : nullptr;

    return s;
}

} // namespace stemab
