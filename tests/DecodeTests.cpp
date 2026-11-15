#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>

#include "decode/Decode.h"

namespace
{
    struct RealPair
    {
        const char* original;           // in "Audio Files/"
        const char* stemFile;           // in "Stem Files/"
        juce::int64 originalSamples;    // exact FLAC STREAMINFO total samples
        juce::int64 mdhdSamples;        // Stem track duration declared by the MP4 (mdhd)
    };

    const RealPair realPairs[] =
    {
        { "03 Heresy.flac",
          "03 Nine Inch Nails - Heresy.stem.m4a",
          10331160, 10338304 },
        { "And One - Bodypop - 06 Body Company.flac",
          "And One - Bodypop - 06 Body Company.stem.m4a",
          14248416, 14255104 },
        { "Boy Harsher - Careful - 02 Face the Fire.flac",
          "Boy Harsher - Careful - 02 Face the Fire.stem.m4a",
          14727168, 14734336 },
    };

    juce::File testDataFile (const char* folder, const char* name)
    {
        return juce::File (juce::String::fromUTF8 (STEMAB_TEST_DATA_DIR))
                   .getChildFile (folder)
                   .getChildFile (juce::String::fromUTF8 (name));
    }

    double rmsDbfs (const juce::AudioBuffer<float>& b)
    {
        double sum = 0.0;

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* p = b.getReadPointer (ch);

            for (int i = 0; i < b.getNumSamples(); ++i)
                sum += (double) p[i] * (double) p[i];
        }

        const double meanSquare = sum / ((double) b.getNumChannels() * (double) b.getNumSamples());
        return 10.0 * std::log10 (meanSquare + 1.0e-30);
    }

    /** Writes a minimal 16-bit PCM WAV (a different sine per channel) without needing juce_audio_formats. */
    juce::File writeTestWav (int channels, int numSamples)
    {
        auto tmp = juce::File::createTempFile (".wav");
        juce::MemoryOutputStream out;

        const auto dataBytes = (juce::uint32) (numSamples * channels * 2);
        out.write ("RIFF", 4);
        out.writeInt (36 + (int) dataBytes);
        out.write ("WAVEfmt ", 8);
        out.writeInt (16);
        out.writeShort (1);                         // PCM
        out.writeShort ((short) channels);
        out.writeInt (44100);
        out.writeInt (44100 * channels * 2);
        out.writeShort ((short) (channels * 2));
        out.writeShort (16);
        out.write ("data", 4);
        out.writeInt ((int) dataBytes);

        for (int i = 0; i < numSamples; ++i)
            for (int ch = 0; ch < channels; ++ch)
                out.writeShort ((short) std::lround (32767.0 * 0.5 * std::sin (0.05 * (double) i * (double) (ch + 1))));

        REQUIRE (tmp.replaceWithData (out.getData(), out.getDataSize()));
        return tmp;
    }
}

TEST_CASE ("FFmpeg is linked and reports a version", "[decode]")
{
    const auto version = stemab::ffmpegVersionString();
    CHECK_FALSE (version.empty());
    CHECK_THAT (version, Catch::Matchers::ContainsSubstring ("FFmpeg"));
    CHECK_THAT (version, Catch::Matchers::ContainsSubstring ("libavformat"));
    CHECK_THAT (version, Catch::Matchers::ContainsSubstring ("libavcodec"));
}

TEST_CASE ("stem names follow the fixed order", "[decode]")
{
    CHECK (juce::String (stemab::stemName (stemab::Stem::Drums))  == "Drums");
    CHECK (juce::String (stemab::stemName (stemab::Stem::Bass))   == "Bass");
    CHECK (juce::String (stemab::stemName (stemab::Stem::Other))  == "Other");
    CHECK (juce::String (stemab::stemName (stemab::Stem::Vocals)) == "Vocals");
}

