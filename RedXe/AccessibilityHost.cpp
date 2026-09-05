#include "AccessibilityHost.h"
#include "AccessibilityValidation.h"
#include "PlugInterfaces/FactoryImpl.h"
#include <UIAutomation.h>
#include <array>
#include <atomic>
#include <bit>
#include <new>
#include <utility>
#include <wil/result.h>

#pragma comment(lib, "uiautomationcore.lib")

namespace
{
constexpr size_t MaximumViews = 32;
std::atomic<uint64_t> identities{1};
uint64_t NextIdentity() noexcept
{
    return identities.fetch_add(1, std::memory_order_relaxed);
}
bool SamePlacement(const RedXeAccessibilityPlacement& a, const RedXeAccessibilityPlacement& b) noexcept
{
    return a.viewId == b.viewId && a.left == b.left && a.top == b.top && a.width == b.width && a.height == b.height &&
           a.keyboardFocused == b.keyboardFocused;
}
} // namespace

struct AccessibilityHost::State final
{
    struct Slot final
    {
        wil::com_ptr_nothrow<IRedXeAccessibilityWidget> widget;
        wil::com_ptr_nothrow<IRawElementProviderFragmentRoot> root;
        RedXeAccessibilityPlacement placement;
        bool attempted = false;
    };
    std::array<Slot, MaximumViews> slots;
    HWND window = nullptr;
    DWORD thread = GetCurrentThreadId();
    UINT message = RegisterWindowMessageW(L"RedXe.Accessibility.Pending.v1");
    uint64_t cookie = NextIdentity();
    uint64_t pending = 0;
    size_t focus = SIZE_MAX;
    bool posted = false;
    IRawElementProviderSimple* root = nullptr;
    bool Ready() const noexcept
    {
        return window && GetCurrentThreadId() == thread;
    }
    bool Matches(size_t index, uint64_t id) const noexcept
    {
        return Ready() && index < slots.size() && slots[index].widget && slots[index].placement.attachmentId == id;
    }
    HRESULT Queue(size_t index, uint64_t id, bool requestFocus) noexcept
    {
        if (!Matches(index, id))
            return UIA_E_ELEMENTNOTAVAILABLE;
        pending |= uint64_t{1} << index;
        if (requestFocus)
            focus = index;
        if (!posted)
        {
            if (!PostMessageW(window, message, static_cast<WPARAM>(cookie), 0))
                return HRESULT_FROM_WIN32(GetLastError());
            posted = true;
        }
        return S_OK;
    }
    void Remove(size_t index) noexcept
    {
        auto& slot = slots[index];
        auto widget = std::move(slot.widget);
        const auto view = slot.placement.viewId;
        slot.placement = {};
        slot.attempted = false;
        slot.root.reset();
        pending &= ~(uint64_t{1} << index);
        if (focus == index)
            focus = SIZE_MAX;
        if (widget)
            widget->DisconnectAccessibility(view);
    }
    HRESULT Neighbor(size_t index, NavigateDirection direction, IRawElementProviderFragment** result) noexcept
    {
        *result = nullptr;
        if (!Ready())
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (direction == NavigateDirection_Parent)
            return root ? root->QueryInterface(IID_PPV_ARGS(result)) : UIA_E_ELEMENTNOTAVAILABLE;
        const bool reverse = direction == NavigateDirection_PreviousSibling || direction == NavigateDirection_LastChild;
        const bool child = direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild;
        if (!child && direction != NavigateDirection_NextSibling && direction != NavigateDirection_PreviousSibling)
            return E_INVALIDARG;
        int cursor =
            child ? (reverse ? static_cast<int>(slots.size()) - 1 : 0) : static_cast<int>(index) + (reverse ? -1 : 1);
        for (; cursor >= 0 && cursor < static_cast<int>(slots.size()); cursor += reverse ? -1 : 1)
            if (slots[static_cast<size_t>(cursor)].root)
                return slots[static_cast<size_t>(cursor)].root.query_to(result);
        return S_OK;
    }
};

struct AccessibilityHost::Site final : RedXeComObject<Site, IRedXeAccessibilitySite>
{
    Site(const std::shared_ptr<State>& state, size_t index, uint64_t identity) noexcept
        : state(state), index(index), identity(identity)
    {
    }
    std::weak_ptr<State> state;
    size_t index;
    uint64_t identity;
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        const auto active = state.lock();
        if (!active || !active->Matches(index, identity))
            return UIA_E_ELEMENTNOTAVAILABLE;
        return active->Neighbor(index, direction, result);
    }
    HRESULT STDMETHODCALLTYPE GetFragmentRoot(IRawElementProviderFragmentRoot** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        const auto active = state.lock();
        if (!active || !active->Matches(index, identity) || !active->root)
            return UIA_E_ELEMENTNOTAVAILABLE;
        return active->root->QueryInterface(IID_PPV_ARGS(result));
    }
    HRESULT STDMETHODCALLTYPE RequestFocus() noexcept override
    {
        const auto active = state.lock();
        return active ? active->Queue(index, identity, true) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    void STDMETHODCALLTYPE ActionCompleted() noexcept override
    {
        if (const auto active = state.lock())
            (void)active->Queue(index, identity, false);
    }
};

