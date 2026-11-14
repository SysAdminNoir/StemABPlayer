#pragma once

namespace stemab
{

/** Windows only (no-op elsewhere): asks DWM to draw the native title bar in dark mode.
    Pass the native window handle (HWND) of the top-level window. Kept out of JUCE-including
    translation units so windows.h macros can't collide with juce:: names. */
void enableDarkTitleBar (void* nativeWindowHandle);

} // namespace stemab
