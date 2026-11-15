#pragma once

#include <array>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "common/Cancel.h"

namespace stemab
{

/** The four Stems (Drums, Bass, Other, Vocals), as decodeStemFile() returns them. */
using StemBuffers = std::array<juce::AudioBuffer<float>, 4>;

//==============================================================================
/** Tuning for measureAlignment(). The defaults are what the app uses; tests shrink the windows for speed. */
struct AlignmentOptions
{
    int    windowSamples = 1 << 16;    // length of each measurement window (~1.5 s at 44.1 kHz); shrunk to A's length
    double maxLagSeconds = 0.5;        // search range: offsets in [-maxLag, +maxLag]
    int    numWindows    = 3;          // windows wanted (at ~20 %, 50 %, 80 % of A, then fallbacks)

    /** > 1: a coarse cross-correlation of A and the Stem Sum averaged over blocks of this many samples finds
        candidate lags (FFT over the whole range), then the exact full-rate correlation is evaluated around the
        best few candidates and its peak is taken - the result is still whole-sample exact.
        1: one full-rate FFT over the whole lag range (the reference path). */
    int    decimation    = 4;
};

/** One measurement window of measureAlignment(). */
struct WindowMeasure
{
    juce::int64 start = 0;         // first sample of the window on A's timeline
    int  length = 0;               // window length in samples
    bool used = false;             // false: skipped because A or the Stem Sum is near-silent there (< -60 dBFS)

    juce::int64 lagSamples = 0;    // correlation peak, same sign convention as AlignmentResult::offsetSamples
    int    polarity = +1;          // sign of the correlation peak
    double correlation = 0.0;      // Pearson coefficient of A and the Stem Sum at lagSamples (signed, -1..1)
    double peakRatio = 0.0;        // |main peak| / |highest peak more than 100 samples away|; 0 = no other peak in range
                                   // (with decimation > 1 measured on the coarse correlation; diagnostic only)
    double subSampleLag = 0.0;     // diagnostic only: parabolic interpolation of the peak (never applied)
};

/** Result of measureAlignment(). */
struct AlignmentResult
{
    bool ok = false;               // false: nothing could be measured (empty input, silence); see `error`
    juce::String error;

    /** How many samples the Stem Sum (B) is delayed relative to the Original (A):

            A[n] ~= polarity * B[n + offsetSamples]

        Positive: B's content arrives later than A's (e.g. AAC encoder priming), so B must be moved earlier.
        This is the value to pass to alignStems(). When `ok` is false it is 0. */
    juce::int64 offsetSamples = 0;
    int    polarity = +1;          // +1, or -1 when B is inverted relative to A
    double confidence = 0.0;       // 0..1: (fraction of windows agreeing) * median |Pearson| of the agreeing windows
    bool   lowConfidence = true;   // see measureAlignment() for the rule

    std::vector<WindowMeasure> windows;   // every window tried, including skipped ones
};

/** Measures the Alignment Offset and polarity of the Stem Sum against the Original (ADR 0003).

    Both A and the Stem Sum are downmixed to mono, (L+R)/2, inside the windows only. Several windows of A
    (default 2^16 samples at ~20 %, 50 % and 80 % of the track; windows whose A or Stem Sum is below
    -60 dBFS RMS are skipped and replaced by others at 35 %, 65 %, 10 %, 90 %) are cross-correlated against
    the Stem Sum over a +/-0.5 s lag range (default), at whole-sample precision: a coarse FFT pass on
    decimated audio, then the exact correlation around the best coarse candidates (see
    AlignmentOptions::decimation). The polarity is the sign of the exact correlation peak.

    The final offset is the most common window lag (ties: the one nearest the median). Confidence and the
    low-confidence flag:
      - confidence    = (windows agreeing exactly with the offset / windows used) * median |Pearson| of those
      - lowConfidence = any used window disagrees in lag or polarity, OR median |Pearson| < 0.7,
                        OR the offset sits at the edge of the search range.
    |Pearson| < 0.7 means even a gain-matched null could not exceed ~3 dB: B is not this A (or is broken).
    A single window (very short file) cannot be cross-checked, so only the Pearson rule applies to it.

    Preconditions (the caller's job): A and the Stems share one sample rate, `sampleRate`. A and the Stems may
    differ in length; the shorter side is treated as zero beyond its end. Windows shrink to fit short files.

    `cancel` is polled between windows; when it fires the result is { ok = false, error = "Cancelled." }.

    Offline: allocates and runs FFTs; call it on a worker thread, never on the audio thread. Never throws. */
AlignmentResult measureAlignment (const juce::AudioBuffer<float>& original,
                                  const StemBuffers& stems,
                                  double sampleRate,
                                  const AlignmentOptions& options = {},
                                  const CancelCheck& cancel = {});

/** Puts the Stems on A's timeline: returns four buffers of `timelineLength` samples (A's length), each with
    the same channel count as its source, where

        out[n] = polarity * stem[n + offsetSamples]      (0 where stem[n + offsetSamples] does not exist)

    Any integer offset works (measured or manually nudged, positive or negative); samples shifted outside
    the timeline are dropped and gaps are zero-padded. Every Stem gets exactly the same shift and polarity and
    nothing else: no gain, no trimming of content inside the timeline (ADR 0004). `polarity` < 0 means -1,
    anything else +1. Offline (allocates). */
StemBuffers alignStems (const StemBuffers& stems,
                        juce::int64 offsetSamples,
                        int polarity,
                        juce::int64 timelineLength);

} // namespace stemab
