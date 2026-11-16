#pragma once

#include <mutex>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "decode/StemSet.h"

namespace stemab
{

/** What identifies a file for the cache: full path, size and last-modified time. Any change -> a miss. */
struct FileIdentity
{
    juce::String path;
    juce::int64 size = -1;
    juce::int64 modifiedMs = 0;

    static FileIdentity of (const juce::File&);
    /** A Stem File's identity, or one over all the files of a Stem Set (in Stem order; path "stem set:a|b||d").
        Invalid if a set file does not exist. */
    static FileIdentity of (const StemSource&);
    bool isValid() const noexcept                               { return path.isNotEmpty() && size >= 0; }
    bool operator== (const FileIdentity& o) const noexcept      { return path == o.path && size == o.size && modifiedMs == o.modifiedMs; }
    bool operator!= (const FileIdentity& o) const noexcept      { return ! operator== (o); }
};

/** A pair's measured Alignment Offset (AlignmentResult's essentials). */
struct CachedAlignment
{
    juce::int64 offsetSamples = 0;
    int polarity = +1;
    double confidence = 0.0;
    bool lowConfidence = false;

    bool sameAlignment (const CachedAlignment& o) const noexcept   { return offsetSamples == o.offsetSamples && polarity == o.polarity; }
};

/** Persistent cache of measured alignments, keyed by both files' FileIdentity, stored as a small JSON file
    (by default %APPDATA%/StemABPlayer/alignment-cache.json). Loaded lazily on first use; every store()
    rewrites the file (atomically, via a temporary file). Thread-safe. A missing or corrupt file is an empty
    cache, never an error. Holds at most maxEntries pairs (the least recently stored go first). */
class AlignmentCache
{
public:
    static constexpr int maxEntries = 1000;

    explicit AlignmentCache (juce::File jsonFile = defaultFile());

    static juce::File defaultFile();
    const juce::File& getFile() const noexcept      { return file; }

    std::optional<CachedAlignment> find (const FileIdentity& original, const FileIdentity& stemFile);
    void store (const FileIdentity& original, const FileIdentity& stemFile, const CachedAlignment& alignment);

    int size();

private:
    struct Entry
    {
        FileIdentity original, stemFile;
        CachedAlignment alignment;
    };

    void loadIfNeeded();
    void save();

    juce::File file;
    std::mutex mutex;
    bool loaded = false;
    std::vector<Entry> entries;   // oldest first
};

} // namespace stemab
