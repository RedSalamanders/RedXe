#define REDMONITOR_DEFINE_PROVIDER
#include "MonitorDiagnostics.h"
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
} // namespace
void MonitorDiagnostics::Initialize() noexcept
{
    (void)Diagnostics().Status();
}
void MonitorDiagnostics::Write(RedMonitor::Severity severity, std::wstring_view text) noexcept
{
    (void)Diagnostics().Write(severity, text);
}
void MonitorDiagnostics::WriteUtf8(RedMonitor::Severity severity, std::string_view text) noexcept
{
    (void)Diagnostics().WriteUtf8(severity, text);
}
int MonitorDiagnostics::HandleCommand(std::wstring_view command) noexcept
{
    if (command == L"--redmonitor-probe")
    {
        if (Diagnostics().Status() != ERROR_SUCCESS)
            return 1;
        // Test-only bounded readiness barrier. Ordinary startup never waits for a viewer.
        const ULONGLONG start = GetTickCount64();
        while (!Diagnostics().IsEnabled() && GetTickCount64() - start < 2000)
            Sleep(10);
        if (!Diagnostics().IsEnabled())
            return 2;
        return Diagnostics().Write(RedMonitor::Severity::Warning, L"diagnostic probe \u00e9\u65e5\U0001F642") ==
                           ERROR_SUCCESS &&
                       Diagnostics().WriteUtf8(RedMonitor::Severity::Info, "copied plugin/worker diagnostic") ==
                           ERROR_SUCCESS
                   ? 0
                   : 2;
    }
    if (!RedMonitor::IsViewerCommand(command))
    {
#if defined(_DEBUG)
        if (RedMonitor::IsOrdinaryStartup(command))
        {
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
        }
#endif
        return -1;
    }
    try
    {
        return RedMonitor::LaunchInstalledViewer() == ERROR_SUCCESS ? 0 : 1;
    }
    catch (const std::bad_alloc&)
    {
        std::terminate();
    }
    catch (const std::exception&)
    {
        // Explicit optional launch failed; no document/application state has been created.
        return 1;
    }
}
