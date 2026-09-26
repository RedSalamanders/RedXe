#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/Action.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Service.h"
#include "ZoomSettings.h"

#include <array>
#include <cstdint>
#include <new>
#include <string_view>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

#include <yyjson.h>

namespace
{
constexpr RedXePluginSettingsContract kServiceContract{sizeof(RedXePluginSettingsContract), Zoom::kSettingsSchema,
                                                       sizeof(Zoom::kSettingsSchema) - 1, Zoom::kSettingsDefaults,
                                                       sizeof(Zoom::kSettingsDefaults) - 1};

constexpr std::array kMetadata{
    RedXePluginMetadata{sizeof(RedXePluginMetadata), Zoom::kPluginId, L"Zoom web",
                        L"Open the Zoom web join page or a meeting invite in the default browser.", L"RedXe", L"1.0.0",
                        RedXePluginCapabilityService | RedXePluginCapabilityActions},
};
constexpr std::array kSettingsContracts{RedXeSettingsContractEntry{Zoom::kPluginId, &kServiceContract}};

constexpr std::array kZoomActions{
    RedXeActionDescriptor{sizeof(RedXeActionDescriptor), RedXeActionFlagDeferred, "zoom.open", L"Open Zoom web", L"",
                          RedXeActionTargetNone, 0, 0, nullptr},
    RedXeActionDescriptor{sizeof(RedXeActionDescriptor), RedXeActionFlagDeferred, "zoom.join", L"Open Zoom meeting",
                          L"<https://zoom.us/j/... meeting invite>", RedXeActionTargetText, 0, 0, nullptr},
};
constexpr std::array kZoomNamespaces{RedXeActionNamespace{sizeof(RedXeActionNamespace),
                                                          static_cast<uint32_t>(kZoomActions.size()),
                                                          Zoom::kActionNamespace, kZoomActions.data()}};
constexpr RedXeActionContract kZoomActionContract{
    sizeof(RedXeActionContract), static_cast<uint32_t>(kZoomNamespaces.size()), kZoomNamespaces.data()};

class ZoomService final : public RedXeComObject<ZoomService, IRedXeService, IRedXeActionPack>
{
  public:
    explicit ZoomService(IRedXeHost* host) noexcept : _host(host) {}

    [[nodiscard]] HRESULT ParseConfiguration(const char* json, uint32_t bytes) noexcept
    {
        if (!json && bytes == 0)
        {
            return S_OK;
        }
        if (!json || bytes == 0)
        {
            return E_INVALIDARG;
        }
        yyjson_doc* document = yyjson_read(json, bytes, YYJSON_READ_NOFLAG);
        if (!document)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        yyjson_val* root = yyjson_doc_get_root(document);
        yyjson_val* plugin = yyjson_is_obj(root) ? yyjson_obj_get(root, "plugin") : nullptr;
        yyjson_val* instance = yyjson_is_obj(root) ? yyjson_obj_get(root, "instance") : nullptr;
        Zoom::Settings settings{};
        const bool valid = yyjson_is_obj(root) && yyjson_obj_size(root) == 2 && yyjson_is_obj(plugin) &&
                           yyjson_obj_size(plugin) == 0 &&
                           SUCCEEDED(Zoom::ParseSettings(instance, settings, nullptr, 0));
        yyjson_doc_free(document);
        return valid ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    HRESULT STDMETHODCALLTYPE Start(const RedXeServiceStartContext* context) noexcept override
    {
        if (!context || context->sizeBytes != sizeof(RedXeServiceStartContext))
        {
            return E_INVALIDARG;
        }
        _started = true;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ApplySettings(const char* json, uint32_t bytes) noexcept override
    {
        Zoom::Settings settings{};
        return json && bytes != 0 ? Zoom::ParseSettingsJson(std::string_view(json, bytes), settings, nullptr, 0)
                                  : E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE OnHostState(const RedXeHostState* state) noexcept override
    {
        return state && state->sizeBytes == sizeof(RedXeHostState) ? S_OK : E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE Stop() noexcept override
    {
        _started = false;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Execute(const RedXeActionRequest* request) noexcept override
    {
        if (!request || request->sizeBytes != sizeof(RedXeActionRequest) || !request->actionUtf8)
        {
            return E_INVALIDARG;
        }
        if (!_started || !_host)
        {
            return E_NOT_VALID_STATE;
        }
        const std::string_view action(request->actionUtf8);
        const std::string_view target(request->targetUtf8 ? request->targetUtf8 : "");
        const char* url = nullptr;
        if (action == "zoom.open" && target.empty())
        {
            url = Zoom::kWebJoinPage;
        }
        else if (action == "zoom.join" && Zoom::IsMeetingUrl(target))
        {
            url = request->targetUtf8;
        }
        else
        {
            return E_INVALIDARG;
        }
        RedXeActionRequest launch{};
        launch.sizeBytes = sizeof(launch);
        launch.actionUtf8 = "system.launch";
        launch.targetUtf8 = url;
        launch.sourcePluginId = Zoom::kPluginId;
        return _host->RequestAction(&launch);
    }

  private:
    IRedXeHost* _host = nullptr; // Borrowed from the process runtime, which stops the service before teardown.
    bool _started = false;       // Service and action execution both run on the UI thread.
};

HRESULT CreateZoomService(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                          void** result) noexcept
{
    if (interfaceId != __uuidof(IRedXeService))
    {
        return E_NOINTERFACE;
    }
    if (!host || !result)
    {
        return E_POINTER;
    }
    if (options && (options->sizeBytes != sizeof(RedXeFactoryOptions) ||
                    options->configurationBytes > kRedXeMaximumFactoryConfigurationBytes))
    {
        return E_INVALIDARG;
    }
    wil::com_ptr_nothrow<ZoomService> service;
    service.attach(new (std::nothrow) ZoomService(host));
    if (!service)
    {
        return E_OUTOFMEMORY;
    }
    const HRESULT parsed = service->ParseConfiguration(options ? options->configurationJsonUtf8 : nullptr,
                                                       options ? options->configurationBytes : 0);
    if (FAILED(parsed))
    {
        return parsed;
    }
    *result = static_cast<IRedXeService*>(service.detach());
    return S_OK;
}

constexpr std::array kFactoryEntries{RedXeFactoryEntry{&kMetadata[0], CreateZoomService}};
} // namespace

extern "C" HRESULT __stdcall RedXeCreate(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                         const char* pluginId, void** result) noexcept
{
    return RedXeCreateFromFactoryEntries(kFactoryEntries.data(), static_cast<uint32_t>(kFactoryEntries.size()),
                                         interfaceId, options, host, pluginId, result);
}

extern "C" HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata, uint32_t* count) noexcept
{
    return RedXeEnumerateFactoryMetadata(kMetadata.data(), static_cast<uint32_t>(kMetadata.size()), metadata, count);
}

extern "C" HRESULT __stdcall RedXeGetPluginSettingsContract(const char* pluginId,
                                                            const RedXePluginSettingsContract** contract) noexcept
{
    return RedXeGetPluginSettingsContractFromEntries(
        kSettingsContracts.data(), static_cast<uint32_t>(kSettingsContracts.size()), pluginId, contract);
}

extern "C" HRESULT __stdcall RedXeGetActionContract(const char* pluginId, const RedXeActionContract** contract) noexcept
{
    if (!contract)
    {
        return E_POINTER;
    }
    *contract = nullptr;
    if (!RedXeAsciiEqualsIgnoreCase(pluginId, Zoom::kPluginId))
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    *contract = &kZoomActionContract;
    return S_OK;
}
