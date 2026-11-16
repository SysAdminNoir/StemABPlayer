#include "decode/StemSet.h"

#include <atomic>
#include <cmath>
#include <exception>
#include <thread>
#include <vector>

namespace stemab
{

namespace
{
    const char* const cancelledMessage = "Cancelled.";     // (as decodeStemFile's)

    juce::String fileStem (const juce::String& fileName)
    {
        return fileName.containsChar ('.') ? fileName.upToLastOccurrenceOf (".", false, false) : fileName;
    }

    /** Lower-case words: runs of letters and digits. */
    juce::StringArray wordsOf (const juce::String& text)
    {
        juce::StringArray words;
        juce::String word;
        const auto lower = text.toLowerCase();      // (kept alive: the loop walks its characters)

        for (auto p = lower.getCharPointer(); ! p.isEmpty(); ++p)
        {
            const auto c = *p;
            if (juce::CharacterFunctions::isLetterOrDigit (c))
            {
                word += juce::String::charToString (c);
            }
            else if (word.isNotEmpty())
            {
                words.add (word);
                word.clear();
            }
        }

        if (word.isNotEmpty())
            words.add (word);

        return words;
    }

    int keywordStem (const juce::String& w)
    {
        static const juce::StringArray drums  { "drums", "drum", "kick", "kicks", "perc", "percs", "percussion" };
        static const juce::StringArray bass   { "bass", "bassline" };
        static const juce::StringArray other  { "other", "others", "instrumental", "instrumentals", "inst", "instrum",
                                                "accomp", "accompaniment", "music", "melody", "melodies", "karaoke" };
        static const juce::StringArray vocals { "vocals", "vocal", "vox", "voice", "voices", "acapella", "acappella",
                                                "capella", "cappella" };

        if (drums.contains (w))  return (int) Stem::Drums;
        if (bass.contains (w))   return (int) Stem::Bass;
        if (vocals.contains (w)) return (int) Stem::Vocals;
        if (other.contains (w))  return (int) Stem::Other;
        return -1;
    }

    bool isNegation (const juce::String& w)
    {
        return w == "no" || w == "without" || w == "non" || w == "minus";
    }

    bool sameRate (double a, double b) noexcept     { return std::abs (a - b) < 0.5; }
    juce::String hz (double rate)                   { return juce::String (rate, 0) + " Hz"; }

    juce::String commonPrefix (const juce::StringArray& names)
    {
        if (names.isEmpty())
            return {};

        auto prefix = names[0];
        for (const auto& n : names)
        {
            int i = 0;
            while (i < prefix.length() && i < n.length() && juce::CharacterFunctions::toLowerCase (prefix[i])
                                                              == juce::CharacterFunctions::toLowerCase (n[i]))
                ++i;
            prefix = prefix.substring (0, i);
        }
        return prefix;
    }

    /** A filename prefix worth showing: trimmed of separators, at least three characters, not just digits. */
    juce::String meaningfulPrefix (juce::String prefix)
    {
        const juce::String separators (" _-.([{");
        while (prefix.isNotEmpty() && separators.containsChar (prefix.getLastCharacter()))
            prefix = prefix.dropLastCharacters (1);
        while (prefix.isNotEmpty() && separators.containsChar (prefix[0]))
            prefix = prefix.substring (1);

        if (prefix.length() < 3 || prefix.containsOnly ("0123456789"))
            return {};
        return prefix;
    }

    struct Part
    {
        int stem = 0;
        juce::File file;
        DecodedOriginal decoded;
    };

