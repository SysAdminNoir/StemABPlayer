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
 #include <mmdeviceapi.h>
 #include <audioclient.h>

 #include <string>

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

#if defined (_WIN32)
namespace
{
    template <typename T>
    struct ComPtr
    {
        T* p = nullptr;
        ~ComPtr()               { if (p != nullptr) p->Release(); }
        T* operator->() const   { return p; }
        T** out()               { return &p; }
    };

    /** Mix rate of the first active render endpoint whose friendly name equals `name`, else 0. */
    double findMixRate (IMMDeviceEnumerator* enumerator, const std::wstring& name)
    {
        ComPtr<IMMDeviceCollection> devices;
        if (FAILED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, devices.out())))
            return 0.0;

        UINT count = 0;
        devices->GetCount (&count);

        // PKEY_Device_FriendlyName, defined here to avoid functiondiscoverykeys/initguid.
        const PROPERTYKEY friendlyNameKey = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IMMDevice> device;
            if (FAILED (devices->Item (i, device.out())))
                continue;

            ComPtr<IPropertyStore> props;
            if (FAILED (device->OpenPropertyStore (STGM_READ, props.out())))
                continue;

            PROPVARIANT value;
            PropVariantInit (&value);
            const bool match = SUCCEEDED (props->GetValue (friendlyNameKey, &value))
                               && value.vt == VT_LPWSTR && value.pwszVal != nullptr && name == value.pwszVal;
            PropVariantClear (&value);

            if (! match)
                continue;

            ComPtr<IAudioClient> client;
            if (FAILED (device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**> (client.out()))))
                return 0.0;

            WAVEFORMATEX* mix = nullptr;
            if (FAILED (client->GetMixFormat (&mix)) || mix == nullptr)
                return 0.0;

            const double rate = (double) mix->nSamplesPerSec;
            CoTaskMemFree (mix);
            return rate;
        }

        return 0.0;
    }
}
#endif

double sharedModeMixRate ([[maybe_unused]] const wchar_t* endpointFriendlyName)
{
#if defined (_WIN32)
    if (endpointFriendlyName == nullptr)
        return 0.0;

    const HRESULT init = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    double rate = 0.0;

    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                         __uuidof (IMMDeviceEnumerator), reinterpret_cast<void**> (enumerator.out()))))
        {
            std::wstring name (endpointFriendlyName);
            rate = findMixRate (enumerator.p, name);

            // JUCE appends " (2)", " (3)"... to duplicate names.
            if (rate <= 0.0 && name.size() > 4 && name.back() == L')')
                if (const auto open = name.rfind (L" ("); open != std::wstring::npos)
                    rate = findMixRate (enumerator.p, name.substr (0, open));
        }
    }

    if (SUCCEEDED (init))
        CoUninitialize();

    return rate;
#else
    return 0.0;
#endif
}

} // namespace stemab
