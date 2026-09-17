#pragma once

// Resolves a parsed window selector (ActionTargets.h) to top-level windows and fronts one of them. Shared by the
// host's default actions and by publishers that need a window (the Zoom service's zoom.focus). Bounded and
// allocation-free: EnumWindows stops at the first match unless every matching window is requested.

#include "ActionTargets.h"

#include <cstdint>
#include <windows.h>

namespace RedXeActions
{
inline constexpr uint32_t kMaximumSelectedWindows = 16;

struct SelectedWindows final
{
    std::array<HWND, kMaximumSelectedWindows> windows{};
    uint32_t count = 0;
};

// Finds the visible, non-tool top-level windows matching selector in Z order. `all` collects every match (bounded);
// otherwise the first match only. Executable names compare case-insensitively against the process image file name;
// class names compare exactly; titles are case-insensitive substrings. Returns false when nothing matched.
[[nodiscard]] bool SelectWindows(const WindowSelector& selector, bool all, SelectedWindows& selected) noexcept;

// Brings window to the foreground. A background process may only set the foreground window after it received the
// last input, so one synthetic Alt press/release is injected first (deviceAccess false skips the injection and the
// call). Returns S_OK when window is foreground afterwards, E_ACCESSDENIED otherwise.
[[nodiscard]] HRESULT BringToForeground(HWND window, bool deviceAccess) noexcept;
} // namespace RedXeActions
