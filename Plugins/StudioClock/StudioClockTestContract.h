#pragma once

#include <cstdint>
#include <windows.h>

struct StudioClockTestTime final
{
    uint32_t sizeBytes;
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
    uint16_t milliseconds;
};

struct StudioClockTestDiagnostics final
{
    uint32_t sizeBytes;
    uint32_t liveProviderCount;
    uint32_t liveWidgetCount;
    uint32_t liveSharedDeviceResourceSetCount;
    uint32_t liveConstantBufferCount;
    uint32_t lastMapCount;
    uint32_t lastDrawCount;
    uint32_t lastInstanceCount;
    uint64_t timeSampleCount;
};

using StudioClockSetTestTimeFn = HRESULT(__stdcall*)(const StudioClockTestTime* time) noexcept;
using StudioClockGetTestDiagnosticsFn = HRESULT(__stdcall*)(StudioClockTestDiagnostics* diagnostics) noexcept;

inline constexpr char kStudioClockSetTestTimeExport[] = "RedXeStudioClockSetTestTime";
inline constexpr char kStudioClockGetTestDiagnosticsExport[] = "RedXeStudioClockGetTestDiagnostics";
