# Plan: Stem A/B Player — C++ / JUCE rebuild

Vocabulary is defined in [CONTEXT.md](CONTEXT.md); decisions and their reasons in [docs/adr/](docs/adr/).
The previous WinUI 3 / .NET implementation (commits `e7a2ba9..3baea66`, moved to `dotnet-legacy/` during the
rebuild) is a reference for intent only (ADR 0001).

## What this is
A native Windows desktop tool for critical listening that judges **stem-separation quality**. Load an
**Original (A)** (e.g. `Audio Files/*.flac`) and a **Stem File** (`Stem Files/*.stem.m4a`, NI layout:
Embedded Master + 4 Stems). The four Stems summed at their original amplitude form **B**. If separation is
perfect, B equals A. The tool lets you hear and measure that:
- **Crossfade** A ⇄ B (Linear or Equal-power).
- **Residual** (A − all four Stems), as a third listening position.
- **Null Depth**: Track (whole file, computed on load) and Live (short window at the playhead).
- **Mute / Solo** per Stem to isolate bleed (listening only; never affects Residual / Null Depth).

UI target: `design/Stem AB Player.dc.html` (dark Fluent: bg `#202020`, panel `#272727`, accent `#60CDFF`,
Segoe UI Variable), with the changes listed under *UI* below.

## Hard rules
- **In-process decoding only.** FFmpeg *libraries* (libavformat/libavcodec) via vcpkg. Never spawn
  `ffmpeg.exe` or any decoder process (ADR 0002).
- **Identical treatment of A and B** in the listening path: no limiter, no level-matching, no auto-trim.
  Master Gain is one scalar; over 0 dBFS shows **Clip** (ADR 0004).
- **Native Rate preferred**: ask the device for the source rate; if it refuses, the engine mixes at the
  source rate and resamples its single stereo output to the device rate in realtime (r8brain; ADR 0005). The UI shows the active rate and whether audio is resampled.
- **Realtime path is allocation-free and lock-free**; UI sets atomic targets and polls meters at ~60 fps.
- **Click-free**: every gain change, pause, seek and loop wrap ramps (`SmoothedValue`, ~5–10 ms).

## Stack
- C++20, CMake, **JUCE 8** via `FetchContent` pinned to an exact tag (AGPLv3, personal use).
- **VS 2022 Community** (Desktop development with C++) + CMake. User installs (elevated).
- Audio out: `AudioDeviceManager` + `AudioDeviceSelectorComponent`. **WASAPI** (shared + exclusive) and
  **ASIO**. ASIO SDK (Steinberg, GPLv3 dual-licence since Oct 2025) lives outside the repo at
  `C:\Projects\SDKs\asiosdk`, passed in through a CMake cache variable. If it's missing, the build continues
  WASAPI-only with a warning.
- Third-party libs via `FetchContent`: r8brain-free-src (resample), Catch2 v3 (tests).
- **FFmpeg** (avformat, avcodec; LGPL, shared DLLs) via **vcpkg manifest mode** (`vcpkg.json` in the repo,
  vcpkg toolchain file in CMake). vcpkg ships with VS 2022.
- Windows-only for now; decoding sits behind an interface so a macOS backend could be added later.

## Targets (module boundaries)
| Target | Owns | Depends on |
|---|---|---|
| `StemAB_Decode` | One libav-based reader for both inputs: the Original (any FFmpeg format) and the 4 Stem streams of a Stem File (Embedded Master never decoded) → one A buffer + four Stem buffers at the source rate (`juce::AudioBuffer<float>`, in RAM); offline `resample()` only if A and the Stems differ in rate | FFmpeg (avformat, avcodec), r8brain |
| `StemAB_Engine` | Transport (play / pause / seek / loop with fades, one atomic playhead); mix (A bus, B = audible Stems, Residual = A − all Stems; crossfade curves; mute/solo); Master Gain; Clip; peak/RMS meters; Live Null Depth. Realtime only: no files, no GUI. | JUCE audio_basics, dsp |
| `StemAB_Analysis` | Offline, on load: Alignment (cross-correlation, polarity, offset + confidence, manual nudge; ADR 0003) and Track Null Depth. Waveform min/max peak pyramid; A's 3-band (lo/mid/hi) energies; STFT spectrogram cache (log-freq, dB colour map, one shared scale across lanes) | JUCE dsp |
| `src/load` (`PairLoader`) | Staged, cancelable loading: A and the Stem File decoded concurrently (Stems on one thread each); A playable first (~0.1 s), B/R fade in when Stems + alignment are ready (~0.2–0.35 s); Track Null Depth afterwards; alignment cache (`%APPDATA%\StemABPlayer\alignment-cache.json`) | Decode, Analysis, Engine |
| `StemAB_App` | GUI: LookAndFeel, lanes, ruler + loop drag, transport bar, crossfader + A/B/R, device selector, keyboard map, settings persistence | all of the above |
| `StemAB_Tests` | Catch2 + CTest. Engine + Analysis on synthetic signals. Decode + alignment on the real test files (skipped if absent). | Decode, Engine, Analysis |

