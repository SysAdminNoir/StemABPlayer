# Changelog

## 0.1.0 — 2026-11-15

First complete version of the C++/JUCE rebuild.

### Listening and analysis
- Sample-accurate alignment of B to A by cross-correlation, cached per file pair.
- Track Null Depth (whole track) and Live Null Depth (around the playhead).
- One Crossfade A ↔ B (equal-power or linear), Residual (A − all stems), Center, Loop.
- Mute / Solo per stem; listening only, never affects the Residual or null depth.
- One Master Gain (max 0 dB) for everything heard; Clip indicator, no limiter.
- Plays at the files' native sample rate when the device allows; otherwise real-time resampling of the
  mixed output (identical for A and B).
- WASAPI output; ASIO when built with the Steinberg SDK.

### Files
- Originals in any format FFmpeg decodes (FLAC, WAV, AIFF, MP3, AAC, ALAC, Ogg, Opus, WMA, APE, WavPack,
  DSD, CAF, W64, …).
- B as an NI/Traktor Stem File (`.stem.m4a`) or a **Stem Set** of 1–4 separate audio files, matched to
  Drums / Bass / Other / Vocals by name; missing stems are silent.
- In-process decoding with the FFmpeg libraries, in parallel; waveform preview as soon as one file is chosen.
- Unload all.

### Interface
- Hardware-panel look: graphite panel, sunken screens, lit pads, bundled Archivo / JetBrains Mono fonts.
- DJ-style waveform lanes (A, four stems, Residual with a +24 dB / 0 dB view toggle) and one shared playhead
  with click/drag seeking.
- 40-segment L/R LED meters with 0.5 s peak hold.
- Interface sizes 90–130 %; layout adapts to smaller windows.
- Color-blind safe (red–green) palette.
- Preferences: start folders, interface size and palette, audio device, session resume.
- Resumes the last session (pair, position, mix settings, device).
- App icon: "a/b" monogram.

### Tools
- `StemAB_Probe`: headless alignment / null-depth report, and `--export-stems` to write a Stem File's stems
  as separate WAVs.

### Before 0.1.0
- 2026-10: WinUI 3 / .NET prototype (kept in `dotnet-legacy/`).
