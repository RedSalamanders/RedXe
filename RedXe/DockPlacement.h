#pragma once

#include "../Common/Actions/ActionTargets.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <windows.h>

// Screen-edge dock: RedXe as a bar along one edge of one monitor (UI_XeneonDisplayWindowing.md "Dock window kind").
// Everything here is a pure function of rectangles, integers, and host state so HostPluginTests proves placement,
// monitor selection, MINMAXINFO, and the autohide state machine without a display topology or a window.

enum class DockEdge : uint8_t
{
    None = 0,
    Top,
    Bottom,
    Left,
    Right,
};

enum class DockMode : uint8_t
{
    Fixed = 0,
    Autohide,
};

// Settings ranges and defaults (Core_Settings.md "dock"). Thickness is DIPs across the edge; peek is physical pixels
// because the strip is a screen-edge target whose size only affects how visible it is.
inline constexpr uint32_t kDockMinimumThicknessDips = 32;
inline constexpr uint32_t kDockMaximumThicknessDips = 1080;
inline constexpr uint32_t kDockDefaultThicknessDips = 180;
inline constexpr uint32_t kDockMinimumPeekPixels = 1;
inline constexpr uint32_t kDockMaximumPeekPixels = 64;
inline constexpr uint32_t kDockDefaultPeekPixels = 4;
inline constexpr uint32_t kDockMaximumRevealDelayMilliseconds = 2000;
inline constexpr uint32_t kDockDefaultRevealDelayMilliseconds = 150;
inline constexpr uint32_t kDockMaximumHideDelayMilliseconds = 10000;
inline constexpr uint32_t kDockDefaultHideDelayMilliseconds = 800;
inline constexpr std::string_view kDockDefaultMonitor = "primary";

[[nodiscard]] constexpr bool DockEdgeParse(std::string_view text, DockEdge& edge) noexcept
{
    if (text == "none")
        edge = DockEdge::None;
    else if (text == "top")
        edge = DockEdge::Top;
    else if (text == "bottom")
        edge = DockEdge::Bottom;
    else if (text == "left")
        edge = DockEdge::Left;
    else if (text == "right")
        edge = DockEdge::Right;
    else
        return false;
    return true;
}

[[nodiscard]] constexpr const char* DockEdgeName(DockEdge edge) noexcept
{
    switch (edge)
    {
    case DockEdge::Top:
        return "top";
    case DockEdge::Bottom:
        return "bottom";
    case DockEdge::Left:
        return "left";
    case DockEdge::Right:
        return "right";
    default:
        return "none";
    }
}

[[nodiscard]] constexpr bool DockModeParse(std::string_view text, DockMode& mode) noexcept
{
    if (text == "fixed")
        mode = DockMode::Fixed;
    else if (text == "autohide")
        mode = DockMode::Autohide;
    else
        return false;
    return true;
}

// A top or bottom bar runs along the horizontal edge; its cross axis (thickness) is vertical.
[[nodiscard]] constexpr bool DockEdgeIsHorizontal(DockEdge edge) noexcept
{
    return edge == DockEdge::Top || edge == DockEdge::Bottom;
}

// ABE_* value for SHAppBarMessage.
[[nodiscard]] constexpr UINT DockAppBarEdge(DockEdge edge) noexcept
{
    switch (edge)
    {
    case DockEdge::Top:
        return 1; // ABE_TOP
    case DockEdge::Bottom:
        return 3; // ABE_BOTTOM
    case DockEdge::Left:
        return 0; // ABE_LEFT
    case DockEdge::Right:
        return 2; // ABE_RIGHT
    default:
        return 3;
    }
}

[[nodiscard]] inline LONG DockThicknessPixels(uint32_t thicknessDips, UINT dpi) noexcept
{
    const int scaleDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi);
    return std::max(1L, static_cast<LONG>(MulDiv(static_cast<int>(thicknessDips), scaleDpi, USER_DEFAULT_SCREEN_DPI)));
}

// At least half of the monitor's cross dimension stays free. Returns the clamped thickness; `clamped` reports it.
[[nodiscard]] constexpr LONG DockClampThickness(LONG thicknessPx, const RECT& monitor, DockEdge edge,
                                                bool& clamped) noexcept
{
    const LONG cross = DockEdgeIsHorizontal(edge) ? monitor.bottom - monitor.top : monitor.right - monitor.left;
    const LONG limit = cross > 1 ? cross / 2 : 1;
    clamped = thicknessPx > limit;
    const LONG result = clamped ? limit : thicknessPx;
    return result < 1 ? 1 : result;
}

