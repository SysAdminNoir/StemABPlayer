#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#include <juce_audio_formats/juce_audio_formats.h>

#include "decode/StemSet.h"
#include "load/AlignmentCache.h"
#include "load/DecodeStash.h"
#include "load/PairLoader.h"

using namespace stemab;

namespace
{
constexpr double fs = 44100.0;

/** A fresh folder in the temp directory, deleted at the end of the test. */
struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("StemAB_StemSetTests_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));

    TempDir()   { dir.createDirectory(); }
    ~TempDir()  { dir.deleteRecursively(); }
};

/** Files that need not exist: the mapping only looks at names. */
juce::Array<juce::File> named (std::initializer_list<const char*> names, const juce::String& folder = "C:\\Sep\\Song")
{
    juce::Array<juce::File> files;
    for (const auto* n : names)
        files.add (juce::File (folder).getChildFile (n));
    return files;
}

juce::String nameIn (const StemMapping& m, Stem s)
{
    return m.files[(size_t) s].getFileName();
}

/** A 32-bit float WAV (lossless for the decoded floats). */
juce::File writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}
                                               .withSampleRate (rate)
                                               .withNumChannels (audio.getNumChannels())
                                               .withBitsPerSample (32)
                                               .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    REQUIRE (writer != nullptr);
    REQUIRE (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()));
    return file;
}

/** Deterministic white noise, `channels` x `length`. */
juce::AudioBuffer<float> noise (int channels, int length, int seed, float level = 0.25f)
{
    juce::Random random (seed);
    juce::AudioBuffer<float> b (channels, length);
    for (int ch = 0; ch < channels; ++ch)
        for (int n = 0; n < length; ++n)
            b.setSample (ch, n, level * (2.0f * random.nextFloat() - 1.0f));
    return b;
}

/** `a` delayed by `delay` samples, `length` long (zero elsewhere). */
juce::AudioBuffer<float> delayed (const juce::AudioBuffer<float>& a, int delay, int length)
{
    juce::AudioBuffer<float> b (a.getNumChannels(), length);
    b.clear();
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int n = 0; n < length; ++n)
            if (const int src = n - delay; src >= 0 && src < a.getNumSamples())
                b.setSample (ch, n, a.getSample (ch, src));
    return b;
}

/** The owner thread's message queue, played by the test thread. */
class TaskQueue
{
public:
    PairLoader::Post poster()
    {
        return [this] (PairLoader::Task t)
        {
            const std::lock_guard lock (mutex);
            tasks.push_back (std::move (t));
        };
    }

    void runAll()
    {
        for (;;)
        {
            PairLoader::Task t;
            {
                const std::lock_guard lock (mutex);
                if (tasks.empty())
                    return;
                t = std::move (tasks.front());
                tasks.pop_front();
            }
            t();
        }
    }

private:
    std::mutex mutex;
    std::deque<PairLoader::Task> tasks;
};

struct LoadOutcome
{
    std::shared_ptr<const Session> upgraded;
    PairLoader::StemsReady stemsEvent;
    double trackNullDepthDb = 0.0;
    bool hasNullDepth = false;
    juce::String error;
};

LoadOutcome loadPair (const juce::File& original, const StemSource& stems, PairLoader::Backend backend = PairLoader::defaultBackend())
{
    TaskQueue queue;
    LoadOutcome out;
    PairLoader loader (queue.poster(), nullptr, std::move (backend));
    loader.onStemsReady = [&out] (const auto& ev) { out.upgraded = ev.session; out.stemsEvent = ev; };
    loader.onAlignmentCorrected = [&out] (const auto& ev) { out.upgraded = ev.session; };
    loader.onTrackNullDepth = [&out] (const auto& ev) { out.trackNullDepthDb = ev.trackNullDepthDb; out.hasNullDepth = true; };
    loader.onFailed = [&out] (LoadStage, const juce::String& m) { out.error = m; };
    loader.load (original, stems);
    REQUIRE (loader.waitUntilIdle (120000));
    queue.runAll();
    return out;
}

