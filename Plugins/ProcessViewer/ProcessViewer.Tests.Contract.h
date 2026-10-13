#pragma once

#include <cstdint>
#include <windows.h>

struct ProcessViewerTestDiagnostics final
{
    uint32_t sizeBytes;
    uint32_t liveProviderCount;
    uint32_t liveWidgetCount;
    uint32_t liveSubscriptionCount;
    uint32_t sampleCount;
    uint32_t paintCount;
    uint32_t lastPublishedRowCount;
    uint32_t configuredTopN;
    uint32_t deviceCallbacksWhileVisible;
    // Process Viewer overflow paging as of its last tile frame: page count, current page, and the page-control
    // dot layout in widget pixels (first dot centre, strip centre y, centre gap; zero when no dots were drawn).
    uint32_t pageCount;
    uint32_t pageIndex;
    float pageDotFirstX;
    float pageDotY;
    float pageDotGap;
    uint32_t cpuAtlasBytes;
    uint32_t liveGpuResourceSets;
    uint32_t glyphUploadCells;
    uint32_t fontSessions;
    uint64_t glyphUploadBytes;
    uint32_t pendingSampleBytes;
    uint32_t preparations;
    uint32_t livePreparationFonts;
    uint32_t preparationFailures;
};
static_assert(sizeof(ProcessViewerTestDiagnostics) == 96);

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_PROCESS_VIEWER_TEST_API __declspec(dllexport)
#else
#define REDXE_PROCESS_VIEWER_TEST_API
#endif

extern "C" REDXE_PROCESS_VIEWER_TEST_API HRESULT __stdcall RedXeProcessViewerGetTestDiagnostics(
    ProcessViewerTestDiagnostics* diagnostics) noexcept;
extern "C" REDXE_PROCESS_VIEWER_TEST_API void __stdcall RedXeProcessViewerFailNextAtlasInitialization() noexcept;
extern "C" REDXE_PROCESS_VIEWER_TEST_API void __stdcall RedXeProcessViewerFailNextGlyphPreparation() noexcept;

using ProcessViewerGetTestDiagnosticsFn = decltype(&RedXeProcessViewerGetTestDiagnostics);
inline constexpr char kProcessViewerGetTestDiagnosticsExport[] = "RedXeProcessViewerGetTestDiagnostics";

#undef REDXE_PROCESS_VIEWER_TEST_API
