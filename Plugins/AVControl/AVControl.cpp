#define REDXE_PLUGIN_EXPORTS
#include "AVControlSettings.h"
#include "AVControlTestContract.h"
#include "AccessibilityValidation.h"
#include "Coordinator.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>
#include <wil/result.h>
#include <yyjson.h>

namespace AVControl
{
namespace
{
constexpr RedXePluginMetadata Metadata{sizeof(RedXePluginMetadata),
                                       PluginId,
                                       L"AV Control",
                                       L"Audio levels, microphone mute, camera route and device profiles",
                                       L"RedSalamanders",
                                       L"0.1.0",
                                       RedXePluginCapabilityWidgetProvider};
constexpr RedXeWidgetTypeDescriptor WidgetType{sizeof(RedXeWidgetTypeDescriptor),
                                               TypeId,
                                               L"AV Control",
                                               L"Live output, microphone and camera controls",
                                               640,
                                               360,
                                               160,
                                               180,
                                               RedXeWidgetFlagNone};
wil::com_ptr_nothrow<Coordinator> coordinator;
std::weak_ptr<DxUi::GraphicsDevice> graphics;
bool syntheticBackend = false;
uint32_t providerCount = 0;

HRESULT KeepAccessibilityCodeMapped() noexcept
{
    // A UIA client can retain provider code after widget/runtime teardown. One process-lifetime image pin keeps
    // disconnected COM objects callable; control trees, device resources and the coordinator still tear down.
    static bool pinned = false;
    if (pinned)
        return S_OK;
    HMODULE image = nullptr;
    RETURN_IF_WIN32_BOOL_FALSE(
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&KeepAccessibilityCodeMapped), &image));
    pinned = true;
    return S_OK;
}

HRESULT GetCoordinator(IRedXeHost* host, wil::com_ptr_nothrow<Coordinator>& result) noexcept
{
    if (!host)
        return E_POINTER;
    if (coordinator)
    {
        if (coordinator->Host() != host)
            return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        result = coordinator;
        return S_OK;
    }
    try
    {
        HMODULE module = nullptr;
        RETURN_IF_WIN32_BOOL_FALSE(
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&GetCoordinator), &module));
        std::wstring path(32768, L'\0');
        const DWORD count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!count || count == path.size())
            return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
        path.resize(count);
        auto helper = std::filesystem::path(path).parent_path() / L"AVControlBroker.exe";
        wil::com_ptr_nothrow<Coordinator> created;
        created.attach(new Coordinator(host, helper.native(), syntheticBackend));
        RETURN_IF_FAILED(created->Initialize());
        coordinator = created;
        result = created;
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
    }
}
HRESULT ReadConfiguration(const RedXeFactoryOptions* options, Configuration& configuration) noexcept
{
    if (!options || (!options->configurationJsonUtf8 && !options->configurationBytes))
        return S_OK;
    if (options->sizeBytes != sizeof(RedXeFactoryOptions) || !options->configurationJsonUtf8 ||
        !options->configurationBytes || options->configurationBytes > kRedXeMaximumFactoryConfigurationBytes)
        return E_INVALIDARG;
    using Document = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
    Document document(yyjson_read(options->configurationJsonUtf8, options->configurationBytes, 0));
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    auto* plugin = yyjson_obj_get(root, "plugin");
    auto* instance = yyjson_obj_get(root, "instance");
    if (!yyjson_is_obj(root) || yyjson_obj_size(root) != 2 || !yyjson_is_obj(plugin) || yyjson_obj_size(plugin) != 0 ||
        !yyjson_is_obj(instance))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (yyjson_obj_size(instance) == 0)
        return S_OK;
    size_t length = 0;
    wil::unique_any<char*, decltype(&free), free> serialized(yyjson_val_write(instance, 0, &length));
    if (!serialized)
        return E_OUTOFMEMORY;
    return ParseConfiguration({serialized.get(), length}, configuration);
}

