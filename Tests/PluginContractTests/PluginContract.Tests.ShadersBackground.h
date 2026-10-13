#pragma once

#include <cstdint>
#include <windows.h>

// Compare every production background mip texel to the frozen original pixel-major algorithm.
[[nodiscard]] HRESULT ValidateShadersBackgroundReference(const uint32_t* actual, uint32_t count) noexcept;
