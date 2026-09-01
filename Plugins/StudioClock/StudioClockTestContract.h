#pragma once

#include <cstdint>
#include <windows.h>

struct StudioClockTestTime final
{
    std::uint32_t sizeBytes;
    std::uint16_t year;
    std::uint16_t month;
    std::uint16_t day;
    std::uint16_t hour;
    std::uint16_t minute;
    std::uint16_t second;
    std::uint16_t milliseconds;
};

struct StudioClockTestDiagnostics final
{
    std::uint32_t sizeBytes;
    std::uint32_t liveProviderCount;
    std::uint32_t liveWidgetCount;
    std::uint32_t liveSharedDeviceResourceSetCount;
    std::uint32_t liveConstantBufferCount;
    std::uint32_t lastMapCount;
    std::uint32_t lastDrawCount;
    std::uint32_t lastInstanceCount;
    std::uint64_t timeSampleCount;
};

using StudioClockSetTestTimeFn = HRESULT(__stdcall*)(const StudioClockTestTime* time) noexcept;
using StudioClockGetTestDiagnosticsFn = HRESULT(__stdcall*)(StudioClockTestDiagnostics* diagnostics) noexcept;

inline constexpr char kStudioClockSetTestTimeExport[] = "RedXeStudioClockSetTestTime";
inline constexpr char kStudioClockGetTestDiagnosticsExport[] = "RedXeStudioClockGetTestDiagnostics";