bool allZero (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int n = 0; n < b.getNumSamples(); ++n)
            if (b.getSample (ch, n) != 0.0f)
                return false;
    return true;
}
} // namespace

//==============================================================================
TEST_CASE ("stem set: file names map to Stems", "[stemset][mapping]")
{
    SECTION ("Demucs names")
    {
        const auto m = mapStemFiles (named ({ "vocals.wav", "other.wav", "bass.wav", "drums.wav" }));
        REQUIRE (m.ok);
        CHECK (nameIn (m, Stem::Drums) == "drums.wav");
        CHECK (nameIn (m, Stem::Bass) == "bass.wav");
        CHECK (nameIn (m, Stem::Other) == "other.wav");
        CHECK (nameIn (m, Stem::Vocals) == "vocals.wav");
    }

    SECTION ("UVR-style names, case-insensitive")
    {
        const auto m = mapStemFiles (named ({ "1_My Song_(Vocals).wav", "1_My Song_(Instrumental).wav" }));
        REQUIRE (m.ok);
        CHECK (nameIn (m, Stem::Vocals) == "1_My Song_(Vocals).wav");
        CHECK (nameIn (m, Stem::Other) == "1_My Song_(Instrumental).wav");
        CHECK (m.files[(size_t) Stem::Drums] == juce::File());
        CHECK (m.files[(size_t) Stem::Bass] == juce::File());

        const auto upper = mapStemFiles (named ({ "SONG_(DRUMS).WAV", "Song_(BASS).flac", "song_(other).mp3", "song_(VOCALS).ogg" }));
        REQUIRE (upper.ok);
        CHECK (nameIn (upper, Stem::Drums) == "SONG_(DRUMS).WAV");
        CHECK (nameIn (upper, Stem::Bass) == "Song_(BASS).flac");
        CHECK (nameIn (upper, Stem::Other) == "song_(other).mp3");
        CHECK (nameIn (upper, Stem::Vocals) == "song_(VOCALS).ogg");
    }

    SECTION ("2-stem sets: everything-but-vocals is Other")
    {
        const auto demucs = mapStemFiles (named ({ "no_vocals.wav", "vocals.wav" }));
        REQUIRE (demucs.ok);
        CHECK (nameIn (demucs, Stem::Vocals) == "vocals.wav");
        CHECK (nameIn (demucs, Stem::Other) == "no_vocals.wav");

        CHECK (stemIndexFromFileName ("Song (No Drums).wav") == (int) Stem::Other);
        CHECK (stemIndexFromFileName ("song-novocals.flac") == (int) Stem::Other);
        CHECK (stemIndexFromFileName ("Song_(Instrumental)_UVR-MDX.wav") == (int) Stem::Other);
    }

    SECTION ("keywords")
    {
        CHECK (stemIndexFromFileName ("kick.wav") == (int) Stem::Drums);
        CHECK (stemIndexFromFileName ("Perc Loop.aiff") == (int) Stem::Drums);
        CHECK (stemIndexFromFileName ("lead vox.wav") == (int) Stem::Vocals);
        CHECK (stemIndexFromFileName ("Song Acapella.mp3") == (int) Stem::Vocals);
        CHECK (stemIndexFromFileName ("accomp.wav") == (int) Stem::Other);
        CHECK (stemIndexFromFileName ("Melody.wav") == (int) Stem::Other);
        // Whole words only, and the last keyword wins (the stem label usually ends the name).
        CHECK (stemIndexFromFileName ("Bassnectar - Track (Drums).wav") == (int) Stem::Drums);
        CHECK (stemIndexFromFileName ("The Other Side (Vocals).wav") == (int) Stem::Vocals);
        CHECK (stemIndexFromFileName ("Brothers.wav") == -1);
        CHECK (stemIndexFromFileName ("track 01.wav") == -1);
    }

    SECTION ("unmatched names fill the free slots, Other first")
    {
        const auto two = mapStemFiles (named ({ "a.wav", "b.wav" }));
        REQUIRE (two.ok);
        CHECK (nameIn (two, Stem::Other) == "a.wav");
        CHECK (nameIn (two, Stem::Drums) == "b.wav");

        const auto mixed = mapStemFiles (named ({ "x.wav", "drums.wav", "y.wav", "other.wav" }));
        REQUIRE (mixed.ok);
        CHECK (nameIn (mixed, Stem::Drums) == "drums.wav");
        CHECK (nameIn (mixed, Stem::Other) == "other.wav");
        CHECK (nameIn (mixed, Stem::Bass) == "x.wav");
        CHECK (nameIn (mixed, Stem::Vocals) == "y.wav");

        const auto one = mapStemFiles (named ({ "guitar.wav" }));
        REQUIRE (one.ok);
        CHECK (nameIn (one, Stem::Other) == "guitar.wav");
    }

    SECTION ("errors: duplicates, too many, none")
    {
        const auto dup = mapStemFiles (named ({ "drums.wav", "Drum Loop.wav" }));
        CHECK_FALSE (dup.ok);
        CHECK (dup.error.contains ("Drums"));
        CHECK (dup.files == StemSetFiles {});

        const auto twoOthers = mapStemFiles (named ({ "song_(Instrumental).wav", "other.wav", "vocals.wav" }));
        CHECK_FALSE (twoOthers.ok);

        CHECK_FALSE (mapStemFiles (named ({ "drums.wav", "bass.wav", "other.wav", "vocals.wav", "piano.wav" })).ok);
        CHECK_FALSE (mapStemFiles ({}).ok);

        auto same = named ({ "drums.wav" });
        same.add (same.getFirst());
        CHECK_FALSE (mapStemFiles (same).ok);
    }
}

