#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The RedXe command line, declared once. `--help` prints this catalog and Main.cpp parses every switch through its
// names, so a switch cannot exist without an entry here. Changing a switch changes this table, the "Command line"
// section of docs/usage.md, and the owning mode row of Specs/UI/UI_XeneonDisplayWindowing.md in the same change
// (AGENTS.md); SettingsTests pins the catalog and test.ps1 runs `--help`.

enum class RedXeSwitch : uint8_t
{
    Help = 0,
    Settings,
    Warp,
    Dock,
    DockMode,
    DockThickness,
    DockReserve,
    DockPeek,
    Screenshot,
    Page,
    Widget,
    After,
    SelfTest,
    CrashTest,
    CrashTestStackOverflow,
    CrashTestDirectory,
    Count,
};

enum class RedXeSwitchValue : uint8_t
{
    // The switch stands alone.
    None = 0,
    // The value is the next argument: `--settings <path>`.
    Separate,
    // The value is joined with `=`: `--crash-test-directory=<dir>`.
    Joined,
};

struct RedXeCommandLineSwitch final
{
    RedXeSwitch id;
    // Exact token (or, for a joined value, the part before `=`).
    const wchar_t* name;
    // Other tokens that select the same switch, for the help text and the argument scanner.
    const wchar_t* aliases;
    RedXeSwitchValue valueKind;
    // Value placeholder shown after the name, or nullptr.
    const wchar_t* value;
    const wchar_t* summary;
    const wchar_t* group;
    // The alias launcher waits for modes that exit on their own and forwards their exit status.
    bool launcherWaitForExit = false;
};

// Help aliases; the primary name is the first catalog entry.
inline constexpr std::array<const wchar_t*, 4> kRedXeHelpArguments{L"--help", L"-h", L"/?", L"-?"};

inline constexpr std::array<RedXeCommandLineSwitch, static_cast<size_t>(RedXeSwitch::Count)> kRedXeCommandLineSwitches{
    RedXeCommandLineSwitch{RedXeSwitch::Help, L"--help", L"-h, /?, -?", RedXeSwitchValue::None, nullptr,
                           L"Show this text and exit.", L"General", true},
    RedXeCommandLineSwitch{RedXeSwitch::Settings, L"--settings", nullptr, RedXeSwitchValue::Separate, L"<path>",
                           L"Use one portable settings file instead of the one under %LocalAppData%\\RedXe\\Settings; "
                           L"its Logs folder sits beside it.",
                           L"General"},
    RedXeCommandLineSwitch{RedXeSwitch::Warp, L"--warp", nullptr, RedXeSwitchValue::None, nullptr,
                           L"Render on the Microsoft Basic Render Driver (WARP) instead of the GPU.", L"General"},
    RedXeCommandLineSwitch{RedXeSwitch::Dock, L"--dock", nullptr, RedXeSwitchValue::Separate, L"<edge>[@<monitor>]",
                           L"Run as a bar on that screen edge: none, top, bottom, left, or right, on monitor primary, "
                           L"secondary, xeneon, <n>, or name:<substring> (default primary). Overrides the settings "
                           L"dock.edge/monitor.",
                           L"Dock"},
    RedXeCommandLineSwitch{RedXeSwitch::DockMode, L"--dock-mode", nullptr, RedXeSwitchValue::Separate, L"<mode>",
                           L"fixed (always on screen) or autohide (collapses to a peek strip).", L"Dock"},
    RedXeCommandLineSwitch{RedXeSwitch::DockThickness, L"--dock-thickness", nullptr, RedXeSwitchValue::Separate,
                           L"<dips>", L"Bar thickness in DIPs, 32 through 1080 (default 180).", L"Dock"},
    RedXeCommandLineSwitch{
        RedXeSwitch::DockReserve, L"--dock-reserve", nullptr, RedXeSwitchValue::Separate, L"<on|off>",
        L"Fixed bars only: shrink the maximize area so windows stop at the bar (default on).", L"Dock"},
    RedXeCommandLineSwitch{RedXeSwitch::DockPeek, L"--dock-peek", nullptr, RedXeSwitchValue::Separate, L"<pixels>",
                           L"Autohide only: pixels that stay visible while hidden, 1 through 64 (default 4).", L"Dock"},
    RedXeCommandLineSwitch{
        RedXeSwitch::Screenshot, L"--screenshot", nullptr, RedXeSwitchValue::Separate, L"<png>",
        L"Start normally, wait, save the window (or one widget) as PNG, and exit: 0 written, 8 not written.",
        L"Screenshot", true},
    RedXeCommandLineSwitch{RedXeSwitch::Page, L"--page", nullptr, RedXeSwitchValue::Separate, L"<id>",
                           L"Page to show before the capture (default: the first page).", L"Screenshot"},
    RedXeCommandLineSwitch{RedXeSwitch::Widget, L"--widget", nullptr, RedXeSwitchValue::Separate, L"<ordinal>",
                           L"Crop the capture to the 0-based widget on that page.", L"Screenshot"},
    RedXeCommandLineSwitch{RedXeSwitch::After, L"--after", nullptr, RedXeSwitchValue::Separate, L"<milliseconds>",
                           L"Settle time before the capture, 1 through 120000 (default 3000).", L"Screenshot"},
    RedXeCommandLineSwitch{RedXeSwitch::SelfTest, L"--self-test", nullptr, RedXeSwitchValue::None, nullptr,
                           L"Hidden startup validation with the deployed template; exits 0 when the host works.",
                           L"Diagnostics", true},
    RedXeCommandLineSwitch{RedXeSwitch::CrashTest, L"--crash-test", nullptr, RedXeSwitchValue::None, nullptr,
                           L"Raise a test crash to exercise the dump writer.", L"Diagnostics", true},
    RedXeCommandLineSwitch{RedXeSwitch::CrashTestStackOverflow, L"--crash-test-stack-overflow", nullptr,
                           RedXeSwitchValue::None, nullptr, L"Raise a stack-overflow test crash.", L"Diagnostics",
                           true},
    RedXeCommandLineSwitch{RedXeSwitch::CrashTestDirectory, L"--crash-test-directory", nullptr,
                           RedXeSwitchValue::Joined, L"<directory>",
                           L"Where a test crash writes its dump instead of the user's crash folder.", L"Diagnostics"},
};

