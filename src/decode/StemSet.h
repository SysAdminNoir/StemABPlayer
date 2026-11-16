#pragma once

#include <array>

#include <juce_core/juce_core.h>

#include "decode/Decode.h"

namespace stemab
{

/** A Stem Set: separate audio files, one per Stem (Drums, Bass, Other, Vocals - CONTEXT.md "Stem"), e.g. the
    WAVs a separator like Demucs or UVR writes. An empty juce::File() is a Stem without a file: it plays as
    silence (the Residual is still A minus the Stems that exist). */
using StemSetFiles = std::array<juce::File, numStems>;

/** What the B side is made of: either one Stem File (.stem.m4a) or a Stem Set. A plain value; the loaders
    and the stash key it by its files, in Stem order.

    Implicitly constructible from a juce::File, which means "this Stem File". */
struct StemSource
{
    juce::File stemFile;            // a Stem File, or empty for a Stem Set
    StemSetFiles setFiles {};       // a Stem Set (only when stemFile is empty)

    StemSource() = default;
    StemSource (const juce::File& file) : stemFile (file) {}   // NOLINT: implicit on purpose (a Stem File)

    static StemSource fromSet (const StemSetFiles& files);

    /** True for a Stem Set (at least one slot has a file), false for a Stem File or nothing. */
    bool isSet() const noexcept;
    /** Neither a Stem File nor any Stem Set file. */
    bool isEmpty() const noexcept;
    /** The Stem File exists, or every file of the Stem Set does (and there is at least one). */
    bool existsAsFiles() const;
    /** Stem `i` has a file (always true for a Stem File). */
    bool hasStem (int index) const noexcept;
    /** The number of Stems with a file (4 for a Stem File, 0 when empty). */
    int numFiles() const noexcept;

    /** The Stem File, or the Stem Set's files in Stem order (empty slots left out). */
    juce::Array<juce::File> files() const;
    /** The first of files() that does not exist (an empty File if all do). */
    juce::File firstMissingFile() const;

    /** A short label for the B file screen: the Stem File's name, or for a Stem Set the name its files share
        (a common filename prefix, else their folder) plus "(4 files)", or "4 stem files · drums.wav, bass.wav, …". */
    juce::String displayName() const;
    /** Full path(s), one per line; for a Stem Set "Drums: C:\...\drums.wav", "Bass: (no file)", ... */
    juce::String describePaths() const;

    bool operator== (const StemSource& o) const noexcept    { return stemFile == o.stemFile && setFiles == o.setFiles; }
    bool operator!= (const StemSource& o) const noexcept    { return ! operator== (o); }
};

//==============================================================================
/** Result of mapStemFiles(). */
struct StemMapping
{
    bool ok = false;
    juce::String error;             // why not, when ! ok
    StemSetFiles files {};          // Drums, Bass, Other, Vocals (empty File = no file for that Stem)
};

/** Which Stem a file is, from keywords in its name (case-insensitive, whole words; the last keyword in the
    name wins, so "The Other Side (Vocals)" is Vocals):

      drums / drum / kick / perc / percussion        -> Drums
      bass                                           -> Bass
      vocals / vocal / vox / voice / acapella        -> Vocals
      other / instrumental / inst / accomp(animent) / music / melody, and "no vocals" (a 2-stem
      separator's "everything else" - "no_vocals", "No Drums", ...)    -> Other

    Returns -1 (as an int Stem index otherwise) for a name with none of them. */
int stemIndexFromFileName (const juce::String& fileName);

/** Assigns 1-4 files to the four Stems by their names (stemIndexFromFileName); files that match nothing fill
    the remaining slots in order, Other first, then Drums, Bass, Vocals. More than four files, none, or two
    files that both name the same Stem are an error. */
StemMapping mapStemFiles (const juce::Array<juce::File>& files);

//==============================================================================
/** Decodes a Stem Set: each file with decodeOriginal() (any format the Original accepts), concurrently when
    options.parallel. Mono files are duplicated to stereo; more than two channels is an error. All files
    must share one sample rate (they are never resampled relative to each other - that is an error), shorter
    ones are zero-padded to the longest, and a Stem without a file is a silent stereo buffer of that length.
    The result has the shape of decodeStemFile()'s, so everything after decoding treats both alike.

    Cancel: as decodeStemFile() ("Cancelled."). The first failing file cancels the others. Never throws. */
DecodedStems decodeStemSet (const StemSetFiles& files, const DecodeOptions& options = {});

/** decodeStemFile() or decodeStemSet(), whichever `source` is. */
DecodedStems decodeStemSource (const StemSource& source, const DecodeOptions& options = {});

/** The number of audio streams in `file`'s container, or -1 if FFmpeg cannot open it. A Stem File has five
    (StemSource: tells a .stem.m4a from a single-stem .m4a). Reads only the container header. */
int countAudioStreams (const juce::File& file);

/** countAudioStreams (file) == 5: an NI Stem File. */
bool looksLikeStemFile (const juce::File& file);

} // namespace stemab
