// RedXeLauncher: the executable behind the winget command alias.
//
// winget installs the portable package under its own packages root and exposes the `RedXe` command as a symbolic
// link in its Links directory. A process started through that link resolves app-local DLLs, `Plugins\`, and
// `Settings\` against the link's directory, not the package, so RedXe.exe itself cannot be the alias target.
// This dependency-free shim resolves its own final path through the link, then starts the package-root RedXe.exe
// by absolute path with the package as working directory and every argument passed through unchanged.
//
// A normal dashboard launch returns to the console at once. Modes that finish on their own (--help, --self-test,
// --screenshot, the crash harness) are awaited so the caller sees their output and exit code.
#include <windows.h>

#include "../RedXe/CommandLine.h"

#include <shellapi.h>

#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <wil/resource.h>

namespace
{
constexpr wchar_t kTargetExecutable[] = L"RedXe.exe";
constexpr wchar_t kErrorCaption[] = L"RedXe";

void ShowError(std::wstring_view message) noexcept
{
    const std::wstring text(message);
    (void)MessageBoxW(nullptr, text.c_str(), kErrorCaption, MB_OK | MB_ICONERROR);
}

[[nodiscard]] std::wstring FormatWin32Failure(std::wstring_view what, DWORD error)
{
    std::wstring text(what);
    text.append(L" (Win32 error ");
    text.append(std::to_wstring(error));
    text.append(L").");
    return text;
}

[[nodiscard]] std::wstring StripLongPathPrefix(std::wstring path)
{
    constexpr std::wstring_view kUncPrefix = LR"(\\?\UNC\)";
    constexpr std::wstring_view kDosPrefix = LR"(\\?\)";
    if (path.starts_with(kUncPrefix))
    {
        return LR"(\\)" + path.substr(kUncPrefix.size());
    }
    if (path.starts_with(kDosPrefix))
    {
        return path.substr(kDosPrefix.size());
    }
    return path;
}

// The path this process was started through, which is the winget link when launched by alias.
[[nodiscard]] std::wstring GetLaunchedModulePath()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD written = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (written == 0)
        {
            return {};
        }
        if (written < path.size())
        {
            path.resize(written);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

// The file the launched path finally denotes, with every symbolic link and junction resolved.
[[nodiscard]] std::wstring ResolveFinalPath(const std::wstring& path)
{
    const wil::unique_hfile file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
    {
        return {};
    }

    std::wstring resolved(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD written = GetFinalPathNameByHandleW(file.get(), resolved.data(),
                                                        static_cast<DWORD>(resolved.size()),
                                                        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (written == 0)
        {
            return {};
        }
        if (written < resolved.size())
        {
            resolved.resize(written);
            return StripLongPathPrefix(std::move(resolved));
        }
        resolved.resize(static_cast<size_t>(written) + 1);
    }
}

[[nodiscard]] std::wstring GetParentDirectory(std::wstring_view path)
{
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring_view::npos ? std::wstring{} : std::wstring(path.substr(0, separator));
}

// CommandLineToArgvW-compatible quoting so RedXe.exe parses exactly the arguments this process received.
[[nodiscard]] std::wstring QuoteArgument(std::wstring_view argument)
{
    if (argument.empty())
    {
        return L"\"\"";
    }
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
    {
        return std::wstring(argument);
    }

    std::wstring quoted(1, L'"');
    size_t backslashes = 0;
    for (const wchar_t character : argument)
    {
        if (character == L'\\')
        {
            ++backslashes;
            continue;
        }
        if (character == L'"')
        {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(character);
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

[[nodiscard]] bool ShouldAwaitTarget(wchar_t* const* arguments, int argumentCount) noexcept
{
    for (int index = 1; index < argumentCount; ++index)
    {
        if (!arguments[index])
        {
            continue;
        }
        const std::wstring_view argument(arguments[index]);
        const RedXeCommandLineSwitch* entry = RedXeFindSwitch(argument);
        if (entry && entry->launcherWaitForExit)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] HANDLE InheritableStandardHandle(DWORD standardHandle) noexcept
{
    const HANDLE handle = GetStdHandle(standardHandle);
    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}

int Launch()
{
    // The shim links the CRT statically and needs only system DLLs; never search the link directory.
    (void)SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

    const std::wstring launchedPath = GetLaunchedModulePath();
    if (launchedPath.empty())
    {
        ShowError(FormatWin32Failure(L"Could not read the launcher path", GetLastError()));
        return 1;
    }
    const std::wstring finalPath = ResolveFinalPath(launchedPath);
    if (finalPath.empty())
    {
        ShowError(FormatWin32Failure(L"Could not resolve the launcher target path for " + launchedPath,
                                     GetLastError()));
        return 1;
    }
    const std::wstring packageDirectory = GetParentDirectory(finalPath);
    if (packageDirectory.empty())
    {
        ShowError(L"Could not determine the package directory from " + finalPath + L".");
        return 1;
    }
    const std::wstring targetPath = packageDirectory + L"\\" + kTargetExecutable;
    if (GetFileAttributesW(targetPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        ShowError(targetPath + L" was not found beside the launcher. Reinstall the RedXe package.");
        return 1;
    }

    int argumentCount = 0;
    const wil::unique_hlocal_ptr<wchar_t*> arguments(CommandLineToArgvW(GetCommandLineW(), &argumentCount));
    if (!arguments)
    {
        ShowError(FormatWin32Failure(L"Could not parse the command line", GetLastError()));
        return 1;
    }

    std::wstring commandLine = QuoteArgument(targetPath);
    for (int index = 1; index < argumentCount; ++index)
    {
        commandLine.push_back(L' ');
        commandLine.append(QuoteArgument(arguments.get()[index] ? std::wstring_view(arguments.get()[index])
                                                                : std::wstring_view{}));
    }
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    // Hand the console (or a redirection) to RedXe.exe so `RedXe --help > file` and terminal output work
    // even though RedXe.exe is a GUI-subsystem image.
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = InheritableStandardHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = InheritableStandardHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = InheritableStandardHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(targetPath.c_str(), mutableCommandLine.data(), nullptr, nullptr, TRUE, 0, nullptr,
                        packageDirectory.c_str(), &startup, &process))
    {
        ShowError(FormatWin32Failure(L"Could not start " + targetPath, GetLastError()));
        return 1;
    }
    const wil::unique_handle processHandle(process.hProcess);
    const wil::unique_handle threadHandle(process.hThread);

    if (!ShouldAwaitTarget(arguments.get(), argumentCount))
    {
        return 0;
    }
    if (WaitForSingleObject(processHandle.get(), INFINITE) != WAIT_OBJECT_0)
    {
        ShowError(FormatWin32Failure(L"Failed while waiting for " + targetPath, GetLastError()));
        return 1;
    }
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(processHandle.get(), &exitCode))
    {
        ShowError(FormatWin32Failure(L"Could not read the exit code of " + targetPath, GetLastError()));
        return 1;
    }
    return exitCode <= static_cast<DWORD>((std::numeric_limits<int>::max)()) ? static_cast<int>(exitCode) : 1;
}
} // namespace

int wmain()
{
    return Launch();
}
