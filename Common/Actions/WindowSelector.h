#pragma once

// Resolves a parsed window selector (ActionTargets.h) to top-level windows for the host's default actions. Bounded and
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
} // namespace RedXeActions