    DecodedStems decodeStemSetImpl (const StemSetFiles& files, const DecodeOptions& options)
    {
        DecodedStems result;

        std::vector<Part> parts;
        for (int s = 0; s < numStems; ++s)
            if (files[(size_t) s] != juce::File())
                parts.push_back ({ s, files[(size_t) s], {} });

        if (parts.empty())
        {
            result.error = "No stem files.";
            return result;
        }

        // The first file that fails stops the others (their decoders poll this too).
        std::atomic<bool> failed { false };
        const CancelCheck cancel = [&failed, outer = options.cancel] { return failed.load() || isCancelled (outer); };
        const DecodeOptions partOptions { cancel, options.parallel };

        const auto run = [&] (Part& p)
        {
            p.decoded = decodeOriginal (p.file, partOptions);
            if (! p.decoded.ok)
                failed = true;
        };

        if (options.parallel && parts.size() > 1)
        {
            std::vector<std::thread> threads;
            threads.reserve (parts.size() - 1);

            try
            {
                for (size_t i = 1; i < parts.size(); ++i)
                    threads.emplace_back (run, std::ref (parts[i]));
            }
            catch (...)
            {
                failed = true;      // could not start a thread: stop the ones that did start, then report
            }

            run (parts[0]);

            for (auto& t : threads)
                t.join();

            if (threads.size() != parts.size() - 1 && ! isCancelled (options.cancel))
            {
                result.error = "Could not start the decoder threads.";
                return result;
            }
        }
        else
        {
            for (auto& p : parts)
            {
                run (p);
                if (! p.decoded.ok)
                    break;
            }
        }

        if (isCancelled (options.cancel))
        {
            result.error = cancelledMessage;
            return result;
        }

        // The file that failed on its own (the others may only have been stopped because of it).
        for (const auto& p : parts)
        {
            if (! p.decoded.ok && p.decoded.error != cancelledMessage)
            {
                result.error = p.file.getFileName() + " (" + stemName ((Stem) p.stem) + "): " + p.decoded.error;
                return result;
            }
        }

        for (const auto& p : parts)
        {
            if (! p.decoded.ok)
            {
                result.error = p.file.getFileName() + ": " + p.decoded.error;
                return result;
            }
        }

        // One sample rate for all: the Stems are never resampled relative to each other.
        const double rate = parts[0].decoded.sampleRate;
        for (const auto& p : parts)
        {
            if (! sameRate (p.decoded.sampleRate, rate))
            {
                juce::StringArray rates;
                for (const auto& q : parts)
                    rates.add (q.file.getFileName() + " " + hz (q.decoded.sampleRate));

                result.error = "The stem files have different sample rates (" + rates.joinIntoString (", ")
                             + "). Use files that all have the same sample rate.";
                return result;
            }
        }

        int longest = 0;
        juce::int64 declared = 0;
        for (const auto& p : parts)
        {
            longest = juce::jmax (longest, p.decoded.audio.getNumSamples());
            declared = juce::jmax (declared, p.decoded.declaredLength);
        }

        for (auto& p : parts)
        {
            const juce::String label = juce::String (stemName ((Stem) p.stem)) + " (" + p.file.getFileName() + ")";

            for (auto note : p.decoded.notes)
                result.notes.add (label + ": " + note.replace ("(Original)", "(" + juce::String (stemName ((Stem) p.stem)) + ")"));

            auto& audio = p.decoded.audio;
            const int length = audio.getNumSamples();

            if (length < longest)
            {
                audio.setSize (2, longest, true, true, false);
                audio.clear (length, longest - length);
                result.notes.add (label + " is " + juce::String (longest - length)
                                  + " samples shorter than the longest stem file: zero-padded.");
            }

            result.stems[(size_t) p.stem] = std::move (audio);
        }

        for (int s = 0; s < numStems; ++s)
        {
            if (files[(size_t) s] == juce::File())
            {
                result.stems[(size_t) s].setSize (2, longest);
                result.stems[(size_t) s].clear();
                result.notes.add (juce::String (stemName ((Stem) s)) + ": no file (silent).");
            }
        }

        result.sampleRate = rate;
        result.declaredLength = declared;
        result.ok = true;
        return result;
    }
}

//==============================================================================
StemSource StemSource::fromSet (const StemSetFiles& files)
{
    StemSource s;
    s.setFiles = files;
    return s;
}

bool StemSource::isSet() const noexcept
{
    if (stemFile != juce::File())
        return false;

    for (const auto& f : setFiles)
        if (f != juce::File())
            return true;

    return false;
}

bool StemSource::isEmpty() const noexcept
{
    return stemFile == juce::File() && ! isSet();
}

bool StemSource::existsAsFiles() const
{
    if (isEmpty())
        return false;

    for (const auto& f : files())
        if (! f.existsAsFile())
            return false;

    return true;
}

bool StemSource::hasStem (int index) const noexcept
{
    if (! juce::isPositiveAndBelow (index, numStems))
        return false;
    if (! isSet())
        return stemFile != juce::File();
    return setFiles[(size_t) index] != juce::File();
}

int StemSource::numFiles() const noexcept
{
    int n = 0;
    for (int s = 0; s < numStems; ++s)
        n += hasStem (s) ? 1 : 0;
    return n;
}

juce::Array<juce::File> StemSource::files() const
{
    juce::Array<juce::File> result;

    if (! isSet())
    {
        if (stemFile != juce::File())
            result.add (stemFile);
        return result;
    }

    for (const auto& f : setFiles)
        if (f != juce::File())
            result.add (f);

    return result;
}

juce::File StemSource::firstMissingFile() const
{
    for (const auto& f : files())
        if (! f.existsAsFile())
            return f;

    return {};
}

juce::String StemSource::displayName() const
{
    if (! isSet())
        return stemFile.getFileName();

    const auto list = files();
    const int n = list.size();
    const juce::String count = n == 1 ? juce::String ("1 file") : juce::String (n) + " files";

    juce::StringArray names, stems;
    for (const auto& f : list)
    {
        names.add (f.getFileName());
        stems.add (fileStem (f.getFileName()));
    }

    if (const auto prefix = meaningfulPrefix (commonPrefix (stems)); prefix.isNotEmpty())
        return prefix + " (" + count + ")";

    bool oneFolder = true;
    for (const auto& f : list)
        oneFolder = oneFolder && f.getParentDirectory() == list.getFirst().getParentDirectory();

    if (oneFolder && list.getFirst().getParentDirectory().getFileName().isNotEmpty())
        return list.getFirst().getParentDirectory().getFileName() + " (" + count + ")";

    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 ")), ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    juce::String label = juce::String (n) + (n == 1 ? " stem file" : " stem files") + dot + names[0];
    if (n > 1)
        label << ", " << names[1];
    if (n > 2)
        label << ", " << ellipsis;
    return label;
}

juce::String StemSource::describePaths() const
{
    if (! isSet())
        return stemFile.getFullPathName();

    juce::StringArray lines;
    for (int s = 0; s < numStems; ++s)
        lines.add (juce::String (stemName ((Stem) s)) + ": "
                   + (setFiles[(size_t) s] != juce::File() ? setFiles[(size_t) s].getFullPathName() : juce::String ("(no file)")));
    return lines.joinIntoString ("\n");
}

//==============================================================================
int stemIndexFromFileName (const juce::String& fileName)
{
    const auto words = wordsOf (fileStem (fileName));
    int found = -1;

    for (int i = 0; i < words.size(); ++i)
    {
        const auto& w = words[i];

        // "no vocals", "without drums": a 2-stem separator's everything-but - that is Other.
        if (isNegation (w) && i + 1 < words.size() && keywordStem (words[i + 1]) >= 0)
        {
            found = (int) Stem::Other;
            ++i;
            continue;
        }

        if (w.startsWith ("no") && keywordStem (w.substring (2)) >= 0)     // "novocals"
        {
            found = (int) Stem::Other;
            continue;
        }

        if (const int s = keywordStem (w); s >= 0)
            found = s;          // the last keyword wins: the stem label usually ends the name
    }

    return found;
}

StemMapping mapStemFiles (const juce::Array<juce::File>& files)
{
    StemMapping m;

    if (files.isEmpty())
    {
        m.error = "No stem files chosen.";
        return m;
    }

    if (files.size() > numStems)
    {
        m.error = "Too many files (" + juce::String (files.size()) + "): a stem set has at most four, "
                  "one each for Drums, Bass, Other and Vocals.";
        return m;
    }

    for (int i = 0; i < files.size(); ++i)
        for (int j = i + 1; j < files.size(); ++j)
            if (files[i] == files[j])
            {
                m.error = files[i].getFileName() + " was chosen twice.";
                return m;
            }

    juce::Array<juce::File> unmatched;

    for (const auto& f : files)
    {
        const int s = stemIndexFromFileName (f.getFileName());

        if (s < 0)
        {
            unmatched.add (f);
            continue;
        }

        if (m.files[(size_t) s] != juce::File())
        {
            m.error = m.files[(size_t) s].getFileName() + " and " + f.getFileName() + " both look like "
                    + stemName ((Stem) s) + ". Choose one file per stem.";
            m.files = {};
            return m;
        }

        m.files[(size_t) s] = f;
    }

    // Unrecognised names fill the free slots: Other first (a separator's catch-all), then in Stem order.
    const Stem fillOrder[] { Stem::Other, Stem::Drums, Stem::Bass, Stem::Vocals };
    for (const auto& f : unmatched)
        for (const auto s : fillOrder)
            if (m.files[(size_t) s] == juce::File())
            {
                m.files[(size_t) s] = f;
                break;
            }

    m.ok = true;
    return m;
}

DecodedStems decodeStemSet (const StemSetFiles& files, const DecodeOptions& options)
{
    try
    {
        return decodeStemSetImpl (files, options);
    }
    catch (const std::exception& e)
    {
        DecodedStems failed;
        failed.error = "Decoding failed: " + juce::String (e.what());
        return failed;
    }
}

DecodedStems decodeStemSource (const StemSource& source, const DecodeOptions& options)
{
    if (source.isSet())
        return decodeStemSet (source.setFiles, options);
    return decodeStemFile (source.stemFile, options);
}

bool looksLikeStemFile (const juce::File& file)
{
    return countAudioStreams (file) == 1 + numStems;
}

} // namespace stemab
