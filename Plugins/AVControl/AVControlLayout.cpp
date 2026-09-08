#include "AVControlLayout.h"
#include <algorithm>
#include <cmath>

namespace AVControl
{
namespace
{
void FinishLiveLayout(LiveLayout& layout) noexcept
{
    for (size_t i = 0; i < layout.sliders.size(); ++i)
    {
        auto& slider = layout.sliders[i];
        const float leading = (std::min)(108.0f, (std::max)(0.0f, slider.width - 48.0f));
        layout.levelMutes[i] = {slider.x, slider.y, leading, slider.height};
        slider.x += leading;
        slider.width -= leading;
        constexpr float kSliderHitHeightDip = 48.0f;
        if (slider.height > kSliderHitHeightDip)
        {
            slider.y += (slider.height - kSliderHitHeightDip) * 0.5f;
            slider.height = kSliderHitHeightDip;
        }
    }
    layout.showDeviceNames = layout.toggles[0].width >= 96.0f;
}
} // namespace

LiveLayout LayoutLive(float width, float height) noexcept
{
    LiveLayout layout;
    if (!std::isfinite(width) || !std::isfinite(height) || width < 160 || height < 180)
        return layout;
    if (width < 320 || height < 300)
        layout.density = Density::Minimal;
    else if (width < 800 && height >= 620)
        layout.density = Density::Tall;
    else if (width >= 800 && height < 480)
        layout.density = Density::Wide;
    else if (width < 800 || height < 480)
        layout.density = Density::Compact;
    else
        layout.density = Density::Large;

    if (layout.density == Density::Minimal)
    {
        constexpr float padding = 8;
        constexpr float gap = 4;
        const float bodyWidth = width - 2 * padding;
        layout.header = {padding, 0, bodyWidth, 20};
        const float rowHeight = std::clamp((height - 20 - padding - 2 * gap) / 3, 48.0f, 72.0f);
        const float toggleWidth = bodyWidth / 3;
        for (size_t i = 0; i < 3; ++i)
            layout.toggles[i] = {padding + static_cast<float>(i) * toggleWidth, 20, toggleWidth, rowHeight};
        for (size_t i = 0; i < 2; ++i)
        {
            const float top = 20 + static_cast<float>(i + 1) * (rowHeight + gap);
            const float levelWidth = bodyWidth - (i ? 48 + gap : 0);
            layout.levelPanels[i] = {padding, top, levelWidth, rowHeight};
            layout.sliders[i] = layout.levelPanels[i];
        }
        layout.profileSelector = {width - padding - 48, layout.levelPanels[1].y, 48, rowHeight};
        FinishLiveLayout(layout);
        return layout;
    }

    const bool tall = layout.density == Density::Tall;
    const bool large = layout.density == Density::Large;
    const bool columns = large || layout.density == Density::Wide;
    const float padding = large ? 24.0f : tall ? 16.0f : 8.0f;
    const float gap = large ? 16.0f : tall ? 12.0f : 8.0f;
    const float bodyWidth = width - 2 * padding;
    layout.header = {padding, padding, bodyWidth, 48};
    layout.profileSelector = {padding, padding, std::min(bodyWidth, 360.0f), 48};
    layout.labelFont = 16;
    layout.stateFont = large ? 36.0f : 24.0f;
    layout.valueFont = large ? 48.0f : 28.0f;
    layout.showScope = large;
    float y = padding + 48 + gap;
    if (tall)
    {
        const float unit = (height - 2 * padding - 48 - 5 * gap) / 8;
        for (size_t i = 0; i < 3; ++i)
        {
            layout.toggles[i] = {padding, y, bodyWidth, unit};
            y += unit + gap;
        }
        for (size_t i = 0; i < 2; ++i)
        {
            const float panelHeight = unit * 2.5f;
            layout.levelPanels[i] = {padding, y, bodyWidth, panelHeight};
            layout.sliders[i] = layout.levelPanels[i];
            y += panelHeight + gap;
        }
    }
    else
    {
        const float remaining = height - padding - y;
        const float toggleHeight =
            columns ? std::max(64.0f, (remaining - gap) * 0.45f) : std::max(64.0f, (remaining - 2 * gap) * 0.30f);
        const float toggleWidth = (bodyWidth - 2 * gap) / 3;
        for (size_t i = 0; i < 3; ++i)
            layout.toggles[i] = {padding + static_cast<float>(i) * (toggleWidth + gap), y, toggleWidth, toggleHeight};
        y += toggleHeight + gap;
        const float panelWidth = columns ? (bodyWidth - gap) / 2 : bodyWidth;
        const float panelHeight = columns ? height - padding - y : (height - padding - y - gap) / 2;
        for (size_t i = 0; i < 2; ++i)
        {
            const float x = padding + (columns ? static_cast<float>(i) * (panelWidth + gap) : 0);
            const float top = y + (columns ? 0 : static_cast<float>(i) * (panelHeight + gap));
            layout.levelPanels[i] = {x, top, panelWidth, panelHeight};
            layout.sliders[i] = layout.levelPanels[i];
        }
    }
    FinishLiveLayout(layout);
    return layout;
}
} // namespace AVControl
