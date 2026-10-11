#pragma once

#include <cstdint>
#include <shlobj.h>
#include <windows.h>

struct LauncherTestDiagnostics final
{
    uint32_t sizeBytes;
    uint32_t authoredCount;
    uint32_t displayCount;
    uint32_t usingTaskbarPins;
    uint32_t columns;
    uint32_t rows;
    uint32_t lastInstanceCount;
    uint32_t lastDrawCount;
    uint32_t lastLaunchKind;
    uint32_t lastShellMask;
    uint32_t lastVerbWasNull;
    uint32_t lastShow;
    uint64_t extractCalls;
    uint64_t textureUploads;
    uint64_t drawCalls;
    uint64_t launchCount;
    uint64_t shellExecuteCount;
    uint32_t largestIconEdge;
    uint32_t liveWidgets;
    uint32_t pageCount;
    uint32_t pageIndex;
    int32_t pageSlidePx;
    uint32_t pageSettling;
    uint32_t iconHalfExtentPx;
};

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_LAUNCHER_TEST_API __declspec(dllexport)
#else
#define REDXE_LAUNCHER_TEST_API
#endif

extern "C"
{
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherSetTestPinDirectory(const wchar_t* directory) noexcept;
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherGetTestDiagnostics(
        LauncherTestDiagnostics* diagnostics) noexcept;
    REDXE_LAUNCHER_TEST_API void __stdcall RedXeLauncherResetTestDiagnostics() noexcept;
    // 0 clears; 1 fails replacement texture creation; 2 fails its view, after texture allocation. Automated hosts only.
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherSetTestIconFailure(uint32_t phase) noexcept;
    // Automated hosts only, UI thread. Borrows the extractor only for this call; exercises the production
    // handler/decode path. Outputs are zero on refusal/failure. Adds no retained test override or COM owner.
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherTestShellHandler(IExtractIconW* extractor,
                                                                            uint64_t* pixelHash,
                                                                            uint32_t* sourceEdge) noexcept;
}

using LauncherSetTestPinDirectoryFn = decltype(&RedXeLauncherSetTestPinDirectory);
using LauncherGetTestDiagnosticsFn = decltype(&RedXeLauncherGetTestDiagnostics);
using LauncherResetTestDiagnosticsFn = decltype(&RedXeLauncherResetTestDiagnostics);
using LauncherSetTestIconFailureFn = decltype(&RedXeLauncherSetTestIconFailure);
using LauncherTestShellHandlerFn = decltype(&RedXeLauncherTestShellHandler);

inline constexpr char kLauncherSetTestPinDirectoryExport[] = "RedXeLauncherSetTestPinDirectory";
inline constexpr char kLauncherGetTestDiagnosticsExport[] = "RedXeLauncherGetTestDiagnostics";
inline constexpr char kLauncherResetTestDiagnosticsExport[] = "RedXeLauncherResetTestDiagnostics";
inline constexpr char kLauncherSetTestIconFailureExport[] = "RedXeLauncherSetTestIconFailure";
inline constexpr char kLauncherTestShellHandlerExport[] = "RedXeLauncherTestShellHandler";

#undef REDXE_LAUNCHER_TEST_API
