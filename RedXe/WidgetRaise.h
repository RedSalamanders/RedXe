#pragma once

#include "PlugInterfaces/Widget.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <windows.h>

[[nodiscard]] constexpr bool RedXeRaisedExtentIsValid(RedXeRaisedExtent extent) noexcept
{
    return extent >= RedXeRaisedExtentQuarter && extent <= RedXeRaisedExtentFull;
}

[[nodiscard]] constexpr float RedXeRaisedExtentScale(RedXeRaisedExtent extent) noexcept
{
    switch (extent)
    {
    case RedXeRaisedExtentQuarter:
        return 0.25f;
    case RedXeRaisedExtentThird:
        return 1.0f / 3.0f;
    case RedXeRaisedExtentHalf:
        return 0.5f;
    case RedXeRaisedExtentFull:
        return 1.0f;
    default:
        return 0.0f;
    }
}

[[nodiscard]] inline LONG RaisedDipPixels(int dips, UINT dpi) noexcept
{
    const int scaleDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi);
    return MulDiv(dips, scaleDpi, USER_DEFAULT_SCREEN_DPI);
}

struct RaisedLayout final
{
    RECT overlay{};
    RECT content{};
    RECT close{};
    RECT shadow{};
};

[[nodiscard]] inline RaisedLayout MakeRaisedLayout(UINT clientWidth, UINT clientHeight, RedXeRaisedExtent extent,
                                                   UINT dpi, const RECT* tileBounds = nullptr) noexcept
{
    RaisedLayout layout{};
    if (clientWidth == 0 || clientHeight == 0 || !RedXeRaisedExtentIsValid(extent))
    {
        return layout;
    }

    const LONG minWidth = RaisedDipPixels(160, dpi);
    const LONG shadow = RaisedDipPixels(14, dpi);
    LONG tileWidth = 0;
    LONG tileLeft = 0;
    if (tileBounds)
    {
        tileWidth = std::max(0L, tileBounds->right - tileBounds->left);
        tileLeft = tileBounds->left;
    }

    // Fraction of client *width*, full client height. Other tiles stay in place under a dim.
    LONG overlayWidth = std::max(
        minWidth, static_cast<LONG>(std::lround(static_cast<float>(clientWidth) * RedXeRaisedExtentScale(extent))));
    if (tileWidth > 0)
    {
        overlayWidth = std::max(overlayWidth, tileWidth);
    }
    overlayWidth = std::min(overlayWidth, static_cast<LONG>(clientWidth));
    const LONG overlayHeight = static_cast<LONG>(clientHeight);
    LONG left = 0;
    if (tileWidth > 0)
    {
        left = tileLeft;
        if (left + overlayWidth > static_cast<LONG>(clientWidth))
        {
            left = static_cast<LONG>(clientWidth) - overlayWidth;
        }
        if (left < 0)
        {
            left = 0;
        }
    }
    layout.overlay = {left, 0, left + overlayWidth, overlayHeight};
    layout.content = layout.overlay;
    const LONG closeSize = RaisedDipPixels(22, dpi);
    const LONG closePad = RaisedDipPixels(8, dpi);
    layout.close = {layout.overlay.right - closePad - closeSize, layout.overlay.top + closePad,
                    layout.overlay.right - closePad, layout.overlay.top + closePad + closeSize};
    if (layout.overlay.right < static_cast<LONG>(clientWidth))
    {
        layout.shadow = {layout.overlay.right, 0,
                         std::min(static_cast<LONG>(clientWidth), layout.overlay.right + shadow), overlayHeight};
    }
    else if (layout.overlay.left > 0)
    {
        layout.shadow = {std::max(0L, layout.overlay.left - shadow), 0, layout.overlay.left, overlayHeight};
    }
    return layout;
}

[[nodiscard]] inline bool WidgetFillsClient(const RECT& bounds, UINT clientWidth, UINT clientHeight) noexcept
{
    return bounds.left <= 0 && bounds.top <= 0 && bounds.right >= static_cast<LONG>(clientWidth) &&
           bounds.bottom >= static_cast<LONG>(clientHeight);
}

[[nodiscard]] constexpr bool PointInRectInclusive(const RECT& bounds, POINT point) noexcept
{
    return point.x >= bounds.left && point.x < bounds.right && point.y >= bounds.top && point.y < bounds.bottom;
}

[[nodiscard]] inline size_t HitTestTopmostWidget(POINT point, const RECT* bounds, size_t count) noexcept
{
    if (!bounds || count == 0)
    {
        return SIZE_MAX;
    }
    for (size_t index = count; index > 0; --index)
    {
        if (PointInRectInclusive(bounds[index - 1], point))
        {
            return index - 1;
        }
    }
    return SIZE_MAX;
}

[[nodiscard]] constexpr bool IsDoubleActivate(ULONGLONG firstTick, POINT first, ULONGLONG secondTick, POINT second,
                                              UINT intervalMilliseconds, LONG slopPixels) noexcept
{
    if (secondTick < firstTick || intervalMilliseconds == 0)
    {
        return false;
    }
    if ((secondTick - firstTick) > intervalMilliseconds)
    {
        return false;
    }
    const LONG deltaX = second.x - first.x;
    const LONG deltaY = second.y - first.y;
    const LONG slop = std::max(slopPixels, 1L);
    return deltaX * deltaX + deltaY * deltaY <= slop * slop;
}

[[nodiscard]] inline bool CanRaiseWidget(const RECT& tileBounds, UINT clientWidth, UINT clientHeight,
                                         RedXeRaisedExtent extent) noexcept
{
    return RedXeRaisedExtentIsValid(extent) && clientWidth != 0 && clientHeight != 0 &&
           !WidgetFillsClient(tileBounds, clientWidth, clientHeight);
}

[[nodiscard]] inline HRGN CreateRaisedOverlayRegion(UINT clientWidth, UINT clientHeight, const RECT& content,
                                                    const RECT& close) noexcept
{
    if (clientWidth == 0 || clientHeight == 0 || content.right <= content.left || content.bottom <= content.top)
    {
        return nullptr;
    }
    const HRGN client = CreateRectRgn(0, 0, static_cast<int>(clientWidth), static_cast<int>(clientHeight));
    const HRGN hole = CreateRectRgn(content.left, content.top, content.right, content.bottom);
    if (!client || !hole)
    {
        if (client)
        {
            DeleteObject(client);
        }
        if (hole)
        {
            DeleteObject(hole);
        }
        return nullptr;
    }
    int combined = CombineRgn(client, client, hole, RGN_DIFF);
    DeleteObject(hole);
    if (combined == ERROR)
    {
        DeleteObject(client);
        return nullptr;
    }
    if (close.right > close.left && close.bottom > close.top)
    {
        const HRGN closeRegion = CreateRectRgn(close.left, close.top, close.right, close.bottom);
        if (!closeRegion)
        {
            DeleteObject(client);
            return nullptr;
        }
        combined = CombineRgn(client, client, closeRegion, RGN_OR);
        DeleteObject(closeRegion);
        if (combined == ERROR)
        {
            DeleteObject(client);
            return nullptr;
        }
    }
    return client;
}
