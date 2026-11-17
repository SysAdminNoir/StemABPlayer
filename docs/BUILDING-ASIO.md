# Building Stem A/B Player with ASIO

ASIO output is optional. Without it the app still builds and plays through WASAPI (shared or exclusive).
With it, ASIO drivers, such as those of most audio interfaces and ASIO4ALL, appear as an extra
device type in **Preferences → Audio**. ASIO is often the easiest way to get low latency and the files'
native sample rate.

This guide covers only the ASIO part. Everything else (Visual Studio, vcpkg, CMake presets) is in
[BUILDING.md](../BUILDING.md); get a normal build working first.

## 1. Get the Steinberg ASIO SDK

1. Download the **ASIO SDK** (2.3.4 or newer) from Steinberg:
   <https://www.steinberg.net/developers/> (see also
   <https://www.steinberg.net/developers/asiosdk-open/>).
2. Unpack it **outside this repository**, for example to `C:\Projects\SDKs\asiosdk`. The SDK is never
   committed here.
3. Check the layout. The folder you will point CMake at must contain `common\iasiodrv.h`:

   ```
   asiosdk\
     common\        asio.h, asiosys.h, iasiodrv.h, ...   <- required
     host\
     driver\
     LICENSE.txt
     ...
   ```

   If the zip unpacked into an extra folder (e.g. `asiosdk\asiosdk_2.3.4\common`), use the inner folder.

### License

From version 2.3.4 the SDK is **dual-licensed**: Steinberg's proprietary ASIO License, or **GPLv3**. This
project uses it under **GPLv3**, which is compatible with the app's AGPL-3.0 license. You don't need to sign
anything with Steinberg. If you distribute an ASIO-enabled build, it is covered by the AGPL/GPL, so
offer its source (this repository) to recipients, as for any build. "ASIO" is a Steinberg trademark: you
may say a build supports ASIO, but don't put "ASIO" in a product name, and don't alter the logo. See
[THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) and the SDK's own `LICENSE.txt` and usage guidelines.

## 2. Tell CMake where the SDK is

The build reads the CMake cache variable `STEMAB_ASIO_SDK_DIR`. Its default is
`C:/Projects/SDKs/asiosdk`, so if you unpacked the SDK there, nothing more is needed:

```powershell
cmake --preset vs2022
```

Anywhere else, pass the path (forward slashes):

```powershell
cmake --preset vs2022 -DSTEMAB_ASIO_SDK_DIR=D:/SDKs/asiosdk
```

The value is cached in `build/vs2022`, so you only pass it once. To change it later, pass it again. To go
back to a WASAPI-only build, pass a path that doesn't exist.

## 3. Check that configure found it

Configure prints one of these lines:

| Output | Meaning |
|---|---|
| `StemAB: ASIO SDK found at ... - ASIO enabled` | ASIO is compiled in. |
| `CMake Warning: ASIO SDK not found at '...' (expected common/iasiodrv.h) - building WASAPI-only.` | The path is wrong or points one folder too high or too low. Fix it and configure again. |

A missing SDK is a warning, not an error, so check for this line. Otherwise you get a working build
without ASIO.

## 4. Build, test, package

Exactly as in [BUILDING.md](../BUILDING.md):

```powershell
cmake --build --preset release
ctest --preset release
```

When enabled, the build defines `JUCE_ASIO=1` and `JUCE_ASIO_USE_EXTERNAL_SDK=1` and adds
`<sdk>/common` to the app's include path. JUCE's own copy of the ASIO headers is *not* used, so the driver
interface always matches the SDK you downloaded. No extra DLL is needed at run time: ASIO drivers are
installed system-wide by the interface's driver package. To package the build, follow "Packaging a release"
in BUILDING.md.

## 5. Check it in the app

1. Run `Stem AB Player.exe` and open **Preferences** (Ctrl+,) → **Audio**.
2. **Output device type** should list **ASIO** next to **Windows Audio** (WASAPI). Pick it and choose your
   driver.
3. The status line shows the device and sample rate. With ASIO, most drivers accept the files' own rate,
   so there is no *(resampled)* tag.

If **ASIO** is not in the list:

- **ASIO isn't compiled in.** Re-run configure and look for the "ASIO enabled" line (step 3).
- **No ASIO driver is installed.** Install your interface's driver, or ASIO4ALL for built-in sound.
- **Another program holds the driver.** Many ASIO drivers allow only one program at a time, so close the
  DAW or other player using it.

## Troubleshooting

| Problem | Fix |
|---|---|
| `fatal error C1083: Cannot open include file: 'iasiodrv.h'` | The cached path points to a folder without `common/iasiodrv.h`. Re-run configure with the correct `-DSTEMAB_ASIO_SDK_DIR`. |
| Configure says "found" but the app has no ASIO | You are running an older exe. Rebuild, and make sure no copy of the app is running while you build. |
| Driver opens but plays nothing / crackles | Raise the buffer size in Preferences → Audio. Check the driver's own control panel for the sample rate and clock source. |
| Sample rate can't be changed | Some ASIO drivers fix the rate in their control panel. Set it there, or let the app resample (playback is identical for A and B either way). |