TEST_CASE ("decoding a missing file reports an error instead of throwing", "[decode]")
{
    const juce::File missing ("C:/definitely/not/here/nothing.flac");

    const auto original = stemab::decodeOriginal (missing);
    CHECK_FALSE (original.ok);
    CHECK (original.error.isNotEmpty());
    CHECK (original.audio.getNumSamples() == 0);

    const auto stems = stemab::decodeStemFile (missing);
    CHECK_FALSE (stems.ok);
    CHECK (stems.error.isNotEmpty());
}

TEST_CASE ("a non-audio file is rejected cleanly", "[decode]")
{
    auto tmp = juce::File::createTempFile (".txt");
    REQUIRE (tmp.replaceWithText ("this is not audio"));

    CHECK_FALSE (stemab::decodeOriginal (tmp).ok);
    CHECK_FALSE (stemab::decodeStemFile (tmp).ok);
    tmp.deleteFile();
}

TEST_CASE ("synthetic WAV: the Original decodes exactly, mono is duplicated", "[decode]")
{
    for (int channels : { 1, 2 })
    {
        const int n = 5000;
        const auto tmp = writeTestWav (channels, n);

        const auto r = stemab::decodeOriginal (tmp);
        REQUIRE (r.ok);
        CHECK (r.audio.getNumChannels() == 2);
        CHECK (r.audio.getNumSamples() == n);
        CHECK (r.sampleRate == 44100.0);

        if (channels == 1)
            for (int i = 0; i < n; ++i)
                REQUIRE (r.audio.getSample (0, i) == r.audio.getSample (1, i));

        // 16-bit quantisation only
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float k = (channels == 1) ? 1.0f : (float) (ch + 1);
                REQUIRE (std::abs ((double) r.audio.getSample (ch, i) - 0.5 * std::sin (0.05 * (double) i * (double) k)) < 1.0e-4);
            }

        // A plain WAV is not a Stem File.
        CHECK_FALSE (stemab::decodeStemFile (tmp).ok);
        tmp.deleteFile();
    }
}

TEST_CASE ("each Original decodes to stereo 44.1 kHz with exactly the FLAC sample count", "[decode][realfiles]")
{
    for (const auto& pair : realPairs)
    {
        DYNAMIC_SECTION (pair.original)
        {
            const auto file = testDataFile ("Audio Files", pair.original);

            if (! file.existsAsFile())
                SKIP ("test file not present: " << file.getFullPathName());

            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto r = stemab::decodeOriginal (file);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

            INFO (r.error);
            REQUIRE (r.ok);
            WARN (pair.original << ": " << r.audio.getNumSamples() << " samples, decoded in " << (int) ms << " ms; "
                  << r.notes.joinIntoString (" | "));

            CHECK (r.audio.getNumChannels() == 2);
            CHECK (r.sampleRate == 44100.0);
            CHECK ((juce::int64) r.audio.getNumSamples() == pair.originalSamples);
            CHECK (rmsDbfs (r.audio) > -60.0);
        }
    }
}

TEST_CASE ("each Stem File decodes to four stereo Stems", "[decode][realfiles]")
{
    for (const auto& pair : realPairs)
    {
        DYNAMIC_SECTION (pair.stemFile)
        {
            const auto file = testDataFile ("Stem Files", pair.stemFile);

            if (! file.existsAsFile())
                SKIP ("test file not present: " << file.getFullPathName());

            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto r = stemab::decodeStemFile (file);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

            INFO (r.error);
            REQUIRE (r.ok);

            const juce::int64 length = r.stems[0].getNumSamples();
            WARN (pair.stemFile << ": " << length << " samples per Stem (mdhd " << pair.mdhdSamples
                  << ", declared " << r.declaredLength << ", diff vs mdhd " << (length - pair.mdhdSamples)
                  << "), decoded in " << (int) ms << " ms\n  " << r.notes.joinIntoString ("\n  "));

            CHECK (r.sampleRate == 44100.0);
            CHECK (std::abs (length - pair.mdhdSamples) <= 2048);

            for (int s = 0; s < stemab::numStems; ++s)
            {
                INFO ("stem " << stemab::stemName ((stemab::Stem) s));
                CHECK (r.stems[(size_t) s].getNumChannels() == 2);
                CHECK ((juce::int64) r.stems[(size_t) s].getNumSamples() == length);
                CHECK (rmsDbfs (r.stems[(size_t) s]) > -60.0);
            }
        }
    }
}

