#pragma once

#include <cstddef>
#include <cstdint>
#include <windows.h>

struct RedXeGpuDeviceContext;

enum class ShadersSetupPhase : uint32_t
{
    CoreDeviceResources,
    BackgroundPixels,
    BackgroundMips,
    BackgroundUpload,
    ConstantBuffer,
    LookupTableBake,
};

inline constexpr uint32_t kShadersSetupPhaseCount = 6;
inline constexpr uint32_t kShadersBackgroundTexelCount = 349'525;

// Call-local setup attribution. No host call, override, retained pointer or production timing state.
struct ShadersSetupTimings final
{
    uint32_t sizeBytes;
    uint32_t phaseCount;
    uint64_t frequency;
    uint64_t totalTicks;
    uint64_t phaseTicks[kShadersSetupPhaseCount];
    uint32_t phaseCalls[kShadersSetupPhaseCount];
    HRESULT phaseResults[kShadersSetupPhaseCount];
};
static_assert(sizeof(ShadersSetupTimings) == 120);
static_assert(offsetof(ShadersSetupTimings, phaseTicks) == 24);

// Test seams of 5H4D3R5.dll (Plugins_API.md: never called by the host, not a plugin ABI).
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
    // How many times any widget has drawn one catalog entry's lookup tables since the DLL was loaded.
    uint32_t lookupTableBakeCount;
};

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_SHADERS_TEST_API __declspec(dllexport)
#else
#define REDXE_SHADERS_TEST_API
#endif

extern "C" REDXE_SHADERS_TEST_API HRESULT __stdcall RedXeShadersGetTestDiagnostics(
    ShadersTestDiagnostics* diagnostics) noexcept;
// Fresh slideshow device setup through the same implementation as OnDeviceCreated. The caller owns the
// isolated device and its context; all temporary widget/shared owners retire before returning.
extern "C" REDXE_SHADERS_TEST_API HRESULT __stdcall RedXeShadersMeasureTestSetup(const RedXeGpuDeviceContext* context,
                                                                                 ShadersSetupTimings* timings) noexcept;
// Production CPU background and all ten mip levels, in descending size. Writes exactly the required prefix;
// insufficient capacity refuses before writing. Used to compare the actual old/new DLL payloads.
extern "C" REDXE_SHADERS_TEST_API HRESULT __stdcall RedXeShadersCopyTestBackground(uint32_t* texels,
                                                                                   uint32_t capacityTexels) noexcept;

using ShadersGetTestDiagnosticsFn = decltype(&RedXeShadersGetTestDiagnostics);
using ShadersMeasureTestSetupFn = decltype(&RedXeShadersMeasureTestSetup);
using ShadersCopyTestBackgroundFn = decltype(&RedXeShadersCopyTestBackground);

inline constexpr char kShadersGetTestDiagnosticsExport[] = "RedXeShadersGetTestDiagnostics";
inline constexpr char kShadersMeasureTestSetupExport[] = "RedXeShadersMeasureTestSetup";
inline constexpr char kShadersCopyTestBackgroundExport[] = "RedXeShadersCopyTestBackground";

#undef REDXE_SHADERS_TEST_API
