#include "PlatformWindows.h"

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <dwmapi.h>

 #ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
  #define DWMWA_USE_IMMERSIVE_DARK_MODE 20
 #endif
#endif

namespace stemab
{

void enableDarkTitleBar ([[maybe_unused]] void* nativeWindowHandle)
{
#if defined (_WIN32)
    if (nativeWindowHandle == nullptr)
        return;

    const BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute (static_cast<HWND> (nativeWindowHandle),
                           DWMWA_USE_IMMERSIVE_DARK_MODE,
                           &useDarkMode,
                           sizeof (useDarkMode));
#endif
}

} // namespace stemab
