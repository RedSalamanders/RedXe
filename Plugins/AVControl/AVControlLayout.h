#pragma once
#include <array>
#include <cstdint>

namespace AVControl
{
struct Rect final
{
    float x = 0, y = 0, width = 0, height = 0;
    [[nodiscard]] bool Contains(float px, float py) const noexcept
    {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};
enum class Density : uint32_t
{
    Unusable,
    Minimal,
    Tall,
    Wide,
    Compact,
    Large
};
struct LiveLayout final
{
    Density density = Density::Unusable;
    Rect header;
    Rect profileSelector;
    std::array<Rect, 3> toggles{};
    std::array<Rect, 2> levelPanels{};
    std::array<Rect, 2> sliders{};
    float labelFont = 14;
    float stateFont = 16;
    float valueFont = 18;
    bool showDeviceNames = false;
    bool showScope = false;
};
// Logical pixel geometry, no allocation or OS work. All usable layouts retain the three toggles and both levels.
[[nodiscard]] LiveLayout LayoutLive(float width, float height) noexcept;
} // namespace AVControl
