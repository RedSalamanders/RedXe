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

// Test-support surface. Like every other bundled plugin's test contract this is declared through one export macro
// rather than a raw __declspec in the implementation, so the shipped export set is visible from the contract header.
// Plugins_API.md names this surface: it is present in Release because the required Release validation drives it.
#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_STUDIO_CLOCK_TEST_API __declspec(dllexport)
#else
#define REDXE_STUDIO_CLOCK_TEST_API
#endif

extern "C"
{
    REDXE_STUDIO_CLOCK_TEST_API HRESULT __stdcall RedXeStudioClockSetTestTime(
        const StudioClockTestTime* testTime) noexcept;
    REDXE_STUDIO_CLOCK_TEST_API HRESULT __stdcall RedXeStudioClockGetTestDiagnostics(
        StudioClockTestDiagnostics* diagnostics) noexcept;
}

using StudioClockSetTestTimeFn = decltype(&RedXeStudioClockSetTestTime);
using StudioClockGetTestDiagnosticsFn = decltype(&RedXeStudioClockGetTestDiagnostics);

#undef REDXE_STUDIO_CLOCK_TEST_API

inline constexpr char kStudioClockSetTestTimeExport[] = "RedXeStudioClockSetTestTime";
inline constexpr char kStudioClockGetTestDiagnosticsExport[] = "RedXeStudioClockGetTestDiagnostics";
