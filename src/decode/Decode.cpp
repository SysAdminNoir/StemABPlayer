#include "decode/Decode.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
}

namespace stemab
{

//==============================================================================
// Version string
//==============================================================================
namespace
{
    std::string formatVersion (const char* name, unsigned int v)
    {
        return std::string (name) + " "
             + std::to_string (AV_VERSION_MAJOR (v)) + "."
             + std::to_string (AV_VERSION_MINOR (v)) + "."
             + std::to_string (AV_VERSION_MICRO (v));
    }
}

std::string ffmpegVersionString()
{
    const char* info = av_version_info();

    return std::string ("FFmpeg ") + (info != nullptr ? info : "?")
         + " (" + formatVersion ("libavformat", avformat_version())
         + ", "  + formatVersion ("libavcodec",  avcodec_version()) + ")";
}

const char* stemName (Stem stem) noexcept
{
    switch (stem)
    {
        case Stem::Drums:  return "Drums";
        case Stem::Bass:   return "Bass";
        case Stem::Other:  return "Other";
        case Stem::Vocals: return "Vocals";
    }

    return "?";
}

//==============================================================================
// Everything below is private to this file: all FFmpeg detail lives here.
//==============================================================================
namespace
{
    //==========================================================================
    // RAII wrappers
    struct FormatCloser { void operator() (AVFormatContext* c) const noexcept { avformat_close_input (&c); } };
    struct CodecFreer   { void operator() (AVCodecContext*  c) const noexcept { avcodec_free_context (&c); } };
    struct FrameFreer   { void operator() (AVFrame*         f) const noexcept { av_frame_free (&f); } };
    struct PacketFreer  { void operator() (AVPacket*        p) const noexcept { av_packet_free (&p); } };

    using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
    using CodecPtr  = std::unique_ptr<AVCodecContext,  CodecFreer>;
    using FramePtr  = std::unique_ptr<AVFrame,         FrameFreer>;
    using PacketPtr = std::unique_ptr<AVPacket,        PacketFreer>;

    juce::String avErrorString (int code)
    {
        char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror (code, buffer, sizeof (buffer));
        return juce::String::fromUTF8 (buffer) + " (" + juce::String (code) + ")";
    }

    //==========================================================================
    /** Opens `file` and reads its stream info. FFmpeg on Windows takes UTF-8 paths. */
    bool openFile (const juce::File& file, FormatPtr& format, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "File not found: " + file.getFullPathName();
            return false;
        }

        AVFormatContext* raw = nullptr;
        int ret = avformat_open_input (&raw, file.getFullPathName().toUTF8(), nullptr, nullptr);

        if (ret < 0)
        {
            error = "Could not open \"" + file.getFileName() + "\": " + avErrorString (ret);
            return false;
        }

        format.reset (raw);
        ret = avformat_find_stream_info (format.get(), nullptr);

        if (ret < 0)
        {
            error = "Could not read stream info of \"" + file.getFileName() + "\": " + avErrorString (ret);
            return false;
        }

        return true;
    }

    /** Length the container declares for `stream`, in samples at `sampleRate` (0 if unknown). */
    juce::int64 declaredLengthSamples (const AVStream& stream, double sampleRate)
    {
        if (stream.duration == AV_NOPTS_VALUE || sampleRate <= 0.0)
            return 0;

        return av_rescale_q (stream.duration, stream.time_base, AVRational { 1, (int) (sampleRate + 0.5) });
    }

    //==========================================================================
    /** Growing stereo float destination for one decoded stream. Writes straight into a juce::AudioBuffer
        sized from the declared duration, so the common case never reallocates or copies. */
    class StereoSink
    {
    public:
        void reserve (juce::int64 expectedSamples)
        {
            const auto capacity = (int) juce::jlimit<juce::int64> (1 << 16, kMaxSamples, expectedSamples + 4096);
            buffer.setSize (2, capacity);
            length = 0;
        }

        juce::int64 getLength() const noexcept { return length; }

        /** Room for `n` more samples; false if that would exceed the 2^31 sample AudioBuffer limit. */
        bool prepareToAppend (int n)
        {
            const juce::int64 needed = length + n;

            if (needed > kMaxSamples)
                return false;

            if (needed > buffer.getNumSamples())
                buffer.setSize (2, (int) juce::jmin<juce::int64> (kMaxSamples, juce::jmax<juce::int64> (needed, (juce::int64) buffer.getNumSamples() * 3 / 2)),
                                true, false, false);
            return true;
        }

