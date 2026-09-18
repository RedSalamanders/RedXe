#pragma once

// The local path of the Zoom service: the Zoom Workplace client's own meeting controls, reached through the
// accessibility tree it exposes for screen readers (MSAA, IAccessible). Zoom names each toolbar button with its
// state ("Unmute, currently muted, …", "Start my video, …") and performs the button's default action on request,
// so this path reads the state and presses the control without a keystroke, a focus change, a Marketplace app, or
// a token. It is what a managed account that refuses the app falls back to. The service (ZoomService) chooses
// between this path and the Plugin SDK session; this file only finds the window, reads the toolbar, and presses.
//
// Zoom's meeting window does not expose these controls through UI Automation (its tree stops at the panes), which
// is why oleacc is used directly. Pinned against Zoom Workplace 7.1.5 on 2026-09-18.

#include "ZoomSettings.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

#include <oleacc.h>

namespace Zoom::Local
{
// Window classes of Zoom Workplace: the in-meeting window (7.x), its older name, the floating mini video window,
// and the child panel that holds the meeting toolbar.
inline constexpr char kMeetingWindowClass[] = "ConfMultiTabContentWndClass";
inline constexpr char kLegacyMeetingWindowClass[] = "ZPContentViewWndClass";
inline constexpr char kFloatingWindowClass[] = "ZPFloatVideoWndClass";
inline constexpr wchar_t kToolbarPanelClass[] = L"ZPControlPanelClass";
inline constexpr char kZoomExecutable[] = "Zoom.exe";

// The visible Zoom meeting window, else the floating video window, else null (not in a meeting).
[[nodiscard]] HWND FindMeetingWindow() noexcept;

inline constexpr uint32_t kMaximumButtons = 64;
inline constexpr uint32_t kMaximumNameCharacters = 256;
inline constexpr uint32_t kMaximumNodes = 512;
inline constexpr uint32_t kMaximumDepth = 8;

// One visible push or split button of the meeting toolbar, with the accessible object that presses it.
struct Button final
{
    std::array<wchar_t, kMaximumNameCharacters> name{};
    wil::com_ptr_nothrow<IAccessible> object;
    // CHILDID_SELF for a full object, else the simple child id within `object`.
    LONG child = 0;
};

struct Toolbar final
{
    std::array<Button, kMaximumButtons> buttons{};
    uint32_t count = 0;
};

// Reads the visible buttons of window's meeting toolbar (the ZPControlPanelClass child when present, else the
// window's own client object), walking at most kMaximumNodes accessible nodes kMaximumDepth deep. S_FALSE when
// nothing accessible was found.
[[nodiscard]] HRESULT ReadToolbar(HWND window, Toolbar& toolbar) noexcept;

enum class Tri : uint8_t
{
    Unknown = 0,
    No,
    Yes,
};

struct MeetingState final
{
    Tri muted = Tri::Unknown;
    Tri videoOn = Tri::Unknown;
    Tri handRaised = Tri::Unknown;
    uint32_t buttons = 0;
};

// The states the labels can tell from the toolbar (case-insensitive substring match against every button name;
// the "yes" label wins when both match). A state whose labels appear nowhere stays Unknown.
void ReadState(const Toolbar& toolbar, const Settings& settings, MeetingState& state) noexcept;

// The index of the first button whose name contains label (case-insensitive), or UINT32_MAX.
[[nodiscard]] uint32_t FindButton(const Toolbar& toolbar, std::string_view label) noexcept;

// Performs the button's default action (what a click does).
[[nodiscard]] HRESULT Press(const Toolbar& toolbar, uint32_t index) noexcept;

// Whether target is a state request (on/off) the local path must check before pressing, as opposed to toggle.
[[nodiscard]] bool WantsState(std::string_view verb, std::string_view target, bool& wanted) noexcept;

// The state a verb toggles, from the read snapshot.
[[nodiscard]] Tri StateFor(std::string_view verb, const MeetingState& state) noexcept;

// zoommtg://zoom.us/join?confno=<number>[&pwd=<passcode>] for the Zoom client's URL handler.
[[nodiscard]] bool BuildJoinUri(uint64_t meetingNumber, std::string_view passcode, char* out, size_t capacity) noexcept;
} // namespace Zoom::Local
