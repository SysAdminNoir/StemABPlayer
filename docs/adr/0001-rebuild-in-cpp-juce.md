# Rebuild in C++ / JUCE instead of WinUI 3 / .NET

Phases 1–2 were built in WinUI 3 / .NET 8 / NAudio / Win2D. We are rebuilding the
whole app in C++ with JUCE 8 (CMake, `FetchContent` pinned to a tag, AGPLv3 — personal tool). JUCE supplies
most of what was hand-rolled in .NET — device I/O for both WASAPI and ASIO, a device selector, planar float
buffers, DSP and a component GUI — and avoids the NAudio endpoint-starvation workarounds. The .NET code is a
reference for intent only, not a line-by-line port. It lived in `dotnet-legacy/` during the rebuild and was
removed, including from the git history, once the JUCE app was complete (2026-11-16).

Windows-only for now. Decoding sits behind an interface so a macOS backend could be added later.