        float* channelToWrite (int channel) noexcept { return buffer.getWritePointer (channel) + length; }
        void commit (int n) noexcept                  { length += n; }

        /** Zero-pads to `targetLength` (>= current length) and hands the buffer over at exactly that size. */
        juce::AudioBuffer<float> finish (juce::int64 targetLength)
        {
            if (targetLength > length)
            {
                if (targetLength > buffer.getNumSamples())
                    buffer.setSize (2, (int) targetLength, true, false, false);

                buffer.clear ((int) length, (int) (targetLength - length));
            }

            // Shrinking with avoidReallocating keeps the existing allocation and channel pointers.
            buffer.setSize (2, (int) targetLength, true, false, true);
            length = targetLength;
            return std::move (buffer);
        }

    private:
        static constexpr juce::int64 kMaxSamples = 0x7ffffff0;

        juce::AudioBuffer<float> buffer;
        juce::int64 length = 0;
    };

    //==========================================================================
    // Sample format conversion (libswresample is not built): planar/interleaved u8/s16/s32/s64/flt/dbl -> float.
    template <typename Src, typename ToFloat>
    void convertChannel (const AVFrame& frame, int channel, int numChannels, bool planar, float* dst, ToFloat toFloat)
    {
        const int n = frame.nb_samples;

        if (planar)
        {
            const auto* s = reinterpret_cast<const Src*> (frame.extended_data[channel]);

            for (int i = 0; i < n; ++i)
                dst[i] = toFloat (s[i]);
        }
        else
        {
            const auto* s = reinterpret_cast<const Src*> (frame.extended_data[0]) + channel;

            for (int i = 0; i < n; ++i, s += numChannels)
                dst[i] = toFloat (*s);
        }
    }

    /** Converts one channel of `frame` to float. Returns false for an unsupported sample format. */
    bool convertFrameChannel (const AVFrame& frame, int channel, int numChannels, float* dst)
    {
        const auto format = (AVSampleFormat) frame.format;
        const bool planar = av_sample_fmt_is_planar (format) != 0;

        switch (av_get_packed_sample_fmt (format))
        {
            case AV_SAMPLE_FMT_U8:
                convertChannel<std::uint8_t> (frame, channel, numChannels, planar, dst,
                                              [] (std::uint8_t v) { return ((float) v - 128.0f) * (1.0f / 128.0f); });
                return true;

            case AV_SAMPLE_FMT_S16:
                convertChannel<std::int16_t> (frame, channel, numChannels, planar, dst,
                                              [] (std::int16_t v) { return (float) v * (1.0f / 32768.0f); });
                return true;

            case AV_SAMPLE_FMT_S32:
                convertChannel<std::int32_t> (frame, channel, numChannels, planar, dst,
                                              [] (std::int32_t v) { return (float) ((double) v * (1.0 / 2147483648.0)); });
                return true;

            case AV_SAMPLE_FMT_S64:
                convertChannel<std::int64_t> (frame, channel, numChannels, planar, dst,
                                              [] (std::int64_t v) { return (float) ((double) v * (1.0 / 9223372036854775808.0)); });
                return true;

            case AV_SAMPLE_FMT_FLT:
                convertChannel<float> (frame, channel, numChannels, planar, dst, [] (float v) { return v; });
                return true;

            case AV_SAMPLE_FMT_DBL:
                convertChannel<double> (frame, channel, numChannels, planar, dst, [] (double v) { return (float) v; });
                return true;

            default:
                return false;
        }
    }