TEST_CASE ("stem set: label and tooltip", "[stemset]")
{
    const auto demucs = StemSource::fromSet (mapStemFiles (named ({ "drums.wav", "bass.wav", "other.wav", "vocals.wav" },
                                                                  "C:\\out\\htdemucs\\03 Heresy")).files);
    CHECK (demucs.isSet());
    CHECK (demucs.numFiles() == 4);
    CHECK (demucs.displayName() == "03 Heresy (4 files)");
    CHECK (juce::StringArray::fromLines (demucs.describePaths()).size() == 4);
    CHECK (demucs.describePaths().contains ("C:\\out\\htdemucs\\03 Heresy\\drums.wav"));

    const auto uvr = StemSource::fromSet (mapStemFiles (named ({ "1_My Song_(Vocals).wav", "1_My Song_(Instrumental).wav" },
                                                               "C:\\UVR")).files);
    CHECK (uvr.displayName() == "1_My Song (2 files)");
    CHECK (uvr.describePaths().contains ("Drums: (no file)"));
    CHECK_FALSE (uvr.hasStem ((int) Stem::Drums));
    CHECK (uvr.hasStem ((int) Stem::Vocals));

    StemSetFiles scattered;
    scattered[0] = juce::File ("C:\\a\\drums.wav");
    scattered[1] = juce::File ("C:\\b\\bass.wav");
    scattered[2] = juce::File ("C:\\c\\keys.wav");
    const auto label = StemSource::fromSet (scattered).displayName();
    CHECK (label.startsWith ("3 stem files"));
    CHECK (label.contains ("drums.wav, bass.wav"));

    const StemSource single (juce::File ("C:\\Stems\\x.stem.m4a"));
    CHECK_FALSE (single.isSet());
    CHECK (single.numFiles() == 4);
    CHECK (single.displayName() == "x.stem.m4a");
    CHECK (StemSource().isEmpty());
}

