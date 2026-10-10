#pragma once

#include "DockPlacement.h"

#include <array>
#include <cstddef>
#include <windows.h>

// The active displays in EnumDisplayMonitors order (UI_XeneonDisplayWindowing.md "Monitor and placement"): the one walk
// behind the dock's monitor selection (Application::ResolveDockMonitor) and the first-run bar's measurement
// (MakeFirstRunDock), so the display a first-run `secondary` is measured on is the display that selector resolves to at
// runtime. The storage is bounded and belongs to the caller: the walk allocates nothing.
inline constexpr size_t kMaximumDisplays = 16;

// The effective DPI of `monitor` (GetDpiForMonitor, MDT_EFFECTIVE_DPI), USER_DEFAULT_SCREEN_DPI when the query fails or
// reports 0: the DPI the walk records for each display and the standard window is placed at.
[[nodiscard]] UINT EffectiveMonitorDpi(HMONITOR monitor) noexcept;

// One walk's displays: candidates[i] is the i-th display recorded, with its rectangle, work area, primary flag
// (MONITORINFOF_PRIMARY, even when another display holds (0,0)), effective DPI, and GDI device name, which points into
// info[i]. `xeneon` and `friendlyName` are left to the caller. Filled in place and never copied, so the names stay
// valid.
struct DisplayEnumeration final
{
    DisplayEnumeration() = default;
    DisplayEnumeration(const DisplayEnumeration&) = delete;
    DisplayEnumeration& operator=(const DisplayEnumeration&) = delete;

    std::array<MONITORINFOEXW, kMaximumDisplays> info{};
    std::array<DockMonitorCandidate, kMaximumDisplays> candidates{};
    size_t count = 0;
};

// Records `monitor` as the next display. False once kMaximumDisplays are recorded, which ends the walk; a display whose
// GetMonitorInfoW fails (it left during the walk) is skipped and the walk goes on.
[[nodiscard]] bool AppendDisplay(DisplayEnumeration& displays, HMONITOR monitor) noexcept;

// Starts `displays` over with the displays EnumDisplayMonitors reports, in its order, through AppendDisplay.
void EnumerateDisplays(DisplayEnumeration& displays) noexcept;