inline constexpr std::array<const wchar_t*, 4> kRedXeCommandLineGroups{L"General", L"Dock", L"Screenshot",
                                                                       L"Diagnostics"};

[[nodiscard]] constexpr const RedXeCommandLineSwitch& RedXeSwitchInfo(RedXeSwitch id) noexcept
{
    return kRedXeCommandLineSwitches[static_cast<size_t>(id)];
}

// The token Main.cpp matches for a switch (for a joined value, append `=`).
[[nodiscard]] constexpr const wchar_t* RedXeSwitchName(RedXeSwitch id) noexcept
{
    return RedXeSwitchInfo(id).name;
}

[[nodiscard]] inline bool RedXeIsHelpArgument(std::wstring_view argument) noexcept
{
    for (const wchar_t* alias : kRedXeHelpArguments)
    {
        if (argument == alias)
        {
            return true;
        }
    }
    return false;
}

// Catalog entry a token selects, or nullptr. A joined-value switch matches `name=` followed by anything.
[[nodiscard]] inline const RedXeCommandLineSwitch* RedXeFindSwitch(std::wstring_view argument) noexcept
{
    if (RedXeIsHelpArgument(argument))
    {
        return &RedXeSwitchInfo(RedXeSwitch::Help);
    }
    for (const RedXeCommandLineSwitch& entry : kRedXeCommandLineSwitches)
    {
        const std::wstring_view name{entry.name};
        if (entry.valueKind == RedXeSwitchValue::Joined)
        {
            if (argument.size() > name.size() + 1 && argument.starts_with(name) && argument[name.size()] == L'=')
            {
                return &entry;
            }
            continue;
        }
        if (argument == name)
        {
            return &entry;
        }
    }
    return nullptr;
}

// First argument that is neither a catalog switch nor the value of a separate-value switch, else nullptr. A
// separate-value switch consumes the following argument whatever it looks like, the way Main.cpp reads it.
[[nodiscard]] inline const wchar_t* RedXeFindUnknownArgument(wchar_t* const* arguments, int argumentCount) noexcept
{
    for (int index = 1; index < argumentCount; ++index)
    {
        const RedXeCommandLineSwitch* entry = RedXeFindSwitch(arguments[index]);
        if (!entry)
        {
            return arguments[index];
        }
        if (entry->valueKind == RedXeSwitchValue::Separate && index + 1 < argumentCount)
        {
            ++index;
        }
    }
    return nullptr;
}

// Whether a run is unattended: `--self-test` (test.ps1) or `--screenshot` (documentation and agent captures) on the
// line, found before any value is checked, so a malformed `--screenshot` value counts too. Nobody answers a modal box
// in such a run, so it never shows one (UI_XeneonDisplayWindowing.md "Configuration behavior"): a command-line error
// prints its usage to the console or redirected output (exit 2), the settings fallback notice is a Warning record,
// the Release missing-display prompt takes its Yes answer, the previous-crash notice waits for the next interactive
// start, a failure exit is a record instead of the exit-code box, and a failed Debug check ends the run with its report
// (Common/FailureReports.h). The run reports through its exit code, that output, and (a capture) the JSONL log.
[[nodiscard]] constexpr bool RedXeIsUnattendedRun(bool selfTest, bool screenshot) noexcept
{
    return selfTest || screenshot;
}

// Text Main.cpp prints about the command line instead of running it.
enum class RedXeCommandLineText : uint8_t
{
    // The `--help` catalog (exit 0).
    Help = 0,
    // A command-line error, such as an unknown token (exit 2).
    Error,
};

