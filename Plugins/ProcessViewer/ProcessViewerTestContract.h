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
};

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_PROCESS_VIEWER_TEST_API __declspec(dllexport)
#else
#define REDXE_PROCESS_VIEWER_TEST_API
#endif

extern "C" REDXE_PROCESS_VIEWER_TEST_API HRESULT __stdcall RedXeProcessViewerGetTestDiagnostics(
    ProcessViewerTestDiagnostics* diagnostics) noexcept;

using ProcessViewerGetTestDiagnosticsFn = decltype(&RedXeProcessViewerGetTestDiagnostics);
inline constexpr char kProcessViewerGetTestDiagnosticsExport[] = "RedXeProcessViewerGetTestDiagnostics";

#undef REDXE_PROCESS_VIEWER_TEST_API
