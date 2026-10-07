#include "DisplayEnumeration.h"

#include <shellscalingapi.h>
#include <string_view>

// GetDpiForMonitor, for every project that builds this file (RedXe links it already; HostPluginTests does not).
#pragma comment(lib, "shcore.lib")

UINT EffectiveMonitorDpi(HMONITOR monitor) noexcept
{
    UINT dpiX = USER_DEFAULT_SCREEN_DPI;
    UINT dpiY = USER_DEFAULT_SCREEN_DPI;
    return SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX != 0 ? dpiX
                                                                                              : USER_DEFAULT_SCREEN_DPI;
}

bool AppendDisplay(DisplayEnumeration& displays, HMONITOR monitor) noexcept
{
    if (displays.count >= displays.candidates.size())
    {
        return false;
    }
    MONITORINFOEXW& info = displays.info[displays.count];
    info = MONITORINFOEXW{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
    {
        return true;
    }
    DockMonitorCandidate& candidate = displays.candidates[displays.count];
    candidate = DockMonitorCandidate{};
    candidate.monitor = info.rcMonitor;
    candidate.work = info.rcWork;
    candidate.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    candidate.dpi = EffectiveMonitorDpi(monitor);
    candidate.deviceName = std::wstring_view{info.szDevice};
    ++displays.count;
    return true;
}

void EnumerateDisplays(DisplayEnumeration& displays) noexcept
{
    displays.count = 0;
    const auto record = [](HMONITOR monitor, HDC, LPRECT, LPARAM data) noexcept -> BOOL
    { return AppendDisplay(*reinterpret_cast<DisplayEnumeration*>(data), monitor) ? TRUE : FALSE; };
    (void)EnumDisplayMonitors(nullptr, nullptr, record, reinterpret_cast<LPARAM>(&displays));
}
