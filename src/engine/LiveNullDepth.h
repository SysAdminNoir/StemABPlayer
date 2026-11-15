#pragma once

#include "engine/Session.h"

namespace stemab
{

/** Window of the Live Null Depth (CONTEXT.md: "Null Depth"), centred on the playhead. */
inline constexpr double liveNullDepthWindowSeconds = 0.4;

struct LiveNullDepth
{
    double nullDepthDb = 0.0;   // 10*log10 (energy(A) / energy(Residual)) over the window; always finite
    bool quiet = true;          // A is below quietBlockThresholdDbfs RMS here: too quiet to judge (show "-")
};

/** Live Null Depth: the Residual (A - all four Stems, Mute/Solo never enter) against A, over
    `windowSeconds` centred on `centreSample` (shifted to stay inside the track, shortened if the track is
    shorter), all channels, accumulated in double precision.

    Pure function of the immutable Session: call it on the UI thread (it reads ~0.4 s of audio), never in the
    audio callback. An empty session gives { 0 dB, quiet = true }. */
LiveNullDepth measureLiveNullDepth (const Session& session,
                                    juce::int64 centreSample,
                                    double windowSeconds = liveNullDepthWindowSeconds) noexcept;

} // namespace stemab
