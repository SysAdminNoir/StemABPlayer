#pragma once

#include <string>

namespace stemab
{

/** e.g. "FFmpeg 8.0 (libavformat 62.3.100, libavcodec 62.11.100)". Proves FFmpeg is linked and loadable. */
std::string ffmpegVersionString();

} // namespace stemab
