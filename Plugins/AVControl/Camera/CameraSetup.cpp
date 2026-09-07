#include "CameraRegistration.h"
#include <array>
#include <cstdio>
#include <mfapi.h>
#include <string_view>
#include <wil/resource.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
bool NativeArchitecture() noexcept
{
    USHORT process = 0, native = 0;
    if (!IsWow64Process2(GetCurrentProcess(), &process, &native) || process != IMAGE_FILE_MACHINE_UNKNOWN)
        return false;
#if defined(_M_ARM64)
    return native == IMAGE_FILE_MACHINE_ARM64;
#else
    return native == IMAGE_FILE_MACHINE_AMD64;
#endif
}
int Run(int argc, wchar_t** argv)
{
    if (argc != 2 || (std::wstring_view(argv[1]) != L"--check" && std::wstring_view(argv[1]) != L"--register" &&
                      std::wstring_view(argv[1]) != L"--remove"))
    {
        std::fputs("RedXe Camera setup\n"
                   "  --check     Check installed native source; no device is created or opened.\n"
                   "  --register  Create/reopen RedXe Camera for the current Windows user.\n"
                   "  --remove    Remove RedXe Camera for the current Windows user.\n"
                   "Install the Release source package first with install-camera.ps1 -Action Install.\n"
                   "Run per-user commands without elevation. Select RedXe Camera once in each capture app.\n",
                   stdout);
        return 2;
    }
    if (!NativeArchitecture())
    {
        std::fputs("Use the native x64 or ARM64 setup program.\n", stderr);
        return 3;
    }
    const auto operation = std::wstring_view(argv[1]);
    if (operation == L"--check")
    {
        const HRESULT result = AVControl::Camera::VirtualCameraRoute::ProbeInstalledSource();
        std::printf("Source registration: 0x%08lX\n", static_cast<unsigned long>(result));
        return SUCCEEDED(result) ? 0 : 1;
    }
#ifdef _DEBUG
    std::fputs("Camera registration/removal requires the Release setup program.\n", stderr);
    return 3;
#else
    // A UAC credential switch can target a different account. Machine installation and per-user device
    // registration are separate, explicit operations; never create a camera in an elevated administrator's profile.
    wil::unique_handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()))
        return 3;
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &bytes) ||
        elevation.TokenIsElevated)
    {
        std::fputs("Run per-user camera registration/removal from a non-elevated terminal.\n", stderr);
        return 3;
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized))
        return 4;
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    if (FAILED(MFStartup(MF_VERSION)))
        return 4;
    const auto mf = wil::scope_exit([] { (void)MFShutdown(); });
    AVControl::Camera::VirtualCameraRoute route;
    const HRESULT result = operation == L"--register" ? route.Open() : route.Remove();
    std::printf("%s: 0x%08lX\n",
                operation == L"--register" ? "Register current-user route" : "Remove current-user route",
                static_cast<unsigned long>(result));
    if (SUCCEEDED(result) && operation == L"--register")
        std::fputs("Select RedXe Camera in each capture app. Camera Off sends neutral frames through this route.\n",
                   stdout);
    return SUCCEEDED(result) ? 0 : 1;
#endif
}
} // namespace
int wmain(int argc, wchar_t** argv)
{
    try
    {
        return Run(argc, argv);
    }
    catch (...)
    {
        std::fputs("Camera setup failed.\n", stderr);
        return 4;
    }
}
