#define REDXE_PLUGIN_EXPORTS
#include "Actions/ActionTargets.h"
#include "PlugInterfaces/Action.h"
#include "PlugInterfaces/FactoryImpl.h"
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
// A dedicated action DLL (Plugins_Actions.md): no settings, no service, created on the first zoom.* execution.
constexpr std::array kMetadata{
    RedXePluginMetadata{sizeof(RedXePluginMetadata), Zoom::kPluginId, L"Zoom web",
                        L"Open the Zoom web join page or a meeting invite in the default browser.", L"RedXe", L"1.0.0",
                        RedXePluginCapabilityActions},
};

constexpr std::array kZoomActions{
    RedXeActionDescriptor{sizeof(RedXeActionDescriptor), RedXeActionFlagDeferred, "zoom.open", L"Open Zoom web", L"",
                          RedXeActionTargetNone, 0, 0, nullptr},
    RedXeActionDescriptor{sizeof(RedXeActionDescriptor), RedXeActionFlagDeferred, "zoom.join", L"Open Zoom meeting",
                          L"<https://zoom.us/j/... or /wc/.../join meeting link>", RedXeActionTargetMeeting, 0, 0,
                          nullptr},
};
constexpr std::array kZoomNamespaces{RedXeActionNamespace{sizeof(RedXeActionNamespace),
                                                          static_cast<uint32_t>(kZoomActions.size()),
                                                          Zoom::kActionNamespace, kZoomActions.data()}};
constexpr RedXeActionContract kZoomActionContract{
    sizeof(RedXeActionContract), static_cast<uint32_t>(kZoomNamespaces.size()), kZoomNamespaces.data()};

class ZoomActions final : public RedXeComObject<ZoomActions, IRedXeActionPack>
{
  public:
    explicit ZoomActions(IRedXeHost* host) noexcept : _host(host) {}

    // The host creates the executor with the empty {"plugin":{},"instance":{}} envelope; Zoom has no settings.
    [[nodiscard]] static HRESULT ParseConfiguration(const char* json, uint32_t bytes) noexcept
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
        const bool valid = yyjson_is_obj(root) && yyjson_obj_size(root) == 2 && yyjson_is_obj(plugin) &&
                           yyjson_obj_size(plugin) == 0 && yyjson_is_obj(instance) && yyjson_obj_size(instance) == 0;
        yyjson_doc_free(document);
        return valid ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    HRESULT STDMETHODCALLTYPE Execute(const RedXeActionRequest* request) noexcept override
    {
        if (!request || request->sizeBytes != sizeof(RedXeActionRequest) || !request->actionUtf8)
        {
            return E_INVALIDARG;
        }
        const std::string_view action(request->actionUtf8);
        const char* url = nullptr;
        if (action == "zoom.open")
        {
            // A None target: an authored target is ignored (Action.h).
            url = Zoom::kWebJoinPage;
        }
        else if (action == "zoom.join" && request->targetUtf8 && RedXeActions::ParseMeeting(request->targetUtf8))
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
        // Both actions are Deferred: the host drains the queued (or coalesced) system.launch after this returns and
        // opens the browser on its launch worker; a refused request is returned unchanged.
        const HRESULT queued = _host->RequestAction(&launch);
        return FAILED(queued) ? queued : S_FALSE;
    }

  private:
    // Borrowed from the process runtime, which releases this executor in PluginHost::Shutdown before it goes away.
    IRedXeHost* _host = nullptr;
};

HRESULT CreateZoomActions(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                          void** result) noexcept
{
    if (interfaceId != __uuidof(IRedXeActionPack))
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
    const HRESULT parsed = ZoomActions::ParseConfiguration(options ? options->configurationJsonUtf8 : nullptr,
                                                           options ? options->configurationBytes : 0);
    if (FAILED(parsed))
    {
        return parsed;
    }
    wil::com_ptr_nothrow<ZoomActions> actions;
    actions.attach(new (std::nothrow) ZoomActions(host));
    if (!actions)
    {
        return E_OUTOFMEMORY;
    }
    *result = static_cast<IRedXeActionPack*>(actions.detach());
    return S_OK;
}

constexpr std::array kFactoryEntries{RedXeFactoryEntry{&kMetadata[0], CreateZoomActions}};
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
