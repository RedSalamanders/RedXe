#pragma once

#include <cstddef>
#include <windows.h>

namespace CrashHandler
{
inline constexpr int kCrashExitCode              = 127;
inline constexpr size_t kCrashNoticePathCapacity = 1024;

void Install() noexcept;
void EnableInteractiveReports() noexcept;
void RetainRecentReports(const wchar_t* directory) noexcept;
[[nodiscard]] bool TakePendingNotice(bool interactive, wchar_t (&dumpPath)[kCrashNoticePathCapacity]) noexcept;

[[nodiscard]] int WriteDumpForException(EXCEPTION_POINTERS* exceptionPointers) noexcept;

void ShowPreviousCrashUiIfPresent(HWND ownerWindow) noexcept;

[[nodiscard]] HRESULT SetCrashDirectoryForTesting(const wchar_t* directory) noexcept;

[[noreturn]] void TriggerCrashTest() noexcept;
[[noreturn]] void TriggerStackOverflowCrashTest() noexcept;
} // namespace CrashHandler