// `bounds` trimmed to `thicknessPx` on the edge side. Used for the proposal to the shell (bounds = monitor) and for
// the re-trim after ABM_QUERYPOS (bounds = the rectangle the shell returned).
[[nodiscard]] constexpr RECT DockTrimToThickness(const RECT& bounds, DockEdge edge, LONG thicknessPx) noexcept
{
    RECT rect = bounds;
    switch (edge)
    {
    case DockEdge::Top:
        rect.bottom = rect.top + thicknessPx;
        break;
    case DockEdge::Bottom:
        rect.top = rect.bottom - thicknessPx;
        break;
    case DockEdge::Left:
        rect.right = rect.left + thicknessPx;
        break;
    case DockEdge::Right:
        rect.left = rect.right - thicknessPx;
        break;
    default:
        break;
    }
    return rect;
}

// Overlay and autohide placement: hug the edge of the work area, spanning the work area along the edge, so the bar
// never covers the taskbar or another app bar.
[[nodiscard]] constexpr RECT DockOverlayRect(const RECT& workArea, DockEdge edge, LONG thicknessPx) noexcept
{
    return DockTrimToThickness(workArea, edge, thicknessPx);
}

// Autohide hidden rectangle: the outer `peekPx` of the full rectangle, same along-edge extent.
[[nodiscard]] constexpr RECT DockHiddenRect(const RECT& full, DockEdge edge, LONG peekPx) noexcept
{
    RECT rect = full;
    switch (edge)
    {
    case DockEdge::Top:
        rect.bottom = rect.top + peekPx;
        break;
    case DockEdge::Bottom:
        rect.top = rect.bottom - peekPx;
        break;
    case DockEdge::Left:
        rect.right = rect.left + peekPx;
        break;
    case DockEdge::Right:
        rect.left = rect.right - peekPx;
        break;
    default:
        break;
    }
    return rect;
}

// The part of the full-size back buffer that the hidden window shows. The dock swap chain uses DXGI_SCALING_NONE,
// which aligns the back buffer's top-left with the client's, so the visible strip is always rows 0..peek (top and
// bottom docks) or columns 0..peek (left and right docks) regardless of the edge.
[[nodiscard]] constexpr RECT DockGripRect(LONG fullWidth, LONG fullHeight, DockEdge edge, LONG peekPx) noexcept
{
    if (fullWidth <= 0 || fullHeight <= 0 || peekPx <= 0 || edge == DockEdge::None)
    {
        return RECT{};
    }
    if (DockEdgeIsHorizontal(edge))
    {
        return RECT{0, 0, fullWidth, std::min(peekPx, fullHeight)};
    }
    return RECT{0, 0, std::min(peekPx, fullWidth), fullHeight};
}

// The one-DIP accent line of the grip goes on the side of the strip that faces the desktop, so the user sees a line
// where the bar begins. With DXGI_SCALING_NONE the strip always shows the bar's first rows or columns, which sit at
// the screen edge for a top or left dock and against the desktop for a bottom or right dock; either way the
// desktop-facing side is the strip's inner side.
[[nodiscard]] constexpr RECT DockGripAccentRect(const RECT& grip, DockEdge edge, LONG accentPx) noexcept
{
    if (grip.right <= grip.left || grip.bottom <= grip.top || accentPx <= 0)
    {
        return RECT{};
    }
    RECT rect = grip;
    const LONG height = grip.bottom - grip.top;
    const LONG width = grip.right - grip.left;
    switch (edge)
    {
    case DockEdge::Top:
        rect.top = grip.bottom - std::min(accentPx, height);
        break;
    case DockEdge::Bottom:
        rect.bottom = grip.top + std::min(accentPx, height);
        break;
    case DockEdge::Left:
        rect.left = grip.right - std::min(accentPx, width);
        break;
    case DockEdge::Right:
        rect.right = grip.left + std::min(accentPx, width);
        break;
    default:
        return RECT{};
    }
    return rect;
}

// Drag-to-resize: the inner edge of the bar (the side facing the desktop) is a grip band; dragging it changes the
// thickness and the result is persisted as `dock.thickness`. The band is host-owned like the page edge bands, so a
// widget under it cannot be clicked there.
inline constexpr int kDockResizeBandDips = 6;

// The band in client coordinates of the full bar, or an empty rectangle when the client is too thin to hold it.
[[nodiscard]] constexpr RECT DockResizeBandRect(LONG clientWidth, LONG clientHeight, DockEdge edge,
                                                LONG bandPx) noexcept
{
    if (clientWidth <= 0 || clientHeight <= 0 || bandPx <= 0 || edge == DockEdge::None)
    {
        return RECT{};
    }
    const LONG band = std::min(bandPx, DockEdgeIsHorizontal(edge) ? clientHeight / 2 : clientWidth / 2);
    switch (edge)
    {
    case DockEdge::Top:
        return RECT{0, clientHeight - band, clientWidth, clientHeight};
    case DockEdge::Bottom:
        return RECT{0, 0, clientWidth, band};
    case DockEdge::Left:
        return RECT{clientWidth - band, 0, clientWidth, clientHeight};
    case DockEdge::Right:
        return RECT{0, 0, band, clientHeight};
    default:
        return RECT{};
    }
}

