#pragma once

#include <array>
#include <string>

#include <juce_audio_basics/juce_audio_basics.h>

#include "common/Cancel.h"

namespace stemab
{

/** e.g. "FFmpeg 9.0.2 (libavformat 62.3.100, libavcodec 63.1.102)". Proves FFmpeg is linked and loadable. */
std::string ffmpegVersionString();

//==============================================================================
/** The four Stems of a Stem File, always in this order (CONTEXT.md: "Stem"). */
enum class Stem : int { Drums = 0, Bass, Other, Vocals };

inline constexpr int numStems = 4;

/** "Drums", "Bass", "Other" or "Vocals". */
const char* stemName (Stem) noexcept;

//==============================================================================
/** Result of decodeOriginal(). Check `ok` first; on failure `error` says why and the other members are empty. */
struct DecodedOriginal
{
    bool ok = false;
    juce::String error;

    juce::AudioBuffer<float> audio;     // always stereo; a mono source is duplicated to both channels
    double sampleRate = 0.0;            // the source's own rate (Native Rate)
    juce::int64 declaredLength = 0;     // length the container declares, in samples (0 if unknown)
    juce::StringArray notes;            // non-fatal observations (what libavcodec did, mono duplication, ...)
};

/** Result of decodeStemFile(). Check `ok` first; on failure `error` says why and the other members are empty. */
struct DecodedStems
{
    bool ok = false;
    juce::String error;

    std::array<juce::AudioBuffer<float>, numStems> stems;   // Drums, Bass, Other, Vocals; each stereo, equal length
    double sampleRate = 0.0;            // the Stem File's own rate (Native Rate)
    juce::int64 declaredLength = 0;     // length the container declares for the Stems, in samples (0 if unknown)
    juce::StringArray notes;            // non-fatal observations (zero-padding, libavcodec skip side data, ...)
};

//==============================================================================
/** How decodeOriginal() / decodeStemFile() run. The decoded audio is bit-identical either way. */
struct DecodeOptions
{
    /** Polled per packet (and by every decoder thread); when it returns true decoding stops and the result
        is { ok = false, error = "Cancelled." }. */
    CancelCheck cancel;

    /** true: Stem File - one demux pass on the calling thread feeding four decoder threads (one per Stem)
        through bounded packet queues; Original - FFmpeg frame threading where the codec supports it (FLAC).
        false: everything on the calling thread (the reference path the tests compare against). */
    bool parallel = true;
};

/** Decodes the Original (A): the best audio stream of any file FFmpeg can read, to stereo float.
    Mono is duplicated to both channels; more than two channels is an error.

    Nothing is trimmed, aligned or level-changed. In-process FFmpeg libraries only. Safe to call from any
    worker thread; never throws. */
DecodedOriginal decodeOriginal (const juce::File& file, const DecodeOptions& options = {});

/** Decodes the four Stems of a NI/Traktor Stem File (.stem.m4a) in a single demux pass.

    The file must have exactly five audio streams: the first is the Embedded Master, whose packets are
    discarded without a decoder ever being opened for it; the other four are Drums, Bass, Other, Vocals, in
    that fixed order. All four must share sample rate and channel count. If their decoded lengths differ the
    shorter ones are zero-padded to the longest (noted in `notes`).

    Nothing is trimmed, aligned or level-changed: codec delay/padding is left in place, because the Alignment
    Offset is measured later. In-process FFmpeg libraries only. Safe to call from any worker thread; never
    throws. With options.parallel (the default) the four Stems decode concurrently; the output is
    bit-identical to the sequential path. */
DecodedStems decodeStemFile (const juce::File& file, const DecodeOptions& options = {});

//==============================================================================
/** Offline, whole-buffer sample-rate conversion with r8brain-free-src (24-bit-grade, linear phase).

    Every channel is processed identically (ADR 0004). Output length is exactly
    round (numSamples * toRate / fromRate) and r8brain's latency is compensated, so the output lines up with
    the input in time. Equal rates give a plain copy. If either rate is not positive an empty buffer is
    returned. Safe to call from any worker thread. */
juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& input, double fromRate, double toRate);

} // namespace stemab
