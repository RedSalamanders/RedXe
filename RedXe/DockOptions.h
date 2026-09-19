#pragma once

#include "DockPlacement.h"
#include "Settings.h"

#include <array>
#include <cstdint>
#include <cwchar>
#include <string_view>
#include <windows.h>

// The --dock* command line (UI_XeneonDisplayWindowing.md "Dock window kind"): every switch overrides one member of
// the document's `dock` object for the process lifetime, including across live reloads. Parsing and the merge are
// pure so SettingsTests proves the grammar, the errors, and the precedence without a process.

struct DockOverrides final
{
    bool hasEdge = false;
    DockEdge edge = DockEdge::None;
    bool hasMonitor = false;
    SettingsText monitor;
    bool hasMode = false;
    DockMode mode = DockMode::Fixed;
    bool hasThickness = false;
    uint32_t thicknessDips = kDockDefaultThicknessDips;
    bool hasReserve = false;
    bool reserveWorkArea = true;
    bool hasPeek = false;
    uint32_t peekPixels = kDockDefaultPeekPixels;

    [[nodiscard]] bool Any() const noexcept
    {
        return hasEdge || hasMonitor || hasMode || hasThickness || hasReserve || hasPeek;
    }
};

namespace DockOptionsDetail
{
// ASCII-only narrowing of a switch value; the grammar has no non-ASCII token except a `name:` needle, which is
// copied as UTF-8.
[[nodiscard]] inline bool NarrowUtf8(std::wstring_view wide, SettingsText& text) noexcept
{
    text = SettingsText{};
    if (wide.empty())
    {
        return false;
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
                                          text.utf8.data(), static_cast<int>(text.utf8.size() - 1), nullptr, nullptr);
    if (bytes <= 0 || static_cast<size_t>(bytes) > kMaximumSettingsTextBytes)
    {
        text = SettingsText{};
        return false;
    }
    text.bytes = static_cast<uint32_t>(bytes);
    text.utf8[static_cast<size_t>(bytes)] = '\0';
    return true;
}

[[nodiscard]] inline bool ParseUnsigned(std::wstring_view value, uint32_t minimum, uint32_t maximum,
                                        uint32_t& parsed) noexcept
{
    if (value.empty() || value.size() > 10)
    {
        return false;
    }
    uint64_t result = 0;
    for (const wchar_t character : value)
    {
        if (character < L'0' || character > L'9')
        {
            return false;
        }
        result = result * 10 + static_cast<uint64_t>(character - L'0');
    }
    if (result < minimum || result > maximum)
    {
        return false;
    }
    parsed = static_cast<uint32_t>(result);
    return true;
}
} // namespace DockOptionsDetail

// --dock <edge>[@<monitor>]: none | top | bottom | left | right, optionally followed by @ and a monitor selector
// (primary, xeneon, <n>, name:<substring>; never all).
[[nodiscard]] inline bool ParseDockEdgeArgument(std::wstring_view value, DockOverrides& overrides) noexcept
{
    std::wstring_view edgeText = value;
    std::wstring_view monitorText;
    const size_t at = value.find(L'@');
    if (at != std::wstring_view::npos)
    {
        edgeText = value.substr(0, at);
        monitorText = value.substr(at + 1);
        if (monitorText.empty())
        {
            return false;
        }
    }
    SettingsText narrowEdge;
    DockEdge edge = DockEdge::None;
    if (!DockOptionsDetail::NarrowUtf8(edgeText, narrowEdge) || !DockEdgeParse(narrowEdge.View(), edge))
    {
        return false;
    }
    SettingsText monitor;
    if (!monitorText.empty())
    {
        RedXeActions::MonitorSelector selector{};
        if (!DockOptionsDetail::NarrowUtf8(monitorText, monitor) ||
            !RedXeActions::ParseMonitorSelector(monitor.View(), false, selector))
        {
            return false;
        }
    }
    overrides.hasEdge = true;
    overrides.edge = edge;
    if (!monitorText.empty())
    {
        overrides.hasMonitor = true;
        overrides.monitor = monitor;
    }
    return true;
}

// --dock-mode fixed | autohide
[[nodiscard]] inline bool ParseDockModeArgument(std::wstring_view value, DockOverrides& overrides) noexcept
{
    SettingsText narrow;
    DockMode mode = DockMode::Fixed;
    if (!DockOptionsDetail::NarrowUtf8(value, narrow) || !DockModeParse(narrow.View(), mode))
    {
        return false;
    }
    overrides.hasMode = true;
    overrides.mode = mode;
    return true;
}

// --dock-thickness <dips>
[[nodiscard]] inline bool ParseDockThicknessArgument(std::wstring_view value, DockOverrides& overrides) noexcept
{
    uint32_t parsed = 0;
    if (!DockOptionsDetail::ParseUnsigned(value, kDockMinimumThicknessDips, kDockMaximumThicknessDips, parsed))
    {
        return false;
    }
    overrides.hasThickness = true;
    overrides.thicknessDips = parsed;
    return true;
}

// --dock-reserve on | off
[[nodiscard]] inline bool ParseDockReserveArgument(std::wstring_view value, DockOverrides& overrides) noexcept
{
    if (value == L"on")
    {
        overrides.reserveWorkArea = true;
    }
    else if (value == L"off")
    {
        overrides.reserveWorkArea = false;
    }
    else
    {
        return false;
    }
    overrides.hasReserve = true;
    return true;
}

// --dock-peek <pixels>
[[nodiscard]] inline bool ParseDockPeekArgument(std::wstring_view value, DockOverrides& overrides) noexcept
{
    uint32_t parsed = 0;
    if (!DockOptionsDetail::ParseUnsigned(value, kDockMinimumPeekPixels, kDockMaximumPeekPixels, parsed))
    {
        return false;
    }
    overrides.hasPeek = true;
    overrides.peekPixels = parsed;
    return true;
}

// Effective dock for this process: the document's `dock` (defaults already merged by the parser) with each present
// switch replacing its member. Re-run on every live reload.
[[nodiscard]] inline DockSettings EffectiveDockSettings(const DockSettings& document,
                                                        const DockOverrides& overrides) noexcept
{
    DockSettings effective = document;
    if (overrides.hasEdge)
    {
        effective.edge = overrides.edge;
    }
    if (overrides.hasMonitor)
    {
        effective.monitor = overrides.monitor;
    }
    if (overrides.hasMode)
    {
        effective.mode = overrides.mode;
    }
    if (overrides.hasThickness)
    {
        effective.thicknessDips = overrides.thicknessDips;
    }
    if (overrides.hasReserve)
    {
        effective.reserveWorkArea = overrides.reserveWorkArea;
    }
    if (overrides.hasPeek)
    {
        effective.peekPixels = overrides.peekPixels;
    }
    return effective;
}
