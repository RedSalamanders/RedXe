#define REDMONITOR_DEFINE_PROVIDER
#include "MonitorDiagnostics.h"
#include "PlugInterfaces/Host.h"
#include <RedMonitor/Client.h>
#include <exception>
#include <new>

namespace
{
RedMonitor::Application& Diagnostics() noexcept
{
    // Initialized explicitly by main, before workers; destroyed after application/plugin shutdown.
    static RedMonitor::Application application(L"RedXe");
    return application;
}

RedMonitor::Severity SeverityOf(RedXeLogLevel level) noexcept
{
    switch (level)
    {
    case RedXeLogLevelError:
        return RedMonitor::Severity::Error;
    case RedXeLogLevelWarning:
        return RedMonitor::Severity::Warning;
    case RedXeLogLevelDebug:
        return RedMonitor::Severity::Debug;
    case RedXeLogLevelInfo:
    default:
        return RedMonitor::Severity::Info;
    }
}
} // namespace
void MonitorDiagnostics::Initialize() noexcept
{
    (void)Diagnostics().Status();
}
void MonitorDiagnostics::Write(RedXeLogLevel level, std::wstring_view text) noexcept
{
    (void)Diagnostics().Write(SeverityOf(level), text);
}
void MonitorDiagnostics::WriteUtf8(RedXeLogLevel level, std::string_view text) noexcept
{
    (void)Diagnostics().WriteUtf8(SeverityOf(level), text);
}
unsigned long MonitorDiagnostics::LaunchViewer() noexcept
{
    try
    {
        return RedMonitor::LaunchInstalledViewer();
    }
    catch (const std::bad_alloc&)
    {
        std::terminate();
    }
    catch (const std::exception&)
    {
        // Explicit optional launch failed; no document/application state has been created.
        return ERROR_INTERNAL_ERROR;
    }
}
std::wstring MonitorDiagnostics::DescribeLaunchFailure(unsigned long error)
{
    if (error == ERROR_FILE_NOT_FOUND)
    {
        // Client.h discovery: only these two production Release images, never PATH or a build output.
        return L"RedMonitor is not installed: no Release RedMonitor.exe in %LocalAppData%\\Programs\\RedMonitor or "
               L"%ProgramFiles%\\RedMonitor. Install it from https://github.com/RedSalamanders/RedMonitor/releases, or "
               L"build its package from that repository until it publishes one.\n";
    }
    std::wstring text = L"RedMonitor could not be started";
    wchar_t* reason = nullptr;
    const DWORD length =
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, error, 0, reinterpret_cast<wchar_t*>(&reason), 0, nullptr);
    const wil::unique_hlocal_string owner(reason);
    std::wstring_view message(reason ? reason : L"", length);
    while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' || message.back() == L'.'))
        message.remove_suffix(1);
    if (!message.empty())
    {
        text += L": ";
        text += message;
    }
    text += L" (error " + std::to_wstring(error) + L").\n";
    return text;
}
int MonitorDiagnostics::RunProbe() noexcept
{
    if (Diagnostics().Status() != ERROR_SUCCESS)
        return 1;
    // Test-only bounded readiness barrier. Ordinary startup never waits for a viewer.
    const ULONGLONG start = GetTickCount64();
    while (!Diagnostics().IsEnabled() && GetTickCount64() - start < 2000)
        Sleep(10);
    if (!Diagnostics().IsEnabled())
        return 2;
    return Diagnostics().Write(RedMonitor::Severity::Warning, L"diagnostic probe é日\U0001F642") == ERROR_SUCCESS &&
                   Diagnostics().WriteUtf8(RedMonitor::Severity::Info, "copied plugin/worker diagnostic") ==
                       ERROR_SUCCESS
               ? 0
               : 2;
}
const wchar_t* MonitorDiagnostics::DescribeProbeResult(int exitCode) noexcept
{
    switch (exitCode)
    {
    case 0:
        return L"RedMonitor probe: a listener received the test messages.\n";
    case 1:
        return L"RedMonitor probe: the RedMonitor ETW provider could not be registered.\n";
    default:
        return L"RedMonitor probe: no RedMonitor listener within 2 seconds, or ETW refused a test message.\n";
    }
}
void MonitorDiagnostics::QueueOptionalDebugViewerLaunch() noexcept
{
#if defined(_DEBUG)
    (void)TrySubmitThreadpoolCallback(
        [](PTP_CALLBACK_INSTANCE, void*) noexcept
        {
            try
            {
                (void)RedMonitor::LaunchInstalledViewer();
            }
            catch (const std::bad_alloc&)
            {
                std::terminate();
            }
            catch (const std::exception&)
            { /* Optional helper failure never blocks the app. */
            }
        },
        nullptr, nullptr);
#endif
}
