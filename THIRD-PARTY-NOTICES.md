# Third-party notices

Stem A/B Player is built on the following software and fonts. Check each license before distributing
builds of the app.

| Component | Used for | License |
|---|---|---|
| [JUCE](https://juce.com) 8.0.17 | Application framework, audio I/O, UI | Dual-licensed: AGPLv3, or the JUCE commercial / Starter licence. Distributing a build requires complying with one of them. |
| [FFmpeg](https://ffmpeg.org) 9.0.2 — libavcodec, libavformat, libavutil | Decoding audio files | LGPL 2.1 or later. Built by vcpkg with only the `avcodec` and `avformat` features (no GPL components) and linked as **shared DLLs**, which the LGPL requires to be replaceable. Ship the DLLs unmodified and point recipients to the FFmpeg source. |
| [r8brain-free-src](https://github.com/avaneev/r8brain-free-src) 6.5 | Sample-rate conversion | MIT |
| [Catch2](https://github.com/catchorg/Catch2) 3.16.1 | Unit tests only (not shipped) | Boost Software License 1.0 |
| Steinberg ASIO SDK 2.3.4 (optional) | ASIO driver support | Dual-licensed: Steinberg's proprietary ASIO License, or **GPLv3**. This project uses it under GPLv3, which is compatible with the app's AGPL-3.0 (GPLv3 §13 / AGPLv3 §13), so ASIO-enabled builds may be distributed with their source. See [steinberg.net/developers/asiosdk-open](https://www.steinberg.net/developers/asiosdk-open/). The SDK itself is not included in this repository; builds without it are WASAPI-only. |
| [Archivo](https://github.com/Omnibus-Type/Archivo), Archivo Narrow | UI text (bundled) | SIL Open Font License 1.1 — `resources/fonts/OFL-Archivo.txt`, `OFL-ArchivoNarrow.txt` |
| [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono) | Numbers and readouts (bundled) | SIL Open Font License 1.1 — `resources/fonts/OFL-JetBrainsMono.txt` |

The color-blind safe palette uses the Okabe–Ito colors (Okabe & Ito, *Color Universal Design*, 2002).

ASIO is a trademark and software of Steinberg Media Technologies GmbH. The name is used here only to describe
compatibility; the trademark is not covered by the GPL.

"Stems" and the `.stem.m4a` format are by Native Instruments; this app reads the format and is not affiliated
with Native Instruments.
