#pragma once

#include <cstdint>
#include <windows.h>

struct MatrixRainTestDiagnostics final
{
    std::uint32_t sizeBytes;
    std::uint32_t liveProviderCount;
    std::uint32_t liveWidgetCount;
    std::uint32_t liveDeviceResourceSetCount;
};

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_MATRIX_TEST_API __declspec(dllexport)
#else
#define REDXE_MATRIX_TEST_API
#endif

extern "C" REDXE_MATRIX_TEST_API HRESULT __stdcall RedXeMatrixRainGetTestDiagnostics(
    MatrixRainTestDiagnostics* diagnostics) noexcept;

using MatrixRainGetTestDiagnosticsFn = decltype(&RedXeMatrixRainGetTestDiagnostics);

inline constexpr char kMatrixRainGetTestDiagnosticsExport[] = "RedXeMatrixRainGetTestDiagnostics";

#undef REDXE_MATRIX_TEST_API
