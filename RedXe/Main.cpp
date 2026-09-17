#include "Application.h"
#include "CrashHandler.h"
#include "PluginHost.h"

#include <cwchar>
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

// One optional `<switch> <value>` pair; false when the switch repeats or lacks its value.
[[nodiscard]] bool GetValueArgument(wchar_t* const* arguments, int argumentCount, std::wstring_view expected,
                                    std::wstring_view& value) noexcept
{
    value = {};
    for (int index = 1; index < argumentCount; ++index)
    {
        if (std::wstring_view{arguments[index]} != expected)
        {
            continue;
        }
        if (!value.empty() || index + 1 >= argumentCount || arguments[index + 1][0] == L'\0')
        {
            return false;
        }
        value = arguments[++index];
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
    // Documentation capture: --screenshot <png> [--page <id>] [--widget <ordinal>] [--after <milliseconds>] runs
    // the dashboard, jumps to the page, waits, captures its own window (or one tile), and exits (0 on success, 8
    // when the capture failed).
    std::wstring_view screenshotPath;
    std::wstring_view screenshotPage;
    std::wstring_view screenshotWidget;
    std::wstring_view screenshotDelay;
    if (!GetValueArgument(arguments.get(), argumentCount, L"--screenshot", screenshotPath) ||
        !GetValueArgument(arguments.get(), argumentCount, L"--page", screenshotPage) ||
        !GetValueArgument(arguments.get(), argumentCount, L"--widget", screenshotWidget) ||
        !GetValueArgument(arguments.get(), argumentCount, L"--after", screenshotDelay))
    {
        MessageBoxW(nullptr,
                    L"Use --screenshot <file.png> [--page <id>] [--widget <ordinal>] [--after <milliseconds>].",
                    L"RedXe", MB_OK | MB_ICONERROR);
        return 2;
    }
    uint32_t screenshotDelayMilliseconds = 3000;
    uint32_t screenshotWidgetOrdinal = UINT32_MAX;
    if (!screenshotDelay.empty())
    {
        wchar_t* end = nullptr;
        const unsigned long parsed = std::wcstoul(screenshotDelay.data(), &end, 10);
        if (!end || *end != L'\0' || parsed == 0 || parsed > 120'000UL)
        {
            MessageBoxW(nullptr, L"--after takes a delay of 1 through 120000 milliseconds.", L"RedXe",
                        MB_OK | MB_ICONERROR);
            return 2;
        }
        screenshotDelayMilliseconds = static_cast<uint32_t>(parsed);
    }
    if (!screenshotWidget.empty())
    {
        wchar_t* end = nullptr;
        const unsigned long parsed = std::wcstoul(screenshotWidget.data(), &end, 10);
        if (!end || *end != L'\0' || parsed >= 1024UL)
        {
            MessageBoxW(nullptr, L"--widget takes the 0-based ordinal of a widget on the captured page.", L"RedXe",
                        MB_OK | MB_ICONERROR);
            return 2;
        }
        screenshotWidgetOrdinal = static_cast<uint32_t>(parsed);
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
            if (selfTest)
            {
                PluginHost::Instance().SetNetworkAccessEnabled(false);
            }
            if (!screenshotPath.empty() && !selfTest)
            {
                application->RequestScreenshot(screenshotPath, screenshotPage, screenshotDelayMilliseconds,
                                               screenshotWidgetOrdinal);
            }
            exitCode = selfTest ? application->RunSelfTest(settingsPath) : application->Run(showCommand, settingsPath);
            if (!screenshotPath.empty() && !selfTest && exitCode == 0 && FAILED(application->ScreenshotResult()))
            {
                exitCode = 8;
            }
        }
    }

    // Every widget, provider, and subscription is released with the Application above. Release the process plugin
    // runtime here so its acquisition worker is joined and optional RedXePluginShutdown runs exactly once per module.
    PluginHost::ShutdownProcessRuntime();

    if (exitCode != 0 && !selfTest)
    {
        const wchar_t* message = L"RedXe could not start. See the debugger output.";
        switch (exitCode)
        {
        case 1:
            message = L"RedXe could not load settings. See the debugger output.";
            break;
        case 2:
            message = L"RedXe could not create its window. See the debugger output.";
            break;
        case 3:
            message = L"RedXe could not initialize bundled plugins. See the debugger output.";
            break;
        case 5:
            message = L"RedXe could not complete graphics initialization or rendering. See the debugger output.";
            break;
        case 7:
            message = L"RedXe could not watch its settings file. See the debugger output.";
            break;
        case 8:
            // A capture run is scripted; its failure is an exit code, never a modal prompt.
            OutputDebugStringW(L"RedXe could not capture the screenshot.\n");
            return exitCode;
        default:
            break;
        }
        MessageBoxW(nullptr, message, L"RedXe", MB_OK | MB_ICONERROR);
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
