#pragma once

#include <windows.h>

struct AppSettings final
{
    float rotationRadiansPerSecond = 0.72f;
};

[[nodiscard]] HRESULT LoadDefaultSettings(AppSettings& settings) noexcept;