TEST_CASE ("a plain FLAC is not a Stem File", "[decode][realfiles]")
{
    const auto file = testDataFile ("Audio Files", realPairs[0].original);

    if (! file.existsAsFile())
        SKIP ("test file not present: " << file.getFullPathName());

    const auto r = stemab::decodeStemFile (file);
    CHECK_FALSE (r.ok);
    CHECK_THAT (r.error.toStdString(), Catch::Matchers::ContainsSubstring ("not a Stem File"));
    CHECK_THAT (r.error.toStdString(), Catch::Matchers::ContainsSubstring ("found 1"));
}

//==============================================================================
namespace
{
    bool bitIdentical (const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y)
    {
        if (x.getNumChannels() != y.getNumChannels() || x.getNumSamples() != y.getNumSamples())
            return false;

        for (int ch = 0; ch < x.getNumChannels(); ++ch)
            if (std::memcmp (x.getReadPointer (ch), y.getReadPointer (ch), sizeof (float) * (size_t) x.getNumSamples()) != 0)
                return false;

        return true;
    }
}

TEST_CASE ("parallel decode is bit-identical to sequential decode", "[decode][realfiles][parallel]")
{
    const auto& pair = realPairs[0];
    const auto originalFile = testDataFile ("Audio Files", pair.original);
    const auto stemFile = testDataFile ("Stem Files", pair.stemFile);

    if (! originalFile.existsAsFile() || ! stemFile.existsAsFile())
        SKIP ("test files not present: " << pair.original);

    const stemab::DecodeOptions sequential { {}, false }, parallel { {}, true };

    SECTION ("Stem File: one demux pass, four decoder threads")
    {
        const auto seq = stemab::decodeStemFile (stemFile, sequential);
        const auto par = stemab::decodeStemFile (stemFile, parallel);
        REQUIRE (seq.ok);
        REQUIRE (par.ok);
        CHECK (par.sampleRate == seq.sampleRate);
        CHECK (par.declaredLength == seq.declaredLength);
        CHECK (par.notes == seq.notes);

        for (int s = 0; s < stemab::numStems; ++s)
        {
            CAPTURE (s);
            CHECK (bitIdentical (par.stems[(size_t) s], seq.stems[(size_t) s]));
        }
    }

    SECTION ("Original: frame-threaded FLAC")
    {
        const auto seq = stemab::decodeOriginal (originalFile, sequential);
        const auto par = stemab::decodeOriginal (originalFile, parallel);
        REQUIRE (seq.ok);
        REQUIRE (par.ok);
        CHECK (par.sampleRate == seq.sampleRate);
        CHECK (bitIdentical (par.audio, seq.audio));
    }
}

TEST_CASE ("decoding stops early when cancelled", "[decode][realfiles]")
{
    const auto& pair = realPairs[0];
    const auto originalFile = testDataFile ("Audio Files", pair.original);
    const auto stemFile = testDataFile ("Stem Files", pair.stemFile);

    if (! originalFile.existsAsFile() || ! stemFile.existsAsFile())
        SKIP ("test files not present: " << pair.original);

    // Cancel after a handful of polls: well before the end of the file.
    for (const bool parallel : { false, true })
    {
        CAPTURE (parallel);
        auto polls = std::make_shared<std::atomic<int>> (0);
        const stemab::DecodeOptions options { [polls] { return polls->fetch_add (1) > 200; }, parallel };

        const auto stems = stemab::decodeStemFile (stemFile, options);
        CHECK_FALSE (stems.ok);
        CHECK (stems.error == "Cancelled.");

        polls->store (0);
        const auto original = stemab::decodeOriginal (originalFile, options);
        CHECK_FALSE (original.ok);
        CHECK (original.error == "Cancelled.");
    }
}
