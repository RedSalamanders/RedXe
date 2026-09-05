#pragma once
#include "PlugInterfaces/Widget.h"
#include <cmath>

inline bool IsValidRedXeAccessibilityPlacement(const RedXeAccessibilityPlacement& value) noexcept
{
    return value.sizeBytes == sizeof(value) && value.viewId <= 1 && value.attachmentId && !value.reserved &&
           (value.keyboardFocused == FALSE || value.keyboardFocused == TRUE) && std::isfinite(value.left) &&
           std::isfinite(value.top) && std::isfinite(value.width) && std::isfinite(value.height) && value.width > 0 &&
           value.height > 0 && value.left >= LONG_MIN && value.top >= LONG_MIN &&
           value.left + value.width <= LONG_MAX && value.top + value.height <= LONG_MAX;
}