//==============================================================================
TEST_CASE ("stem set: decoding WAV files", "[stemset][decode]")
{
    TempDir tmp;
    const int len = 4000;

    const auto drums = noise (2, len, 1);
    const auto bassMono = noise (1, len - 600, 2);       // mono and shorter
    const auto vocals = noise (2, len, 3);

    StemSetFiles files;
    files[(size_t) Stem::Drums]  = writeWav (tmp.dir.getChildFile ("drums.wav"), drums, fs);
    files[(size_t) Stem::Bass]   = writeWav (tmp.dir.getChildFile ("bass.wav"), bassMono, fs);
    files[(size_t) Stem::Vocals] = writeWav (tmp.dir.getChildFile ("vocals.wav"), vocals, fs);
    // Other: no file

    SECTION ("mono -> stereo, zero-padded, a missing Stem is silent")
    {
        for (const bool parallel : { true, false })
        {
            const auto d = decodeStemSet (files, DecodeOptions { {}, parallel });
            REQUIRE (d.ok);
            CHECK (d.sampleRate == fs);

            for (const auto& s : d.stems)
            {
                CHECK (s.getNumChannels() == 2);
                CHECK (s.getNumSamples() == len);
            }

            const auto& bass = d.stems[(size_t) Stem::Bass];
            bool bassOk = true;
            for (int n = 0; n < len; ++n)
            {
                const float expected = n < len - 600 ? bassMono.getSample (0, n) : 0.0f;
                bassOk = bassOk && bass.getSample (0, n) == expected && bass.getSample (1, n) == expected;
            }
            CHECK (bassOk);

            bool drumsOk = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < len; ++n)
                    drumsOk = drumsOk && d.stems[(size_t) Stem::Drums].getSample (ch, n) == drums.getSample (ch, n);
            CHECK (drumsOk);

            CHECK (allZero (d.stems[(size_t) Stem::Other]));
            CHECK (d.notes.joinIntoString ("\n").contains ("Other: no file"));
        }
    }

    SECTION ("the parallel and sequential paths agree")
    {
        const auto par = decodeStemSet (files, DecodeOptions { {}, true });
        const auto seq = decodeStemSet (files, DecodeOptions { {}, false });
        REQUIRE (par.ok);
        REQUIRE (seq.ok);
        for (size_t s = 0; s < par.stems.size(); ++s)
            for (int ch = 0; ch < 2; ++ch)
                CHECK (std::memcmp (par.stems[s].getReadPointer (ch), seq.stems[s].getReadPointer (ch),
                                    sizeof (float) * (size_t) len) == 0);
    }

    SECTION ("different sample rates are an error, not a resample")
    {
        auto mixed = files;
        mixed[(size_t) Stem::Other] = writeWav (tmp.dir.getChildFile ("other.wav"), noise (2, len, 4), 48000.0);
        const auto d = decodeStemSet (mixed);
        CHECK_FALSE (d.ok);
        CHECK (d.error.contains ("sample rate"));
        CHECK (d.error.contains ("48000"));
    }

    SECTION ("more than two channels is an error")
    {
        auto wide = files;
        wide[(size_t) Stem::Other] = writeWav (tmp.dir.getChildFile ("other.wav"), noise (3, len, 5), fs);
        const auto d = decodeStemSet (wide);
        CHECK_FALSE (d.ok);
        CHECK (d.error.contains ("other.wav"));
    }

    SECTION ("a missing file, no files, a cancel")
    {
        auto missing = files;
        missing[(size_t) Stem::Other] = tmp.dir.getChildFile ("nope.wav");
        CHECK_FALSE (decodeStemSet (missing).ok);
        CHECK_FALSE (decodeStemSet (StemSetFiles {}).ok);

        const auto cancelled = decodeStemSet (files, DecodeOptions { [] { return true; }, true });
        CHECK_FALSE (cancelled.ok);
        CHECK (cancelled.error == "Cancelled.");
    }

    SECTION ("decodeStemSource dispatches; identities follow the ordered files")
    {
        const auto source = StemSource::fromSet (files);
        CHECK (source.existsAsFiles());
        CHECK (decodeStemSource (source).ok);

        const auto id = FileIdentity::of (source);
        CHECK (id.isValid());
        CHECK (id == FileIdentity::of (StemSource::fromSet (files)));

        auto swapped = files;
        std::swap (swapped[(size_t) Stem::Drums], swapped[(size_t) Stem::Vocals]);
        CHECK (FileIdentity::of (StemSource::fromSet (swapped)) != id);

        auto missing = files;
        missing[(size_t) Stem::Other] = tmp.dir.getChildFile ("nope.wav");
        CHECK_FALSE (FileIdentity::of (StemSource::fromSet (missing)).isValid());
        CHECK_FALSE (StemSource::fromSet (missing).existsAsFiles());
        CHECK (StemSource::fromSet (missing).firstMissingFile().getFileName() == "nope.wav");
        CHECK (countAudioStreams (files[0]) == 1);
        CHECK_FALSE (looksLikeStemFile (files[0]));
    }
}