struct AccessibilityHost::Root final
    : RedXeComObject<Root, IRawElementProviderSimple, IRawElementProviderFragment, IRawElementProviderFragmentRoot>
{
    explicit Root(std::shared_ptr<State> state) noexcept : state(std::move(state)) {}
    std::shared_ptr<State> state;
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading |
                                               ProviderOptions_ProviderOwnsSetFocus);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return state->Ready() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* result) noexcept override
    {
        if (!result)
            return E_POINTER;
        VariantInit(result);
        if (!state->Ready())
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_NamePropertyId)
        {
            result->bstrVal = SysAllocString(L"RedXe dashboard");
            if (!result->bstrVal)
                return E_OUTOFMEMORY;
            result->vt = VT_BSTR;
        }
        else if (id == UIA_ControlTypePropertyId)
        {
            result->vt = VT_I4;
            result->lVal = UIA_PaneControlTypeId;
        }
        else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId ||
                 id == UIA_IsKeyboardFocusablePropertyId)
        {
            result->vt = VT_BOOL;
            result->boolVal = VARIANT_TRUE;
        }
        else if (id == UIA_IsEnabledPropertyId)
        {
            result->vt = VT_BOOL;
            result->boolVal = IsWindowEnabled(state->window) ? VARIANT_TRUE : VARIANT_FALSE;
        }
        else if (id == UIA_HasKeyboardFocusPropertyId)
        {
            result->vt = VT_BOOL;
            result->boolVal = ::GetFocus() == state->window ? VARIANT_TRUE : VARIANT_FALSE;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return state->Ready() ? UiaHostProviderFromHwnd(state->window, result) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (!state->Ready())
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (direction != NavigateDirection_FirstChild && direction != NavigateDirection_LastChild)
            return S_OK;
        return state->Neighbor(0, direction, result);
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr; // The HWND host provider supplies the root runtime ID.
        return state->Ready() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = {};
        if (!state->Ready())
            return UIA_E_ELEMENTNOTAVAILABLE;
        RECT rect{};
        POINT origin{};
        if (!GetClientRect(state->window, &rect) || !ClientToScreen(state->window, &origin))
            return HRESULT_FROM_WIN32(GetLastError());
        *result = {static_cast<double>(origin.x), static_cast<double>(origin.y), static_cast<double>(rect.right),
                   static_cast<double>(rect.bottom)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr; // Widget providers are direct fragment children, not HWND-hosted nested fragments.
        return state->Ready() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() noexcept override
    {
        if (!state->Ready())
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (!IsWindowEnabled(state->window))
            return UIA_E_ELEMENTNOTENABLED;
        if (!IsWindowVisible(state->window))
            return UIA_E_ELEMENTNOTAVAILABLE;
        ::SetFocus(state->window);
        return ::GetFocus() == state->window ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return state->Ready() ? QueryInterface(IID_PPV_ARGS(result)) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
                                                       IRawElementProviderFragment** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (!state->Ready())
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (!std::isfinite(x) || !std::isfinite(y))
            return E_INVALIDARG;
        for (const auto& slot : state->slots)
        {
            const auto& p = slot.placement;
            if (slot.root && x >= p.left && x < p.left + p.width && y >= p.top && y < p.top + p.height)
            {
                const auto root = slot.root;
                const HRESULT hr = root->ElementProviderFromPoint(x, y, result);
                if (hr == S_OK && !*result)
                    return root.query_to(result);
                return hr;
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (!state->Ready())
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (::GetFocus() != state->window)
            return S_OK;
        for (const auto& slot : state->slots)
            if (slot.root && slot.placement.keyboardFocused)
            {
                const auto root = slot.root;
                return root->GetFocus(result);
            }
        return S_OK;
    }
};

HRESULT AccessibilityHost::Create(HWND window, std::unique_ptr<AccessibilityHost>& result) noexcept
{
    if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId())
        return E_INVALIDARG;
    APTTYPE apartment{};
    APTTYPEQUALIFIER qualifier{};
    RETURN_IF_FAILED(CoGetApartmentType(&apartment, &qualifier));
    if (apartment != APTTYPE_STA && apartment != APTTYPE_MAINSTA)
        return RPC_E_WRONG_THREAD;
    try
    {
        auto created = std::unique_ptr<AccessibilityHost>(new AccessibilityHost);
        created->_state = std::make_shared<State>();
        if (!created->_state->message)
            return HRESULT_FROM_WIN32(GetLastError());
        created->_state->window = window;
        created->_root.attach(new Root(created->_state));
        created->_state->root = created->_root.get();
        result = std::move(created);
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}
AccessibilityHost::~AccessibilityHost()
{
    Disconnect();
}
IRawElementProviderSimple* AccessibilityHost::Provider() const noexcept
{
    return _root.get();
}
void AccessibilityHost::ClearViews() noexcept
{
    if (!_state || !_state->Ready())
        return;
    bool changed = false;
    for (size_t i = 0; i < MaximumViews; ++i)
    {
        changed = changed || bool(_state->slots[i].root);
        _state->Remove(i);
    }
    if (changed && _root && UiaClientsAreListening())
        (void)UiaRaiseStructureChangedEvent(_root.get(), StructureChangeType_ChildrenInvalidated, nullptr, 0);
}
void AccessibilityHost::Disconnect() noexcept
{
    if (!_state || !_state->Ready())
        return;
    ClearViews();
    const HWND window = std::exchange(_state->window, nullptr);
    _state->root = nullptr;
    if (_root)
        (void)UiaDisconnectProvider(_root.get());
    (void)UiaReturnRawElementProvider(window, 0, 0, nullptr);
    _root.reset();
}
HRESULT AccessibilityHost::Update(std::span<const AccessibleWidgetView> views) noexcept
{
    if (!_state || !_state->Ready())
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (views.size() > MaximumViews)
        return E_INVALIDARG;
    uint64_t present = 0;
    for (const auto& item : views)
    {
        if (item.index >= MaximumViews || !item.widget || item.viewId > 1 || (present & (uint64_t{1} << item.index)))
            return E_INVALIDARG;
        present |= uint64_t{1} << item.index;
        if (item.screenBounds.right <= item.screenBounds.left || item.screenBounds.bottom <= item.screenBounds.top)
            return E_INVALIDARG;
    }
    bool structure = false;
    for (size_t i = 0; i < MaximumViews; ++i)
        if (!(present & (uint64_t{1} << i)) && _state->slots[i].widget)
        {
            structure = true;
            _state->Remove(i);
        }
    for (const auto& item : views)
    {
        auto& slot = _state->slots[item.index];
        RedXeAccessibilityPlacement placement;
        placement.viewId = item.viewId;
        placement.attachmentId = slot.placement.attachmentId;
        placement.left = item.screenBounds.left;
        placement.top = item.screenBounds.top;
        placement.width = static_cast<double>(item.screenBounds.right) - item.screenBounds.left;
        placement.height = static_cast<double>(item.screenBounds.bottom) - item.screenBounds.top;
        placement.keyboardFocused = item.keyboardFocused ? TRUE : FALSE;
        if (slot.widget.get() != item.widget || slot.placement.viewId != item.viewId)
        {
            structure = structure || bool(slot.root);
            _state->Remove(item.index);
            slot.widget = item.widget;
        }
        if (slot.root && (item.prepared || !SamePlacement(placement, slot.placement)))
        {
            const HRESULT updated = slot.widget->UpdateAccessibility(&placement);
            if (updated != S_OK)
            {
                structure = true;
                _state->Remove(item.index);
                slot.widget = item.widget;
            }
            else
                slot.placement = placement;
        }
        if (!slot.root)
        {
            if (slot.attempted && !item.prepared && SamePlacement(placement, slot.placement))
                continue;
            placement.attachmentId = NextIdentity();
            slot.placement = placement;
            slot.attempted = true;
            wil::com_ptr_nothrow<IRedXeAccessibilitySite> site;
            site.attach(new (std::nothrow) Site(_state, item.index, placement.attachmentId));
            if (!site)
                return E_OUTOFMEMORY;
            const HRESULT connected = slot.widget->ConnectAccessibility(&placement, site.get(), slot.root.put());
            if (connected == S_OK && slot.root)
                structure = true;
            // An unavailable widget cannot suppress accessible siblings or create an idle retry loop.
            else
                slot.root.reset();
        }
    }
    if (structure && _root && UiaClientsAreListening())
        (void)UiaRaiseStructureChangedEvent(_root.get(), StructureChangeType_ChildrenInvalidated, nullptr, 0);
    return S_OK;
}
bool AccessibilityHost::IsMessage(UINT message, WPARAM cookie) noexcept
{
    if (!_state || !_state->Ready() || message != _state->message || cookie != _state->cookie)
        return false;
    _state->posted = false;
    return true;
}
bool AccessibilityHost::TakeRequest(AccessibilityRequest& request) noexcept
{
    request = {};
    if (!_state || !_state->Ready() || !_state->pending)
        return false;
    const auto index = static_cast<size_t>(std::countr_zero(_state->pending));
    _state->pending &= ~(uint64_t{1} << index);
    const auto& slot = _state->slots[index];
    request.index = index;
    request.viewId = slot.placement.viewId;
    request.focus = _state->focus == index;
    if (request.focus)
        _state->focus = SIZE_MAX;
    if (slot.widget)
    {
        const auto widget = slot.widget;
        (void)widget->TakeAccessibilityAction(request.viewId, &request.action);
        if (request.action != S_OK && request.action != RedXePointerRaise && request.action != RedXePointerDismiss)
            request.action = S_OK;
    }
    return true;
}
