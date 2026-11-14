# Building Stem A/B Player (C++ / JUCE)

## Prerequisites

- Windows 10/11 x64
- Visual Studio 2022 Community with the **Desktop development with C++** workload (includes MSVC, the
  Windows SDK, CMake and vcpkg)
- CMake 3.25 or newer (the copy bundled with VS 2022 is fine)
- Git (JUCE and Catch2 are fetched with `FetchContent`; FFmpeg is built by vcpkg)
- Optional: Steinberg ASIO SDK (see below)

## vcpkg

FFmpeg is installed by vcpkg in manifest mode (`vcpkg.json`), driven by the toolchain file in the
`vs2022` preset, which reads the `VCPKG_ROOT` environment variable.

- From a **Developer PowerShell for VS 2022**, `VCPKG_ROOT` is already set (to VS's bundled vcpkg).
- From a plain shell, set it yourself, e.g. a standalone clone:
  `git clone https://github.com/microsoft/vcpkg C:\vcpkg; C:\vcpkg\bootstrap-vcpkg.bat; $env:VCPKG_ROOT = "C:\vcpkg"`
  (set it permanently with `setx VCPKG_ROOT C:\vcpkg`).

The first configure builds FFmpeg (avcodec + avformat only, LGPL, shared DLLs) and takes roughly
15-30 minutes. Later configures reuse vcpkg's binary cache.

## Configure, build, test

```powershell
cmake --preset vs2022
cmake --build --preset debug          # or: --preset release
ctest --preset debug                  # or: --preset release
```

The app is built to `build/vs2022/src/app/StemAB_App_artefacts/<Config>/Stem AB Player.exe`. vcpkg's
post-build step copies the FFmpeg DLLs next to the app and next to `StemAB_Tests.exe`.

## ASIO SDK

ASIO is optional. Point `STEMAB_ASIO_SDK_DIR` at the unpacked Steinberg SDK (the folder that contains
`common/iasiodrv.h`). Default: `C:/Projects/SDKs/asiosdk`.

```powershell
cmake --preset vs2022 -DSTEMAB_ASIO_SDK_DIR=D:/SDKs/asiosdk
```

If the header is not found, configure prints a warning and the app builds WASAPI-only. The status line
at the top of the app shows the available device types (and `[built without ASIO]`).

## Where things come from

| Dependency | Source | Version |
|---|---|---|
| JUCE | `FetchContent` (GitHub) | tag `8.0.17-security` |
| Catch2 | `FetchContent` (GitHub) | tag `v3.16.1` |
| FFmpeg | vcpkg manifest (`vcpkg.json`) | whatever the pinned `builtin-baseline` provides (FFmpeg 9.0.2) |
| ASIO SDK | local folder (`STEMAB_ASIO_SDK_DIR`) | Steinberg ASIO SDK 2.3.x |