// Thickness in DIPs for a cursor at `screenPoint` while dragging the inner edge of `full` (screen coordinates), the
// outer edge staying put: the distance from the outer edge, converted from pixels at `dpi`, clamped to the settings
// range and to half of the monitor's cross dimension.
[[nodiscard]] inline uint32_t DockThicknessFromDrag(const RECT& full, const RECT& monitor, DockEdge edge,
                                                    POINT screenPoint, UINT dpi) noexcept
{
    LONG pixels = 0;
    switch (edge)
    {
    case DockEdge::Top:
        pixels = screenPoint.y - full.top;
        break;
    case DockEdge::Bottom:
        pixels = full.bottom - screenPoint.y;
        break;
    case DockEdge::Left:
        pixels = screenPoint.x - full.left;
        break;
    case DockEdge::Right:
        pixels = full.right - screenPoint.x;
        break;
    default:
        return kDockDefaultThicknessDips;
    }
    bool clamped = false;
    pixels = DockClampThickness(std::max(1L, pixels), monitor, edge, clamped);
    const int scaleDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi);
    const LONG dips = MulDiv(pixels, USER_DEFAULT_SCREEN_DPI, scaleDpi);
    return static_cast<uint32_t>(
        std::clamp(dips, static_cast<LONG>(kDockMinimumThicknessDips), static_cast<LONG>(kDockMaximumThicknessDips)));
}

// MINMAXINFO for the dock window. Windows applies ptMinTrackSize to SetWindowPos as well as to user tracking, so the
// titled window's 480×320 minimum would refuse the peek strip; the dock answers with the strip as its minimum and
// the monitor as its maximum.
constexpr void DockMinMaxInfo(const RECT& monitor, DockEdge edge, LONG peekPx, bool autohide, const RECT& full,
                              MINMAXINFO& info) noexcept
{
    const RECT smallest = autohide ? DockHiddenRect(full, edge, peekPx) : full;
    info.ptMinTrackSize =
        POINT{std::max(1L, smallest.right - smallest.left), std::max(1L, smallest.bottom - smallest.top)};
    info.ptMaxTrackSize = POINT{std::max(1L, monitor.right - monitor.left), std::max(1L, monitor.bottom - monitor.top)};
    info.ptMaxSize = info.ptMaxTrackSize;
}

