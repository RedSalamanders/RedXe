#pragma once
#include <cstdint>
#include <string>
#include <string_view>

enum RedXeLogLevel : uint32_t;

// RedXe's only RedMonitor adapter: Client.h, its provider and its link libraries stay in MonitorDiagnostics.cpp.
namespace MonitorDiagnostics
{
// Call before application workers. Provider lifetime is module-owned; no SDK object crosses a plugin ABI.
void Initialize() noexcept;
void Write(RedXeLogLevel level, std::wstring_view text) noexcept;
void WriteUtf8(RedXeLogLevel level, std::string_view text) noexcept;
// `--redmonitor`: ERROR_SUCCESS once the installed production viewer runs, ERROR_FILE_NOT_FOUND when none is
// installed, else the Win32 error that stopped its launch. Creates no settings, plugin or window state.
[[nodiscard]] unsigned long LaunchViewer() noexcept;
// The `--redmonitor` failure text for LaunchViewer's error: what was searched or why the launch failed.
[[nodiscard]] std::wstring DescribeLaunchFailure(unsigned long error);
// `--redmonitor-probe`: wait up to 2 seconds for a listener, then send RedMonitor's cross-application test messages
// from the calling (main) thread. Exit 0 sent, 1 no provider, 2 no listener or a rejected write.
[[nodiscard]] int RunProbe() noexcept;
// Bare Debug startup only: start an installed viewer on a thread-pool worker without waiting. Release does nothing.
void QueueOptionalDebugViewerLaunch() noexcept;
} // namespace MonitorDiagnostics
