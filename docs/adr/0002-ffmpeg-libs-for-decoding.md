# Decode all inputs in-process with the FFmpeg libraries (via vcpkg)

A Stem File (`.stem.m4a`) is one MP4 with five AAC tracks: an Embedded Master that is the only enabled track,
plus four Stems. Default decoders (NAudio, and JUCE has no Windows AAC reader at all) return only the Embedded
Master. We link **libavformat + libavcodec**, which expose every track as its own stream, and read the four
Stem streams. The Embedded Master is never decoded. The Original goes through the same reader, so A and B are
decoded the same way and A may be any format FFmpeg reads.

FFmpeg comes from **vcpkg** (manifest mode, LGPL feature set, shared DLLs shipped beside the exe). It builds
from source, so versions are reproducible, at the cost of a slow first build. Running the `ffmpeg` executable
or spawning any decoder process is ruled out by the project owner. Only in-process library calls are allowed.

## Considered Options
- **Windows Media Foundation `IMFSourceReader`**: no dependency, but it was unproven whether it can select an
  MP4's non-default audio tracks. That would have been a go/no-go risk; FFmpeg removes it and is
  cross-platform.
- **JUCE's built-in FLAC reader for A**: zero code, but it would mean two decode paths and limit A to the
  formats JUCE supports.
- **Prebuilt FFmpeg DLLs** (manual download): faster to set up, less reproducible.
