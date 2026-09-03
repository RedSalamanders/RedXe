#include "Application.h"
#include "CrashHandler.h"
#include "PluginHost.h"

#include <memory>
#include <new>
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

bool HasArgument(wchar_t* const* arguments, int argumentCount, std::wstring_view expected) noexcept
{
    for (int index = 1; index < argumentCount; ++index)
    {
        if (expected == arguments[index])
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool GetSettingsArgument(wchar_t* const* arguments, int argumentCount,
                                       std::wstring_view& selectedPath) noexcept
{
    selectedPath = {};
    for (int index = 1; index < argumentCount; ++index)
    {
        if (std::wstring_view{arguments[index]} != L"--settings")
        {
            continue;
        }
        if (!selectedPath.empty() || index + 1 >= argumentCount || arguments[index + 1][0] == L'\0')
        {
            return false;
        }
        selectedPath = arguments[++index];
    }
    return true;
}

enum class CrashDirectoryOverrideStatus
{
    NotPresent,
    Configured,
    Invalid,
};

CrashDirectoryOverrideStatus ConfigureCrashTestDirectoryOverride(wchar_t* const* arguments, int argumentCount) noexcept
{
    constexpr std::wstring_view prefix = L"--crash-test-directory=";
    for (int index = 1; index < argumentCount; ++index)
    {
        const std::wstring_view argument{arguments[index]};
        if (argument.starts_with(prefix))
        {
            const wchar_t* value = arguments[index] + prefix.size();
            if (value[0] == L'\0')
            {
                return CrashDirectoryOverrideStatus::Invalid;
            }

            return SUCCEEDED(CrashHandler::SetCrashDirectoryForTesting(value))
                       ? CrashDirectoryOverrideStatus::Configured
                       : CrashDirectoryOverrideStatus::Invalid;
        }
    }
    return CrashDirectoryOverrideStatus::NotPresent;
}

int RunApplication(HINSTANCE instance, int showCommand) noexcept
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argumentCount = 0;
    std::unique_ptr<wchar_t*, LocalFreeDeleter> arguments{CommandLineToArgvW(GetCommandLineW(), &argumentCount)};
    if (!arguments)
    {
        return 1;
    }

    const bool selfTest = HasArgument(arguments.get(), argumentCount, L"--self-test");
    const bool forceWarp = HasArgument(arguments.get(), argumentCount, L"--warp");
    const bool crashTest = HasArgument(arguments.get(), argumentCount, L"--crash-test");
    const bool stackOverflowCrashTest = HasArgument(arguments.get(), argumentCount, L"--crash-test-stack-overflow");
    std::wstring_view settingsPath;
    if (!GetSettingsArgument(arguments.get(), argumentCount, settingsPath))
    {
        MessageBoxW(nullptr, L"Use --settings followed by exactly one settings file path.", L"RedXe",
                    MB_OK | MB_ICONERROR);
        return 2;
    }
    if (crashTest || stackOverflowCrashTest)
    {
        if (ConfigureCrashTestDirectoryOverride(arguments.get(), argumentCount) ==
            CrashDirectoryOverrideStatus::Invalid)
        {
            OutputDebugStringW(L"The crash-test output directory is invalid.\n");
            return 2;
        }
        if (stackOverflowCrashTest)
        {
            CrashHandler::TriggerStackOverflowCrashTest();
        }
        CrashHandler::TriggerCrashTest();
    }

    int exitCode = 1;
    {
        const std::unique_ptr<Application> application{new (std::nothrow) Application(instance, forceWarp)};
        if (!application)
        {
            exitCode = 1;
        }
        else
        {
            exitCode = selfTest ? application->RunSelfTest(settingsPath) : application->Run(showCommand, settingsPath);
        }
    }

    // Every widget, provider, and subscription is released with the Application above. Release the process plugin
    // runtime here so its acquisition worker is joined and optional RedXePluginShutdown runs exactly once per module.
    PluginHost::ShutdownProcessRuntime();

    if (exitCode != 0 && !selfTest)
    {
        MessageBoxW(nullptr, L"RedXe could not complete graphics initialization or rendering. See the debugger output.",
                    L"RedXe", MB_OK | MB_ICONERROR);
    }
    return exitCode;
}

int RunWithCrashBoundary(HINSTANCE instance, int showCommand) noexcept
{
    __try
    {
        return RunApplication(instance, showCommand);
    }
    __except (CrashHandler::WriteDumpForException(GetExceptionInformation()))
    {
        OutputDebugStringW(L"RedXe terminated after an unhandled exception; a diagnostic dump was requested.\n");
        return CrashHandler::kCrashExitCode;
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    CrashHandler::Install();
    return RunWithCrashBoundary(instance, showCommand);
}
