# Plan: Stem A/B Player — standalone native Windows app

## What this is
A **separate, standalone project** (its own solution/repo — **not** part of the stem-separation framework).
A native Windows desktop app for **critical A/B listening**: load an **original** track (A) and that track's
**separated stems** (B), audition them sample-aligned under one transport, and crossfade original ⇄ stem-sum
to hunt separation artifacts. It is the ear-based QA tool for the separation work, but it is architecturally
independent of it.

UI is specified by the design mockup at `design/Stem AB Player.dc.html` (+ `support.js` runtime,
`Stem-AB-Player.thumbnail.png`). Those waveforms are synthetic placeholders; the real app renders loaded audio.
Dark Fluent styling: bg `#202020`, panel `#272727`, accent `#60CDFF`, Segoe UI Variable.

## Why it's split out (relationship to the separation framework)
The player only **reads and plays already-separated stem files from disk** — it never runs separation. So it
needs **none** of the framework's heavy stack (no ONNX Runtime, no CUDA, no PyTorch models). Keeping it separate
means:
- No dependency on `StemSep.Core`, CUDA, or the model files — the player stays small and fast to build/run.
- The only contract between the two projects is the **stem file format on disk** (see *Input formats*).
- It can be developed, versioned, and shipped on its own timeline, and later reused as a standalone utility.

The separation framework (`NuoStems.sln`, see `StemSep-CSharp-Rebuild-Plan.md`) **produces** the stem files;
this player **consumes** them. That's the whole integration surface.

- **Intended location:** its own folder/repo, e.g. `C:\Projects\StemABPlayer\` (sibling to `NuoStemsRev`).
  This plan doc currently lives under `NuoStemsRev\docs\` for convenience; it moves with the project when scaffolded.

## Tech stack (native, FAST, zero-lag)
- **Shell/UI: WinUI 3 (.NET 8, `net8.0-windows10.0.19041.0`+)** — natively *is* the Fluent dark look in the
  mockup (Segoe UI Variable, dark theme, custom caption buttons).
- **Rendering: Win2D (`Microsoft.Graphics.Win2D`) → Direct2D/Direct3D11 (GPU).** The "DirectX, most efficient"
  path. Static content (waveforms, spectrograms) is baked once and blitted; only the playhead + meters redraw
  each frame.
- **Audio: WASAPI, event-driven, exclusive-capable**, on a dedicated real-time mixing thread. Baseline via
  **NAudio** (`WasapiOut`, `MediaFoundationReader`/`AudioFileReader`); drop to **raw WASAPI (Vortice.Windows)**
  if we need exclusive-mode latency or tighter buffer control.
- **No GPU-compute dependency** (no OpenCL/CUDA): drawing is GPU via Direct2D; the small STFT for the
  spectrogram view is cheap CPU work, cached. (A D2D compute/pixel shader for spectrograms is an optional
  later optimization, not needed for zero-lag.)

## Audio engine (the core of "zero-lag")
### Threading model
- **UI thread** — WinUI; only sets atomic parameter targets and reads meter snapshots. Never blocks on audio.
- **Audio render thread** — the WASAPI event callback. Pulls from source buffers, applies per-source gains,
  sums the A bus and the audible-B bus, crossfades, applies master gain + limiter, writes the device buffer.
  No allocation, no locks, no file I/O here.
- **Load/decode worker** — opens files, decodes to float32, resamples to the device rate, builds the waveform
  peak cache. Hands finished immutable buffers to the audio thread via a single atomic pointer swap.

### Clock & alignment
- One **shared sample timeline** at the device sample rate. A single atomic `playheadSample` is the position for
  **all** sources, so A and every B stem are inherently sample-aligned.
- On load, every source is resampled (offline, on the worker) to the device rate so the callback never resamples.
  (WDL `resample`, `soxr`, or NAudio's resampler; quality > speed since it's offline.)

### Source & mix graph
```
A (orig L/R) ─► gainA ─┐
                       ├─► [crossfade gA] ─┐
B stems:               │                   ├─► master gain ─► soft limiter ─► device
  drums  ─► gS_drums ─┐│                   │
  bass   ─► gS_bass  ─┤├─► sum(audible) ──►┘ (B bus)  [crossfade gB]
  other  ─► gS_other ─┤
  vocals ─► gS_vocals┘
