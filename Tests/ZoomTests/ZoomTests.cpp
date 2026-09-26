#include "PlugInterfaces/Action.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Service.h"
#include "ZoomSettings.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
bool Check(bool condition, const wchar_t* message) noexcept
{
    if (!condition)
    {
        std::fwprintf(stderr, L"FAIL: %ls\n", message);
    }
    return condition;
}

class TestHost final : public IRedXeHost
{
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (id != __uuidof(IUnknown) && id != __uuidof(IRedXeHost))
        {
            return E_NOINTERFACE;
        }
        *result = static_cast<IRedXeHost*>(this);
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return 2;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        return 1;
    }
    HRESULT STDMETHODCALLTYPE GetDataProvider(const char*, IRedXeDataProvider** provider) noexcept override
    {
        if (provider)
        {
            *provider = nullptr;
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    HRESULT STDMETHODCALLTYPE RequestFrame() noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReportWidgetStatus(const char*, const RedXeWidgetStatusReport*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PersistWidgetSettings(const char*, const char*, uint32_t) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueueControlWork(IRedXeControlWork*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE RequestAction(const RedXeActionRequest* request) noexcept override
    {
        if (!request || request->sizeBytes != sizeof(RedXeActionRequest) || !request->actionUtf8)
        {
            return E_INVALIDARG;
        }
        (void)strncpy_s(action.data(), action.size(), request->actionUtf8, _TRUNCATE);
        (void)strncpy_s(target.data(), target.size(), request->targetUtf8 ? request->targetUtf8 : "", _TRUNCATE);
        ++requests;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ExecuteAction(const RedXeActionRequest* request) noexcept override
    {
        return RequestAction(request);
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

    std::array<char, 64> action{};
    std::array<char, 1025> target{};
    uint32_t requests = 0;
};

template <typename Function> Function Resolve(HMODULE module, const char* name) noexcept
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

bool TestSettingsAndUrls() noexcept
{
    bool success = true;
    Zoom::Settings settings{};
    std::array<char, 160> diagnostic{};
    success &= Check(
        SUCCEEDED(Zoom::ParseSettingsJson(Zoom::kSettingsDefaults, settings, diagnostic.data(), diagnostic.size())),
        L"empty browser settings accepted");
    success &=
        Check(FAILED(Zoom::ParseSettingsJson(R"({"clientId":"x"})", settings, diagnostic.data(), diagnostic.size())),
              L"Marketplace client id rejected");
    success &= Check(
        FAILED(Zoom::ParseSettingsJson(R"({"domain":"evil.example"})", settings, diagnostic.data(), diagnostic.size())),
        L"token domain removed");
    success &= Check(Zoom::IsMeetingUrl("https://zoom.us/j/1234567890?pwd=opaque%2Bvalue") &&
                         Zoom::IsMeetingUrl("https://team.zoom.us/j/987654321"),
                     L"Zoom meeting links accepted");
    for (const std::string_view invalid :
         {"http://zoom.us/j/1234567890", "https://evil.example/j/1234567890",
          "https://zoom.us.evil.example/j/1234567890", "https://zoom.us@evil.example/j/1234567890",
          "https://zoom.us/j/123", "https://zoom.us/j/1234567890/other", "https://zoom.us/j/1234567890\n"})
    {
        success &= Check(!Zoom::IsMeetingUrl(invalid), L"unsafe or malformed meeting link rejected");
    }
    return success;
}

bool TestPlugin() noexcept
{
    std::array<wchar_t, 32768> modulePath{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (!Check(length != 0 && length < modulePath.size(), L"test executable path"))
    {
        return false;
    }
    wchar_t* separator = wcsrchr(modulePath.data(), L'\\');
    if (!Check(separator != nullptr, L"test executable directory"))
    {
        return false;
    }
    (void)wcscpy_s(separator + 1, modulePath.size() - static_cast<size_t>(separator + 1 - modulePath.data()),
                   L"Plugins\\zoom.action.dll");
    wil::unique_hmodule module{LoadLibraryW(modulePath.data())};
    if (!Check(module != nullptr, L"zoom.action.dll maps"))
    {
        return false;
    }
    const auto enumerate = Resolve<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    const auto create = Resolve<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const auto settingsContract =
        Resolve<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);
    const auto actionContract = Resolve<RedXeGetActionContractFn>(module.get(), kRedXeGetActionContractExport);
    if (!Check(enumerate && create && settingsContract && actionContract, L"Zoom exports"))
    {
        return false;
    }
    bool success = true;
    const RedXePluginMetadata* metadata = nullptr;
    uint32_t count = 0;
    success &= Check(SUCCEEDED(enumerate(&metadata, &count)) && count == 1 && metadata &&
                         std::strcmp(metadata[0].id, Zoom::kPluginId) == 0,
                     L"one Zoom service metadata row");
    const RedXePluginSettingsContract* settings = nullptr;
    success &= Check(SUCCEEDED(settingsContract(Zoom::kPluginId, &settings)) && settings &&
                         std::string_view(settings->defaultsJsonUtf8, settings->defaultsBytes) == "{}",
                     L"Zoom has empty settings defaults");
    const RedXeActionContract* actions = nullptr;
    success &= Check(SUCCEEDED(actionContract(Zoom::kPluginId, &actions)) && actions && actions->namespaceCount == 1 &&
                         actions->namespaces[0].actionCount == 2 &&
                         std::strcmp(actions->namespaces[0].actions[0].name, "zoom.open") == 0 &&
                         std::strcmp(actions->namespaces[0].actions[1].name, "zoom.join") == 0,
                     L"only browser actions are published");

    TestHost host;
    constexpr char envelope[] = R"({"plugin":{},"instance":{}})";
    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
    options.configurationJsonUtf8 = envelope;
    options.configurationBytes = sizeof(envelope) - 1;
    void* object = nullptr;
    success &= Check(SUCCEEDED(create(__uuidof(IRedXeService), &options, &host, Zoom::kPluginId, &object)) && object,
                     L"browser service created");
    if (!object)
    {
        return false;
    }
    wil::com_ptr_nothrow<IRedXeService> service;
    service.attach(static_cast<IRedXeService*>(object));
    wil::com_ptr_nothrow<IRedXeActionPack> pack;
    success &= Check(SUCCEEDED(service.query_to(pack.put())) && pack, L"action pack shares service object");
    if (!pack)
    {
        return false;
    }
    RedXeServiceStartContext start{};
    start.sizeBytes = sizeof(start);
    success &= Check(SUCCEEDED(service->Start(&start)), L"browser service starts without a device lane");
    RedXeActionRequest request{};
    request.sizeBytes = sizeof(request);
    request.actionUtf8 = "zoom.open";
    success &= Check(SUCCEEDED(pack->Execute(&request)) && std::strcmp(host.action.data(), "system.launch") == 0 &&
                         std::strcmp(host.target.data(), Zoom::kWebJoinPage) == 0,
                     L"zoom.open forwards the web join page");
    constexpr char meeting[] = "https://team.zoom.us/j/1234567890?pwd=opaque%2Bvalue";
    request.actionUtf8 = "zoom.join";
    request.targetUtf8 = meeting;
    success &= Check(SUCCEEDED(pack->Execute(&request)) && std::strcmp(host.target.data(), meeting) == 0,
                     L"zoom.join preserves the invite URL");
    const uint32_t before = host.requests;
    request.targetUtf8 = "https://evil.example/j/1234567890";
    success &=
        Check(pack->Execute(&request) == E_INVALIDARG && host.requests == before, L"bad meeting URL cannot launch");
    success &= Check(SUCCEEDED(service->Stop()) && pack->Execute(&request) == E_NOT_VALID_STATE,
                     L"stopped service rejects actions");
    return success;
}
} // namespace

int wmain()
{
    return TestSettingsAndUrls() && TestPlugin() ? 0 : 1;
}
