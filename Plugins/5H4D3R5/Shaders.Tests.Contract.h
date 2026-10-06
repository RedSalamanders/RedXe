#pragma once

#include <cstdint>
#include <windows.h>

// Read-only test seam of 5H4D3R5.dll (Plugins_API.md: never called by the host, not a plugin ABI).
struct ShadersTestDiagnostics final
{
    uint32_t sizeBytes;
    uint32_t liveProviderCount;
    uint32_t liveWidgetCount;
    uint32_t liveSharedResourceSetCount;
    // The most recent frame any widget rendered: which catalog entry it drew and how many frames that shader (or its
    // feedback buffer) has run since it started.
    uint32_t lastShaderIndex;
    uint32_t lastShaderFrame;
};

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_SHADERS_TEST_API __declspec(dllexport)
#else
#define REDXE_SHADERS_TEST_API
#endif

extern "C" REDXE_SHADERS_TEST_API HRESULT __stdcall RedXeShadersGetTestDiagnostics(
    ShadersTestDiagnostics* diagnostics) noexcept;

using ShadersGetTestDiagnosticsFn = decltype(&RedXeShadersGetTestDiagnostics);

inline constexpr char kShadersGetTestDiagnosticsExport[] = "RedXeShadersGetTestDiagnostics";

#undef REDXE_SHADERS_TEST_API
