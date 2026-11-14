# Rebuild in C++ / JUCE instead of WinUI 3 / .NET

Phases 1–2 were built in WinUI 3 / .NET 8 / NAudio / Win2D (commits `e7a2ba9..3baea66`). We are rebuilding the
whole app in C++ with JUCE 8 (CMake, `FetchContent` pinned to a tag, AGPLv3 — personal tool). JUCE supplies
most of what was hand-rolled in .NET — device I/O for both WASAPI and ASIO, a device selector, planar float
buffers, DSP and a component GUI — and avoids the NAudio endpoint-starvation workarounds. The .NET code is a
reference for intent only, not a line-by-line port. It lives in `dotnet-legacy/` during the rebuild and is
deleted once the JUCE app reaches the mockup UI.

Windows-only for now. Decoding sits behind an interface so a macOS backend could be added later.