// One candidate display for selection. `friendlyName` is the QueryDisplayConfig target name (what the user sees in
// Settings > Display), `deviceName` the GDI name (\\.\DISPLAYn); `name:<substring>` matches either.
struct DockMonitorCandidate final
{
    RECT monitor{};
    RECT work{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    bool primary = false;
    bool xeneon = false;
    std::wstring_view friendlyName;
    std::wstring_view deviceName;
};

[[nodiscard]] inline bool DockNameContains(std::wstring_view haystack, std::wstring_view needle) noexcept
{
    if (needle.empty() || haystack.size() < needle.size())
    {
        return false;
    }
    for (size_t offset = 0; offset + needle.size() <= haystack.size(); ++offset)
    {
        if (CompareStringOrdinal(haystack.data() + offset, static_cast<int>(needle.size()), needle.data(),
                                 static_cast<int>(needle.size()), TRUE) == CSTR_EQUAL)
        {
            return true;
        }
    }
    return false;
}

// Index of the candidate the selector names, else the primary (`fellBack` = true), else the first candidate.
// SIZE_MAX only when there is no candidate at all. `index` selectors are 1-based in enumeration order.
[[nodiscard]] inline size_t SelectDockMonitor(const RedXeActions::MonitorSelector& selector,
                                              std::wstring_view nameNeedle, const DockMonitorCandidate* candidates,
                                              size_t count, bool& fellBack) noexcept
{
    fellBack = false;
    if (!candidates || count == 0)
    {
        return SIZE_MAX;
    }
    size_t primary = 0;
    for (size_t index = 0; index < count; ++index)
    {
        if (candidates[index].primary)
        {
            primary = index;
            break;
        }
    }
    using Kind = RedXeActions::MonitorSelector::Kind;
    switch (selector.kind)
    {
    case Kind::Primary:
        return primary;
    case Kind::Xeneon:
        for (size_t index = 0; index < count; ++index)
        {
            if (candidates[index].xeneon)
            {
                return index;
            }
        }
        break;
    case Kind::Index:
        if (selector.index >= 1 && selector.index <= count)
        {
            return selector.index - 1;
        }
        break;
    case Kind::Name:
        for (size_t index = 0; index < count; ++index)
        {
            if (DockNameContains(candidates[index].friendlyName, nameNeedle) ||
                DockNameContains(candidates[index].deviceName, nameNeedle))
            {
                return index;
            }
        }
        break;
    default:
        break;
    }
    fellBack = true;
    return primary;
}

// Autohide state machine (UI_Dashboard.md "Autohide dock"). The window is the peek strip in Hidden and RevealPending
// and the full rectangle otherwise; the dashboard is visible only in Revealed and HidePending.
enum class DockRevealState : uint8_t
{
    Revealed = 0,
    HidePending,
    Hidden,
    RevealPending,
};

enum class DockRevealEvent : uint8_t
{
    // A real mouse move inside the strip while hidden (touch-synthesized mouse messages are not this event).
    PointerEnteredStrip = 0,
    // WM_MOUSELEAVE while the dwell runs.
    PointerLeft,
    DwellElapsed,
    HideElapsed,
    // A touch or pen contact on the strip, redxe.dock.show, or redxe.dock.toggle while hidden: reveal at once.
    TouchOnStrip,
    ActionShow,
    ActionHide,
    ActionToggle,
    // Any hold input changed; the decision re-reads `holds`.
    HoldsChanged,
    // A --screenshot run, or a live reload to `fixed`: revealed and kept there.
    Pin,
};

struct DockHolds final
{
    bool pointerInside = false;
    bool windowActive = false;
    // Pointer pan, settle, staged neighbour, interactive capture, or raise settle.
    bool captureActive = false;
    bool widgetRaised = false;
    bool dialogShown = false;
    // Screenshot pending or a fixed-mode / capture pin.
    bool pinned = false;
    // redxe.dock.show revealed the bar with nothing holding it: it stays until some other hold appears and clears.
    bool pinnedByAction = false;

    [[nodiscard]] constexpr bool Any() const noexcept
    {
        return pointerInside || windowActive || captureActive || widgetRaised || dialogShown || pinned ||
               pinnedByAction;
    }
    // Holds that also refuse redxe.dock.hide (the pointer merely being inside does not).
    [[nodiscard]] constexpr bool RefusesHide() const noexcept
    {
        return captureActive || widgetRaised || dialogShown || pinned;
    }
};

[[nodiscard]] constexpr bool DockStateShowsStrip(DockRevealState state) noexcept
{
    return state == DockRevealState::Hidden || state == DockRevealState::RevealPending;
}

[[nodiscard]] constexpr DockRevealState NextDockRevealState(DockRevealState current, DockRevealEvent event,
                                                            const DockHolds& holds, uint32_t revealDelayMilliseconds,
                                                            uint32_t hideDelayMilliseconds) noexcept
{
    const DockRevealState hideTarget =
        hideDelayMilliseconds == 0 ? DockRevealState::Hidden : DockRevealState::HidePending;
    const DockRevealState revealTarget =
        revealDelayMilliseconds == 0 ? DockRevealState::Revealed : DockRevealState::RevealPending;
    switch (event)
    {
    case DockRevealEvent::Pin:
        return DockRevealState::Revealed;
    case DockRevealEvent::PointerEnteredStrip:
        return current == DockRevealState::Hidden ? revealTarget : current;
    case DockRevealEvent::DwellElapsed:
        return current == DockRevealState::RevealPending ? DockRevealState::Revealed : current;
    case DockRevealEvent::PointerLeft:
        return current == DockRevealState::RevealPending ? DockRevealState::Hidden : current;
    case DockRevealEvent::TouchOnStrip:
    case DockRevealEvent::ActionShow:
        return DockRevealState::Revealed;
    case DockRevealEvent::ActionHide:
        if (DockStateShowsStrip(current))
            return DockRevealState::Hidden;
        return holds.RefusesHide() ? current : DockRevealState::Hidden;
    case DockRevealEvent::ActionToggle:
        if (DockStateShowsStrip(current))
            return DockRevealState::Revealed;
        return holds.RefusesHide() ? current : DockRevealState::Hidden;
    case DockRevealEvent::HideElapsed:
        return current == DockRevealState::HidePending ? DockRevealState::Hidden : current;
    case DockRevealEvent::HoldsChanged:
        if (current == DockRevealState::Revealed)
            return holds.Any() ? current : hideTarget;
        if (current == DockRevealState::HidePending)
            return holds.Any() ? DockRevealState::Revealed : current;
        return current;
    default:
        return current;
    }
}
