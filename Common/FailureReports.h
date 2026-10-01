#pragma once

#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <windows.h>

// A test process never waits on a dialog. In a Debug build a failed runtime check (an STL range check, a CRT assertion)
// otherwise opens a modal Abort/Retry/Ignore box on the desktop: an unattended run hangs until its time budget ends it,
// and an attended one interrupts the person at the desktop. The report goes to stderr, which the executable's log
// keeps, and the check ends the process with exit code 3 (the dialog's Abort), with no Windows Error Reporting dialog
// either: a fail-fast or an abort ends the process quietly too. RedXe.exe --self-test is a test process as well, so the
// header lives beside the product's shared sources rather than with the test executables.
namespace RedXeFailureReports
{
#if defined(_DEBUG)
inline int __cdecl ReportAndEnd(int reportType, wchar_t* message, int* returnValue) noexcept
{
    if (reportType == _CRT_WARN)
    {
        return FALSE; // Warnings keep the CRT's own handling: the debugger output.
    }
    std::fputws(message ? message : L"(a CRT report without text)\n", stderr);
    std::fflush(stderr);
    *returnValue = 0;
    std::abort();
}
#endif

// Call first in a test executable's wmain, and in RedXe.exe as soon as --self-test is known: before any check can fail.
inline void RouteAwayFromDialogs() noexcept
{
    static_cast<void>(_set_abort_behavior(0u, _WRITE_ABORT_MSG | _CALL_REPORTFAULT));
    static_cast<void>(SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX));
#if defined(_DEBUG)
    static_cast<void>(_CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, &ReportAndEnd));
#endif
}
} // namespace RedXeFailureReports
