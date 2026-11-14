#include "decode/Decode.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace stemab
{

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

} // namespace stemab
