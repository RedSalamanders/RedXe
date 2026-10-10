#include "../Common/FailureReports.h"
#include "Application.h"
#include "CommandLine.h"
#include "CrashHandler.h"
#include "DockOptions.h"
#include "PluginHost.h"

#include <array>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <new>
#include <shellapi.h>
#include <string>
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
        if (std::wstring_view{arguments[index]} != RedXeSwitchName(RedXeSwitch::Settings))
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
    const std::wstring prefix = std::wstring{RedXeSwitchName(RedXeSwitch::CrashTestDirectory)} + L"=";
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

// Command-line text (the `--help` catalog or an argument error) goes to the console this process was started from
// (a GUI process has none of its own, so it attaches to the parent's), to a redirected stdout as UTF-8, or, without
// either, to a message box unless `quiet` (an unattended run, RedXeIsUnattendedRun) forbids one.
void EmitCommandLineText(const std::wstring& text, bool error, bool quiet) noexcept
{
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    bool attached = false;
    if (!output || output == INVALID_HANDLE_VALUE)
    {
        attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
        output = attached ? GetStdHandle(STD_OUTPUT_HANDLE) : nullptr;
    }
    if (output && output != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(output, &mode))
        {
            // The parent's prompt is already on screen: start on a fresh line.
            const std::wstring console = L"\n" + text;
            DWORD written = 0;
            (void)WriteConsoleW(output, console.c_str(), static_cast<DWORD>(console.size()), &written, nullptr);
        }
        else
        {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                                  nullptr, nullptr);
            std::string utf8(static_cast<size_t>(bytes > 0 ? bytes : 0), '\0');
            if (bytes > 0)
            {
                (void)WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), bytes,
                                          nullptr, nullptr);
                DWORD written = 0;
                (void)WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            }
        }
        if (attached)
        {
            FreeConsole();
        }
        return;
    }
    if (quiet)
    {
        OutputDebugStringW(text.c_str());
        return;
    }
    MessageBoxW(nullptr, text.c_str(), L"RedXe command line", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

// A switch with a missing or invalid value (exit 2): a message box for a person; an unattended run
// (RedXeIsUnattendedRun) gets the text where EmitCommandLineText puts it, never a box.
void ReportCommandLineError(const wchar_t* text, bool unattended) noexcept
{
    if (!unattended)
    {
        MessageBoxW(nullptr, text, L"RedXe", MB_OK | MB_ICONERROR);
        return;
    }
    try
    {
        EmitCommandLineText(std::wstring{text} + L"\n", true, true);
    }
    catch (...)
    {
        OutputDebugStringW(text);
    }
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

    // Help wins over everything else on the line, and every other token must be a catalogued switch or its value
    // so a typo never runs the dashboard with a silently ignored option.
    const bool selfTest = HasArgument(arguments.get(), argumentCount, RedXeSwitchName(RedXeSwitch::SelfTest));
    // test.ps1 runs the self-test and agents run captures unattended: such a run never waits on a modal box. Its caller
    // reads the exit code, the console or redirected output, and (for a capture) the JSONL log, and a failed Debug
    // check ends it with its report instead of a dialog.
    const bool screenshotSwitch = HasArgument(arguments.get(), argumentCount, RedXeSwitchName(RedXeSwitch::Screenshot));
    const bool unattended = RedXeIsUnattendedRun(selfTest, screenshotSwitch);
    if (unattended)
    {
        RedXeFailureReports::RouteAwayFromDialogs();
    }
    try
    {
        for (int index = 1; index < argumentCount; ++index)
        {
            if (RedXeIsHelpArgument(arguments.get()[index]))
            {
                // Help wins over every other switch, but `--self-test` or `--screenshot` beside it still means a caller
                // that never waits on a box: without a console or a redirected stdout the catalog reaches only the
                // debugger output.
                EmitCommandLineText(RedXeFormatCommandLineHelp(), false, unattended);
                return 0;
            }
        }
        if (const wchar_t* unknown = RedXeFindUnknownArgument(arguments.get(), argumentCount))
        {
            std::wstring message = L"Unknown argument \"";
            message += unknown;
            message += L"\". Run RedXe.exe --help for the command line.\n";
            EmitCommandLineText(message, true, unattended);
            return 2;
        }
    }
    catch (...)
    {
        return 2;
    }

    const bool forceWarp = HasArgument(arguments.get(), argumentCount, RedXeSwitchName(RedXeSwitch::Warp));
    const bool crashTest = HasArgument(arguments.get(), argumentCount, RedXeSwitchName(RedXeSwitch::CrashTest));
    const bool stackOverflowCrashTest =
        HasArgument(arguments.get(), argumentCount, RedXeSwitchName(RedXeSwitch::CrashTestStackOverflow));
    std::wstring_view settingsPath;
    if (!GetSettingsArgument(arguments.get(), argumentCount, settingsPath))
    {
        ReportCommandLineError(L"Use --settings followed by exactly one settings file path.", unattended);
        return 2;
    }
    // Documentation capture: --screenshot <png> [--page <id>] [--widget <ordinal>] [--after <milliseconds>] runs
    // the dashboard, jumps to the page, waits, captures its own window (or one tile), and exits (0 with the PNG
    // written, 8 without it).
    std::wstring_view screenshotPath;
    std::wstring_view screenshotPage;
    std::wstring_view screenshotWidget;
    std::wstring_view screenshotDelay;
    if (!GetValueArgument(arguments.get(), argumentCount, RedXeSwitchName(RedXeSwitch::Screenshot), screenshotPath) ||
        !GetValueArgument(arguments.get(), argumentCount, RedXeSwitchName(RedXeSwitch::Page), screenshotPage) ||
        !GetValueArgument(arguments.get(), argumentCount, RedXeSwitchName(RedXeSwitch::Widget), screenshotWidget) ||
        !GetValueArgument(arguments.get(), argumentCount, RedXeSwitchName(RedXeSwitch::After), screenshotDelay))
    {
        ReportCommandLineError(
            L"Use --screenshot <file.png> [--page <id>] [--widget <ordinal>] [--after <milliseconds>].", unattended);
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
            ReportCommandLineError(L"--after takes a delay of 1 through 120000 milliseconds.", unattended);
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
            ReportCommandLineError(L"--widget takes the 0-based ordinal of a widget on the captured page.", unattended);
            return 2;
        }
        screenshotWidgetOrdinal = static_cast<uint32_t>(parsed);
    }
    // A capture run (the self-test ignores --screenshot): unattended, and exit 8 whenever it wrote no PNG.
    const bool screenshotRun = !screenshotPath.empty() && !selfTest;
    // Screen-edge dock for this run: --dock <edge>[@<monitor>] [--dock-mode fixed|autohide]
    // [--dock-thickness <dips>] [--dock-reserve on|off] [--dock-peek <pixels>]. Each switch overrides the same
    // member of the settings document's `dock` object for the process lifetime (UI_XeneonDisplayWindowing.md).
    DockOverrides dockOverrides{};
    {
        struct DockSwitch final
        {
            const wchar_t* name;
            bool (*parse)(std::wstring_view, DockOverrides&) noexcept;
            const wchar_t* usage;
        };
        constexpr DockSwitch dockSwitches[]{
            {RedXeSwitchName(RedXeSwitch::Dock), ParseDockEdgeArgument,
             L"--dock takes none, top, bottom, left, or right, optionally followed by @primary, @secondary, @xeneon, "
             L"@<n>, or @name:<substring>."},
            {RedXeSwitchName(RedXeSwitch::DockMode), ParseDockModeArgument, L"--dock-mode takes fixed or autohide."},
            {RedXeSwitchName(RedXeSwitch::DockThickness), ParseDockThicknessArgument,
             L"--dock-thickness takes 32 through 1080 DIPs."},
            {RedXeSwitchName(RedXeSwitch::DockReserve), ParseDockReserveArgument, L"--dock-reserve takes on or off."},
            {RedXeSwitchName(RedXeSwitch::DockPeek), ParseDockPeekArgument, L"--dock-peek takes 1 through 64 pixels."},
        };
        for (const DockSwitch& dockSwitch : dockSwitches)
        {
            std::wstring_view value;
            if (!GetValueArgument(arguments.get(), argumentCount, dockSwitch.name, value) ||
                (!value.empty() && !dockSwitch.parse(value, dockOverrides)))
            {
                ReportCommandLineError(dockSwitch.usage, unattended);
                return 2;
            }
        }
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
            // Nothing ran, so a --screenshot run wrote no PNG.
            exitCode = screenshotRun ? RedXeScreenshotExitCode(1, false) : 1;
        }
        else
        {
            if (selfTest)
            {
                PluginHost::Instance().SetNetworkAccessEnabled(false);
            }
            if (dockOverrides.Any() && !selfTest)
            {
                application->SetDockOverrides(dockOverrides);
            }
            if (unattended)
            {
                application->SetUnattended();
            }
            if (screenshotRun)
            {
                // The first request of the process, so never busy; the window closes once the capture has run.
                (void)application->RequestScreenshot(screenshotPath, screenshotPage, screenshotDelayMilliseconds,
                                                     screenshotWidgetOrdinal, true);
            }
            exitCode = selfTest ? application->RunSelfTest(settingsPath) : application->Run(showCommand, settingsPath);
            // Exit 8 unless the PNG was written, including a run that ended before its capture finished and one that
            // a startup, graphics, or rendering failure closed first.
            if (screenshotRun)
            {
                exitCode = RedXeScreenshotExitCode(exitCode, SUCCEEDED(application->FinishScreenshot()));
            }
        }
    }

    if (screenshotRun && exitCode != 0)
    {
        // A capture run's failure is its exit code, this record (once the settings load has opened the log), and the
        // debugger output, never a message box (RedXeShowsExitCodeBox below).
        std::array<char, 96> message{};
        (void)std::snprintf(message.data(), message.size(), "RedXe exited with code %d (%s).", exitCode,
                            RedXeExitCodeName(exitCode));
        (void)RedXeHostLog(PluginHost::Instance().Interface(), RedXeLogLevelError, nullptr, nullptr, "failure-exit",
                           message.data());
        OutputDebugStringW(exitCode == kRedXeScreenshotFailedExitCode
                               ? L"RedXe could not capture the screenshot.\n"
                               : L"RedXe failed after it wrote the screenshot. See the debugger output.\n");
    }

    // Every widget, provider, and subscription is released with the Application above. Release the process plugin
    // runtime here so its acquisition worker is joined and optional RedXePluginShutdown runs exactly once per module.
    PluginHost::ShutdownProcessRuntime();

    if (RedXeShowsExitCodeBox(exitCode, selfTest, screenshotSwitch))
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
        default:
            break;
        }
        // Destroying the main window posted a WM_QUIT that no loop retrieved on these paths, and a modal loop that
        // finds one ends at once. Discard it so the box waits for the user; wWinMain returns the exit code directly.
        MSG quit{};
        (void)PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
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
        // Ended here like every other fatal path: returning from wWinMain would let CRT exit shut the process plugin
        // runtime down (service drains, worker joins, RedXePluginShutdown) inside the crashed process.
        TerminateProcess(GetCurrentProcess(), static_cast<UINT>(CrashHandler::kCrashExitCode));
        __assume(false);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    CrashHandler::Install();
    return RunWithCrashBoundary(instance, showCommand);
}
