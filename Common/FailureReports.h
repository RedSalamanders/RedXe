#pragma once

#include <crtdbg.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <windows.h>

// A test process never waits on a dialog. In a Debug build a failed runtime check (an STL range check, a CRT assertion)
// otherwise opens a modal Abort/Retry/Ignore box on the desktop: an unattended run hangs until its time budget ends it,
// and an attended one interrupts the person at the desktop. The report goes to stderr, which the executable's log
// keeps, and the check ends the process with exit code 3 (the dialog's Abort), with no Windows Error Reporting dialog
// either. An abort() that no failed check reported (an assert(), std::terminate after an unhandled exception, or a
// direct call) says so on stderr and ends the process with exit code 4. No other path of a process that calls this
// header may end with 3 or 4, so each keeps its one meaning. RedXe.exe --self-test and --screenshot are
// unattended runs as well, so the header lives beside the product's shared sources rather than with the test
// executables; there, RedXe.exe's own terminate handler (CrashHandler, exit code 127) takes std::terminate.
//
// A test process never loses its last lines either. Redirected to a pipe, stdout is fully buffered by the CRT, and a
// process terminated at its time budget runs no exit code that would flush the buffer: the log would end cases before
// the one that hung. Both standard streams are therefore unbuffered, so every line is in the pipe once written.
namespace RedXeFailureReports
{
inline constexpr int kFailedCheckExitCode = 3;
inline constexpr int kAbortExitCode = 4;

// Writes text to stderr without the CRT's streams: as UTF-16 to a console, as UTF-8 to a pipe or a file. A CRT stream
// in the default "C" locale stops at the first character above U+00FF, which would cut a report off inside a source
// path under such a user or folder name. Each chunk goes through a fixed stack buffer, so a failure path allocates
// nothing and takes no stream lock.
inline void WriteToStandardError(const wchar_t* text) noexcept
{
    const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (!text || !error || error == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD mode = 0;
    const bool console = GetConsoleMode(error, &mode) != FALSE;
    constexpr size_t kChunkCharacters = 256;
    char bytes[kChunkCharacters * 3]; // A UTF-16 code unit becomes at most three UTF-8 bytes.
    for (size_t remaining = std::wcslen(text); remaining != 0;)
    {
        size_t count = remaining < kChunkCharacters ? remaining : kChunkCharacters;
        if (count < remaining && IS_HIGH_SURROGATE(text[count - 1]))
        {
            --count; // A surrogate pair stays in one chunk.
        }
        DWORD written = 0;
        if (console)
        {
            static_cast<void>(WriteConsoleW(error, text, static_cast<DWORD>(count), &written, nullptr));
        }
        else
        {
            const int length = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(count), bytes,
                                                   static_cast<int>(sizeof(bytes)), nullptr, nullptr);
            if (length > 0)
            {
                static_cast<void>(WriteFile(error, bytes, static_cast<DWORD>(length), &written, nullptr));
            }
        }
        text += count;
        remaining -= count;
    }
}

#if defined(_DEBUG)
// The CRT report hook: a failed check writes its report and ends the process, as the dialog's Abort would.
inline int __cdecl ReportAndEnd(int reportType, wchar_t* message, int* returnValue) noexcept
{
    if (reportType == _CRT_WARN)
    {
        return FALSE; // Warnings keep the CRT's own handling: the debugger output.
    }
    WriteToStandardError(message ? message : L"(a CRT report without text)\n");
    *returnValue = 0;
    _exit(kFailedCheckExitCode);
}
#endif

// The SIGABRT handler: an abort() that did not come from a failed check, which ends through ReportAndEnd instead.
inline void __cdecl EndAfterAbort(int) noexcept
{
    // raise() resets the handler to the CRT's default before calling it, and the default would end a concurrent abort()
    // on another thread with exit code 3. Re-arming first leaves only the instant before this line to that race.
    static_cast<void>(std::signal(SIGABRT, &EndAfterAbort));
    WriteToStandardError(L"abort() was called: by assert(), by std::terminate after an unhandled exception, or "
                         L"directly.\n");
    _exit(kAbortExitCode);
}

// Call first in a test executable's wmain, and in RedXe.exe as soon as --self-test or --screenshot is known: before any
// output (a stream's buffering can only change before its first use) and before any check can fail.
inline void RouteAwayFromDialogs() noexcept
{
    static_cast<void>(std::setvbuf(stdout, nullptr, _IONBF, 0));
    static_cast<void>(std::setvbuf(stderr, nullptr, _IONBF, 0));
    // assert() and the CRT's runtime-error messages go to stderr: a GUI-subsystem process such as RedXe.exe would
    // otherwise show them in a message box.
    static_cast<void>(_set_error_mode(_OUT_TO_STDERR));
    // abort() raises SIGABRT for EndAfterAbort, without first reporting "abort() has been called" through the hook
    // (which would end it as a failed check) and without a fail-fast.
    static_cast<void>(_set_abort_behavior(0u, _WRITE_ABORT_MSG | _CALL_REPORTFAULT));
    static_cast<void>(std::signal(SIGABRT, &EndAfterAbort));
    static_cast<void>(SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX));
#if defined(_DEBUG)
    static_cast<void>(_CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, &ReportAndEnd));
#endif
}
} // namespace RedXeFailureReports
