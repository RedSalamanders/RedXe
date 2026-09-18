#include "ZoomLocal.h"

#include "Actions/WindowSelector.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace Zoom::Local
{
namespace
{
using WideLabel = std::array<wchar_t, kMaximumLabelBytes + 1>;

// Lower-cased UTF-16 copy of a UTF-8 label; empty when the label is empty or invalid.
void LowerWide(std::string_view text, WideLabel& out) noexcept
{
    out.fill(L'\0');
    if (text.empty())
    {
        return;
    }
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                            out.data(), static_cast<int>(out.size() - 1));
    if (written <= 0)
    {
        out.fill(L'\0');
        return;
    }
    (void)CharLowerBuffW(out.data(), static_cast<DWORD>(written));
}

[[nodiscard]] bool NameContains(const Button& button, const WideLabel& needleLower) noexcept
{
    if (needleLower[0] == L'\0')
    {
        return false;
    }
    std::array<wchar_t, kMaximumNameCharacters> lower = button.name;
    (void)CharLowerBuffW(lower.data(), static_cast<DWORD>(wcsnlen_s(lower.data(), lower.size())));
    return wcsstr(lower.data(), needleLower.data()) != nullptr;
}

struct Walk final
{
    Toolbar* toolbar;
    uint32_t nodes;
};

// Collects the visible push and split buttons under `acc` (depth-first, bounded), keeping the accessible object
// that performs each one's default action.
void Collect(IAccessible* acc, Walk& walk, uint32_t depth) noexcept
{
    if (!acc || depth > kMaximumDepth || walk.nodes >= kMaximumNodes || walk.toolbar->count >= kMaximumButtons)
    {
        return;
    }
    long count = 0;
    if (FAILED(acc->get_accChildCount(&count)) || count <= 0 || count > static_cast<long>(kMaximumNodes))
    {
        return;
    }
    std::array<VARIANT, 64> children{};
    long offset = 0;
    while (offset < count && walk.nodes < kMaximumNodes && walk.toolbar->count < kMaximumButtons)
    {
        const long batch = static_cast<long>(std::min<long>(count - offset, static_cast<long>(children.size())));
        long obtained = 0;
        if (AccessibleChildren(acc, offset, batch, children.data(), &obtained) != S_OK || obtained <= 0)
        {
            return;
        }
        offset += obtained;
        for (long index = 0; index < obtained; ++index)
        {
            VARIANT& child = children[static_cast<size_t>(index)];
            ++walk.nodes;
            wil::com_ptr_nothrow<IAccessible> object;
            LONG childId = CHILDID_SELF;
            IAccessible* roleSource = acc;
            VARIANT self;
            self.vt = VT_I4;
            self.lVal = CHILDID_SELF;
            VARIANT id = self;
            if (child.vt == VT_DISPATCH && child.pdispVal)
            {
                (void)child.pdispVal->QueryInterface(IID_IAccessible, object.put_void());
                if (object)
                {
                    roleSource = object.get();
                }
            }
            else if (child.vt == VT_I4)
            {
                childId = child.lVal;
                id.lVal = childId;
            }
            if (roleSource)
            {
                VARIANT role;
                VariantInit(&role);
                VARIANT state;
                VariantInit(&state);
                (void)roleSource->get_accRole(id, &role);
                (void)roleSource->get_accState(id, &state);
                const bool button =
                    role.vt == VT_I4 && (role.lVal == ROLE_SYSTEM_PUSHBUTTON || role.lVal == ROLE_SYSTEM_SPLITBUTTON);
                const bool visible = state.vt != VT_I4 || (state.lVal & STATE_SYSTEM_INVISIBLE) == 0;
                if (button && visible && walk.toolbar->count < kMaximumButtons)
                {
                    wil::unique_bstr name;
                    if (SUCCEEDED(roleSource->get_accName(id, name.put())) && name && name.get()[0] != L'\0')
                    {
                        Button& entry = walk.toolbar->buttons[walk.toolbar->count];
                        entry.name.fill(L'\0');
                        (void)wcsncpy_s(entry.name.data(), entry.name.size(), name.get(), _TRUNCATE);
                        if (object)
                        {
                            entry.object = object;
                            entry.child = CHILDID_SELF;
                        }
                        else
                        {
                            entry.object = acc;
                            entry.child = childId;
                        }
                        ++walk.toolbar->count;
                    }
                }
                VariantClear(&role);
                VariantClear(&state);
                if (object)
                {
                    Collect(object.get(), walk, depth + 1);
                }
            }
            VariantClear(&child);
        }
    }
}
} // namespace

HWND FindMeetingWindow() noexcept
{
    RedXeActions::SelectedWindows selected{};
    RedXeActions::WindowSelector selector{};
    selector.kind = RedXeActions::WindowSelector::Kind::Class;
    for (const char* windowClass : {kMeetingWindowClass, kLegacyMeetingWindowClass, kFloatingWindowClass})
    {
        selector.value = windowClass;
        if (RedXeActions::SelectWindows(selector, false, selected))
        {
            return selected.windows[0];
        }
    }
    return nullptr;
}