class Widget final
    : public RedXeComObject<Widget, IRedXeWidget, IRedXeGpuWidget, IRedXePreparedGpuWidget, IRedXeInteractiveWidget,
                            IRedXeRaisedWidget, IRedXeKeyboardWidget, IRedXeTextInputWidget, IRedXeAccessibilityWidget>
{
  public:
    Widget(wil::com_ptr_nothrow<IRedXeWidgetProvider> owner, wil::com_ptr_nothrow<Coordinator> runtime,
           const Configuration& configuration, const char* instance)
        : _provider(std::move(owner)), _runtime(std::move(runtime)), _configuration(configuration), _instance(instance)
    {
    }
    ~Widget()
    {
        (void)SetVisible(FALSE);
        _runtime->UnregisterConfiguration(&_configuration);
        OnDeviceLost();
    }
    HRESULT Initialize() noexcept
    {
        return _runtime->RegisterConfiguration(&_configuration);
    }
    HRESULT STDMETHODCALLTYPE SetVisible(BOOL visible) noexcept override
    {
        const bool value = visible != FALSE;
        if (_visible == value)
            return S_OK;
        _visible = value;
        _runtime->SetSubscriberVisible(value);
        _tile.SetVisible(value);
        _overlay.SetVisible(value && _raised);
        if (!value)
        {
            _tile.Cancel();
            _overlay.Cancel();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CollectPersistentSettings(char* json, uint32_t capacity,
                                                        uint32_t* written) noexcept override
    {
        if (!written)
            return E_POINTER;
        *written = 0;
        return SerializeConfiguration(_configuration, json, capacity, *written);
    }
    HRESULT STDMETHODCALLTYPE GetRaisedExtent(RedXeRaisedExtent* extent) noexcept override
    {
        if (!extent)
            return E_POINTER;
        *extent = RedXeRaisedExtentHalf;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetRaised(BOOL raised) noexcept override
    {
        _raised = raised != FALSE;
        _tile.Cancel();
        _overlay.Cancel();
        _tile.LoseTextFocus();
        _overlay.LoseTextFocus();
        _overlay.SetVisible(_visible && _raised);
        if (_raised && _openProfilesOnRaise)
        {
            _tile.CloseProfiles();
            _overlay.ShowProfiles();
            _openProfilesOnRaise = false;
        }
        if (!_raised)
        {
            _overlay.CloseProfiles();
            _tile.CloseProfiles();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceCreated(const RedXeGpuDeviceContext* context) noexcept override
    {
        if (!context)
            return E_POINTER;
        if (context->sizeBytes != sizeof(*context) || !context->device)
            return E_INVALIDARG;
        if (_pool && _pool->GetDevice() == context->device)
            return S_OK;
        OnDeviceLost();
        auto pool = graphics.lock();
        if (!pool || pool->GetDevice() != context->device)
        {
            RETURN_IF_FAILED(DxUi::GraphicsDevice::Create(context->device, pool));
            graphics = pool;
        }
        const ViewCallbacks callbacks{this, [](void* self, const ViewCommand& command) noexcept
                                      { static_cast<Widget*>(self)->Command(command); }, [](void* self) noexcept
                                      { (void)static_cast<Widget*>(self)->_runtime->Host()->RequestFrame(); },
                                      [](void* self, const Configuration& configuration) noexcept
                                      { return static_cast<Widget*>(self)->Save(configuration); }};
        HRESULT result = _tile.Attach(pool, callbacks);
        if (SUCCEEDED(result))
            result = _overlay.Attach(pool, callbacks);
        if (FAILED(result))
        {
            _tile.Detach();
            _overlay.Detach();
            return result;
        }
        _pool = std::move(pool);
        _tile.SetVisible(_visible);
        _overlay.SetVisible(_visible && _raised);
        _publishedRevision = _publishedDevices = 0;
        _profilesDirty = true;
        return S_OK;
    }
    void STDMETHODCALLTYPE OnDeviceLost() noexcept override
    {
        _tile.Detach();
        _overlay.Detach();
        _pool.reset();
        _publishedRevision = _publishedDevices = 0;
    }
    HRESULT STDMETHODCALLTYPE OnTargetSizeChanged(const RedXeGpuTargetSizeContext* context) noexcept override
    {
        return context && context->sizeBytes == sizeof(*context) ? S_OK : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Prepare(const RedXeGpuPreparationContext* context) noexcept override
    {
        if (!context || context->sizeBytes != sizeof(*context) || !context->dpi)
            return E_INVALIDARG;
        if (!_pool || !_visible)
            return S_FALSE;
        if (context->appearance.flags & ~(RedXeAppearanceDark | RedXeAppearanceHighContrast))
            return E_INVALIDARG;
        _tile.SetAppearance(context->appearance);
        _overlay.SetAppearance(context->appearance);
        _runtime->Prepare();
        const auto& current = _runtime->Current();
        if (_publishedRevision != _runtime->Revision())
        {
            std::array<wchar_t, 160> name{};
            Name(name);
            _tile.SetState(current.state, name.data(), _runtime->PendingMask());
            _overlay.SetState(current.state, name.data(), _runtime->PendingMask());
            _publishedRevision = _runtime->Revision();
        }
        if (_profilesDirty || _publishedDevices != _runtime->DeviceRevision())
        {
            _tile.SetProfiles(_configuration, &current, _runtime->DeviceRevision());
            _overlay.SetProfiles(_configuration, &current, _runtime->DeviceRevision());
            _publishedDevices = _runtime->DeviceRevision();
            _profilesDirty = false;
        }
        const HRESULT tile =
            _tile.Prepare(context->tileWidthPixels, context->tileHeightPixels, static_cast<float>(context->dpi));
        RETURN_IF_FAILED(tile);
        _overlay.SetVisible(_raised && context->raisedWidthPixels && context->raisedHeightPixels);
        const HRESULT overlay =
            _overlay.Prepare(context->raisedWidthPixels, context->raisedHeightPixels, static_cast<float>(context->dpi));
        RETURN_IF_FAILED(overlay);
        return tile == S_OK || overlay == S_OK ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Render(const RedXeGpuFrameContext* context) noexcept override
    {
        if (!context || !context->widget)
            return E_POINTER;
        if (context->sizeBytes != sizeof(*context) || context->widget->sizeBytes != sizeof(*context->widget) ||
            context->viewId > 1 || !context->deviceContext)
            return E_INVALIDARG;
        if (!_visible || !_pool)
            return S_FALSE;
        return (context->viewId ? _overlay : _tile).Composite(context->deviceContext, context->viewport);
    }
    HRESULT STDMETHODCALLTYPE OnPointer(const RedXePointerEvent* event) noexcept override
    {
        if (!event)
            return E_POINTER;
        if (event->sizeBytes != sizeof(*event) || event->viewId > 1 || event->phase > RedXePointerPhaseWheel ||
            !std::isfinite(event->x) || !std::isfinite(event->y) || !std::isfinite(event->wheelDelta))
            return E_INVALIDARG;
        if (!_visible || !_pool)
            return S_FALSE;
        _action = S_OK;
        constexpr std::array actions{DxUi::PointerAction::Down, DxUi::PointerAction::Move, DxUi::PointerAction::Up,
                                     DxUi::PointerAction::Cancel, DxUi::PointerAction::Wheel};
        auto& view = event->viewId ? _overlay : _tile;
        const bool handled =
            view.Pointer({actions[event->phase], event->x, event->y, event->modifiers, event->wheelDelta});
        if (_action != S_OK && event->phase == RedXePointerPhaseUp)
            return std::exchange(_action, S_OK);
        return handled ? event->phase == RedXePointerPhaseDown ? RedXePointerCapture : S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE OnDragOver(float, float) noexcept override
    {
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE OnDragLeave() noexcept override
    {
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE OnDrop(const RedXeDropEvent*) noexcept override
    {
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE SetKeyboardFocus(BOOL focused, uint32_t viewId) noexcept override
    {
        if (viewId > 1)
            return E_INVALIDARG;
        auto& view = viewId ? _overlay : _tile;
        if (!focused)
        {
            view.Cancel();
            view.LoseTextFocus();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnKey(const RedXeKeyEvent* event) noexcept override
    {
        if (!event)
            return E_POINTER;
        if (event->sizeBytes != sizeof(*event) || event->viewId > 1)
            return E_INVALIDARG;
        if (!_visible || !_pool)
            return S_FALSE;
        _action = S_OK;
        auto& view = event->viewId ? _overlay : _tile;
        // F6 opens the profile chooser through the same generic raise as its visible button.
        if (event->down && event->virtualKey == VK_F6)
        {
            Command({ViewCommandKind::OpenProfiles});
            return std::exchange(_action, S_OK);
        }
        const bool handled = view.Key(event->virtualKey, event->down != FALSE, event->modifiers);
        if (view.TabBoundary())
            return RedXeKeyboardBoundary;
        return _action != S_OK ? std::exchange(_action, S_OK) : handled ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE OnCharacter(uint32_t character, uint32_t modifiers, uint32_t viewId) noexcept override
    {
        if (character > 0xffff || viewId > 1)
            return E_INVALIDARG;
        if (!_visible || !_pool)
            return S_FALSE;
        _action = S_OK;
        const bool handled = (viewId ? _overlay : _tile).Character(static_cast<wchar_t>(character), modifiers);
        return _action != S_OK ? std::exchange(_action, S_OK) : handled ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE ReadTextState(uint32_t viewId, RedXeTextState* state) noexcept override
    {
        if (!state)
            return E_POINTER;
        *state = {};
        if (viewId > 1)
            return E_INVALIDARG;
        if (!_visible || !_pool || (viewId && !_raised))
            return S_FALSE;
        return (viewId ? _overlay : _tile).ReadText(*state);
    }
    HRESULT STDMETHODCALLTYPE ApplyTextState(uint32_t viewId, uint32_t action,
                                             const RedXeTextState* state) noexcept override
    {
        if (!state)
            return E_POINTER;
        if (viewId > 1)
            return E_INVALIDARG;
        if (!_visible || !_pool || (viewId && !_raised))
            return S_FALSE;
        return (viewId ? _overlay : _tile).ApplyText(action, *state);
    }
    HRESULT STDMETHODCALLTYPE CancelTextInput(uint32_t viewId, uint64_t focusId) noexcept override
    {
        if (viewId > 1)
            return E_INVALIDARG;
        return (viewId ? _overlay : _tile).CancelText(focusId);
    }
    HRESULT STDMETHODCALLTYPE HitTestText(uint32_t viewId, uint64_t revision, float x, float y,
                                          uint32_t* index) noexcept override
    {
        if (!index)
            return E_POINTER;
        *index = 0;
        if (viewId > 1 || !std::isfinite(x) || !std::isfinite(y))
            return E_INVALIDARG;
        if (!_visible || !_pool || (viewId && !_raised))
            return S_FALSE;
        return (viewId ? _overlay : _tile).HitText(revision, x, y, *index);
    }
    HRESULT STDMETHODCALLTYPE GetTextRangeBounds(uint32_t viewId, uint64_t revision, uint32_t start, uint32_t end,
                                                 RedXeTextRectangle* bounds, BOOL* clipped) noexcept override
    {
        if (bounds)
            *bounds = {};
        if (clipped)
            *clipped = TRUE;
        if (!bounds || !clipped)
            return E_POINTER;
        if (viewId > 1)
            return E_INVALIDARG;
        if (!_visible || !_pool || (viewId && !_raised))
            return S_FALSE;
        return (viewId ? _overlay : _tile).TextRange(revision, start, end, *bounds, *clipped);
    }
    HRESULT STDMETHODCALLTYPE ConnectAccessibility(const RedXeAccessibilityPlacement* placement,
                                                   IRedXeAccessibilitySite* site,
                                                   IRawElementProviderFragmentRoot** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (GetCurrentThreadId() != _thread)
            return RPC_E_WRONG_THREAD;
        if (!placement || !site || !IsValidRedXeAccessibilityPlacement(*placement))
            return E_INVALIDARG;
        if (!_visible || !_pool || (placement->viewId && !_raised))
            return S_FALSE;
        auto& view = placement->viewId ? _overlay : _tile;
        wil::com_ptr_nothrow<IRawElementProviderFragmentRoot> provider;
        const HRESULT connected = view.ConnectAccessibility(*placement, site, provider.put());
        if (connected != S_OK)
            return connected;
        const HRESULT pinned = KeepAccessibilityCodeMapped();
        if (FAILED(pinned))
        {
            view.DisconnectAccessibility();
            return pinned;
        }
        *result = provider.detach();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateAccessibility(const RedXeAccessibilityPlacement* placement) noexcept override
    {
        if (GetCurrentThreadId() != _thread)
            return RPC_E_WRONG_THREAD;
        if (!placement || !IsValidRedXeAccessibilityPlacement(*placement))
            return E_INVALIDARG;
        if (!_visible || !_pool || (placement->viewId && !_raised))
            return S_FALSE;
        return (placement->viewId ? _overlay : _tile).UpdateAccessibility(*placement);
    }
    void STDMETHODCALLTYPE DisconnectAccessibility(uint32_t viewId) noexcept override
    {
        if (GetCurrentThreadId() != _thread || viewId > 1)
            return;
        (viewId ? _overlay : _tile).DisconnectAccessibility();
    }
    HRESULT STDMETHODCALLTYPE TakeAccessibilityAction(uint32_t viewId, HRESULT* action) noexcept override
    {
        if (!action)
            return E_POINTER;
        *action = S_OK;
        if (GetCurrentThreadId() != _thread)
            return RPC_E_WRONG_THREAD;
        if (viewId > 1)
            return E_INVALIDARG;
        if (!_visible || (viewId != (_raised ? 1U : 0U)))
            return S_FALSE;
        *action = std::exchange(_action, S_OK);
        return S_OK;
    }

  private:
    DWORD _thread = GetCurrentThreadId();
    wil::com_ptr_nothrow<IRedXeWidgetProvider> _provider;
    wil::com_ptr_nothrow<Coordinator> _runtime;
    Configuration _configuration;
    std::string _instance;
    std::shared_ptr<DxUi::GraphicsDevice> _pool;
    LiveView _tile, _overlay;
    bool _visible = false, _raised = false, _openProfilesOnRaise = false, _profilesDirty = true;
    uint64_t _publishedRevision = 0, _publishedDevices = 0;
    HRESULT _action = S_OK;
    void Name(std::array<wchar_t, 160>& name) const noexcept
    {
        if (_runtime->ProfilePending())
        {
            wcscpy_s(name.data(), name.size(), L"Switching profile…");
            return;
        }
        for (uint32_t i = 0; i < _configuration.count; ++i)
        {
            const auto& profile = _configuration.profiles[i];
            const auto match = MatchProfile(profile, _runtime->Current().state);
            if (match == ProfileMatch::Custom)
                continue;
            const auto text = profile.name.View();
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                  static_cast<int>(text.size()), name.data(), 128);
            if (count <= 0)
                break;
            if (match == ProfileMatch::Adjusted)
                wcscat_s(name.data(), name.size(), L" · Adjusted");
            return;
        }
        wcscpy_s(name.data(), name.size(),
                 FAILED(_runtime->LastResult()) ? L"Request failed · Profiles to retry" : L"Custom");
    }
    void Command(const ViewCommand& command) noexcept
    {
        switch (command.kind)
        {
        case ViewCommandKind::OpenProfiles:
            _runtime->Refresh();
            if (_raised)
                _overlay.ShowProfiles();
            else
            {
                _tile.ShowProfiles();
                _openProfilesOnRaise = true;
                _action = RedXePointerRaise;
            }
            break;
        case ViewCommandKind::CloseProfiles:
            _tile.CloseProfiles();
            _overlay.CloseProfiles();
            _openProfilesOnRaise = false;
            if (_raised)
                _action = RedXePointerDismiss;
            break;
        case ViewCommandKind::ApplyProfile:
            if (command.value < _configuration.count)
            {
                const HRESULT result = _runtime->SubmitProfile(_configuration.profiles[command.value]);
                if (SUCCEEDED(result))
                {
                    _tile.CloseProfiles();
                    _overlay.CloseProfiles();
                    _openProfilesOnRaise = false;
                    if (_raised)
                        _action = RedXePointerDismiss;
                }
            }
            break;
        default:
            (void)_runtime->Submit(command);
            break;
        }
    }
    HRESULT Save(const Configuration& configuration) noexcept
    {
        std::array<char, MaximumSettingsBytes> json{};
        uint32_t bytes = 0;
        RETURN_IF_FAILED(SerializeConfiguration(configuration, json.data(), json.size(), bytes));
        RETURN_IF_FAILED(_runtime->Host()->PersistWidgetSettings(_instance.c_str(), json.data(), bytes));
        _configuration = configuration;
        _profilesDirty = true;
        _publishedRevision = 0;
        _runtime->ConfigurationChanged();
        (void)_runtime->Host()->RequestFrame();
        return S_OK;
    }
};

class Provider final : public RedXeComObject<Provider, IRedXeWidgetProvider>
{
  public:
    Provider(wil::com_ptr_nothrow<Coordinator> runtime, const Configuration& configuration)
        : _runtime(std::move(runtime)), _configuration(configuration)
    {
        ++providerCount;
    }
    ~Provider()
    {
        --providerCount;
    }
    HRESULT STDMETHODCALLTYPE GetWidgetTypes(const RedXeWidgetTypeDescriptor** descriptors,
                                             uint32_t* count) noexcept override
    {
        if (descriptors)
            *descriptors = nullptr;
        if (count)
            *count = 0;
        if (!descriptors || !count)
            return E_POINTER;
        *descriptors = &WidgetType;
        *count = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateWidget(const char* type, const char* instance,
                                           IRedXeWidget** widget) noexcept override
    {
        if (!widget)
            return E_POINTER;
        *widget = nullptr;
        if (!type || !instance || !instance[0] || strnlen_s(instance, 128) >= 128)
            return E_INVALIDARG;
        if (!RedXeAsciiEqualsIgnoreCase(type, TypeId))
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        try
        {
            wil::com_ptr_nothrow<IRedXeWidgetProvider> owner(this);
            wil::com_ptr_nothrow<Widget> created;
            created.attach(new Widget(std::move(owner), _runtime, _configuration, instance));
            RETURN_IF_FAILED(created->Initialize());
            *widget = created.detach();
            return S_OK;
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
    }

  private:
    wil::com_ptr_nothrow<Coordinator> _runtime;
    Configuration _configuration;
};
HRESULT CreateProvider(REFIID iid, const RedXeFactoryOptions* options, IRedXeHost* host, void** result) noexcept
{
    if (iid != __uuidof(IRedXeWidgetProvider))
        return E_NOINTERFACE;
    Configuration configuration;
    RETURN_IF_FAILED(ReadConfiguration(options, configuration));
    wil::com_ptr_nothrow<Coordinator> runtime;
    RETURN_IF_FAILED(GetCoordinator(host, runtime));
    auto* provider = new (std::nothrow) Provider(std::move(runtime), configuration);
    if (!provider)
        return E_OUTOFMEMORY;
    *result = static_cast<IRedXeWidgetProvider*>(provider);
    return S_OK;
}
constexpr RedXeFactoryEntry Entry{&Metadata, CreateProvider};
} // namespace
} // namespace AVControl
extern "C" HRESULT __stdcall RedXeCreate(REFIID iid, const RedXeFactoryOptions* options, IRedXeHost* host,
                                         const char* plugin, void** result) noexcept
{
    return RedXeCreateFromFactoryEntries(&AVControl::Entry, 1, iid, options, host, plugin, result);
}
extern "C" HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata, uint32_t* count) noexcept
{
    return RedXeEnumerateFactoryMetadata(&AVControl::Metadata, 1, metadata, count);
}
extern "C" HRESULT __stdcall RedXeGetPluginSettingsContract(const char* plugin,
                                                            const RedXePluginSettingsContract** contract) noexcept
{
    return RedXeGetStaticPluginSettingsContract(AVControl::PluginId, plugin, &AVControl::SettingsContract, contract);
}
extern "C" void __stdcall RedXePluginShutdown() noexcept
{
    AVControl::coordinator.reset();
    AVControl::graphics.reset();
}
extern "C" HRESULT __stdcall RedXeAVControlUseSyntheticBackend(BOOL enabled) noexcept
{
    if (AVControl::providerCount || AVControl::coordinator)
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    AVControl::syntheticBackend = enabled != FALSE;
    return S_OK;
}
extern "C" HRESULT __stdcall RedXeAVControlTestSnapshot(AVControl::Inventory* inventory, uint32_t bytes) noexcept
{
    if (!inventory)
        return E_POINTER;
    if (bytes != sizeof(*inventory))
        return E_INVALIDARG;
    if (!AVControl::coordinator)
        return E_UNEXPECTED;
    *inventory = AVControl::coordinator->Current();
    return S_OK;
}
