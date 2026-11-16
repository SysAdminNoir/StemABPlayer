#include "load/AlignmentCache.h"

#include <algorithm>

namespace stemab
{

namespace
{
    constexpr int formatVersion = 1;

    juce::var toVar (const FileIdentity& id)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("path", id.path);
        o->setProperty ("size", id.size);
        o->setProperty ("modified", id.modifiedMs);
        return juce::var (o);
    }

    FileIdentity identityFromVar (const juce::var& v)
    {
        FileIdentity id;
        if (! v.isObject())
            return id;

        id.path = v.getProperty ("path", {}).toString();
        id.size = (juce::int64) v.getProperty ("size", -1);
        id.modifiedMs = (juce::int64) v.getProperty ("modified", 0);
        return id;
    }
}

FileIdentity FileIdentity::of (const juce::File& f)
{
    FileIdentity id;
    if (! f.existsAsFile())
        return id;

    id.path = f.getFullPathName();
    id.size = f.getSize();
    id.modifiedMs = f.getLastModificationTime().toMilliseconds();
    return id;
}

FileIdentity FileIdentity::of (const StemSource& source)
{
    if (! source.isSet())
        return of (source.stemFile);

    // A Stem Set: one identity over its slots, in Stem order. Any file's path, size or time changes it.
    FileIdentity id;
    juce::StringArray paths;
    juce::uint64 mix = 1469598103934665603ull;
    juce::int64 size = 0;

    for (const auto& f : source.setFiles)
    {
        if (f == juce::File())
        {
            paths.add ({});
            mix = (mix ^ 0x9e3779b97f4a7c15ull) * 1099511628211ull;
            continue;
        }

        const auto one = of (f);
        if (! one.isValid())
            return {};          // a missing file: no identity (no cache, no stash)

        paths.add (one.path);
        size += one.size;
        mix = (mix ^ (juce::uint64) one.modifiedMs) * 1099511628211ull;
    }

    id.path = "stem set:" + paths.joinIntoString ("|");
    id.size = size;
    id.modifiedMs = (juce::int64) (mix & 0x7fffffffffffffffull);
    return id;
}

//==============================================================================
AlignmentCache::AlignmentCache (juce::File jsonFile) : file (std::move (jsonFile)) {}

juce::File AlignmentCache::defaultFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("StemABPlayer")
               .getChildFile ("alignment-cache.json");
}

std::optional<CachedAlignment> AlignmentCache::find (const FileIdentity& original, const FileIdentity& stemFile)
{
    if (! original.isValid() || ! stemFile.isValid())
        return std::nullopt;

    const std::lock_guard lock (mutex);
    loadIfNeeded();

    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        if (it->original == original && it->stemFile == stemFile)
            return it->alignment;

    return std::nullopt;
}

void AlignmentCache::store (const FileIdentity& original, const FileIdentity& stemFile, const CachedAlignment& alignment)
{
    if (! original.isValid() || ! stemFile.isValid())
        return;

    const std::lock_guard lock (mutex);
    loadIfNeeded();

    // One entry per pair of paths: a changed file replaces its old (now stale) entry.
    entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const Entry& e)
                                   { return e.original.path == original.path && e.stemFile.path == stemFile.path; }),
                   entries.end());

    entries.push_back ({ original, stemFile, alignment });

    if ((int) entries.size() > maxEntries)
        entries.erase (entries.begin(), entries.begin() + ((int) entries.size() - maxEntries));

    save();
}

int AlignmentCache::size()
{
    const std::lock_guard lock (mutex);
    loadIfNeeded();
    return (int) entries.size();
}

void AlignmentCache::loadIfNeeded()
{
    if (loaded)
        return;

    loaded = true;
    entries.clear();

    if (! file.existsAsFile())
        return;

    const auto root = juce::JSON::parse (file.loadFileAsString());

    if ((int) root.getProperty ("version", 0) != formatVersion)
        return;

    if (const auto* list = root.getProperty ("pairs", {}).getArray())
    {
        for (const auto& v : *list)
        {
            Entry e;
            e.original = identityFromVar (v.getProperty ("original", {}));
            e.stemFile = identityFromVar (v.getProperty ("stemFile", {}));
            e.alignment.offsetSamples = (juce::int64) v.getProperty ("offsetSamples", 0);
            e.alignment.polarity = (int) v.getProperty ("polarity", 1) < 0 ? -1 : +1;
            e.alignment.confidence = (double) v.getProperty ("confidence", 0.0);
            e.alignment.lowConfidence = (bool) v.getProperty ("lowConfidence", true);

            if (e.original.isValid() && e.stemFile.isValid())
                entries.push_back (std::move (e));
        }
    }
}

void AlignmentCache::save()
{
    juce::Array<juce::var> list;

    for (const auto& e : entries)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("original", toVar (e.original));
        o->setProperty ("stemFile", toVar (e.stemFile));
        o->setProperty ("offsetSamples", e.alignment.offsetSamples);
        o->setProperty ("polarity", e.alignment.polarity);
        o->setProperty ("confidence", e.alignment.confidence);
        o->setProperty ("lowConfidence", e.alignment.lowConfidence);
        list.add (juce::var (o));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("version", formatVersion);
    root->setProperty ("pairs", list);

    if (file.getParentDirectory().createDirectory().wasOk())
        file.replaceWithText (juce::JSON::toString (juce::var (root)));   // a failed write just means no cache
}

} // namespace stemab
