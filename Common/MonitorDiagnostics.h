#pragma once
#include <RedMonitor/Client.h>
#include <string_view>

namespace MonitorDiagnostics
{
// Call before application workers. Provider lifetime is module-owned; no SDK object crosses a plugin ABI.
void Initialize() noexcept;
void Write(RedMonitor::Severity severity, std::wstring_view text) noexcept;
void WriteUtf8(RedMonitor::Severity severity, std::string_view text) noexcept;
// -1 means ordinary application startup; explicit viewer/probe commands never open a document.
[[nodiscard]] int HandleCommand(std::wstring_view command) noexcept;
} // namespace MonitorDiagnostics
