#pragma once

#include "FluentIcons.h"
#include "PageNavigation.h"

#include <algorithm>
#include <cstdint>
#include <windows.h>

// Geometry and reveal policy for the mouse page-navigation affordance: a band along each client edge that stays
// invisible until the pointer enters it, then paints a directional chevron. Clicking a revealed band commits to the
// adjacent page through the same staging, settle, and promote path a touch swipe uses.
//
// Everything here is a pure function of client size, DPI, and host state so the policy is testable without a window.

// Band width and chevron size in DIPs. Band width is the single tuning point: a wider band is easier to hit but takes
// more of the outer tile away from double-click raise.
inline constexpr int kPageEdgeBandWidthDips = 56;
inline constexpr int kPageEdgeChevronHeightDips = 22;

// Layered alpha of a revealed band. The band window exists only while revealed, so there is no transparent state to
// represent: a layered child at zero alpha is click-through and could never receive the hover that reveals it.
inline constexpr BYTE kPageEdgeRevealedAlpha = 210;

// Direction values match the page-navigation convention: -1 returns to the previous page, +1 advances.
inline constexpr int kPageEdgeDirectionPrevious = -1;
inline constexpr int kPageEdgeDirectionNext = 1;

[[nodiscard]] inline LONG PageEdgeDipPixels(int dips, UINT dpi) noexcept
{
    const int scaleDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi);
    return MulDiv(dips, scaleDpi, USER_DEFAULT_SCREEN_DPI);
}

[[nodiscard]] inline LONG PageEdgeBandWidthPixels(UINT clientWidth, UINT dpi) noexcept
{
    if (clientWidth == 0)
    {
        return 0;
    }
    const LONG requested = std::max(1L, PageEdgeDipPixels(kPageEdgeBandWidthDips, dpi));
    // Never let the two bands meet, so a narrow client keeps a usable centre.
    const LONG maximum = std::max(1L, static_cast<LONG>(clientWidth) / 3);
    return std::min(requested, maximum);
}

// Largest number of displays whose work areas are unioned for one window. A window cannot usefully straddle more.
inline constexpr size_t kPageEdgeMaximumWorkAreas = 8;

// Reachable part of the client, in client coordinates: the client rectangle intersected with the union of the
// supplied work areas on the horizontal axis only. Vertical extent is always the full client.
//
// Horizontal clipping is what keeps a band on-screen when a titled window is wider than its monitor. Vertical
// clipping is not: intersecting with the work area (taskbar, hanging title strip) shortens the band so it no longer
// runs from the top of the window to the bottom. The pointer can already travel the full client height inside the
// window, so the band MUST span that height.
//
// The union still matters horizontally. A window straddling two displays is reachable across both, so clamping to
// the nearest single monitor would drop a band into the middle of the window, over the other screen. With no work
// areas supplied the whole client is treated as reachable.
[[nodiscard]] inline RECT PageEdgeReachableClient(const RECT& client, const RECT* workAreas, size_t count) noexcept
{
    if (client.right <= client.left || client.bottom <= client.top)
    {
        return RECT{};
    }
    if (!workAreas || count == 0)
    {
        return client;
    }

    RECT combined{};
    bool any = false;
    for (size_t index = 0; index < count; ++index)
    {
        const RECT& area = workAreas[index];
        if (area.right <= area.left || area.bottom <= area.top)
        {
            continue;
        }
        if (!any)
        {
            combined = area;
            any = true;
            continue;
        }
        combined.left = std::min(combined.left, area.left);
        combined.top = std::min(combined.top, area.top);
        combined.right = std::max(combined.right, area.right);
        combined.bottom = std::max(combined.bottom, area.bottom);
    }
    if (!any)
    {
        return client;
    }

    const RECT reachable{std::max(client.left, combined.left), client.top, std::min(client.right, combined.right),
                         client.bottom};
    // A window fully outside every work area horizontally leaves nothing reachable; fall back to the client so the
    // bands still exist rather than vanishing.
    return (reachable.right > reachable.left && reachable.bottom > reachable.top) ? reachable : client;
}

// Band placed against one edge of the reachable client area, in client coordinates. An empty rectangle means the band
// cannot be placed.
//
// `reachable` is the part of the client the pointer can actually visit: the client rectangle intersected with the
// display work area. For a window that fits on its monitor -- always the case for the Release fullscreen window --
// this is the whole client and the band hugs the true client edge. For a titled window larger than its monitor, which
// the Debug XENEON canvas can easily be, hugging the client edge would place the band past the side of the screen
// where no mouse can reach it, so the band follows the reachable edge instead.
[[nodiscard]] inline RECT PageEdgeBandRectIn(const RECT& reachable, int direction, UINT dpi) noexcept
{
    RECT band{};
    const LONG reachableWidth = reachable.right - reachable.left;
    const LONG reachableHeight = reachable.bottom - reachable.top;
    if (reachableWidth <= 0 || reachableHeight <= 0 ||
        (direction != kPageEdgeDirectionPrevious && direction != kPageEdgeDirectionNext))
    {
        return band;
    }
    const LONG width = PageEdgeBandWidthPixels(static_cast<UINT>(reachableWidth), dpi);
    if (width <= 0)
    {
        return band;
    }
    if (direction == kPageEdgeDirectionPrevious)
    {
        band = {reachable.left, reachable.top, reachable.left + width, reachable.bottom};
    }
    else
    {
        band = {reachable.right - width, reachable.top, reachable.right, reachable.bottom};
    }
    return band;
}

