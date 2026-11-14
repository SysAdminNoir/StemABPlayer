#include "decode/Decode.h"

#include <cmath>
#include <vector>

#include "CDSPResampler.h"   // r8brain-free-src; kept out of every public header

namespace stemab
{

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& input, double fromRate, double toRate)
{
    if (! (fromRate > 0.0) || ! (toRate > 0.0))
        return {};

    if (fromRate == toRate)
        return input;                                    // plain copy

    const int numChannels = input.getNumChannels();
    const int numIn       = input.getNumSamples();
    const auto numOutWide = (juce::int64) std::llround ((double) numIn * toRate / fromRate);

    if (numOutWide > 0x7ffffff0)
        return {};

    const int numOut = (int) numOutWide;
    juce::AudioBuffer<float> output (numChannels, numOut);
    output.clear();

    if (numIn == 0 || numOut == 0)
        return output;

    // One resampler per channel, all constructed identically (ADR 0004). oneshot() takes care of r8brain's
    // latency (the first output sample corresponds to the first input sample) and of flushing the tail with
    // zeros, producing exactly numOut samples.
    constexpr int maxInputBlock = 1 << 16;
    std::vector<double> out ((size_t) numOut);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        r8b::CDSPResampler24 resampler (fromRate, toRate, juce::jmin (numIn, maxInputBlock));
        resampler.oneshot (input.getReadPointer (ch), numIn, out.data(), numOut);

        float* dst = output.getWritePointer (ch);

        for (int i = 0; i < numOut; ++i)
            dst[i] = (float) out[(size_t) i];
    }

    return output;
}

} // namespace stemab
