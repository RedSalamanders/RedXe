#pragma once

#include <algorithm>
#include <windows.h>

// The settings-error and action-notice windows (UI_XeneonDisplayWindowing.md "Notice windows"): one window of the
// RedXe.SettingsError class with a text control and an OK button, created by Application::CreateNoticeWindow. The
// placement is a pure function of rectangles so HostPluginTests proves it without a display topology or a window.

inline constexpr LONG kNoticeWindowWidth = 600;
inline constexpr LONG kNoticeWindowHeight = 280;
// The text control; the button is IDOK.
inline constexpr int kNoticeTextControlId = 100;

// A `width` x `height` notice centred on `anchor` (the RedXe window; the full bar for a dock) and then moved, and cut
// only when the work area is smaller, to lie inside `work` (the work area of the anchor's monitor): a bar along a
// screen edge, a collapsed autohide strip, or a window partly off its monitor never puts the caption, the text, or the
// OK button off-screen. An empty work area leaves the centred rectangle.
[[nodiscard]] constexpr RECT NoticeWindowRect(const RECT& anchor, const RECT& work, LONG width, LONG height) noexcept
{
    const bool haveWork = work.right > work.left && work.bottom > work.top;
    if (haveWork)
    {
        width = std::min(width, work.right - work.left);
        height = std::min(height, work.bottom - work.top);
    }
    LONG left = anchor.left + ((anchor.right - anchor.left) - width) / 2;
    LONG top = anchor.top + ((anchor.bottom - anchor.top) - height) / 2;
    if (haveWork)
    {
        left = std::clamp(left, work.left, work.right - width);
        top = std::clamp(top, work.top, work.bottom - height);
    }
    return RECT{left, top, left + width, top + height};
}

// The notice's controls, laid out in its client area (GetClientRect) when it is created and again on WM_SIZE and
// WM_DPICHANGED, because NoticeWindowRect can cut the window and the caption and frame take their share of it.
inline constexpr LONG kNoticeMarginX = 24;
inline constexpr LONG kNoticeMarginTop = 20;
inline constexpr LONG kNoticeMarginBottom = 20;
inline constexpr LONG kNoticeButtonWidth = 80;
inline constexpr LONG kNoticeButtonHeight = 28;
inline constexpr LONG kNoticeButtonGap = 12;
// The smallest text control the layout keeps: about one line of the dialog font.
inline constexpr LONG kNoticeMinimumTextHeight = 16;
// A client smaller than this is laid out as if it were this size: the text keeps one line and every control keeps its
// margins and a non-negative position, and what does not fit is clipped at the right and bottom edges.
inline constexpr LONG kNoticeMinimumClientWidth = 2 * kNoticeMarginX + kNoticeButtonWidth;
inline constexpr LONG kNoticeMinimumClientHeight =
    kNoticeMarginTop + kNoticeMinimumTextHeight + kNoticeButtonGap + kNoticeButtonHeight + kNoticeMarginBottom;

struct NoticeControlLayout final
{
    RECT text{};
    RECT button{};
};

// OK keeps its size in the bottom-right corner and the text fills the client above it, both inside the margins.
[[nodiscard]] constexpr NoticeControlLayout NoticeControlsForClient(LONG clientWidth, LONG clientHeight) noexcept
{
    const LONG width = std::max(clientWidth, kNoticeMinimumClientWidth);
    const LONG height = std::max(clientHeight, kNoticeMinimumClientHeight);
    NoticeControlLayout layout{};
    layout.button =
        RECT{width - kNoticeMarginX - kNoticeButtonWidth, height - kNoticeMarginBottom - kNoticeButtonHeight,
             width - kNoticeMarginX, height - kNoticeMarginBottom};
    layout.text = RECT{kNoticeMarginX, kNoticeMarginTop, width - kNoticeMarginX, layout.button.top - kNoticeButtonGap};
    return layout;
}
