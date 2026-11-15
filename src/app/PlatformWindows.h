#pragma once

namespace stemab
{

/** Windows only (no-op elsewhere): asks DWM to draw the native title bar in dark mode.
    Pass the native window handle (HWND) of the top-level window. Kept out of JUCE-including
    translation units so windows.h macros can't collide with juce:: names. */
void enableDarkTitleBar (void* nativeWindowHandle);

/** Windows only: the shared-mode mix rate (Hz) of the active render endpoint with this friendly name, i.e.
    the rate the Windows audio engine actually runs that endpoint at. JUCE's "Windows Audio" (shared) device
    opens at any rate and lets Windows convert to this one (AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM), so a
    "44100 Hz" shared device on a 48000 Hz endpoint is not Native Rate. A trailing " (n)" that JUCE appends to
    duplicate names is ignored if the exact name is not found. Returns 0 if unknown. */
double sharedModeMixRate (const wchar_t* endpointFriendlyName);

} // namespace stemab
