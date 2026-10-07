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
inline constexpr uint32_t kDockMaximumAnimationMilliseconds = 1000;
inline constexpr uint32_t kDockDefaultAnimationMilliseconds = 200;
inline constexpr std::string_view kDockDefaultMonitor = "primary";
// The second screen: the first display in enumeration order that is not the primary (settings minor 3).
inline constexpr std::string_view kDockSecondaryMonitor = "secondary";

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

// The edge an ABE_* value names (the taskbar's `uEdge` from ABM_GETTASKBARPOS); None for anything else.
[[nodiscard]] constexpr DockEdge DockEdgeFromAppBarEdge(UINT appBarEdge) noexcept
{
    switch (appBarEdge)
    {
    case 0: // ABE_LEFT
        return DockEdge::Left;
    case 1: // ABE_TOP
        return DockEdge::Top;
    case 2: // ABE_RIGHT
        return DockEdge::Right;
    case 3: // ABE_BOTTOM
        return DockEdge::Bottom;
    default:
        return DockEdge::None;
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

// First start without a XENEON (Core_Settings.md "Cold load and recovery"): the installed bar takes the XENEON EDGE's
// 32:9 proportions along the work area, so the shipped 2560x720 pages keep the shape they were designed for, capped
// at half the monitor like every dock and kept in the settings range. The DIPs round down so the runtime rescale
// (DockThicknessPixels) stays within the pixels measured here and is not clamped. The one exception is a display
// under 64 DIPs across the edge, where even the 32-DIP settings minimum is more than half of it: the runtime clamps
// that bar (one dock-thickness-clamped record) like any other.
inline constexpr LONG kDockDesignLongSideDips = 2560;
inline constexpr LONG kDockDesignShortSideDips = 720;

[[nodiscard]] inline uint32_t DockFirstRunThicknessDips(const RECT& monitor, const RECT& work, DockEdge edge,
                                                        UINT dpi) noexcept
{
    const LONG span = DockEdgeIsHorizontal(edge) ? work.right - work.left : work.bottom - work.top;
    bool clamped = false;
    const LONG pixels = DockClampThickness(
        std::max(1L, static_cast<LONG>(MulDiv(span, kDockDesignShortSideDips, kDockDesignLongSideDips))), monitor, edge,
        clamped);
    const LONG scaleDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<LONG>(dpi);
    const LONG dips = pixels * USER_DEFAULT_SCREEN_DPI / scaleDpi;
    return static_cast<uint32_t>(
        std::clamp(dips, static_cast<LONG>(kDockMinimumThicknessDips), static_cast<LONG>(kDockMaximumThicknessDips)));
}

// First start without a XENEON: with more than one display the bar goes to the second screen (`secondary`, which
// keeps following whichever display is not the primary), otherwise to the primary.
[[nodiscard]] constexpr std::string_view DockFirstRunMonitor(size_t displayCount) noexcept
{
    return displayCount > 1 ? kDockSecondaryMonitor : kDockDefaultMonitor;
}

// First start without a XENEON: the bar takes the horizontal edge the taskbar leaves free on the bar's monitor, the
// top unless the top is taken and the bottom is not. Evidence of a taken edge, strongest first: the work area trimmed
// on that side (a taskbar that stays visible, any reserving app bar), then an autohide bar registered on that edge of
// that monitor (an auto-hiding taskbar). A monitor with neither, one without a taskbar of its own, follows the
// primary taskbar's edge (`taskbarEdge`, None when unknown), so the bar sits opposite the taskbar the person uses.
[[nodiscard]] constexpr DockEdge DockFirstRunEdge(const RECT& monitor, const RECT& work, bool autohideTop,
                                                  bool autohideBottom, DockEdge taskbarEdge) noexcept
{
    bool top = work.top > monitor.top || autohideTop;
    bool bottom = work.bottom < monitor.bottom || autohideBottom;
    if (!top && !bottom)
    {
        top = taskbarEdge == DockEdge::Top;
        bottom = taskbarEdge == DockEdge::Bottom;
    }
    return top && !bottom ? DockEdge::Bottom : DockEdge::Top;
}

// The autohide evidence for DockFirstRunEdge: the bar ABM_GETAUTOHIDEBAREX reports on an edge holds that edge only
// while its window exists. Explorer keeps the registration of a bar whose process crashed or was killed until another
// bar registers on the edge, and a first-run install after RedXe's own autohide bar died runs in exactly that gap. This
// is the one window-manager query in this header; a window that exists belongs to a live thread.
[[nodiscard]] inline bool DockAutohideBarHoldsEdge(HWND bar) noexcept
{
    return bar != nullptr && IsWindow(bar) != FALSE;
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

// The peek strip of an autohide bar in pixels. `peek` (1–64 physical pixels) and the thickness (32 DIPs and up) are
// validated apart, so the strip is clamped to the bar it belongs to: at least 1 pixel, at most the full bar. Every
// hidden window size, the slide, the grip, and MINMAXINFO use this one value.
[[nodiscard]] constexpr LONG DockClampPeek(uint32_t peekPixels, LONG fullPx) noexcept
{
    return std::clamp(static_cast<LONG>(peekPixels), 1L, std::max(1L, fullPx));
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

// Cross-axis size of a bar rectangle: its height for a top or bottom dock, its width for a side dock.
[[nodiscard]] constexpr LONG DockCrossPixels(const RECT& rect, DockEdge edge) noexcept
{
    return DockEdgeIsHorizontal(edge) ? rect.bottom - rect.top : rect.right - rect.left;
}

// The size the dashboard is laid out at, and so every host-side geometry built on it: tile bounds, hit tests, raises,
// page offsets, and accessibility and text-input rectangles. For a placed dock it is the full bar, whatever part of it
// the window shows (the peek strip while collapsed, a partly open bar while sliding), as for the swap chain; for the
// titled and fullscreen kinds, and a dock before its first placement, it is the client.
[[nodiscard]] constexpr SIZE DockDashboardCanvas(bool dockActive, const RECT& full, SIZE client) noexcept
{
    if (dockActive && full.right > full.left && full.bottom > full.top)
    {
        return SIZE{full.right - full.left, full.bottom - full.top};
    }
    return client;
}

// Autohide slide (UI_XeneonDisplayWindowing.md "Autohide"): over `animationMilliseconds` the window's visible
// thickness travels between the peek strip and the full bar, one SetWindowPos per presented frame (the window is
// DockHiddenRect of the full bar at the visible thickness). A slide covering part of the travel, such as one that
// reverses halfway, takes that share of the time; 0 is no animation (one step, as without a slide).
[[nodiscard]] constexpr uint32_t DockSlideDurationMilliseconds(uint32_t animationMilliseconds, LONG fromPx, LONG toPx,
                                                               LONG peekPx, LONG fullPx) noexcept
{
    const LONG travel = fullPx - peekPx;
    const LONG distance = std::min(fromPx > toPx ? fromPx - toPx : toPx - fromPx, travel);
    if (animationMilliseconds == 0 || travel <= 0 || distance <= 0)
    {
        return 0;
    }
    const uint64_t share = (static_cast<uint64_t>(animationMilliseconds) * static_cast<uint64_t>(distance) +
                            static_cast<uint64_t>(travel) / 2) /
                           static_cast<uint64_t>(travel);
    return static_cast<uint32_t>(std::max<uint64_t>(share, 1));
}

// The visible thickness at progress `t` (0..1) of a slide: a reveal eases out of the edge (fast, then settling at the
// full bar), a hide eases in (slow, then quickly into the edge).
[[nodiscard]] constexpr LONG DockSlideVisiblePixels(LONG fromPx, LONG toPx, float t, bool revealing) noexcept
{
    const float progress = t <= 0.0f ? 0.0f : (t >= 1.0f ? 1.0f : t);
    const float remaining = 1.0f - progress;
    const float eased = revealing ? 1.0f - remaining * remaining * remaining : progress * progress * progress;
    const float mixed = static_cast<float>(fromPx) + static_cast<float>(toPx - fromPx) * eased;
    return static_cast<LONG>(mixed >= 0.0f ? mixed + 0.5f : mixed - 0.5f);
}

// The dashboard translation for a visible thickness during a slide. DXGI_SCALING_NONE shows the back buffer's top-left,
// which already makes a bottom or right bar slide: its content follows the window's moving inner edge. A top or left
// bar is translated back by the part still hidden, so its inner edge leads out of the screen edge the same way.
[[nodiscard]] constexpr POINT DockSlideContentOffset(DockEdge edge, LONG fullPx, LONG visiblePx) noexcept
{
    const LONG hidden = fullPx > visiblePx ? fullPx - visiblePx : 0;
    switch (edge)
    {
    case DockEdge::Top:
        return POINT{0, -hidden};
    case DockEdge::Left:
        return POINT{-hidden, 0};
    default:
        return POINT{0, 0};
    }
}

// A press on a bar sliding in settles the slide first, and the settle moves the dashboard under a pointer that stays
// put: a top or left bar drops its translation, a bottom or right bar moves its window's (and so its client's) origin
// back by the part still hidden. Adding this shift to a client point taken after the settle gives the dashboard point
// that was on screen under it at `visiblePx`, so the press and the rest of its contact reach what the user aimed at.
[[nodiscard]] constexpr POINT DockSettleShift(DockEdge edge, LONG fullPx, LONG visiblePx) noexcept
{
    const LONG hidden = fullPx > visiblePx ? fullPx - visiblePx : 0;
    switch (edge)
    {
    case DockEdge::Top:
        return POINT{0, hidden};
    case DockEdge::Bottom:
        return POINT{0, -hidden};
    case DockEdge::Left:
        return POINT{hidden, 0};
    case DockEdge::Right:
        return POINT{-hidden, 0};
    default:
        return POINT{0, 0};
    }
}

// Where a press or a wheel on the dock goes (UI_XeneonDisplayWindowing.md "Autohide"). The strip and a bar sliding out
// show no dashboard to aim at: a press there only reveals the bar (the rest of its contact goes nowhere) and a wheel is
// dropped, so neither snaps a hiding bar shut nor scrolls or pages a widget nobody can see. A press on a bar sliding in
// settles the slide and keeps DockSettleShift for its contact; a wheel there reaches the tile under it as the frame
// shows it, without settling. Everything else is routed as on any window.
enum class DockInput : uint8_t
{
    Press = 0,
    Wheel,
};

enum class DockInputRoute : uint8_t
{
    Dashboard = 0,
    RevealOnly,
    Drop,
    SettleShifted,
};

// `showsStrip` is DockStateShowsStrip of an active autohide bar (false for a fixed bar and for the standard kinds).
[[nodiscard]] constexpr DockInputRoute DockRouteInput(DockInput input, bool showsStrip, bool slideActive) noexcept
{
    if (showsStrip)
    {
        return input == DockInput::Press ? DockInputRoute::RevealOnly : DockInputRoute::Drop;
    }
    return input == DockInput::Press && slideActive ? DockInputRoute::SettleShifted : DockInputRoute::Dashboard;
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

// Full-screen yield (UI_XeneonDisplayWindowing.md "Window"): ABN_FULLSCREENAPP names no monitor, so the bar steps
// beneath only a foreground window that covers the bar's own monitor. `windowBounds` is GetWindowRect, which includes
// the invisible resize borders: a maximized window with a caption overhangs a monitor whose work area is the whole
// monitor (no taskbar there, or an auto-hiding one) by about 8 px on every side, yet it is an ordinary maximized
// window, never a full-screen one. A captionless window that covers the monitor (a game, a video, a browser in full
// screen) counts, maximized or not, and so does one spanning several monitors.
[[nodiscard]] constexpr bool DockForegroundCoversMonitor(const RECT& windowBounds, const RECT& monitor,
                                                         bool maximizedWithCaption) noexcept
{
    return !maximizedWithCaption && monitor.right > monitor.left && monitor.bottom > monitor.top &&
           windowBounds.left <= monitor.left && windowBounds.top <= monitor.top &&
           windowBounds.right >= monitor.right && windowBounds.bottom >= monitor.bottom;
}

// Placement requests (UI_XeneonDisplayWindowing.md "Monitor and placement"). A placement makes cross-process shell
// calls and moves the window, and the UI thread dispatches sent messages while it waits on them, so a display,
// work-area, DPI, or app-bar change can ask for a placement while one runs. That request is recorded, never dropped:
// the running placement makes one more pass from the topology as it is then, so the last change wins without recursion.
// The passes are bounded so that a request each pass raises again cannot keep the UI thread placing.
inline constexpr uint32_t kDockMaximumExtraPlacementPasses = 2;

struct DockPlacementRequests final
{
    bool placing = false;
    // A request arrived during the running pass; `againResizes` when any of them asked the dashboard to follow.
    bool again = false;
    bool againResizes = false;
    uint32_t extraPasses = 0;
};

// A placement request. True when the caller runs the first pass now; false when a placement is running and the
// request was recorded for it.
[[nodiscard]] constexpr bool BeginDockPlacement(DockPlacementRequests& requests, bool resizeDashboard) noexcept
{
    if (requests.placing)
    {
        requests.again = true;
        requests.againResizes = requests.againResizes || resizeDashboard;
        return false;
    }
    requests = DockPlacementRequests{};
    requests.placing = true;
    return true;
}

// After a pass. True when a recorded request asks for one more pass, with `resizeDashboard` from those requests;
// false ends the placement, dropping a request recorded after the last allowed pass.
[[nodiscard]] constexpr bool NextDockPlacementPass(DockPlacementRequests& requests, bool& resizeDashboard) noexcept
{
    if (!requests.placing || !requests.again || requests.extraPasses >= kDockMaximumExtraPlacementPasses)
    {
        requests = DockPlacementRequests{};
        return false;
    }
    resizeDashboard = requests.againResizes;
    requests.again = false;
    requests.againResizes = false;
    ++requests.extraPasses;
    return true;
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
// SIZE_MAX only when there is no candidate at all. `index` selectors are 1-based in enumeration order; `secondary`
// is the first candidate in enumeration order that is not the primary.
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
    case Kind::Secondary:
        for (size_t index = 0; index < count; ++index)
        {
            if (!candidates[index].primary)
            {
                return index;
            }
        }
        break;
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
    // A page or widget change (a page.* or widget.* host action, an accessibility raise) while the strip shows: reveal
    // at once so the change is seen and laid out on the full bar. Unlike ActionShow it pins nothing: the change's own
    // hold (a page settle, a raise) keeps the bar, and the hide delay starts once no hold is left.
    PageOrWidgetChange,
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
    // A reveal still sliding out of the edge: the hide delay starts once the bar is all the way out.
    bool revealSliding = false;

    [[nodiscard]] constexpr bool Any() const noexcept
    {
        return AnyOther() || pinnedByAction || revealSliding;
    }
    // Holds other than the reveal's own slide and an action's pin: an action-revealed bar keeps its pin until one of
    // these appears.
    [[nodiscard]] constexpr bool AnyOther() const noexcept
    {
        return pointerInside || windowActive || captureActive || widgetRaised || dialogShown || pinned;
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
    case DockRevealEvent::PageOrWidgetChange:
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