    //==========================================================================
    /** One FFmpeg decoder for one audio stream, appending everything it decodes to a StereoSink. */
    class StreamDecoder
    {
    public:
        bool open (const AVStream& stream, int streamNumber, juce::String& error)
        {
            index = streamNumber;
            const auto* par = stream.codecpar;

            if (par->ch_layout.nb_channels > 2)
            {
                error = "Stream " + juce::String (streamNumber) + " has " + juce::String (par->ch_layout.nb_channels)
                      + " channels; only mono and stereo are supported.";
                return false;
            }

            const auto* codec = avcodec_find_decoder (par->codec_id);

            if (codec == nullptr)
            {
                error = "No FFmpeg decoder for stream " + juce::String (streamNumber)
                      + " (codec \"" + juce::String (avcodec_get_name (par->codec_id)) + "\").";
                return false;
            }

            context.reset (avcodec_alloc_context3 (codec));

            if (context == nullptr)
            {
                error = "Out of memory allocating a decoder.";
                return false;
            }

            int ret = avcodec_parameters_to_context (context.get(), par);

            if (ret >= 0)
            {
                context->pkt_timebase = stream.time_base;
                ret = avcodec_open2 (context.get(), codec, nullptr);
            }

            if (ret < 0)
            {
                error = "Could not open the decoder for stream " + juce::String (streamNumber) + ": " + avErrorString (ret);
                return false;
            }

            frame.reset (av_frame_alloc());
            sampleRate = par->sample_rate;
            declaredChannels = par->ch_layout.nb_channels;
            declaredLength = declaredLengthSamples (stream, sampleRate);
            sink.reserve (declaredLength);
            return frame != nullptr;
        }

        /** Sends a packet (or nullptr to flush) and appends every frame it yields. */
        bool decode (const AVPacket* packet, juce::String& error)
        {
            if (packet != nullptr)
            {
                ++numPackets;

                if (! sawSkipSideData)
                {
                    sawSkipSideData = true;
                    size_t size = 0;

                    if (const auto* sd = av_packet_get_side_data (packet, AV_PKT_DATA_SKIP_SAMPLES, &size); sd != nullptr && size >= 10)
                    {
                        skipStart = (juce::int64) AV_RL32 (sd);
                        skipEnd   = (juce::int64) AV_RL32 (sd + 4);
                    }
                }
            }

            int ret = avcodec_send_packet (context.get(), packet);

            if (ret < 0 && ! (packet == nullptr && ret == AVERROR_EOF))
            {
                error = "Decode error in stream " + juce::String (index) + ": " + avErrorString (ret);
                return false;
            }

            for (;;)
            {
                ret = avcodec_receive_frame (context.get(), frame.get());

                if (ret == AVERROR (EAGAIN) || ret == AVERROR_EOF)
                    return true;

                if (ret < 0)
                {
                    error = "Decode error in stream " + juce::String (index) + ": " + avErrorString (ret);
                    return false;
                }

                const bool ok = appendFrame (error);
                av_frame_unref (frame.get());

                if (! ok)
                    return false;
            }
        }

        int getNumSourceChannels() const noexcept  { return sourceChannels > 0 ? sourceChannels : declaredChannels; }
        int getSampleRate() const noexcept         { return sampleRate; }
        juce::int64 getDeclaredLength() const      { return declaredLength; }
        juce::int64 getLength() const              { return sink.getLength(); }
        juce::int64 getNumPackets() const          { return numPackets; }
        juce::int64 getFirstFrameLength() const    { return firstFrameLength; }
        juce::int64 getSkipStart() const           { return skipStart; }
        juce::int64 getSkipEnd() const             { return skipEnd; }
        juce::AudioBuffer<float> finish (juce::int64 targetLength) { return sink.finish (targetLength); }

    private:
        bool appendFrame (juce::String& error)
        {
            const int n = frame->nb_samples;
            const int channels = frame->ch_layout.nb_channels;

            if (n <= 0)
                return true;

            if (channels < 1 || channels > 2)
            {
                error = "Stream " + juce::String (index) + " has " + juce::String (channels)
                      + " channels; only mono and stereo are supported.";
                return false;
            }

            if (sourceChannels == 0)
            {
                sourceChannels = channels;
                firstFrameLength = n;

                if (sampleRate <= 0)
                    sampleRate = frame->sample_rate;
            }
            else if (channels != sourceChannels || frame->sample_rate != sampleRate)
            {
                error = "Stream " + juce::String (index) + " changes format mid-stream (not supported).";
                return false;
            }

            if (! sink.prepareToAppend (n))
            {
                error = "Stream " + juce::String (index) + " is too long to hold in memory.";
                return false;
            }

            float* left  = sink.channelToWrite (0);
            float* right = sink.channelToWrite (1);

            if (! convertFrameChannel (*frame, 0, channels, left))
            {
                error = "Unsupported sample format \"" + juce::String (av_get_sample_fmt_name ((AVSampleFormat) frame->format))
                      + "\" in stream " + juce::String (index) + ".";
                return false;
            }

            if (channels == 2)
                convertFrameChannel (*frame, 1, channels, right);
            else
                std::copy (left, left + n, right);    // mono -> both channels

            sink.commit (n);
            return true;
        }

