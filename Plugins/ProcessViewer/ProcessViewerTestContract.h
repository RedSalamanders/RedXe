#pragma once

#include <cstdint>
#include <windows.h>

struct ProcessViewerTestDiagnostics final
{
    std::uint32_t sizeBytes;
    std::uint32_t liveProviderCount;
    std::uint32_t liveWidgetCount;
    std::uint32_t liveSubscriptionCount;
    std::uint32_t sampleCount;
    std::uint32_t paintCount;
    std::uint32_t lastPublishedRowCount;
    std::uint32_t configuredTopN;
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
