#pragma once
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Widget.h"
#include <UIAutomation.h>
#include <cstdio>
#include <string_view>
#include <thread>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

// Test-only traversal of the public provider tree. Bounds prevent a broken provider from hanging the suite.
inline wil::com_ptr_nothrow<IRawElementProviderSimple> FindAutomationId(IRawElementProviderFragment* fragment,
                                                                        std::wstring_view id, uint32_t depth = 0)
{
    if (!fragment || depth > 32)
        return {};
    wil::com_ptr_nothrow<IRawElementProviderSimple> simple;
    if (SUCCEEDED(fragment->QueryInterface(IID_PPV_ARGS(simple.put()))))
    {
        wil::unique_variant value;
        if (simple->GetPropertyValue(UIA_AutomationIdPropertyId, &value) == S_OK && value.vt == VT_BSTR &&
            value.bstrVal && std::wstring_view(value.bstrVal, SysStringLen(value.bstrVal)) == id)
            return simple;
    }
    wil::com_ptr_nothrow<IRawElementProviderFragment> child;
    if (fragment->Navigate(NavigateDirection_FirstChild, child.put()) != S_OK)
        return {};
    for (size_t i = 0; child && i < 256; ++i)
    {
        if (auto found = FindAutomationId(child.get(), id, depth + 1))
            return found;
        wil::com_ptr_nothrow<IRawElementProviderFragment> next;
        if (child->Navigate(NavigateDirection_NextSibling, next.put()) != S_OK)
            break;
        child = std::move(next);
    }
    return {};
}

class RangeEventHandler final : public RedXeComObject<RangeEventHandler, IUIAutomationPropertyChangedEventHandler>
{
  public:
    wil::unique_event_nothrow changed;
    double expected = 0;
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID property,
                                                         VARIANT value) noexcept override
    {
        if (property == UIA_RangeValueValuePropertyId && value.vt == VT_R8 && value.dblVal == expected)
            changed.SetEvent();
        return S_OK;
    }
};

inline bool PumpAccessibilityFixture(HANDLE signal, DWORD timeout) noexcept
{
    const auto deadline = GetTickCount64() + timeout;
    for (;;)
    {
        const auto now = GetTickCount64();
        if (now >= deadline)
            return WaitForSingleObject(signal, 0) == WAIT_OBJECT_0;
        const DWORD wait = MsgWaitForMultipleObjectsEx(1, &signal, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                                       MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0)
            return true;
        if (wait != WAIT_OBJECT_0 + 1)
            return false;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}

// Real OS UIA discovery and property delivery, restricted to the caller's private HWND and synthetic AV state.
// Subscribe and unsubscribe on the same MTA, while the provider's STA pumps ordinary Windows/COM messages.
template <typename Publish> HRESULT VerifyRangeEvent(HWND window, double expected, Publish publish)
{
    wil::unique_event_nothrow ready, completed, stop;
    RETURN_IF_FAILED(ready.create(wil::EventOptions::ManualReset));
    RETURN_IF_FAILED(completed.create(wil::EventOptions::ManualReset));
    RETURN_IF_FAILED(stop.create(wil::EventOptions::ManualReset));
    HRESULT subscription = E_PENDING, delivery = E_PENDING;
    std::thread client(
        [&]() noexcept
        {
            const auto finished = wil::scope_exit(
                [&]
                {
                    ready.SetEvent();
                    completed.SetEvent();
                });
            subscription = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(subscription))
                return;
            const auto apartment = wil::scope_exit([] { CoUninitialize(); });
            wil::com_ptr_nothrow<IUIAutomation> automation;
            subscription =
                CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(automation.put()));
            if (FAILED(subscription))
                return;
            wil::com_ptr_nothrow<IUIAutomationElement> root, level;
            subscription = automation->ElementFromHandle(window, root.put());
            if (FAILED(subscription) || !root)
            {
                if (SUCCEEDED(subscription))
                    subscription = E_UNEXPECTED;
                return;
            }
            wil::unique_variant value;
            value.vt = VT_BSTR;
            value.bstrVal = SysAllocString(L"av.level.output");
            if (!value.bstrVal)
            {
                subscription = E_OUTOFMEMORY;
                return;
            }
            wil::com_ptr_nothrow<IUIAutomationCondition> condition;
            subscription = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, condition.put());
            if (FAILED(subscription))
                return;
            subscription = root->FindFirst(TreeScope_Subtree, condition.get(), level.put());
            if (FAILED(subscription) || !level)
            {
                if (SUCCEEDED(subscription))
                    subscription = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                return;
            }
            wil::com_ptr_nothrow<RangeEventHandler> handler;
            handler.attach(new (std::nothrow) RangeEventHandler);
            if (!handler)
            {
                subscription = E_OUTOFMEMORY;
                return;
            }
            subscription = handler->changed.create(wil::EventOptions::ManualReset);
            if (FAILED(subscription))
                return;
            handler->expected = expected;
            PROPERTYID property = UIA_RangeValueValuePropertyId;
            subscription = automation->AddPropertyChangedEventHandlerNativeArray(level.get(), TreeScope_Element,
                                                                                 nullptr, handler.get(), &property, 1);
            if (FAILED(subscription))
                return;
            ready.SetEvent();
            const HANDLE signals[]{handler->changed.get(), stop.get()};
            const DWORD observed = WaitForMultipleObjects(2, signals, FALSE, 5000);
            delivery = observed == WAIT_OBJECT_0 ? S_OK : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            const HRESULT removed = automation->RemovePropertyChangedEventHandler(level.get(), handler.get());
            if (FAILED(removed))
                delivery = removed;
        });
    const auto join = wil::scope_exit(
        [&]() noexcept
        {
            stop.SetEvent();
            if (!PumpAccessibilityFixture(completed.get(), 10000))
            {
                std::fputs("UIA fixture client exceeded its shutdown bound.\n", stderr);
                // Fail only this isolated test process; never leave a thread borrowing stack state.
                RaiseFailFastException(nullptr, nullptr, 0);
            }
            client.join();
        });
    if (!PumpAccessibilityFixture(ready.get(), 10000))
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    RETURN_IF_FAILED(subscription);
    RETURN_IF_FAILED(publish());
    if (!PumpAccessibilityFixture(completed.get(), 10000))
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    return delivery;
}