        CodecPtr context;
        FramePtr frame;
        StereoSink sink;

        int index = 0;
        int sampleRate = 0;
        int declaredChannels = 0;
        int sourceChannels = 0;
        juce::int64 declaredLength = 0;
        juce::int64 numPackets = 0;
        juce::int64 firstFrameLength = 0;
        juce::int64 skipStart = 0, skipEnd = 0;
        bool sawSkipSideData = false;
    };

    juce::String describeSkip (const StreamDecoder& d, int streamNumber, juce::String label)
    {
        return "Stream " + juce::String (streamNumber) + " (" + label + "): " + juce::String (d.getNumPackets())
             + " packets, first frame " + juce::String (d.getFirstFrameLength()) + " samples, "
             + "skip-samples side data start=" + juce::String (d.getSkipStart()) + " end=" + juce::String (d.getSkipEnd())
             + ", decoded " + juce::String (d.getLength()) + " vs declared " + juce::String (d.getDeclaredLength()) + ".";
    }

    PacketPtr makePacket() { return PacketPtr (av_packet_alloc()); }

    // ---------------------------------------------------------------------------------------------
    DecodedOriginal decodeOriginalImpl (const juce::File& file)
    {
        DecodedOriginal result;
        FormatPtr format;

        if (! openFile (file, format, result.error))
            return result;

        const int streamNumber = av_find_best_stream (format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

        if (streamNumber < 0)
        {
            result.error = "\"" + file.getFileName() + "\" has no audio stream.";
            return result;
        }

        StreamDecoder decoder;
        AVStream& stream = *format->streams[streamNumber];

        for (unsigned i = 0; i < format->nb_streams; ++i)
            if ((int) i != streamNumber)
                format->streams[i]->discard = AVDISCARD_ALL;

        if (! decoder.open (stream, streamNumber, result.error))
            return result;

        auto packet = makePacket();

        if (packet == nullptr)
        {
            result.error = "Out of memory.";
            return result;
        }

        for (;;)
        {
            const int ret = av_read_frame (format.get(), packet.get());

            if (ret == AVERROR_EOF)
                break;

            if (ret < 0)
            {
                result.error = "Read error in \"" + file.getFileName() + "\": " + avErrorString (ret);
                return result;
            }

            const bool mine = packet->stream_index == streamNumber;
            const bool ok = ! mine || decoder.decode (packet.get(), result.error);
            av_packet_unref (packet.get());

            if (! ok)
                return result;
        }

        if (! decoder.decode (nullptr, result.error))
            return result;

        if (decoder.getLength() == 0 || decoder.getSampleRate() <= 0)
        {
            result.error = "\"" + file.getFileName() + "\" decoded to no audio.";
            return result;
        }

        if (decoder.getNumSourceChannels() == 1)
            result.notes.add ("Mono source duplicated to both channels.");

        result.notes.add (describeSkip (decoder, streamNumber, "Original"));
        result.sampleRate = decoder.getSampleRate();
        result.declaredLength = decoder.getDeclaredLength();
        result.audio = decoder.finish (decoder.getLength());
        result.ok = true;
        return result;
    }

    // ---------------------------------------------------------------------------------------------
    DecodedStems decodeStemFileImpl (const juce::File& file)
    {
        DecodedStems result;
        FormatPtr format;

        if (! openFile (file, format, result.error))
            return result;

        std::vector<int> audioStreams;

        for (unsigned i = 0; i < format->nb_streams; ++i)
            if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                audioStreams.push_back ((int) i);

        if (audioStreams.size() != 1 + numStems)
        {
            result.error = "\"" + file.getFileName() + "\" is not a Stem File: expected exactly "
                         + juce::String (1 + numStems) + " audio streams (Embedded Master + 4 Stems) but found "
                         + juce::String ((int) audioStreams.size()) + ".";
            return result;
        }

        // Audio stream 0 is the Embedded Master: never given a decoder, and the demuxer is told to drop it.
        for (unsigned i = 0; i < format->nb_streams; ++i)
            format->streams[i]->discard = AVDISCARD_ALL;

        std::array<StreamDecoder, numStems> decoders;
        std::vector<int> streamToStem (format->nb_streams, -1);

        for (int s = 0; s < numStems; ++s)
        {
            const int streamNumber = audioStreams[(size_t) s + 1];
            AVStream& stream = *format->streams[streamNumber];
            stream.discard = AVDISCARD_DEFAULT;
            streamToStem[(size_t) streamNumber] = s;

            if (! decoders[(size_t) s].open (stream, streamNumber, result.error))
                return result;
        }

        for (int s = 1; s < numStems; ++s)
        {
            if (decoders[(size_t) s].getSampleRate() != decoders[0].getSampleRate()
                || decoders[(size_t) s].getNumSourceChannels() != decoders[0].getNumSourceChannels())
            {
                result.error = "The Stems of \"" + file.getFileName() + "\" do not share one sample rate and channel count.";
                return result;
            }
        }

        auto packet = makePacket();

        if (packet == nullptr)
        {
            result.error = "Out of memory.";
            return result;
        }

        // One demux pass feeding all four decoders.
        for (;;)
        {
            const int ret = av_read_frame (format.get(), packet.get());

            if (ret == AVERROR_EOF)
                break;

            if (ret < 0)
            {
                result.error = "Read error in \"" + file.getFileName() + "\": " + avErrorString (ret);
                return result;
            }

            const int stem = packet->stream_index >= 0 && (size_t) packet->stream_index < streamToStem.size()
                                ? streamToStem[(size_t) packet->stream_index] : -1;
            const bool ok = stem < 0 || decoders[(size_t) stem].decode (packet.get(), result.error);
            av_packet_unref (packet.get());

            if (! ok)
                return result;
        }

        juce::int64 longest = 0;

        for (auto& d : decoders)
        {
            if (! d.decode (nullptr, result.error))
                return result;

            longest = juce::jmax (longest, d.getLength());
        }

        if (longest == 0)
        {
            result.error = "\"" + file.getFileName() + "\" decoded to no audio.";
            return result;
        }

        // The source rate and channel count must still agree after decoding (the decoders may have refined them).
        for (int s = 1; s < numStems; ++s)
        {
            if (decoders[(size_t) s].getSampleRate() != decoders[0].getSampleRate()
                || decoders[(size_t) s].getNumSourceChannels() != decoders[0].getNumSourceChannels())
            {
                result.error = "The Stems of \"" + file.getFileName() + "\" do not share one sample rate and channel count.";
                return result;
            }
        }

        for (int s = 0; s < numStems; ++s)
        {
            auto& d = decoders[(size_t) s];
            result.notes.add (describeSkip (d, audioStreams[(size_t) s + 1], stemName ((Stem) s)));

            if (d.getLength() != longest)
                result.notes.add (juce::String (stemName ((Stem) s)) + " is " + juce::String (longest - d.getLength())
                                + " samples shorter than the longest Stem; zero-padded at the end.");
        }

        if (decoders[0].getNumSourceChannels() == 1)
            result.notes.add ("Mono Stems duplicated to both channels.");

        result.sampleRate = decoders[0].getSampleRate();
        result.declaredLength = decoders[0].getDeclaredLength();

        for (int s = 0; s < numStems; ++s)
            result.stems[(size_t) s] = decoders[(size_t) s].finish (longest);

        result.ok = true;
        return result;
    }
}

DecodedOriginal decodeOriginal (const juce::File& file)
{
    try
    {
        return decodeOriginalImpl (file);
    }
    catch (const std::exception& e)   // e.g. std::bad_alloc: nothing may propagate across the API
    {
        DecodedOriginal failed;
        failed.error = "Decoding failed: " + juce::String (e.what());
        return failed;
    }
}

DecodedStems decodeStemFile (const juce::File& file)
{
    try
    {
        return decodeStemFileImpl (file);
    }
    catch (const std::exception& e)
    {
        DecodedStems failed;
        failed.error = "Decoding failed: " + juce::String (e.what());
        return failed;
    }
}

} // namespace stemab
