#pragma once

#include <cstdint>
#include <windows.h>

struct DeskClockTestTime final
{
    uint32_t sizeBytes;
    uint16_t year;
    uint16_t month;
    uint16_t dayOfWeek;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t second;
    uint16_t milliseconds;
};

struct DeskClockTestDiagnostics final
{
    uint32_t sizeBytes;
    uint32_t liveProviders;
    uint32_t liveWidgets;
    uint32_t liveDeviceResourceSets;
    uint64_t timeSamples;
    uint64_t constantUploads;
    uint64_t drawCalls;
    uint64_t scheduleQueries;
    uint64_t typographyBuilds;
    uint32_t atlasBytes;
    // Live glyph atlas resolution tier and edge length, so a test can prove the atlas follows the drawn size.
    uint32_t atlasScale;
    uint32_t atlasEdgePixels;
    // Times the host reported a changed target size. It must not grow per frame.
    uint64_t targetSizeChanges;
    uint32_t constantBytes;
};

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_DESK_CLOCK_TEST_API __declspec(dllexport)
#else
#define REDXE_DESK_CLOCK_TEST_API
#endif

extern "C"
{
    REDXE_DESK_CLOCK_TEST_API HRESULT __stdcall RedXeDeskClockSetTestTime(const DeskClockTestTime* time) noexcept;
    REDXE_DESK_CLOCK_TEST_API HRESULT __stdcall RedXeDeskClockGetTestDiagnostics(
        DeskClockTestDiagnostics* diagnostics) noexcept;
}

using DeskClockSetTestTimeFn = decltype(&RedXeDeskClockSetTestTime);
using DeskClockGetTestDiagnosticsFn = decltype(&RedXeDeskClockGetTestDiagnostics);

inline constexpr char kDeskClockSetTestTimeExport[] = "RedXeDeskClockSetTestTime";
inline constexpr char kDeskClockGetTestDiagnosticsExport[] = "RedXeDeskClockGetTestDiagnostics";

#undef REDXE_DESK_CLOCK_TEST_API