TEST_CASE ("stem set: the stash hands a previewed set to the pair load", "[stemset][stash]")
{
    TempDir tmp;
    StemSetFiles files;
    files[0] = writeWav (tmp.dir.getChildFile ("drums.wav"), noise (2, 1000, 1), fs);
    files[3] = writeWav (tmp.dir.getChildFile ("vocals.wav"), noise (2, 1000, 2), fs);
    const auto set = StemSource::fromSet (files);

    auto stash = std::make_shared<DecodeStash>();

    SECTION ("only the same files in the same slots")
    {
        const auto t = stash->beginStems (set);
        stash->putStems (t, decodeStemSet (files));

        auto swapped = files;
        std::swap (swapped[0], swapped[3]);
        CHECK_FALSE (stash->takeStems (StemSource::fromSet (swapped)).has_value());
        CHECK_FALSE (stash->takeStems (files[0]).has_value());          // a single file is another key
        CHECK (stash->takeStems (set).has_value());
        CHECK (stash->isEmpty());
    }

    SECTION ("wrap(): the loader's set decoder takes the stash first")
    {
        std::atomic<int> decodes { 0 };
        PairLoader::Backend inner;
        inner.decodeStemSet = [&decodes] (const StemSetFiles& f, const DecodeOptions& o)
        {
            decodes.fetch_add (1);
            return decodeStemSet (f, o);
        };
        const auto wrapped = DecodeStash::wrap (stash, inner);

        const auto t = stash->beginStems (set);
        stash->putStems (t, decodeStemSet (files));

        CHECK (wrapped.decodeStemSet (files, {}).ok);
        CHECK (decodes.load() == 0);                    // from the stash
        CHECK (wrapped.decodeStemSet (files, {}).ok);
        CHECK (decodes.load() == 1);                    // the stash is empty now: decoded
    }
}

//==============================================================================
TEST_CASE ("stem set: a pair loads, aligns and nulls like a Stem File", "[stemset][load]")
{
    // A = drums + bass + vocals; the set (Other missing) is that, 2112 samples late, as WAVs with a mono Bass
    // and a shorter Vocals file. The Residual must still null: A minus the Stems that exist.
    TempDir tmp;
    const int len = (int) (5.0 * fs), active = (int) (4.5 * fs), delay = 2112;

    auto silenceTail = [active] (juce::AudioBuffer<float> b)
    {
        b.clear (active, b.getNumSamples() - active);
        return b;
    };

    const auto drums = silenceTail (noise (2, len, 11, 0.5f));
    const auto bass = silenceTail (noise (1, len, 12, 0.25f));
    const auto vocals = silenceTail (noise (2, len, 13, 0.125f));

    juce::AudioBuffer<float> a (2, len);
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < len; ++n)
            a.setSample (ch, n, drums.getSample (ch, n) + bass.getSample (0, n) + vocals.getSample (ch, n));

    const auto original = writeWav (tmp.dir.getChildFile ("a.wav"), a, fs);
    StemSetFiles files;
    files[(size_t) Stem::Drums]  = writeWav (tmp.dir.getChildFile ("drums.wav"), delayed (drums, delay, len + 7168), fs);
    files[(size_t) Stem::Bass]   = writeWav (tmp.dir.getChildFile ("bass.wav"), delayed (bass, delay, len + 7168), fs);
    files[(size_t) Stem::Vocals] = writeWav (tmp.dir.getChildFile ("vocals.wav"), delayed (vocals, delay, active + delay + 10), fs);

    const auto out = loadPair (original, StemSource::fromSet (files));

    CHECK (out.error.isEmpty());
    REQUIRE (out.upgraded != nullptr);
    CHECK (out.upgraded->hasStems());
    CHECK (out.upgraded->alignmentOffset() == delay);
    CHECK (out.upgraded->alignmentPolarity() == 1);
    CHECK_FALSE (out.stemsEvent.alignment.lowConfidence);
    REQUIRE (out.hasNullDepth);
    CHECK (out.trackNullDepthDb >= 100.0);
    WARN ("synthetic stem set: offset " << out.upgraded->alignmentOffset() << ", Track Null Depth " << out.trackNullDepthDb << " dB");

    // A failed set (mismatched rates) fails the stems stage with a clear message.
    auto bad = files;
    bad[(size_t) Stem::Other] = writeWav (tmp.dir.getChildFile ("other.wav"), noise (2, 1000, 14), 48000.0);
    const auto failed = loadPair (original, StemSource::fromSet (bad));
    CHECK (failed.upgraded == nullptr);
    CHECK (failed.error.startsWith ("Stem files: "));
    CHECK (failed.error.contains ("sample rate"));
}

