#include "engine/LiveNullDepth.h"

#include <cmath>

#include "analysis/NullDepth.h"

namespace stemab
{

LiveNullDepth measureLiveNullDepth (const Session& session, juce::int64 centreSample, double windowSeconds) noexcept
{
    LiveNullDepth result;

    const juce::int64 len = session.length();
    if (len <= 0 || ! (windowSeconds > 0.0))
        return result;

    const juce::int64 window = juce::jlimit ((juce::int64) 1, len,
                                             (juce::int64) std::llround (windowSeconds * session.sampleRate()));
    const juce::int64 start = juce::jlimit ((juce::int64) 0, len - window, centreSample - window / 2);
    const int s0 = (int) start, n = (int) window;

    double energyA = 0.0, energyR = 0.0;

    for (int ch = 0; ch < 2; ++ch)
    {
        const float* a = session.originalChannel (ch) + s0;
        const float* st[Session::numStems];
        for (int k = 0; k < Session::numStems; ++k)
            st[k] = session.stemChannel (k, ch) + s0;

        for (int i = 0; i < n; ++i)
        {
            const double x = a[i];
            const double r = x - ((double) st[0][i] + (double) st[1][i] + (double) st[2][i] + (double) st[3][i]);
            energyA += x * x;
            energyR += r * r;
        }
    }

    const double meanSquareA = energyA / (2.0 * (double) n);
    result.quiet = meanSquareA < std::pow (10.0, quietBlockThresholdDbfs / 10.0);
    result.nullDepthDb = nullDepthDbFromEnergies (energyA, energyR);
    return result;
}

} // namespace stemab