// Whether that text, finding neither a console to attach to nor a redirected stdout, goes only to the debugger output
// instead of a message box. The help catalog never does: help is answered before any other switch is read, so
// `--self-test` or `--screenshot` beside it changes nothing. A command-line error does in an unattended run
// (`unattended`, RedXeIsUnattendedRun), which never waits on a box.
[[nodiscard]] constexpr bool RedXeCommandLineTextIsQuiet(RedXeCommandLineText text, bool unattended) noexcept
{
    return text == RedXeCommandLineText::Error && unattended;
}

// The name the `--help` text gives an exit code Main.cpp reports, for the `failure-exit` record an unattended capture
// run leaves in the log; "startup" for a code the list does not name.
[[nodiscard]] constexpr const char* RedXeExitCodeName(int exitCode) noexcept
{
    switch (exitCode)
    {
    case 1:
        return "settings";
    case 2:
        return "command line or window";
    case 3:
        return "plugins";
    case 5:
        return "graphics";
    case 7:
        return "settings watcher";
    case 8:
        return "screenshot capture";
    default:
        return "startup";
    }
}

// Exit code of a `--screenshot` run that wrote no PNG.
inline constexpr int kRedXeScreenshotFailedExitCode = 8;

// The exit code Main.cpp returns for a `--screenshot` run: 8 whenever no PNG was written, whatever ended the run (a
// failed capture, a window closed before it, or a startup, graphics, or rendering failure that closed it first);
// with the PNG written, the run's own code.
[[nodiscard]] constexpr int RedXeScreenshotExitCode(int runExitCode, bool pngWritten) noexcept
{
    return pngWritten ? runExitCode : kRedXeScreenshotFailedExitCode;
}

// Whether Main.cpp shows the modal error box for a run's exit code once the main window is gone: only for a failed
// run that is not unattended (RedXeIsUnattendedRun), whatever its exit code.
[[nodiscard]] constexpr bool RedXeShowsExitCodeBox(int exitCode, bool selfTest, bool screenshot) noexcept
{
    return exitCode != 0 && !RedXeIsUnattendedRun(selfTest, screenshot);
}

// The `--help` text: usage line, one block per group, and the exit codes Main.cpp reports.
[[nodiscard]] inline std::wstring RedXeFormatCommandLineHelp()
{
    std::wstring text;
    text += L"RedXe — CORSAIR XENEON EDGE dashboard\n\n";
    text += L"Usage: RedXe.exe [--settings <path>] [--dock <edge>[@<monitor>] [dock options]]\n";
    text += L"                 [--screenshot <png> [--page <id>] [--widget <ordinal>] [--after <ms>]] [--warp]\n";
    text += L"       RedXe.exe --help\n";
    text += L"\nWithout switches RedXe fills the XENEON (Release) or opens a titled window (Debug) and watches the "
            L"settings file\n";
    text += L"under %LocalAppData%\\RedXe\\Settings. Every switch overrides the file for this run only.\n";
    for (const wchar_t* group : kRedXeCommandLineGroups)
    {
        text += L"\n";
        text += group;
        text += L"\n";
        for (const RedXeCommandLineSwitch& entry : kRedXeCommandLineSwitches)
        {
            if (std::wstring_view{entry.group} != group)
            {
                continue;
            }
            std::wstring lead = L"  ";
            lead += entry.name;
            if (entry.value)
            {
                lead += entry.valueKind == RedXeSwitchValue::Joined ? L"=" : L" ";
                lead += entry.value;
            }
            if (entry.aliases)
            {
                lead += L"  (";
                lead += entry.aliases;
                lead += L")";
            }
            constexpr size_t kColumn = 32;
            constexpr size_t kWidth = 110;
            if (lead.size() < kColumn)
            {
                lead.append(kColumn - lead.size(), L' ');
            }
            else
            {
                lead += L"\n";
                lead.append(kColumn, L' ');
            }
            text += lead;
            // Word-wrap the summary under itself so a narrow console still reads one switch per block.
            std::wstring_view summary{entry.summary};
            size_t lineLength = 0;
            while (!summary.empty())
            {
                const size_t space = summary.find(L' ');
                const std::wstring_view word = summary.substr(0, space);
                if (lineLength != 0 && lineLength + 1 + word.size() > kWidth - kColumn)
                {
                    text += L"\n";
                    text.append(kColumn, L' ');
                    lineLength = 0;
                }
                else if (lineLength != 0)
                {
                    text += L" ";
                    ++lineLength;
                }
                text += word;
                lineLength += word.size();
                summary = space == std::wstring_view::npos ? std::wstring_view{} : summary.substr(space + 1);
            }
            text += L"\n";
        }
    }
    text += L"\nExit codes: 0 ok, 1 settings, 2 command line or window, 3 plugins, 5 graphics, 7 settings watcher,\n";
    text += L"8 screenshot capture. Settings, dock, pages, and actions: docs/usage.md and docs/actions.md.\n";
    return text;
}