## Mix graph
```
A ─────────────────────────────┬──────────► gA ─┐
                               │                ├─► [A/B crossfade] ─┐
Drums ─► mute/solo gain ─┐     │                │                    │
Bass  ─► mute/solo gain ─┼─► B (audible sum) ► gB┘                    ├─► Master Gain ─► Clip detect ─► device
Other ─► mute/solo gain ─┤     │                                     │
Vocals─► mute/solo gain ─┘     └─► Residual = A − (all 4 Stems) ──── R (exclusive listen)
```
- Crossfade: Linear `gA = 1−x, gB = x`; Equal-power `gA = cos(xπ/2), gB = sin(xπ/2)`.
- Mute/Solo: any soloed → audible = soloed set; else audible = un-muted set. Targets ramp 0↔1.
- R overrides the crossfade while engaged (crossfader greyed out).
- B is shifted by the Alignment Offset; ends are padded or trimmed so A and B share one timeline.

## UI (mockup + agreed changes)
- **Native Windows title bar** (dark mode), title "Stem A/B · {original} · {stem file}". Not custom-drawn.
- Command bar: Open original… / Open stem file… (plus drag-and-drop onto either panel), Waveform /
  Spectrogram toggle, **Spectrogram Size** chooser (4096 default, 8192, 16384, 32768, 65536), output device.
- Panel A: L/R lanes, 3-band colouring. Panel B: 4 Stem lanes (Drums, Bass, Other, Vocals), M/S buttons,
  meters. Panel B header shows **Track Null Depth** and the **Alignment Offset** (+ confidence, nudge).
- Ruler: drag to set the **Loop Region**. Playhead diamond, dimmed played region.
- Transport: Play/Pause (**no Stop button**), Loop, time display. Crossfader with A / B / **R** buttons,
  Center tick, **Live Null Depth** next to R. Fade curve (Linear / Equal power). **Master in dB**
  (−∞…0 dB, default 0) with a **Clip** indicator.
- Keyboard: `Space` play/pause · `A`/`B`/`R` jump to A, B or Residual · `C` center crossfade · `L` loop on/off ·
  `I`/`O` set loop in/out at playhead · `Home` start (or loop start) · `←`/`→` seek 1 s, `Shift` 5 s ·
  `1`–`4` solo Stem · `Shift`+`1`–`4` mute Stem.
- Remembered between runs (`PropertiesFile`): device + settings, window bounds, last loaded pair (opens
  empty, without an error, if the files are gone), Spectrogram Size.

## Checkpoints (one commit each)
1. **Toolchain + scaffold.** User installs VS 2022 Community (C++ workload) + CMake. Move .NET to
   `dotnet-legacy/`. CMake + JUCE (FetchContent) + ASIO option. A dark window with
   `AudioDeviceSelectorComponent` listing WASAPI and ASIO devices. Catch2 + CTest wired with one passing test.
2. **Decode.** libav read of the 4 Stem streams from each test Stem File, plus the FLAC Original through the
   same reader; Native Rate / r8brain path. Tests: 4 stereo buffers, expected lengths, 44.1 kHz.
3. **Alignment + Residual, headless.** Alignment Offset, polarity, confidence and Track Null Depth for all
   three test pairs. Reported as numbers before any UI exists.
4. **Engine, audible.** Transport with fades, A/B/R mix, curves, mute/solo, Master, Clip, meters, Live Null
   Depth. Verified by ear on the physical console (not RDP).
5. **UI to the mockup.** Lanes (peak pyramid, 3-band A), ruler/playhead/loop drag + `I`/`O`, transport bar,
   crossfader + R, keyboard map, persistence.
6. **Spectrogram.** STFT cache + size chooser, computed in the background, cached per size.
7. **Clean-up.** Delete `dotnet-legacy/` and `_diag/`; update this plan and the handoff docs.

## Working method
Orchestrator plans, reviews and commits. Implementation is delegated to Sonnet 5.5 high-effort subagents,
partitioned so parallel agents never touch the same files. Commit at each checkpoint (local repo, no remote).

## Verification
- **Unit tests (Catch2):** crossfade curves; mute/solo → audible set; Residual ignores mute/solo; ramps
  produce no discontinuity; seek/loop land on the exact sample; pause fades then holds; peak-pyramid min/max
  matches source; alignment recovers a known synthetic offset and polarity flip.
- **Real-file tests:** each test Stem File decodes to 4 Stems; alignment confidence is high; Track Null Depth
  reported per pair.
- **By ear** on local hardware ("Realtek HD Audio 2nd output", "Speakers (Realtek(R) Audio)") and via ASIO
  when available. Never judge audio glitches over RDP.

## Risks
- **vcpkg FFmpeg first build is slow** (~15–30 min) → one-time cost; vcpkg's binary cache makes rebuilds quick.
  FFmpeg DLLs must be copied beside the exe (CMake post-build step).
- **Low alignment confidence** (e.g. quiet intros) → widen the correlation window; manual nudge.
- **RAM**: 4 Stems + A, whole file in float, ~0.5–1 GB for long tracks. Acceptable; no streaming fallback
  planned.
- **ASIO driver quirks / rate switching** → WASAPI shared stays the reliable default.
- **65536-point STFT** on long tracks is slow → compute in the background with progress, cache per size.
