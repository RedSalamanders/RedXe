#include "../../RedXe/AccessibilityHost.h"
#include "AccessibilityValidation.h"
#include "PlugInterfaces/FactoryImpl.h"
#include <UIAutomation.h>
#include <array>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <wil/resource.h>
#include <wil/result.h>

namespace
{
class Fragment final : public RedXeComObject<Fragment, IRawElementProviderFragmentRoot, IRawElementProviderFragment>
{
  public:
    bool alive = true;
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double, double,
                                                       IRawElementProviderFragment** result) noexcept override
    {
        return GetFocus(result);
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return alive ? QueryInterface(IID_PPV_ARGS(result)) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection, IRawElementProviderFragment** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return alive ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return alive ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = {};
        return alive ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return alive ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() noexcept override
    {
        return alive ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return alive ? QueryInterface(IID_PPV_ARGS(result)) : UIA_E_ELEMENTNOTAVAILABLE;
    }
};
class AccessibleWidget final : public RedXeComObject<AccessibleWidget, IRedXeAccessibilityWidget>
{
  public:
    wil::com_ptr_nothrow<IRedXeAccessibilitySite> site;
    wil::com_ptr_nothrow<Fragment> fragment;
    RedXeAccessibilityPlacement placement;
    uint32_t connects = 0, updates = 0, disconnects = 0, actions = 0;
    HRESULT available = S_OK, nextAction = S_OK;
    bool replace = false;
    HRESULT STDMETHODCALLTYPE ConnectAccessibility(const RedXeAccessibilityPlacement* input,
                                                   IRedXeAccessibilitySite* supplied,
                                                   IRawElementProviderFragmentRoot** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        ++connects;
        if (!input || !supplied || !IsValidRedXeAccessibilityPlacement(*input))
            return E_INVALIDARG;
        if (available != S_OK)
            return available;
        placement = *input;
        site = supplied;
        fragment.attach(new (std::nothrow) Fragment);
        return fragment ? fragment.query_to(result) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE UpdateAccessibility(const RedXeAccessibilityPlacement* input) noexcept override
    {
        ++updates;
        if (!input || !IsValidRedXeAccessibilityPlacement(*input))
            return E_INVALIDARG;
        if (std::exchange(replace, false))
            return S_FALSE;
        placement = *input;
        return S_OK;
    }
    void STDMETHODCALLTYPE DisconnectAccessibility(uint32_t) noexcept override
    {
        ++disconnects;
        if (fragment)
            fragment->alive = false;
        fragment.reset();
        site.reset();
    }
    HRESULT STDMETHODCALLTYPE TakeAccessibilityAction(uint32_t view, HRESULT* result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = S_OK;
        if (view != placement.viewId)
            return E_INVALIDARG;
        ++actions;
        *result = std::exchange(nextAction, S_OK);
        return S_OK;
    }
};
} // namespace

uint32_t RunAccessibilityHostTests()
{
    uint32_t checks = 0;
    const auto check = [&](bool condition, const char* reason)
    {
        ++checks;
        if (!condition)
            throw std::runtime_error(reason);
    };
    check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "accessibility fixture COM STA");
    const auto apartment = wil::scope_exit([] { CoUninitialize(); });
    // Hidden private HWND: no desktop focus, screen reader, audio, camera or global settings are changed.
    wil::unique_hwnd window(CreateWindowExW(0, L"STATIC", L"RedXe UIA fixture", WS_POPUP, -120, 60, 640, 360, nullptr,
                                            nullptr, GetModuleHandleW(nullptr), nullptr));
    check(bool(window), "accessibility fixture window");
    std::unique_ptr<AccessibilityHost> host;
    check(AccessibilityHost::Create(nullptr, host) == E_INVALIDARG && !host, "UIA rejects missing HWND");
    check(AccessibilityHost::Create(window.get(), host) == S_OK && host->Provider(), "UIA lazy application root");
    wil::com_ptr_nothrow<IRawElementProviderSimple> heldRoot = host->Provider();
    wil::com_ptr_nothrow<IRawElementProviderFragment> root;
    wil::com_ptr_nothrow<IRawElementProviderFragmentRoot> pointRoot;
    check(heldRoot.query_to(root.put()) == S_OK && heldRoot.query_to(pointRoot.put()) == S_OK, "UIA root interfaces");
    check(root->SetFocus() == UIA_E_ELEMENTNOTAVAILABLE, "UIA hidden root cannot claim focus");
    EnableWindow(window.get(), FALSE);
    VARIANT enabled{};
    check(heldRoot->GetPropertyValue(UIA_IsEnabledPropertyId, &enabled) == S_OK && enabled.vt == VT_BOOL &&
              enabled.boolVal == VARIANT_FALSE && root->SetFocus() == UIA_E_ELEMENTNOTENABLED,
          "UIA disabled application root reports disabled and rejects focus");
    EnableWindow(window.get(), TRUE);
    wil::com_ptr_nothrow<AccessibleWidget> first, second, missing;
    first.attach(new AccessibleWidget);
    second.attach(new AccessibleWidget);
    missing.attach(new AccessibleWidget);
    missing->available = S_FALSE;
    std::array views{AccessibleWidgetView{2, first.get(), 0, {-120, 60, 40, 240}, false, true},
                     AccessibleWidgetView{5, second.get(), 0, {40, 60, 200, 240}, false, true},
                     AccessibleWidgetView{8, missing.get(), 0, {200, 60, 360, 240}, false, true}};
    check(host->Update(views) == S_OK && first->connects == 1 && second->connects == 1 && missing->connects == 1,
          "UIA unavailable widget does not suppress ready siblings");
    const auto attachment = first->placement.attachmentId;
    check(attachment != 0 && attachment != second->placement.attachmentId, "UIA attachment identities are unique");
    for (auto& view : views)
        view.prepared = false;
    for (size_t i = 0; i < 1000; ++i)
        check(host->Update(views) == S_OK, "UIA clean frame update");
    check(first->updates == 0 && first->connects == 1 && missing->connects == 1,
          "UIA clean frames neither rebuild nor retry unavailable widgets");
    auto invalid = views;
    invalid[1].index = invalid[0].index;
    check(host->Update(invalid) == E_INVALIDARG && first->connects == 1, "UIA duplicate view rejected atomically");
    invalid = views;
    invalid[0].screenBounds.right = invalid[0].screenBounds.left;
    check(host->Update(invalid) == E_INVALIDARG, "UIA rejects empty bounds");
    wil::com_ptr_nothrow<IRawElementProviderFragment> found;
    check(root->Navigate(NavigateDirection_FirstChild, found.put()) == S_OK &&
              found.get() == static_cast<IRawElementProviderFragment*>(first->fragment.get()),
          "UIA root child order follows dashboard slot order");
    found.reset();
    check(first->site->Navigate(NavigateDirection_NextSibling, found.put()) == S_OK &&
              found.get() == static_cast<IRawElementProviderFragment*>(second->fragment.get()),
          "UIA sibling navigation skips absent slots");
    found.reset();
    check(first->site->Navigate(NavigateDirection_Parent, found.put()) == S_OK && found.get() == root.get(),
          "UIA widget parent is dashboard");
    found.reset();
    check(pointRoot->ElementProviderFromPoint(-100, 80, found.put()) == S_OK &&
              found.get() == static_cast<IRawElementProviderFragment*>(first->fragment.get()),
          "UIA screen hit testing handles negative monitor origin");
    found.reset();
    check(pointRoot->ElementProviderFromPoint(std::numeric_limits<double>::quiet_NaN(), 80, found.put()) ==
                  E_INVALIDARG &&
              !found,
          "UIA screen hit testing rejects NaN");
    const UINT message = RegisterWindowMessageW(L"RedXe.Accessibility.Pending.v1");
    first->nextAction = RedXePointerRaise;
    for (size_t i = 0; i < 20; ++i)
    {
        check(first->site->RequestFocus() == S_OK, "UIA focus request queued");
        first->site->ActionCompleted();
    }
    MSG posted{};
    check(PeekMessageW(&posted, window.get(), message, message, PM_REMOVE) != FALSE, "UIA action posts app message");
    const WPARAM cookie = posted.wParam;
    check(!host->IsMessage(message, cookie + 1) && host->IsMessage(message, cookie),
          "UIA dispatch validates host generation");
    check(!PeekMessageW(&posted, window.get(), message, message, PM_REMOVE),
          "UIA repeated requests coalesce one message");
    AccessibilityRequest request;
    check(host->TakeRequest(request) && request.index == 2 && request.focus && request.action == RedXePointerRaise &&
              !host->TakeRequest(request),
          "UIA focus and committed navigation are consumed once");
    check(first->actions == 1, "UIA coalescing calls plugin once");
    auto retiredSite = first->site;
    auto retiredFragment = first->fragment;
    check(first->site->RequestFocus() == S_OK, "UIA obsolete queued request fixture");
    views[0].viewId = 1;
    views[0].prepared = true;
    check(host->Update(views) == S_OK && first->connects == 2 && first->placement.attachmentId != attachment,
          "UIA raised view receives a fresh attachment");
    check(retiredSite->RequestFocus() == UIA_E_ELEMENTNOTAVAILABLE && !retiredFragment->alive &&
              !host->TakeRequest(request),
          "UIA old view and pending requests retire together");
    while (PeekMessageW(&posted, window.get(), message, message, PM_REMOVE))
        (void)host->IsMessage(message, posted.wParam);
    first->replace = true;
    retiredSite = first->site;
    check(host->Update(views) == S_OK && first->connects == 3 &&
              retiredSite->RequestFocus() == UIA_E_ELEMENTNOTAVAILABLE,
          "UIA prepared replacement reconnects with a fresh site");
    views[0].prepared = false;
    views[0].screenBounds.left -= 20;
    views[0].screenBounds.right -= 20;
    check(host->Update(views) == S_OK && first->placement.left == -140 && first->connects == 3,
          "UIA geometry update preserves connection");
    HRESULT wrongThread = S_OK;
    std::thread foreign([&] { wrongThread = first->site->RequestFocus(); });
    foreign.join();
    check(wrongThread == UIA_E_ELEMENTNOTAVAILABLE, "UIA raw foreign-thread site cannot mutate application");
    retiredSite = first->site;
    host->ClearViews();
    check(retiredSite->RequestFocus() == UIA_E_ELEMENTNOTAVAILABLE && !host->TakeRequest(request),
          "UIA hide disconnects every view");
    found.reset();
    check(root->Navigate(NavigateDirection_FirstChild, found.put()) == S_OK && !found,
          "UIA hidden dashboard has no stale children");
    host->Disconnect();
    host.reset();
    VARIANT value{};
    check(heldRoot->GetPropertyValue(UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE && value.vt == VT_EMPTY,
          "UIA retained root safely rejects after host destruction");
    check(retiredSite->RequestFocus() == UIA_E_ELEMENTNOTAVAILABLE,
          "UIA retained site safely rejects after host destruction");
    return checks;
}
