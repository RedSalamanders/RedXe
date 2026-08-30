#include "Application.h"

#include <memory>
#include <shellapi.h>
#include <string_view>
#include <windows.h>

namespace
{
struct LocalFreeDeleter
{
    void operator()(wchar_t** value) const noexcept
    {
        if (value)
        {
            LocalFree(value);
        }
    }
};

bool HasArgument(std::wstring_view expected) noexcept
{
    int argumentCount = 0;
    std::unique_ptr<wchar_t*, LocalFreeDeleter> arguments{CommandLineToArgvW(GetCommandLineW(), &argumentCount)};
    if (!arguments)
    {
        return false;
    }

    for (int index = 1; index < argumentCount; ++index)
    {
        if (expected == arguments.get()[index])
        {
            return true;
        }
    }
    return false;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const bool selfTest = HasArgument(L"--self-test");
    const bool forceWarp = HasArgument(L"--warp");
    Application application(instance, forceWarp);
    const int exitCode = application.Run(showCommand, selfTest);

    if (exitCode != 0 && !selfTest)
    {
        MessageBoxW(nullptr, L"RedXe could not complete graphics initialization or rendering. See the debugger output.",
                    L"RedXe", MB_OK | MB_ICONERROR);
    }
    return exitCode;
}
