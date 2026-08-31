#pragma once

#include <windows.h>

namespace CrashHandler
{
inline constexpr int kCrashExitCode = 127;

void Install() noexcept;

[[nodiscard]] int WriteDumpForException(EXCEPTION_POINTERS* exceptionPointers) noexcept;

void ShowPreviousCrashUiIfPresent(HWND ownerWindow) noexcept;

[[nodiscard]] HRESULT SetCrashDirectoryForTesting(const wchar_t* directory) noexcept;

[[noreturn]] void TriggerCrashTest() noexcept;
[[noreturn]] void TriggerStackOverflowCrashTest() noexcept;
} // namespace CrashHandler