// Full-height band hugging one client edge, for a client that is entirely reachable.
[[nodiscard]] inline RECT PageEdgeBandRect(int direction, UINT clientWidth, UINT clientHeight, UINT dpi) noexcept
{
    if (clientWidth == 0 || clientHeight == 0)
    {
        return RECT{};
    }
    const RECT client{0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight)};
    return PageEdgeBandRectIn(client, direction, dpi);
}

// Host state that decides whether an edge band exists at all. Every field maps to state Application already tracks.
struct PageEdgeState final
{
    bool rendererReady = false;
    bool windowVisible = false;
    bool displayPoweredOn = false;
    bool rendererSuspended = true;
    bool rendererOccluded = false;
    bool widgetRaised = false;
    bool pointerNavigationActive = false;
    bool settleActive = false;
    // True while a neighbor is staged or a settle/promote is still holding the transition direction. Distinct from
    // settleActive so an interrupted settle cannot suppress both bands forever after the pointer has already gone.
    bool transitionStaged = false;
    bool wrapPages = false;
    bool atFirstPage = true;
    bool atLastPage = true;
    uint32_t pageCount = 0;

    bool operator==(const PageEdgeState&) const noexcept = default;
};

// A band is shown only when the host is composing a live multi-page dashboard and that direction has a neighbour.
// Blocked ends show no band, which reuses the same wrap and end-stop rule as a touch swipe.
[[nodiscard]] constexpr bool ShouldShowEdgeAffordance(const PageEdgeState& state, int direction) noexcept
{
    if (direction != kPageEdgeDirectionPrevious && direction != kPageEdgeDirectionNext)
    {
        return false;
    }
    if (!state.rendererReady || !state.windowVisible || !state.displayPoweredOn || state.rendererSuspended ||
        state.rendererOccluded)
    {
        return false;
    }
    if (state.widgetRaised || state.pointerNavigationActive || state.settleActive || state.transitionStaged)
    {
        return false;
    }
    if (state.pageCount < 2)
    {
        return false;
    }
    // PageSwipeBlocksDirection speaks in pointer offsets: a positive offset drags the current page right, which
    // returns to the previous page. Map the direction onto that convention so both input paths share one end-stop.
    const LONG offset = direction == kPageEdgeDirectionPrevious ? 1 : -1;
    return !PageSwipeBlocksDirection(offset, state.wrapPages, state.atFirstPage, state.atLastPage);
}

// Windows tags mouse messages synthesized from a touch or pen contact. Those belong to the WM_POINTER path.
[[nodiscard]] inline bool IsPointerSynthesizedMouseMessage() noexcept
{
    constexpr ULONG_PTR kPointerSignatureMask = 0xFFFFFF00;
    constexpr ULONG_PTR kPointerSignature = 0xFF515700;
    return (static_cast<ULONG_PTR>(GetMessageExtraInfo()) & kPointerSignatureMask) == kPointerSignature;
}

// Chevron glyph for one travel direction. Host chrome draws icons from Segoe Fluent Icons rather than hand-drawn
// shapes, so the arrow is hinted, scales with DPI, and matches the shell.
[[nodiscard]] constexpr wchar_t PageEdgeChevronGlyph(int direction, FluentIcons::IconFont font) noexcept
{
    if (direction == kPageEdgeDirectionPrevious)
    {
        return FluentIcons::SelectGlyph(font, FluentIcons::kChevronLeft, FluentIcons::kFallbackChevronLeft);
    }
    if (direction == kPageEdgeDirectionNext)
    {
        return FluentIcons::SelectGlyph(font, FluentIcons::kChevronRight, FluentIcons::kFallbackChevronRight);
    }
    return L'\0';
}

// Chevron cell centred in the band. The glyph is drawn centred inside it.
[[nodiscard]] inline RECT PageEdgeChevronCell(const RECT& band, UINT dpi) noexcept
{
    RECT cell{};
    const LONG bandWidth = band.right - band.left;
    const LONG bandHeight = band.bottom - band.top;
    if (bandWidth <= 0 || bandHeight <= 0)
    {
        return cell;
    }
    const LONG size =
        std::max(1L, std::min({PageEdgeDipPixels(kPageEdgeChevronHeightDips, dpi), bandWidth, bandHeight}));
    const LONG centreX = band.left + bandWidth / 2;
    const LONG centreY = band.top + bandHeight / 2;
    cell = {centreX - size / 2, centreY - size / 2, centreX + size / 2, centreY + size / 2};
    return cell;
}

[[nodiscard]] inline int PageEdgeChevronPixelHeight(UINT dpi) noexcept
{
    return static_cast<int>(std::max(1L, PageEdgeDipPixels(kPageEdgeChevronHeightDips, dpi)));
}

[[nodiscard]] constexpr bool PageEdgeBandContains(const RECT& band, POINT point) noexcept
{
    return band.right > band.left && band.bottom > band.top && point.x >= band.left && point.x < band.right &&
           point.y >= band.top && point.y < band.bottom;
}

// Parent-owned click fallback. A child created under the cursor often does not receive WM_SETCURSOR or
// WM_LBUTTONUP until the mouse moves, so the top-level window navigates when this predicate is true.
[[nodiscard]] constexpr bool PageEdgeClickNavigates(const PageEdgeState& state, int direction, const RECT& band,
                                                    POINT point) noexcept
{
    return ShouldShowEdgeAffordance(state, direction) && PageEdgeBandContains(band, point);
}

// Settle target for a click. The current page leaves toward the opposite side of the travel direction.
[[nodiscard]] constexpr LONG PageEdgeSettleTarget(int direction, LONG clientWidth) noexcept
{
    if (clientWidth <= 0 || (direction != kPageEdgeDirectionPrevious && direction != kPageEdgeDirectionNext))
    {
        return 0;
    }
    return -direction * clientWidth;
}
