#include "../../Common/FailureReports.h"
#include "Actions/ActionTargets.h"
#include "PlugInterfaces/Action.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Service.h"
#include "ZoomSettings.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
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
        return requestResult;
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
    // What RequestAction returns: S_OK queued, S_FALSE coalesced, or a failure such as a full ring.
    HRESULT requestResult = S_OK;
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
    // The retired services entry of earlier releases: { "plugin": "builtin.zoom" } leaves an empty object, and a
    // v1.0.102 file keeps its Zoom SDK members, each of which loads with any value.
    constexpr std::string_view releasedTemplate =
        R"({"clientId":"sHVWQENoR4qrpuBPgsFsPw","redirectPort":48123,"autoConnect":false})";
    constexpr std::string_view anyRetiredValue =
        R"({"clientId":null,"redirectPort":"x","domain":"evil.example","displayName":[],"autoConnect":1,"mode":"bogus","labels":{"unknown":0}})";
    for (const std::string_view retired :
         {std::string_view("{}"), releasedTemplate, anyRetiredValue, std::string_view(R"({"clientId":null})")})
    {
        success &= Check(SUCCEEDED(Zoom::ParseSettingsJson(retired, settings, diagnostic.data(), diagnostic.size())),
                         L"the retired entry loads with or without the v1.0.102 members");
    }
    // An unknown member is refused whatever its value, null included.
    for (const std::string_view rejected :
         {R"({"meeting":"x"})", R"({"meeting":null})", R"({"clientId":null,"x":null})",
          R"({"clientId":"x","ClientId":"x"})", "[]", "\"x\""})
    {
        success &= Check(FAILED(Zoom::ParseSettingsJson(rejected, settings, diagnostic.data(), diagnostic.size())),
                         L"another member or a non-object rejected");
    }

    // RedXeActions::ParseMeeting is the zoom.join grammar for host validation and the pack alike.
    for (const std::string_view valid :
         {"https://zoom.us/j/1234567890?pwd=opaque%2Bvalue", "https://team.zoom.us/j/987654321",
          "https://Team.Zoom.US/j/123456789", "https://us06web.zoom.us/j/12345678901#success",
          "https://app.zoom.us/wc/12345678901/join?fromPWA=1&pwd=opaque", "https://zoom.us/wc/join/1234567890",
          "https://us02web.zoom.us/wc/join/1234567890?pwd=a@b"})
    {
        success &= Check(RedXeActions::ParseMeeting(valid), L"Zoom meeting and browser-join links accepted");
    }
    std::string overlong = "https://zoom.us/j/1234567890?pwd=";
    overlong.append(kRedXeMaximumActionTargetBytes - overlong.size(), 'a');
    success &= Check(RedXeActions::ParseMeeting(overlong) && !RedXeActions::ParseMeeting(overlong + "a"),
                     L"a meeting link is at most 512 bytes");
    for (const std::string_view invalid :
         {"http://zoom.us/j/1234567890", "HTTPS://zoom.us/j/1234567890", "https://evil.example/j/1234567890",
          "https://zoom.us.evil.example/j/1234567890", "https://zoom.us@evil.example/j/1234567890",
          // Authority delimiters a browser ends the host at: each would otherwise open evil.example.
          "https://evil.example?.zoom.us/j/1234567890", "https://evil.example#.zoom.us/j/1234567890",
          "https://evil.example\\.zoom.us/j/1234567890", "https://evil.example@team.zoom.us/j/1234567890",
          "https://user@team.zoom.us/j/1234567890", "https://zoom.us:443/j/1234567890",
          "https://team.zoom.us:8443/j/1234567890", "https://.zoom.us/j/1234567890", "https:///j/1234567890",
          // An empty DNS label: the suffix matches, but the host is no zoom.us subdomain.
          "https://a..zoom.us/j/1234567890", "https://..zoom.us/j/1234567890", "https://team..zoom.us/j/1234567890",
          "https://team.zoom.us./j/1234567890",
          // Path and character rules.
          "https://zoom.us/j/123", "https://zoom.us/j/123456789012", "https://zoom.us/j/12345678a0",
          "https://zoom.us/j/1234567890/other", "https://zoom.us/j/1234567890\n", "https://zoom.us/j/1234567890 ",
          "https://zoom.us/j/1234567890?pwd=a\"b", "https://zoom.us/my/alice", "https://us06web.zoom.us/w/81234567890",
          "https://zoom.us/wc/join/123", "https://zoom.us/wc/1234567890/start", "https://zoom.us/wc/join/1234567890/x",
          "https://zoom.us/wc/join", "https://zoom.us", "zoom.us/j/1234567890", "1234567890:passcode", ""})
    {
        success &= Check(!RedXeActions::ParseMeeting(invalid), L"unsafe or malformed meeting link rejected");
    }
    // The host is a DNS name: labels of 1 to 63 characters, at most 253 characters in all.
    const std::string label63(63, 'a');
    const std::string longestHost = label63 + "." + label63 + "." + label63 + "." + std::string(53, 'b') + ".zoom.us";
    const std::string tooLongHost = label63 + "." + label63 + "." + label63 + "." + std::string(54, 'b') + ".zoom.us";
    success &= Check(longestHost.size() == 253 && tooLongHost.size() == 254, L"the host length fixtures");
    success &= Check(RedXeActions::ParseMeeting("https://" + label63 + ".zoom.us/j/1234567890") &&
                         RedXeActions::ParseMeeting("https://" + longestHost + "/j/1234567890"),
                     L"a 63-character label and a 253-character host accepted");
    success &= Check(!RedXeActions::ParseMeeting("https://" + label63 + "a.zoom.us/j/1234567890") &&
                         !RedXeActions::ParseMeeting("https://" + tooLongHost + "/j/1234567890"),
                     L"a 64-character label and a 254-character host rejected");
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
    const auto actionContract = Resolve<RedXeGetActionContractFn>(module.get(), kRedXeGetActionContractExport);
    if (!Check(enumerate && create && actionContract, L"Zoom exports"))
    {
        return false;
    }
    bool success = true;
    success &= Check(!Resolve<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport),
                     L"the dedicated action DLL publishes no settings contract");
    const RedXePluginMetadata* metadata = nullptr;
    uint32_t count = 0;
    success &= Check(SUCCEEDED(enumerate(&metadata, &count)) && count == 1 && metadata &&
                         std::strcmp(metadata[0].id, Zoom::kPluginId) == 0 &&
                         metadata[0].capabilities == RedXePluginCapabilityActions,
                     L"one Zoom metadata row that only publishes actions");
    const RedXeActionContract* actions = nullptr;
    success &= Check(SUCCEEDED(actionContract(Zoom::kPluginId, &actions)) && actions && actions->namespaceCount == 1 &&
                         actions->namespaces[0].actionCount == 2 &&
                         std::strcmp(actions->namespaces[0].actions[0].name, "zoom.open") == 0 &&
                         actions->namespaces[0].actions[0].targetKind == RedXeActionTargetNone &&
                         actions->namespaces[0].actions[0].flags == RedXeActionFlagDeferred &&
                         std::strcmp(actions->namespaces[0].actions[1].name, "zoom.join") == 0 &&
                         actions->namespaces[0].actions[1].targetKind == RedXeActionTargetMeeting &&
                         actions->namespaces[0].actions[1].flags == RedXeActionFlagDeferred,
                     L"only the deferred browser actions are published, zoom.join with the meeting grammar");

    TestHost host;
    constexpr char envelope[] = R"({"plugin":{},"instance":{}})";
    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
    options.configurationJsonUtf8 = envelope;
    options.configurationBytes = sizeof(envelope) - 1;
    wil::com_ptr_nothrow<IUnknown> service;
    success &=
        Check(create(__uuidof(IRedXeService), &options, &host, Zoom::kPluginId, service.put_void()) == E_NOINTERFACE &&
                  !service,
              L"Zoom is not a service");
    // The host creates the executor once, with the empty envelope, on the first zoom.* execution.
    void* object = nullptr;
    success &= Check(SUCCEEDED(create(__uuidof(IRedXeActionPack), &options, &host, Zoom::kPluginId, &object)) && object,
                     L"the action pack is created from the empty envelope");
    if (!object)
    {
        return false;
    }
    wil::com_ptr_nothrow<IRedXeActionPack> pack;
    pack.attach(static_cast<IRedXeActionPack*>(object));
    wil::com_ptr_nothrow<IRedXeService> serviceIdentity;
    success &= Check(FAILED(pack.query_to(serviceIdentity.put())) && !serviceIdentity,
                     L"the action pack has no service identity");
    constexpr char settingsEnvelope[] = R"({"plugin":{},"instance":{"clientId":"sHVWQENoR4qrpuBPgsFsPw"}})";
    RedXeFactoryOptions settingsOptions = options;
    settingsOptions.configurationJsonUtf8 = settingsEnvelope;
    settingsOptions.configurationBytes = sizeof(settingsEnvelope) - 1;
    wil::com_ptr_nothrow<IRedXeActionPack> withSettings;
    success &= Check(
        FAILED(create(__uuidof(IRedXeActionPack), &settingsOptions, &host, Zoom::kPluginId, withSettings.put_void())) &&
            !withSettings,
        L"the action pack takes no settings");

    // Both actions are deferred: a queued or coalesced launch is S_FALSE, never completion.
    RedXeActionRequest request{};
    request.sizeBytes = sizeof(request);
    request.actionUtf8 = "zoom.open";
    success &= Check(pack->Execute(&request) == S_FALSE && std::strcmp(host.action.data(), "system.launch") == 0 &&
                         std::strcmp(host.target.data(), Zoom::kWebJoinPage) == 0,
                     L"zoom.open forwards the web join page and reports itself deferred");
    request.targetUtf8 = "https://zoom.us/j/1234567890";
    success &= Check(pack->Execute(&request) == S_FALSE && std::strcmp(host.target.data(), Zoom::kWebJoinPage) == 0,
                     L"zoom.open ignores an authored target");
    constexpr char meeting[] = "https://team.zoom.us/j/1234567890?pwd=opaque%2Bvalue";
    request.actionUtf8 = "zoom.join";
    request.targetUtf8 = meeting;
    success &= Check(pack->Execute(&request) == S_FALSE && std::strcmp(host.target.data(), meeting) == 0,
                     L"zoom.join preserves the invite URL and reports itself deferred");
    constexpr char browserJoin[] = "https://app.zoom.us/wc/12345678901/join?fromPWA=1&pwd=opaque";
    request.targetUtf8 = browserJoin;
    success &= Check(pack->Execute(&request) == S_FALSE && std::strcmp(host.target.data(), browserJoin) == 0,
                     L"zoom.join preserves a browser-join link");
    host.requestResult = S_FALSE;
    success &= Check(pack->Execute(&request) == S_FALSE, L"a coalesced launch is still deferred");
    host.requestResult = HRESULT_FROM_WIN32(ERROR_BUSY);
    success &= Check(pack->Execute(&request) == HRESULT_FROM_WIN32(ERROR_BUSY), L"a full host ring is reported");
    host.requestResult = S_OK;
    const uint32_t before = host.requests;
    for (const char* invalid : {"https://evil.example/j/1234567890", "https://evil.example#.zoom.us/j/1234567890",
                                static_cast<const char*>(nullptr)})
    {
        request.targetUtf8 = invalid;
        success &= Check(pack->Execute(&request) == E_INVALIDARG && host.requests == before,
                         L"a bad or missing meeting link cannot launch");
    }
    request.actionUtf8 = "zoom.mute";
    request.targetUtf8 = nullptr;
    success &=
        Check(pack->Execute(&request) == E_INVALIDARG && host.requests == before, L"an unpublished verb cannot launch");
    return success;
}
} // namespace

int wmain()
{
    RedXeFailureReports::RouteAwayFromDialogs();
    return TestSettingsAndUrls() && TestPlugin() ? 0 : 1;
}
