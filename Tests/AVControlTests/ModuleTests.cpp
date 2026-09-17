#include "../../Common/PlugInterfaces/FactoryImpl.h"
#include "../../Common/PlugInterfaces/Widget.h"
#include "../../RedXe/AccessibilityHost.h"
#include "AVControlLayout.h"
#include "AVControlTestContract.h"
#include "AccessibilityTestHelpers.h"
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <wil/com.h>
#include <wil/resource.h>

namespace
{
LRESULT CALLBACK AccessibilityFixtureProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    auto* host = reinterpret_cast<AccessibilityHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_GETOBJECT && static_cast<LONG>(lParam) == UiaRootObjectId && host)
        return UiaReturnRawElementProvider(window, wParam, lParam, host->Provider());
    return DefWindowProcW(window, message, wParam, lParam);
}
uint32_t checks = 0;
void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(description);
}
class Host final : public RedXeComObject<Host, IRedXeHost>
{
  public:
    wil::com_ptr_nothrow<IRedXeControlWork> pending;
    std::atomic<uint32_t> frames{0};
    uint32_t runs = 0, saves = 0;
    std::string saved;
    HRESULT STDMETHODCALLTYPE GetDataProvider(const char*, IRedXeDataProvider** output) noexcept override
    {
        if (output)
            *output = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE RequestFrame() noexcept override
    {
        ++frames;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReportWidgetStatus(const char*, const RedXeWidgetStatusReport*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PersistWidgetSettings(const char*, const char* json, uint32_t bytes) noexcept override
    {
        try
        {
            saved.assign(json, bytes);
            ++saves;
            return S_OK;
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
    }
    HRESULT STDMETHODCALLTYPE QueueControlWork(IRedXeControlWork* work) noexcept override
    {
        if (pending)
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        pending = work;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestAction(const RedXeActionRequest*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ExecuteAction(const RedXeActionRequest*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ValidateAction(const RedXeActionRequest*,
                                             const RedXeActionDescriptor** descriptor) noexcept override
    {
        if (descriptor)
        {
            *descriptor = nullptr;
        }
        return S_OK;
    }
    void Drain() noexcept
    {
        for (uint32_t i = 0; i < 16 && pending; ++i)
        {
            auto work = std::move(pending);
            wil::unique_event_nothrow cancel;
            if (FAILED(cancel.create(wil::EventOptions::ManualReset)))
                return;
            HRESULT result = E_FAIL;
            try
            {
                std::thread worker(
                    [&]
                    {
                        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                        result = FAILED(apartment) ? apartment : work->Run(cancel.get(), 3000);
                        if (SUCCEEDED(apartment))
                            CoUninitialize();
                    });
                worker.join();
            }
            catch (const std::system_error&)
            {
                result = E_FAIL;
            }
            catch (const std::bad_alloc&)
            {
                result = E_OUTOFMEMORY;
            }
            ++runs;
            work->Complete(result);
        }
    }
};
} // namespace
uint32_t RunModuleTests()
{
    checks = 0;
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Check(SUCCEEDED(apartment), "AV module fixture COM apartment");
    const auto uninitialize = wil::scope_exit([&] { CoUninitialize(); });
    wchar_t executable[MAX_PATH]{};
    Check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "module fixture location");
    const auto path = std::filesystem::path(executable).parent_path() / L"Plugins" / L"AVControl.dll";
    wil::unique_hmodule module(
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32));
    Check(bool(module), "AV DLL loads with only its directory and System32 dependency search");
    auto create = reinterpret_cast<RedXeCreateFn>(GetProcAddress(module.get(), kRedXeCreateExport));
    auto enumerate =
        reinterpret_cast<RedXeEnumeratePluginsFn>(GetProcAddress(module.get(), kRedXeEnumeratePluginsExport));
    auto contract = reinterpret_cast<RedXeGetPluginSettingsContractFn>(
        GetProcAddress(module.get(), kRedXeGetPluginSettingsContractExport));
    auto shutdown = reinterpret_cast<RedXePluginShutdownFn>(GetProcAddress(module.get(), kRedXePluginShutdownExport));
    auto synthetic = reinterpret_cast<decltype(&RedXeAVControlUseSyntheticBackend)>(
        GetProcAddress(module.get(), "RedXeAVControlUseSyntheticBackend"));
    auto snapshot = reinterpret_cast<decltype(&RedXeAVControlTestSnapshot)>(
        GetProcAddress(module.get(), "RedXeAVControlTestSnapshot"));
    Check(create && enumerate && contract && shutdown && synthetic && snapshot,
          "AV module exposes required factory and explicit synthetic fixture exports");
    Check(synthetic(TRUE) == S_OK, "DLL fixture explicitly selects synthetic devices before provider construction");
    Host host;
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    wil::com_ptr_nothrow<IRedXeWidget> widget, second;
    wil::com_ptr_nothrow<IRedXeGpuWidget> gpu, secondGpu;
    wil::com_ptr_nothrow<IRedXePreparedGpuWidget> prepare, secondPrepare;
    wil::com_ptr_nothrow<IRedXeInteractiveWidget> input;
    wil::com_ptr_nothrow<IRedXeRaisedWidget> raised;
    wil::com_ptr_nothrow<IRedXeKeyboardWidget> keyboard;
    wil::com_ptr_nothrow<IRedXeTextInputWidget> textInput;
    wil::com_ptr_nothrow<IRawElementProviderSimple> retainedProvider;
    const auto releaseModuleObjects = [&]
    {
        if (widget)
            (void)widget->SetVisible(FALSE);
        if (second)
            (void)second->SetVisible(FALSE);
        host.Drain();
        if (gpu)
            gpu->OnDeviceLost();
        if (secondGpu)
            secondGpu->OnDeviceLost();
        textInput.reset();
        keyboard.reset();
        raised.reset();
        input.reset();
        prepare.reset();
        secondPrepare.reset();
        gpu.reset();
        secondGpu.reset();
        widget.reset();
        second.reset();
        provider.reset();
        host.Drain();
        shutdown();
    };
    auto cleanup = wil::scope_exit([&]() noexcept { releaseModuleObjects(); });
    const RedXePluginMetadata* metadata = nullptr;
    uint32_t count = 0;
    Check(enumerate(&metadata, &count) == S_OK && count == 1 && std::string_view(metadata->id) == "builtin.av-control",
          "AV DLL metadata identifies one bundled logical plugin");
    const RedXePluginSettingsContract* settings = nullptr;
    Check(contract(metadata->id, &settings) == S_OK, "AV settings contract is discoverable before construction");
    AVControl::Configuration defaults;
    Check(AVControl::ParseConfiguration({settings->defaultsJsonUtf8, settings->defaultsBytes}, defaults) == S_OK &&
              defaults.count == 0,
          "AV defaults contain no fabricated device profiles");
    Check(FAILED(create(__uuidof(IRedXeWidgetProvider), nullptr, nullptr, metadata->id, provider.put_void())),
          "AV factory requires host services");
    const std::string invalid = R"({"plugin":{},"instance":{"profiles":[{"id":"invalid"}]}})";
    const RedXeFactoryOptions invalidOptions{sizeof(RedXeFactoryOptions), 0, invalid.data(),
                                             static_cast<uint32_t>(invalid.size())};
    Check(FAILED(create(__uuidof(IRedXeWidgetProvider), &invalidOptions, &host, metadata->id, provider.put_void())) &&
              !provider,
          "AV factory validates profile definitions through the same strict model");
    Check(create(__uuidof(IRedXeWidgetProvider), nullptr, &host, metadata->id, provider.put_void()) == S_OK && provider,
          "AV provider constructs without starting helper work");
    Check(!host.pending && host.runs == 0 && synthetic(FALSE) == HRESULT_FROM_WIN32(ERROR_BUSY),
          "inactive provider has no device work and backend mode cannot change live");
    const RedXeWidgetTypeDescriptor* type = nullptr;
    Check(provider->GetWidgetTypes(&type, &count) == S_OK && count == 1 && type->minimumWidth == 160 &&
              type->minimumHeight == 180 && !type->flags,
          "AV type declares minimum bounds and event-driven scheduling");
    Check(provider->CreateWidget(type->typeId, "av.fixture.1", widget.put()) == S_OK &&
              provider->CreateWidget(type->typeId, "av.fixture.2", second.put()) == S_OK,
          "AV provider creates independent instances");
    Check(widget.query_to(gpu.put()) == S_OK && widget.query_to(prepare.put()) == S_OK &&
              widget.query_to(input.put()) == S_OK && widget.query_to(raised.put()) == S_OK &&
              widget.query_to(keyboard.put()) == S_OK && widget.query_to(textInput.put()) == S_OK &&
              second.query_to(secondGpu.put()) == S_OK && second.query_to(secondPrepare.put()) == S_OK,
          "AV advertises its supported generic mechanisms");
    {
        wil::com_ptr_nothrow<IUnknown> identity, sibling;
        Check(widget.query_to(identity.put()) == S_OK && keyboard.query_to(sibling.put()) == S_OK &&
                  identity.get() == sibling.get(),
              "keyboard and generic widget have the same controlling COM identity");
        sibling.reset();
        Check(textInput.query_to(sibling.put()) == S_OK && identity.get() == sibling.get(),
              "text and generic widget have the same controlling COM identity");
        auto state = std::make_unique<RedXeTextState>();
        state->revision = 17;
        Check(textInput->ReadTextState(0, state.get()) == S_FALSE && state->revision == 0,
              "unprepared widget exposes no stale text state");
        Check(textInput->ReadTextState(2, state.get()) == E_INVALIDARG &&
                  textInput->ReadTextState(0, nullptr) == E_POINTER,
              "text interface validates view and output pointer");
    }
    RedXeRaisedExtent extent{};
    Check(raised->GetRaisedExtent(&extent) == S_OK && extent == RedXeRaisedExtentHalf,
          "AV never requests more than half-screen raise");
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
                                      0, D3D11_SDK_VERSION, device.put(), nullptr, context.put())),
          "AV module receives caller-created WARP device");
    RedXeGpuDeviceContext supplied{sizeof(RedXeGpuDeviceContext), device.get(), DXGI_FORMAT_B8G8R8A8_UNORM,
                                   device->GetFeatureLevel()};
    Check(gpu->OnDeviceCreated(&supplied) == S_OK && secondGpu->OnDeviceCreated(&supplied) == S_OK,
          "two AV instances attach supplied graphics");
    Check(widget->SetVisible(TRUE) == S_OK && second->SetVisible(TRUE) == S_OK,
          "AV instances become visible explicitly");
    RedXeGpuPreparationContext preparation{sizeof(RedXeGpuPreparationContext), 96, 640, 360, 0, 0};
    Check(SUCCEEDED(prepare->Prepare(&preparation)) && SUCCEEDED(secondPrepare->Prepare(&preparation)),
          "AV module prepares both tile layouts before drawing");
    host.Drain();
    Check(host.runs == 1, "two live AV instances share one backend observation");
    Check(SUCCEEDED(prepare->Prepare(&preparation)), "AV applies acknowledged model during preparation");
    auto state = std::make_unique<AVControl::Inventory>();
    Check(snapshot(state.get(), sizeof(*state)) == S_OK &&
              state->state.output.availability == AVControl::Availability::Ready,
          "DLL publishes real IPC-confirmed synthetic state");
    const auto layout = AVControl::LayoutLive(640, 360);
    const auto click = [&](AVControl::Rect rect, uint32_t viewId = 0)
    {
        RedXePointerEvent event{
            sizeof(RedXePointerEvent), 1,      RedXePointerKindTouch, RedXePointerPhaseDown, rect.x + rect.width / 2,
            rect.y + rect.height / 2,  viewId, viewId ? 1280U : 640U, viewId ? 720U : 360U,  96};
        Check(input->OnPointer(&event) == RedXePointerCapture, "DLL control owns full touch gesture");
        event.phase = RedXePointerPhaseUp;
        return input->OnPointer(&event);
    };
    Check(click(layout.toggles[0]) == S_OK, "DLL output mute handles committed touch");
    Check(snapshot(state.get(), sizeof(*state)) == S_OK && !state->state.output.muted,
          "DLL never presents intent as confirmed device state");
    host.Drain();
    Check(snapshot(state.get(), sizeof(*state)) == S_OK && state->state.output.muted,
          "DLL output mute acknowledges worker readback");
    Check(SUCCEEDED(prepare->Prepare(&preparation)), "DLL prepares the new mute state");

    wil::com_ptr_nothrow<ID3D11Texture2D> target;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> rtv;
    D3D11_TEXTURE2D_DESC description{};
    description.Width = 1280;
    description.Height = 720;
    description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    Check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, target.put())) &&
              SUCCEEDED(device->CreateRenderTargetView(target.get(), nullptr, rtv.put())),
          "DLL render fixture owns output surface");
    const auto draw = [&](uint32_t viewId)
    {
        auto* output = rtv.get();
        context->OMSetRenderTargets(1, &output, nullptr);
        const RedXeWidgetFrameContext frame{
            sizeof(RedXeWidgetFrameContext), viewId ? 1280U : 640U, viewId ? 720U : 360U, 96, 0, 0};
        const RedXeGpuFrameContext gpuFrame{sizeof(RedXeGpuFrameContext),
                                            &frame,
                                            context.get(),
                                            {0, 0, float(frame.widthPixels), float(frame.heightPixels), 0, 1},
                                            viewId};
        Check(gpu->Render(&gpuFrame) == S_OK, "DLL composites its cached supplied-device view");
        context->OMSetRenderTargets(0, nullptr, nullptr);
    };
    draw(0);
    Check(click(layout.profileSelector) == RedXePointerRaise,
          "profile button requests host raise after committed touch");
    Check(raised->SetRaised(TRUE) == S_OK, "host grants AV half-screen raise");
    preparation.raisedWidthPixels = 1280;
    preparation.raisedHeightPixels = 720;
    Check(SUCCEEDED(prepare->Prepare(&preparation)), "DLL independently prepares its raised profile view");
    draw(0);
    draw(1);
    const RedXeKeyEvent escape{sizeof(RedXeKeyEvent), VK_ESCAPE, 0, 1, TRUE};
    Check(keyboard->OnKey(&escape) == RedXePointerDismiss, "Escape exits chooser through generic host dismiss result");
    Check(raised->SetRaised(FALSE) == S_OK, "host returns AV to its original tile");
    preparation.raisedWidthPixels = preparation.raisedHeightPixels = 0;
    Check(SUCCEEDED(prepare->Prepare(&preparation)), "tile resumes independent layout after dismiss");
    Check(keyboard->SetKeyboardFocus(TRUE, 0) == S_OK, "host gives keyboard focus without exposing HWND");
    const RedXeKeyEvent profilesKey{sizeof(RedXeKeyEvent), VK_F6, 0, 0, TRUE};
    Check(keyboard->OnKey(&profilesKey) == RedXePointerRaise,
          "F6 provides profile access even without a visible selector");
    Check(raised->SetRaised(TRUE) == S_OK, "keyboard profile raise attaches same shared view");
    preparation.raisedWidthPixels = 1280;
    preparation.raisedHeightPixels = 720;
    Check(SUCCEEDED(prepare->Prepare(&preparation)), "keyboard chooser is prepared");
    Check(keyboard->OnCharacter(L'\0', 0, 2) == E_INVALIDARG, "invalid keyboard view is rejected");
    Check(raised->SetRaised(FALSE) == S_OK, "keyboard chooser can dismiss");
    preparation.raisedWidthPixels = preparation.raisedHeightPixels = 0;
    (void)prepare->Prepare(&preparation);
    const auto beforeIdle = host.frames.load();
    for (uint32_t i = 0; i < 100; ++i)
        draw(0);
    Check(host.frames == beforeIdle, "100 clean module composites request no frames or device work");
    {
        constexpr wchar_t className[] = L"RedXe.AVControl.UIA.Test";
        WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = className;
        windowClass.lpfnWndProc = AccessibilityFixtureProcedure;
        Check(RegisterClassExW(&windowClass) != 0, "DLL UIA fixture class");
        const auto unregister = wil::scope_exit([&] { (void)UnregisterClassW(className, windowClass.hInstance); });
        wil::unique_hwnd window(CreateWindowExW(0, className, L"AV module accessibility fixture", WS_POPUP, -300, 40,
                                                640, 360, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr));
        Check(bool(window), "DLL UIA private hidden application HWND");
        std::unique_ptr<AccessibilityHost> accessibility;
        Check(AccessibilityHost::Create(window.get(), accessibility) == S_OK, "DLL attaches application UIA root");
        SetWindowLongPtrW(window.get(), GWLP_USERDATA, reinterpret_cast<LONG_PTR>(accessibility.get()));
        const auto unbind = wil::scope_exit([&] { SetWindowLongPtrW(window.get(), GWLP_USERDATA, 0); });
        wil::com_ptr_nothrow<IRedXeAccessibilityWidget> accessible;
        Check(widget.query_to(accessible.put()) == S_OK, "DLL exposes generic accessibility mechanism");
        wil::com_ptr_nothrow<IUnknown> identity, sibling;
        Check(widget.query_to(identity.put()) == S_OK && accessible.query_to(sibling.put()) == S_OK &&
                  identity.get() == sibling.get(),
              "DLL accessibility shares controlling widget identity");
        AccessibleWidgetView placement{0, accessible.get(), 0, {-300, 40, 340, 400}, false, true};
        Check(accessibility->Update({&placement, 1}) == S_OK, "DLL UIA prepared tile publication");
        wil::com_ptr_nothrow<IRawElementProviderFragment> root;
        Check(accessibility->Provider()->QueryInterface(IID_PPV_ARGS(root.put())) == S_OK,
              "DLL UIA navigable dashboard root");
        auto level = FindAutomationId(root.get(), L"av.level.output");
        auto toggle = FindAutomationId(root.get(), L"av.toggle.Out");
        auto profile = FindAutomationId(root.get(), L"av.profiles");
        Check(level && toggle && profile, "DLL UIA discovers live controls through public host tree");
        retainedProvider = level;
        wil::com_ptr_nothrow<IUnknown> pattern;
        wil::com_ptr_nothrow<IRangeValueProvider> range;
        Check(level->GetPatternProvider(UIA_RangeValuePatternId, pattern.put()) == S_OK && pattern &&
                  pattern.query_to(range.put()) == S_OK,
              "DLL UIA exposes real range pattern");
        Check(range->SetValue(37) == S_OK, "DLL UIA range commits through normal backend command");
        wil::com_ptr_nothrow<IRawElementProviderFragment> focus;
        Check(level.query_to(focus.put()) == S_OK && focus->SetFocus() == S_OK, "DLL UIA requests logical focus");
        placement.keyboardFocused = true;
        placement.prepared = false;
        Check(accessibility->Update({&placement, 1}) == S_OK, "DLL dirty view keeps attachment for pending focus");
        AccessibilityRequest request;
        Check(accessibility->TakeRequest(request) && request.focus && request.action == S_OK,
              "DLL pending UIA focus survives dirty placement refresh");
        host.Drain();
        Check(snapshot(state.get(), sizeof(*state)) == S_OK && state->state.output.level == 37,
              "DLL UIA scalar arrives at synthetic backend and confirmed readback");
        Check(SUCCEEDED(prepare->Prepare(&preparation)), "DLL UIA committed model prepares");
        placement.prepared = true;
        Check(accessibility->Update({&placement, 1}) == S_OK, "DLL UIA publishes committed range");
        double scalar = 0;
        Check(range->get_Value(&scalar) == S_OK && scalar == 37, "DLL retained range sees confirmed level");
        const HRESULT eventResult = VerifyRangeEvent(window.get(), 38,
                                                     [&]() noexcept -> HRESULT
                                                     {
                                                         RETURN_IF_FAILED(range->SetValue(38));
                                                         host.Drain();
                                                         RETURN_IF_FAILED(prepare->Prepare(&preparation));
                                                         return accessibility->Update({&placement, 1});
                                                     });
        if (FAILED(eventResult))
            std::fprintf(stderr, "UIA property delivery: 0x%08lX\n", static_cast<unsigned long>(eventResult));
        Check(eventResult == S_OK,
              "DLL real OS UIA client discovers output slider and receives committed value change");
        constexpr std::array toggleIds{L"av.toggle.Out", L"av.toggle.Mic", L"av.toggle.Cam"};
        for (const auto* id : toggleIds)
        {
            auto control = FindAutomationId(root.get(), id);
            wil::com_ptr_nothrow<IUnknown> togglePattern;
            wil::com_ptr_nothrow<IToggleProvider> toggler;
            Check(control && control->GetPatternProvider(UIA_TogglePatternId, togglePattern.put()) == S_OK &&
                      togglePattern && togglePattern.query_to(toggler.put()) == S_OK,
                  "DLL all three safety controls expose Toggle");
            ToggleState before{}, pending{}, confirmed{};
            Check(toggler->get_ToggleState(&before) == S_OK && toggler->Toggle() == S_OK &&
                      toggler->get_ToggleState(&pending) == S_OK && pending == before,
                  "DLL UIA safety action preserves confirmed state until acknowledgment");
            host.Drain();
            Check(SUCCEEDED(prepare->Prepare(&preparation)) && accessibility->Update({&placement, 1}) == S_OK &&
                      toggler->get_ToggleState(&confirmed) == S_OK && confirmed != before,
                  "DLL UIA output, microphone and camera actions publish confirmed state");
        }
        pattern.reset();
        wil::com_ptr_nothrow<IInvokeProvider> invoke;
        Check(profile->GetPatternProvider(UIA_InvokePatternId, pattern.put()) == S_OK && pattern &&
                  pattern.query_to(invoke.put()) == S_OK && invoke->Invoke() == S_OK,
              "DLL UIA Profile invokes same action as pointer");
        Check(accessibility->TakeRequest(request) && request.action == RedXePointerRaise &&
                  !accessibility->TakeRequest(request),
              "DLL UIA Profile requests host raise exactly once");
        Check(raised->SetRaised(TRUE) == S_OK, "DLL host grants UIA profile raise");
        preparation.raisedWidthPixels = 1280;
        preparation.raisedHeightPixels = 720;
        Check(SUCCEEDED(prepare->Prepare(&preparation)), "DLL UIA profile overlay prepares");
        placement.viewId = 1;
        placement.screenBounds = {-300, 40, 980, 760};
        Check(accessibility->Update({&placement, 1}) == S_OK && range->get_Value(&scalar) == UIA_E_ELEMENTNOTAVAILABLE,
              "DLL raised tree retires background tile provider");
        Check(raised->SetRaised(FALSE) == S_OK, "DLL UIA fixture dismisses profile view");
        preparation.raisedWidthPixels = preparation.raisedHeightPixels = 0;
        accessibility->ClearViews();
        Check(SUCCEEDED(prepare->Prepare(&preparation)), "DLL resumes after UIA fixture");
    }
    Check(widget->SetVisible(FALSE) == S_OK, "first instance hides");
    Check(second->SetVisible(FALSE) == S_OK, "last instance hides");
    host.Drain();
    gpu->OnDeviceLost();
    secondGpu->OnDeviceLost();
    Check(gpu->OnDeviceCreated(&supplied) == S_OK, "DLL device resources rebuild after loss");
    Check(widget->SetVisible(TRUE) == S_OK, "DLL resumes after device recovery");
    Check(SUCCEEDED(prepare->Prepare(&preparation)), "recovered DLL prepares restored logical state");
    host.Drain();
    (void)prepare->Prepare(&preparation);
    draw(0);
    Check(host.saves == 0, "visibility, mute, rendering and device recovery never persist profile definitions");
    releaseModuleObjects();
    cleanup.release();
    module.reset();
    VARIANT retired{};
    Check(retainedProvider &&
              retainedProvider->GetPropertyValue(UIA_NamePropertyId, &retired) == UIA_E_ELEMENTNOTAVAILABLE &&
              retired.vt == VT_EMPTY,
          "DLL disconnected provider remains callable after plugin shutdown and loader-owner release");
    return checks;
}
