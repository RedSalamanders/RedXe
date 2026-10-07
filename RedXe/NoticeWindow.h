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