HRESULT ReadToolbar(HWND window, Toolbar& toolbar) noexcept
{
    toolbar = Toolbar{};
    if (!window || !IsWindow(window))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    }
    // The meeting toolbar lives in the ZPControlPanelClass child; a window without one (the test stand-in) is read
    // itself.
    HWND panel = nullptr;
    (void)EnumChildWindows(
        window,
        [](HWND child, LPARAM data) noexcept -> BOOL
        {
            std::array<wchar_t, 64> className{};
            if (GetClassNameW(child, className.data(), static_cast<int>(className.size())) > 0 &&
                wcscmp(className.data(), kToolbarPanelClass) == 0)
            {
                *reinterpret_cast<HWND*>(data) = child;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&panel));
    wil::com_ptr_nothrow<IAccessible> root;
    const HRESULT acquired = AccessibleObjectFromWindow(panel ? panel : window, static_cast<DWORD>(OBJID_CLIENT),
                                                        IID_IAccessible, root.put_void());
    if (FAILED(acquired) || !root)
    {
        return FAILED(acquired) ? acquired : E_FAIL;
    }
    Walk walk{&toolbar, 0};
    Collect(root.get(), walk, 0);
    return toolbar.count != 0 ? S_OK : S_FALSE;
}

void ReadState(const Toolbar& toolbar, const Settings& settings, MeetingState& state) noexcept
{
    state = MeetingState{};
    state.buttons = toolbar.count;
    struct Pair final
    {
        Label yes;
        Label no;
        Tri MeetingState::* field;
    };
    static constexpr std::array<Pair, 3> kPairs{
        Pair{Label::Muted, Label::Unmuted, &MeetingState::muted},
        Pair{Label::VideoOn, Label::VideoOff, &MeetingState::videoOn},
        Pair{Label::HandRaised, Label::HandLowered, &MeetingState::handRaised},
    };
    for (const Pair& pair : kPairs)
    {
        WideLabel yes{};
        WideLabel no{};
        LowerWide(settings.LabelText(pair.yes), yes);
        LowerWide(settings.LabelText(pair.no), no);
        bool sawYes = false;
        bool sawNo = false;
        for (uint32_t index = 0; index < toolbar.count; ++index)
        {
            sawYes = sawYes || NameContains(toolbar.buttons[index], yes);
            sawNo = sawNo || NameContains(toolbar.buttons[index], no);
        }
        // The "yes" label wins when a name carries both.
        state.*pair.field = sawYes ? Tri::Yes : (sawNo ? Tri::No : Tri::Unknown);
    }
}

uint32_t FindButton(const Toolbar& toolbar, std::string_view label) noexcept
{
    WideLabel needle{};
    LowerWide(label, needle);
    for (uint32_t index = 0; index < toolbar.count; ++index)
    {
        if (NameContains(toolbar.buttons[index], needle))
        {
            return index;
        }
    }
    return UINT32_MAX;
}

HRESULT Press(const Toolbar& toolbar, uint32_t index) noexcept
{
    if (index >= toolbar.count || !toolbar.buttons[index].object)
    {
        return E_INVALIDARG;
    }
    VARIANT child;
    child.vt = VT_I4;
    child.lVal = toolbar.buttons[index].child;
    return toolbar.buttons[index].object->accDoDefaultAction(child);
}

bool WantsState(std::string_view verb, std::string_view target, bool& wanted) noexcept
{
    if (verb == "mute" || verb == "video" || verb == "raiseHand")
    {
        if (target == "on")
        {
            wanted = true;
            return true;
        }
        if (target == "off")
        {
            wanted = false;
            return true;
        }
    }
    return false;
}

Tri StateFor(std::string_view verb, const MeetingState& state) noexcept
{
    if (verb == "mute")
    {
        return state.muted;
    }
    if (verb == "video")
    {
        return state.videoOn;
    }
    if (verb == "raiseHand")
    {
        return state.handRaised;
    }
    return Tri::Unknown;
}

bool BuildJoinUri(uint64_t meetingNumber, std::string_view passcode, char* out, size_t capacity) noexcept
{
    if (!out || capacity == 0 || meetingNumber == 0)
    {
        return false;
    }
    for (const char character : passcode)
    {
        // The passcode rides in a URI: keep it to the unreserved set so nothing needs escaping.
        const bool safe = (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
                          (character >= 'A' && character <= 'Z') || character == '-' || character == '_' ||
                          character == '.' || character == '~';
        if (!safe)
        {
            return false;
        }
    }
    const int written = passcode.empty() ? std::snprintf(out, capacity, "zoommtg://zoom.us/join?confno=%llu",
                                                         static_cast<unsigned long long>(meetingNumber))
                                         : std::snprintf(out, capacity, "zoommtg://zoom.us/join?confno=%llu&pwd=%.*s",
                                                         static_cast<unsigned long long>(meetingNumber),
                                                         static_cast<int>(passcode.size()), passcode.data());
    return written > 0 && static_cast<size_t>(written) < capacity;
}
} // namespace Zoom::Local
