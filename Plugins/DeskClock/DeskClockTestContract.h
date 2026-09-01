#pragma once

#include <cstdint>
#include <windows.h>

struct DeskClockTestTime final
{
    std::uint32_t sizeBytes;
    std::uint16_t year;
    std::uint16_t month;
    std::uint16_t dayOfWeek;
    std::uint16_t day;
    std::uint16_t hour;
    std::uint16_t minute;
    std::uint16_t second;
    std::uint16_t milliseconds;
};

struct DeskClockTestDiagnostics final
{
    std::uint32_t sizeBytes;
    std::uint32_t liveProviders;
    std::uint32_t liveWidgets;
    std::uint32_t liveDeviceResourceSets;
    std::uint64_t timeSamples;
    std::uint64_t constantUploads;
    std::uint64_t drawCalls;
    std::uint64_t scheduleQueries;
    std::uint64_t typographyBuilds;
    std::uint32_t atlasBytes;
    std::uint32_t constantBytes;
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