//==============================================================================
TEST_CASE ("stem set from a real Stem File: same offset and null depth as the .stem.m4a", "[stemset][load][realfiles]")
{
    struct RealPair { const char* original; const char* stemFile; };
    const RealPair pairs[] {
        { "03 Heresy.flac", "03 Nine Inch Nails - Heresy.stem.m4a" },
        { "And One - Bodypop - 06 Body Company.flac", "And One - Bodypop - 06 Body Company.stem.m4a" },
        { "Boy Harsher - Careful - 02 Face the Fire.flac", "Boy Harsher - Careful - 02 Face the Fire.stem.m4a" },
    };

    const auto dir = juce::File (juce::String::fromUTF8 (STEMAB_TEST_DATA_DIR));
    int ran = 0;

    for (const auto& pair : pairs)
    {
        const auto originalFile = dir.getChildFile ("Audio Files").getChildFile (pair.original);
        const auto stemFile = dir.getChildFile ("Stem Files").getChildFile (pair.stemFile);
        if (! originalFile.existsAsFile() || ! stemFile.existsAsFile())
            continue;

        DYNAMIC_SECTION (pair.stemFile)
        {
            ++ran;
            CHECK (looksLikeStemFile (stemFile));

            // The four decoded Stems as Demucs-named float WAVs (bit-exact), in a temporary folder.
            TempDir tmp;
            const auto folder = tmp.dir.getChildFile (juce::File (stemFile).getFileNameWithoutExtension());
            folder.createDirectory();
            juce::Array<juce::File> written;
            {
                const auto decoded = decodeStemFile (stemFile);
                REQUIRE (decoded.ok);
                const char* names[] { "drums.wav", "bass.wav", "other.wav", "vocals.wav" };
                for (int s = 0; s < numStems; ++s)
                    written.add (writeWav (folder.getChildFile (names[s]), decoded.stems[(size_t) s], decoded.sampleRate));
            }

            const auto mapping = mapStemFiles (written);
            REQUIRE (mapping.ok);

            const auto fromFile = loadPair (originalFile, StemSource (stemFile));
            const auto fromSet = loadPair (originalFile, StemSource::fromSet (mapping.files));

            REQUIRE (fromFile.upgraded != nullptr);
            REQUIRE (fromSet.upgraded != nullptr);
            CHECK (fromSet.error.isEmpty());
            CHECK (fromSet.upgraded->alignmentOffset() == fromFile.upgraded->alignmentOffset());
            CHECK (fromSet.upgraded->alignmentPolarity() == fromFile.upgraded->alignmentPolarity());
            REQUIRE (fromFile.hasNullDepth);
            REQUIRE (fromSet.hasNullDepth);
            CHECK (std::abs (fromSet.trackNullDepthDb - fromFile.trackNullDepthDb) < 0.01);

            WARN (pair.stemFile << ": .stem.m4a offset " << fromFile.upgraded->alignmentOffset() << " / null "
                  << fromFile.trackNullDepthDb << " dB;  WAV set offset " << fromSet.upgraded->alignmentOffset()
                  << " / null " << fromSet.trackNullDepthDb << " dB");
        }
    }

    if (ran == 0)
        SKIP ("test files not present");
}
