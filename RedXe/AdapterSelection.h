#pragma once

#include <cstddef>
#include <span>
#include <windows.h>

// One DXGI output flattened to the two facts the selection policy needs, so the policy can be tested without DXGI.
// Renderer builds these records from IDXGIFactory1::EnumAdapters1 / IDXGIAdapter1::EnumOutputs.
struct RedXeAdapterOutputRecord final
{
    LUID adapterLuid{};
    HMONITOR monitor = nullptr;
    bool softwareAdapter = false;
};

[[nodiscard]] constexpr bool RedXeSameLuid(const LUID& left, const LUID& right) noexcept
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

// Adapter-of-output policy (Core_PerformanceAndResources.md, UI_XeneonDisplayWindowing.md): the Direct3D device is
// created on the hardware adapter that scans out the monitor the window sits on, so presentation never crosses
// adapters through DWM. Returns the first non-software record whose output owns `target`, or SIZE_MAX when nothing
// matches (null monitor, window off every monitor, hidden test window); callers then use the default adapter.
[[nodiscard]] constexpr size_t RedXeSelectAdapterRecordForMonitor(std::span<const RedXeAdapterOutputRecord> records,
                                                                  HMONITOR target) noexcept
{
    if (!target)
    {
        return SIZE_MAX;
    }
    for (size_t index = 0; index < records.size(); ++index)
    {
        if (!records[index].softwareAdapter && records[index].monitor == target)
        {
            return index;
        }
    }
    return SIZE_MAX;
}
