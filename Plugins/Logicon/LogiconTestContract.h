#pragma once

// Bounded test-support exports of Logicon.dll. Each export is declared here behind one macro so the shipped export
// set is readable from this header; the host never calls them. The Debug monitor tile uses the same service
// entry points in-process, so this surface is the only way a test drives the service without hardware.

#include <cstdint>
#include <windows.h>

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_LOGICON_TEST_API __declspec(dllexport)
#else
#define REDXE_LOGICON_TEST_API
#endif

// Snapshot of the service for tests. sizeBytes must equal sizeof.
struct RedXeLogiconTestDiagnostics final
{
    uint32_t sizeBytes;
    uint32_t serviceStarted;
    uint32_t laneRunning;
    uint32_t deviceAccess;
    uint32_t connected;
    uint32_t synthetic;
    uint32_t rendererReady;
    uint32_t keys;
    uint32_t pageButtons;
    uint32_t keyPage;
    uint32_t keyPageCount;
    uint32_t brightness;
    uint32_t faceGeneration;
    uint32_t facesWritten;
    uint32_t actionsRequested;
    uint32_t lastAction;
    uint32_t syntheticImages;
    uint32_t syntheticCommands;
    uint32_t syntheticBrightness;
    uint32_t syntheticPageButtonsDiverted;
    uint32_t syntheticResetSeen;
    uint32_t hostPageIndex;
    uint32_t hostPageCount;
    uint32_t hostFlags;
    uint32_t reportsIn;
    uint32_t reportsOut;
    int32_t lastFailure;
    // Dialpad: HID++ buttons (bit n = kDialpadControls[n] held), presses seen, raw-input wheel sums.
    uint32_t dialpadConnected;
    uint32_t dialButtons;
    uint32_t dialButtonPresses;
    uint32_t wheelsListening;
    int32_t dialRaw;
    int32_t rollerRaw;
    // Detents dispatched to dial/roller actions, and the System Data feed (active flag, rounded percentages, -1
    // while unknown).
    uint32_t wheelSteps;
    uint32_t systemFeed;
    int32_t cpuPercent;
    int32_t memoryPercent;
    int32_t gpuPercent;
};

static_assert(sizeof(RedXeLogiconTestDiagnostics) == 152);

extern "C"
{
    // Fills diagnostics from the current service. ERROR_NOT_READY when no service object exists.
    REDXE_LOGICON_TEST_API HRESULT __stdcall RedXeLogiconGetTestDiagnostics(
        RedXeLogiconTestDiagnostics* diagnostics) noexcept;
    // Routes the device lane to the in-memory synthetic keypad (TRUE) or back to USB discovery (FALSE).
    REDXE_LOGICON_TEST_API HRESULT __stdcall RedXeLogiconUseSyntheticDevice(BOOL enabled) noexcept;
    // Simulates a control: kind 0 = LCD key slot 0..8, kind 1 = page button (0 previous, 1 next), kind 2 = dialpad
    // button 0..3.
    REDXE_LOGICON_TEST_API HRESULT __stdcall RedXeLogiconInjectControl(uint32_t kind, uint32_t index,
                                                                       BOOL down) noexcept;
    // Queues a raw input report on the synthetic keypad (report id first).
    REDXE_LOGICON_TEST_API HRESULT __stdcall RedXeLogiconInjectSyntheticReport(const uint8_t* report,
                                                                               uint32_t bytes) noexcept;
    // Face override for one slot: kind 0 none, 1 solid color (0xRRGGBB), 2 generated test picture.
    REDXE_LOGICON_TEST_API HRESULT __stdcall RedXeLogiconSetFaceOverride(uint32_t slot, uint32_t kind,
                                                                         uint32_t colorRgb) noexcept;
    REDXE_LOGICON_TEST_API HRESULT __stdcall RedXeLogiconSetBrightness(uint32_t percent) noexcept;
}

using RedXeLogiconGetTestDiagnosticsFn = decltype(&RedXeLogiconGetTestDiagnostics);
using RedXeLogiconUseSyntheticDeviceFn = decltype(&RedXeLogiconUseSyntheticDevice);
using RedXeLogiconInjectControlFn = decltype(&RedXeLogiconInjectControl);
using RedXeLogiconInjectSyntheticReportFn = decltype(&RedXeLogiconInjectSyntheticReport);
using RedXeLogiconSetFaceOverrideFn = decltype(&RedXeLogiconSetFaceOverride);
using RedXeLogiconSetBrightnessFn = decltype(&RedXeLogiconSetBrightness);

inline constexpr char kRedXeLogiconGetTestDiagnosticsExport[] = "RedXeLogiconGetTestDiagnostics";
inline constexpr char kRedXeLogiconUseSyntheticDeviceExport[] = "RedXeLogiconUseSyntheticDevice";
inline constexpr char kRedXeLogiconInjectControlExport[] = "RedXeLogiconInjectControl";
inline constexpr char kRedXeLogiconInjectSyntheticReportExport[] = "RedXeLogiconInjectSyntheticReport";
inline constexpr char kRedXeLogiconSetFaceOverrideExport[] = "RedXeLogiconSetFaceOverride";
inline constexpr char kRedXeLogiconSetBrightnessExport[] = "RedXeLogiconSetBrightness";

#undef REDXE_LOGICON_TEST_API