```
- **A bus** = original stereo. **B bus** = sum of **audible** stems (mute/solo resolved to per-stem gain
  targets of 0 or 1). Crossfade blends A vs B → the reconstruction test.
- **Crossfade curves:** *Linear* `gA = 1−x, gB = x`; *Equal-power* `gA = cos(xπ/2), gB = sin(xπ/2)`.
- **Mute/Solo logic:** if any stem soloed → audible = soloed set; else audible = non-muted set. Each stem's
  audibility becomes a 0/1 **target** the audio thread ramps toward.

### Click-free parameter smoothing (why it feels instant)
- Every control (crossfade x, per-stem gain, mute/solo, master) is an **atomic target** set by the UI.
- The audio thread ramps the *actual* value toward the target with a one-pole/linear ramp over ~5–10 ms per
  callback block. No zipper noise, no clicks, and the change is audible immediately.
- Lock-free: UI writes targets with `Interlocked`/`volatile`; the audio thread only reads them. No mutex on
  the render path.

### Buffers, seeking, looping, latency
- **Decode whole file to RAM** (float32) for zero-lag seeking and looping. Budget: ~3–4 min stereo float32 ≈
  120–200 MB for A; a 4–6 stem set ≈ 0.5–1.2 GB. Fine on this machine; add a streaming fallback for very long
  files later.
- **Seek** = set `playheadSample` atomically; the next callback reads from the new position (ramp gains from 0
  briefly to avoid a click on jump).
- **Loop** = sample-accurate loop points; the callback wraps `playheadSample` at the loop end.
- **Latency target** < ~20 ms: WASAPI **event-driven shared** mode by default (period ~10 ms); **exclusive**
  mode as an option for lower latency. Size the device buffer to the WASAPI period; process in sub-blocks.
- **Headroom/limiter:** summing N stems can exceed 0 dBFS, so a soft limiter (or fixed headroom + makeup) sits
  on the master; A/B is **level-matched** so the crossfade compares content, not loudness.

### Metering
- The audio thread computes **peak + RMS** per source and for the A/B masters into lock-free slots
  (atomics / a small SPSC ring). The UI samples these at frame rate (60 fps) for the lane meters and the
  A/B stereo meters, plus the dB mix-gain readouts.

## Render pipeline (GPU, static-bake + cheap overlay)
- **Waveform peak cache:** on load, reduce each channel to **min/max per pixel column** at several zoom levels
  (a mip pyramid). Drawing a lane = slice the cache for the current width and draw 1px columns / filled
  geometry in Win2D. Resize re-slices the cache (no recompute); zoom picks a mip level.
- **3-band original:** precompute lo/mid/hi band energies (split via simple filters on load) so the A lanes
  draw blue/amber/white bands like the mockup; stems draw in their per-stem color.
- **Spectrogram view:** STFT per lane on the worker → magnitude → colormap → a Win2D/D2D bitmap, **cached** and
  blitted/scaled to the lane. Recompute only on file/view change, not per frame (mirrors the mockup's offscreen
  canvas cache).
- **Per-frame work only:** playhead line + diamond, played-region dim overlay, and meter bars. Waveform/spectro
  are static bitmaps/geometry, so frames stay cheap → steady 60–120 fps.
- Use Win2D `CanvasControl` for static lanes (invalidate on data/resize) and a lightweight animated overlay for
  the playhead/meters; honor DPI scaling.

## Input formats (the contract with the framework)
Accept both, auto-detected:
- A single **multichannel `.stem.wav`** — interleaved stereo pairs: **8 ch / 4 stems** (Drums, Bass, Other,
  Vocals) or **12 ch / 6 stems** (+ Guitar, Piano). Traktor/NUO channel order.
- A **folder of per-stem WAVs** (e.g. `drums.wav`, `bass.wav`, `vocals.wav`, `other.wav`).
Original (A) is any common file (WAV/FLAC/MP3/M4A) decoded to float32 stereo.

## Project structure (standalone solution)
`StemABPlayer.sln`:
- `src/StemABPlayer.App/` — WinUI 3 app: windows, views, view-models, Win2D render controls.
- `src/StemABPlayer.Audio/` — the engine: WASAPI output, mixer/bus graph, parameter ramps, metering, decode +
  resample + buffer management. (Plain .NET lib, unit-testable headless.)
- `src/StemABPlayer.Render/` — waveform peak-cache + spectrogram bake + Win2D draw helpers.
- `tests/StemABPlayer.Audio.Tests/` — mixer math (crossfade curves, solo/mute resolution, ramp click-free),
  loop/seek sample accuracy, peak-cache correctness. (Headless, no device.)

## Build phases
1. **Scaffold** `StemABPlayer.sln` (WinUI 3 app + Audio + Render libs + tests). Confirm the app launches with
   the dark Fluent shell and a WASAPI device enumerates. ✅ **Done** — commit `bc68afc`: .NET 8 pinned
   (`global.json` 8.0.426), NAudio 2.2.1 device enumeration + passing test, Win2D 1.4.0 on WindowsAppSDK 2.5.1,
   App converted to unpackaged .exe; dark shell + device list verified on real hardware.
2. **Audio engine first (zero-lag spine).** Split into three checkpoints, each sized to fit roughly one ~5-hour
   Pro usage window and to end on a clean commit:
   - **2a — Decode to RAM.** Load + decode + resample one stereo file to float32 at the device rate, fully in
     memory. WAV via NAudio's managed reader; **AAC / M4A via Windows Media Foundation** (`MediaFoundationReader`)
     — never ffmpeg. Build the waveform peak-cache stub. Headless unit tests on a short fixture (sample count,
     sample rate, channel layout). Inspect a real `.stem.m4a` track structure here (NI/Traktor multi-track MP4 vs
     the plan's multichannel `.stem.wav` assumption — see *Input formats*).
   - **2b — WASAPI output + transport.** Event-driven shared-mode output on a dedicated render thread pulling
     from the RAM buffer; **play / pause / sample-accurate seek / sample-accurate loop** driven by a single
     atomic `playheadSample`. No allocation and no locks on the render path.
   - **2c — Master + verify.** Master gain and a soft limiter with click-free ramps; peak/RMS metering slots.
     Verify glitch-free playback and **measure output latency on a local console session** (not RDP — see *Risks*).
3. **Multi-source mix + A/B:** load A + the B stems aligned; A-bus / B-bus sum; crossfade (linear + equal-power);
   per-stem mute/solo with click-free ramps; metering slots.
4. **Render pipeline:** waveform peak cache + Win2D lanes (3-band A, colored stems), playhead + click-to-seek,
   played-region dim, lane meters. Then the spectrogram view (cached bake).
5. **UI to the mockup:** title bar, command bar (Open original / Open stem file, view toggle, output device),
   twin panels + headers/format chips, transport zone with the hero crossfader, fade-curve + master, keyboard
   map (Space, A/B/C, etc.).
6. **Polish:** device hot-swap, DPI, long-file streaming fallback, drag-drop open, remembered window state.

## Prerequisites
- **.NET 8 SDK** (x64) + **Windows App SDK / WinUI 3** workload; **Win2D** (`Microsoft.Graphics.Win2D`).
- **NAudio** (and optionally **Vortice.Windows** for raw WASAPI). A resampler lib (soxr/WDL) for offline rate
  conversion.
- No CUDA / ONNX / model files — this project is independent of the separation stack.

## Verification
- **By ear** (the point of the tool): crossfade reveals artifacts; solo/mute isolate bleed. No A/B against NUO
  or any commercial software.
- **Engine unit tests:** crossfade-gain curves, solo/mute→audible resolution, ramp produces no discontinuity,
  loop/seek land on the exact sample, peak-cache min/max matches source.
- **Runtime:** glitch-free playback under load, measured output latency, CPU render path stays off the audio
  thread, frame rate holds at 60+ fps while playing.

## Risks
- **Sample-rate mismatch** between A and stems, or vs the device — resample everything to the device rate on load.
- **Summing clip** — headroom + limiter, level-matched A/B.
- **WASAPI exclusive mode** device quirks — keep shared mode as the reliable default.
- **WinUI 3 + Win2D** packaging/deployment friction on Windows — lock SDK/workload versions early.
