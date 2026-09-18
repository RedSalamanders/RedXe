#pragma once

#include <cmath>

// Whole-detent stepping for anything driven by RedXePointerPhaseWheel / RedXePointerPhaseHorizontalWheel samples.
// A classic wheel delivers one WHEEL_DELTA (120) per notch; a precision wheel or a touchpad delivers fractions at a
// high rate. Summing them and stepping once per whole detent keeps one notch equal to one step on every device.
// A direction reversal drops the remainder so a slight back-scroll never fires a late step the other way.
//
// Shared by the host page-navigation policy (RedXe/WheelNavigation.h) and the bundled paged widgets. Plain value
// type: no allocation, no Win32 dependency.
inline constexpr float kRedXeWheelDetentUnits = 120.0f;

struct RedXeWheelDetent final
{
    float remainder = 0.0f;

    // Adds one sample and returns the signed whole detents it completed, keeping the Win32 sign (positive is wheel up
    // or tilt right). Non-finite and zero samples complete nothing and leave the remainder alone.
    [[nodiscard]] int Accumulate(float wheelDelta) noexcept
    {
        if (!std::isfinite(wheelDelta) || wheelDelta == 0.0f)
        {
            return 0;
        }
        if ((wheelDelta < 0.0f) != (remainder < 0.0f))
        {
            remainder = 0.0f;
        }
        remainder += wheelDelta;
        const float steps = std::trunc(remainder / kRedXeWheelDetentUnits);
        remainder -= steps * kRedXeWheelDetentUnits;
        return static_cast<int>(steps);
    }

    void Clear() noexcept
    {
        remainder = 0.0f;
    }
};
