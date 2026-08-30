#pragma once

#include <cstdint>
#include <windows.h>

struct AppSettings final
{
    std::uint32_t rotatingTriangleInstances = 4;
};

[[nodiscard]] HRESULT LoadDefaultSettings(AppSettings& settings) noexcept;
