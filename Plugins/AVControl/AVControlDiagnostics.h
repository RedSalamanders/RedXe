#pragma once

#include <cstdint>
#include <windows.h>

namespace AVControl
{
// UI-thread, read-only aggregate over live tile and overlay views, including a staged page. No DxUi/C++ library
// types cross the DLL boundary. An absent module is never mapped just to obtain diagnostics.
struct ProbeStatistics final
{
    uint32_t sizeBytes = sizeof(ProbeStatistics);
    uint32_t widgets = 0;
    uint64_t preparations = 0, composites = 0, surfaceBytes = 0, replacementPeakBytes = 0;
    uint64_t surfaceAllocations = 0, cachedBrushes = 0, cachedTextFormats = 0;
};
static_assert(sizeof(ProbeStatistics) == 64);
using QueryProbeStatisticsFn = HRESULT(__stdcall*)(ProbeStatistics*) noexcept;
} // namespace AVControl

#if defined(REDXE_PLUGIN_EXPORTS)
extern "C" __declspec(dllexport) HRESULT __stdcall RedXeAVControlQueryProbeStatistics(
    AVControl::ProbeStatistics* statistics) noexcept;
#endif
