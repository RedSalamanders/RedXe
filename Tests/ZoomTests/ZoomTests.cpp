// ZoomTests: the Zoom service without Zoom, a network, or the SDK. Covers the settings model, the OAuth PKCE
// material and token parsing, the loopback redirect listener driven by a local client socket, the in-memory
// credential store, and the shipped zoom.action.dll driven through the synthetic session: contracts, sign-in,
// connection, every action, role refusals, reconnect, and stop.

#include "Actions/ActionTargets.h"
#include "PlugInterfaces/Action.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Service.h"
#include "PlugInterfaces/Widget.h"
#include "ZoomAuth.h"
#include "ZoomSession.h"
#include "ZoomSettings.h"
#include "ZoomTestContract.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr HRESULT kTestFailure = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

#define ZOOM_CHECK(condition, message)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::wprintf(L"FAIL %S (%S:%d)\n", message, __FILE__, __LINE__);                                           \
            return kTestFailure;                                                                                       \
        }                                                                                                              \
    } while (false)

template <typename Function> [[nodiscard]] Function Resolve(HMODULE module, const char* name) noexcept
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

[[nodiscard]] HRESULT BuildSiblingPath(const wchar_t* relativePath, std::array<wchar_t, 1024>& path) noexcept
{
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return E_FAIL;
    }
    wchar_t* separator = wcsrchr(path.data(), L'\\');
    if (!separator)
    {
        return E_FAIL;
    }
    separator[1] = L'\0';
    return wcscat_s(path.data(), path.size(), relativePath) == 0 ? S_OK : E_FAIL;
}

template <typename Predicate> [[nodiscard]] bool WaitUntil(Predicate predicate, DWORD milliseconds) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    while (GetTickCount64() < deadline)
    {
        if (predicate())
        {
            return true;
        }
        Sleep(10);
    }
    return predicate();
}

// A host that records every action the service requests (zoom.signIn launches the browser through the host).
class TestHost final : public IRedXeHost
{
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeHost))
        {
            *result = static_cast<IRedXeHost*>(this);
            return S_OK;
        }
        return E_NOINTERFACE;
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
    HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord* record) noexcept override
    {
        if (!record || record->sizeBytes != sizeof(RedXeLogRecord) || !record->eventId)
        {
            return E_INVALIDARG;
        }
        {
            const auto guard = wil::AcquireSRWLockExclusive(&lock);
            strncpy_s(lastEvent, record->eventId, _TRUNCATE);
        }
        logs.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueueControlWork(IRedXeControlWork*) noexcept override
    {
        return E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE RequestAction(const RedXeActionRequest* request) noexcept override
    {
        if (!request || request->sizeBytes != sizeof(RedXeActionRequest) || !request->actionUtf8)
        {
            return E_INVALIDARG;
        }
        {
            const auto guard = wil::AcquireSRWLockExclusive(&lock);
            strncpy_s(lastAction, request->actionUtf8, _TRUNCATE);
            strncpy_s(lastTarget, request->targetUtf8 ? request->targetUtf8 : "", _TRUNCATE);
        }
        actions.fetch_add(1, std::memory_order_relaxed);
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
    [[nodiscard]] bool SawEvent(const char* eventId) noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&lock);
        return std::strcmp(lastEvent, eventId) == 0;
    }
    [[nodiscard]] std::string LastTarget() noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&lock);
        return lastTarget;
    }
    [[nodiscard]] bool LastActionIs(const char* name) noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&lock);
        return std::strcmp(lastAction, name) == 0;
    }

    std::atomic<uint32_t> logs{0};
    std::atomic<uint32_t> actions{0};
    SRWLOCK lock = SRWLOCK_INIT;
    char lastEvent[65]{};
    char lastAction[65]{};
    char lastTarget[1025]{};
};

[[nodiscard]] HRESULT TestSettingsModel() noexcept
{
    using namespace Zoom;
    Settings settings{};
    std::array<char, 160> diagnostic{};
    ZOOM_CHECK(FAILED(ParseSettingsJson(kSettingsDefaults, settings, diagnostic.data(), diagnostic.size())),
               "the defaults alone are incomplete: clientId is required");
    ZOOM_CHECK(SUCCEEDED(ParseSettingsJson(R"json({"clientId":"abc123"})json", settings, diagnostic.data(),
                                           diagnostic.size())) &&
                   settings.ClientId() == "abc123" && settings.redirectPort == kDefaultRedirectPort &&
                   settings.Domain() == "zoom.us" && settings.DisplayName().empty() && !settings.autoConnect,
               "a client id with defaults parses");
    ZOOM_CHECK(
        SUCCEEDED(ParseSettingsJson(
            R"json({"clientId":"abc","redirectPort":50000,"domain":"zoom.example","displayName":"Desk","autoConnect":true})json",
            settings, diagnostic.data(), diagnostic.size())) &&
            settings.redirectPort == 50000 && settings.Domain() == "zoom.example" && settings.DisplayName() == "Desk" &&
            settings.autoConnect,
        "every member parses");
    ZOOM_CHECK(FAILED(ParseSettingsJson(R"json({"clientId":"abc","redirectPort":80})json", settings, diagnostic.data(),
                                        diagnostic.size())),
               "a privileged port is rejected");
    ZOOM_CHECK(FAILED(ParseSettingsJson(R"json({"clientId":"abc","clientSecret":"x"})json", settings, diagnostic.data(),
                                        diagnostic.size())),
               "a secret member is rejected");
    ZOOM_CHECK(FAILED(ParseSettingsJson(R"json({"clientId":""})json", settings, diagnostic.data(), diagnostic.size())),
               "an empty client id is rejected");
    return S_OK;
}

[[nodiscard]] HRESULT TestAuthMaterial() noexcept
{
    using namespace Zoom;
    std::array<char, kVerifierCharacters + 1> verifier{};
    std::array<char, kVerifierCharacters + 1> other{};
    ZOOM_CHECK(GenerateVerifier(verifier.data(), verifier.size()) && GenerateVerifier(other.data(), other.size()) &&
                   std::strlen(verifier.data()) == kVerifierCharacters &&
                   std::strcmp(verifier.data(), other.data()) != 0,
               "verifiers are 64 base64url characters and random");
    for (const char character : std::string_view{verifier.data()})
    {
        const bool allowed = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                             (character >= '0' && character <= '9') || character == '-' || character == '_';
        ZOOM_CHECK(allowed, "verifier alphabet is base64url");
    }
    // RFC 7636 appendix B vector.
    std::array<char, kChallengeCharacters + 1> challenge{};
    ZOOM_CHECK(ComputeChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", challenge.data(), challenge.size()) &&
                   std::string_view{challenge.data()} == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
               "S256 challenge matches the RFC 7636 vector");
    std::array<char, 8> base64{};
    const uint8_t sample[3]{0xFB, 0xFF, 0xBF};
    ZOOM_CHECK(Base64UrlEncode(sample, 3, base64.data(), base64.size()) && std::string_view{base64.data()} == "-_-_" &&
                   Base64UrlEncode(sample, 2, base64.data(), base64.size()) && std::string_view{base64.data()} == "-_8",
               "base64url uses - and _ without padding");
    std::array<char, kMaximumUrlBytes> url{};
    ZOOM_CHECK(BuildAuthorizeUrl("zoom.us", "cl id", 48199, "CHALLENGE", "STATE", url.data(), url.size()) &&
                   std::string_view{url.data()} ==
                       "https://zoom.us/oauth/authorize?response_type=code&client_id=cl%20id&redirect_uri="
                       "http%3A%2F%2F127.0.0.1%3A48199%2Fredirect&code_challenge=CHALLENGE&code_challenge_method=S256&"
                       "state=STATE",
               "the authorize URL carries the PKCE challenge and the encoded loopback redirect");
    TokenSet tokens{};
    ZOOM_CHECK(
        SUCCEEDED(ParseTokenResponse(
            R"({"access_token":"AT","token_type":"bearer","refresh_token":"RT","expires_in":3600})", 1000, tokens)) &&
            tokens.AccessToken() == "AT" && tokens.RefreshToken() == "RT" && tokens.expiresAt == 4600,
        "a token response parses");
    ZOOM_CHECK(SUCCEEDED(ParseTokenResponse(R"({"access_token":"AT2","expires_in":60})", 5000, tokens)) &&
                   tokens.AccessToken() == "AT2" && tokens.RefreshToken() == "RT" && tokens.expiresAt == 5060,
               "a response without a refresh token keeps the previous one");
    ZOOM_CHECK(ParseTokenResponse(R"({"error":"invalid_grant"})", 0, tokens) == E_ACCESSDENIED,
               "an error response is E_ACCESSDENIED");
    ZOOM_CHECK(FAILED(ParseTokenResponse(R"({"refresh_token":"only"})", 0, tokens)),
               "a response without an access token is rejected");
    MemoryCredentialStore store;
    std::array<char, kMaximumTokenBytes + 1> read{};
    uint32_t bytes = 0;
    ZOOM_CHECK(store.Read("id", read.data(), read.size(), bytes) == S_FALSE && bytes == 0, "empty store reads S_FALSE");
    ZOOM_CHECK(SUCCEEDED(store.Write("id", "refresh")) && store.Read("id", read.data(), read.size(), bytes) == S_OK &&
                   std::string_view(read.data(), bytes) == "refresh",
               "a written credential reads back");
    ZOOM_CHECK(store.Delete("id") == S_OK && store.Delete("id") == S_FALSE, "delete is idempotent");
    return S_OK;
}

// Sends one HTTP GET to the listener from a client socket, the way a browser redirect does.
[[nodiscard]] bool SendRedirect(uint16_t port, const char* requestLine) noexcept
{
    const SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET)
    {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool ok = connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    if (ok)
    {
        std::string request = requestLine;
        request += "\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        ok = send(client, request.c_str(), static_cast<int>(request.size()), 0) == static_cast<int>(request.size());
    }
    // The listener answers only when its owner pumps it, so the client never waits for the reply: it half-closes
    // and leaves; the request bytes still arrive ahead of the FIN.
    (void)shutdown(client, SD_SEND);
    closesocket(client);
    return ok;
}

[[nodiscard]] HRESULT TestLoopbackListener() noexcept
{
    using namespace Zoom;
    WSADATA data{};
    ZOOM_CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0, "winsock");
    const auto cleanup = wil::scope_exit([]() noexcept { WSACleanup(); });
    LoopbackListener listener;
    ZOOM_CHECK(SUCCEEDED(listener.Start(0)) && listener.Running() && listener.Port() != 0 && listener.Event(),
               "the listener binds an ephemeral loopback port");
    std::array<char, kMaximumCodeBytes + 1> code{};
    ZOOM_CHECK(listener.Pump("STATE", code.data(), code.size()) == S_FALSE, "nothing pending yet");
    ZOOM_CHECK(SendRedirect(listener.Port(), "GET /redirect?code=first&state=WRONG HTTP/1.1"), "wrong-state redirect");
    HRESULT pumped = S_FALSE;
    // Pump for a while: the wrong state is answered with 400 and the listener keeps running.
    for (int tick = 0; tick < 50 && listener.Running(); ++tick)
    {
        if (WaitForSingleObject(listener.Event(), 10) == WAIT_OBJECT_0)
        {
            pumped = listener.Pump("STATE", code.data(), code.size());
        }
    }
    ZOOM_CHECK(pumped == S_FALSE, "a redirect with the wrong state keeps listening");
    ZOOM_CHECK(listener.Running() && code[0] == '\0', "the wrong-state code was not accepted");
    ZOOM_CHECK(SendRedirect(listener.Port(), "GET /redirect?state=STATE&code=the-code HTTP/1.1"), "good redirect");
    pumped = S_FALSE;
    ZOOM_CHECK(WaitUntil(
                   [&]() noexcept
                   {
                       if (listener.Running() && WaitForSingleObject(listener.Event(), 50) == WAIT_OBJECT_0)
                       {
                           pumped = listener.Pump("STATE", code.data(), code.size());
                       }
                       return pumped == S_OK;
                   },
                   3000),
               "the redirect with the expected state delivers its code");
    ZOOM_CHECK(std::string_view{code.data()} == "the-code" && !listener.Running(),
               "the code is copied and the listener stops");
    return S_OK;
}

[[nodiscard]] HRESULT TestShippedModule() noexcept
{
    std::array<wchar_t, 1024> path{};
    ZOOM_CHECK(SUCCEEDED(BuildSiblingPath(L"Plugins\\zoom.action.dll", path)), "plugin path");
    wil::unique_hmodule module{
        LoadLibraryExW(path.data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    ZOOM_CHECK(module != nullptr, "zoom.action.dll maps");
    const auto create = Resolve<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const auto enumerate = Resolve<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    const auto contract =
        Resolve<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);
    const auto actionContract = Resolve<RedXeGetActionContractFn>(module.get(), kRedXeGetActionContractExport);
    const auto diagnostics = Resolve<RedXeZoomGetTestDiagnosticsFn>(module.get(), kRedXeZoomGetTestDiagnosticsExport);
    const auto useSynthetic =
        Resolve<RedXeZoomUseSyntheticSessionFn>(module.get(), kRedXeZoomUseSyntheticSessionExport);
    const auto seed = Resolve<RedXeZoomSeedCredentialFn>(module.get(), kRedXeZoomSeedCredentialExport);
    const auto setHost = Resolve<RedXeZoomSyntheticSetHostFn>(module.get(), kRedXeZoomSyntheticSetHostExport);
    const auto drop =
        Resolve<RedXeZoomSyntheticDropConnectionFn>(module.get(), kRedXeZoomSyntheticDropConnectionExport);
    const auto counters = Resolve<RedXeZoomSyntheticCountersFn>(module.get(), kRedXeZoomSyntheticCountersExport);
    const auto stored = Resolve<RedXeZoomStoredCredentialFn>(module.get(), kRedXeZoomStoredCredentialExport);
    ZOOM_CHECK(create && enumerate && contract && actionContract && diagnostics && useSynthetic && seed && setHost &&
                   drop && counters && stored,
               "required and test exports resolve");

    const RedXePluginMetadata* metadata = nullptr;
    uint32_t count = 0;
    ZOOM_CHECK(SUCCEEDED(enumerate(&metadata, &count)) && metadata && count == 1 &&
                   RedXeAsciiEqualsIgnoreCase(metadata[0].id, Zoom::kPluginId) &&
                   metadata[0].capabilities == (RedXePluginCapabilityService | RedXePluginCapabilityActions),
               "metadata: one service that publishes actions");
    const RedXePluginSettingsContract* settingsContract = nullptr;
    ZOOM_CHECK(SUCCEEDED(contract(Zoom::kPluginId, &settingsContract)) && settingsContract &&
                   settingsContract->sizeBytes == sizeof(RedXePluginSettingsContract),
               "settings contract");
    ZOOM_CHECK(contract("builtin.other", &settingsContract) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
               "unknown id has no settings contract");
    const RedXeActionContract* actions = nullptr;
    ZOOM_CHECK(SUCCEEDED(actionContract(Zoom::kPluginId, &actions)) && actions &&
                   actions->sizeBytes == sizeof(RedXeActionContract) && actions->namespaceCount == 1 &&
                   std::strcmp(actions->namespaces[0].name, "zoom") == 0 && actions->namespaces[0].actionCount >= 17,
               "the zoom action contract publishes one namespace");
    bool contractValid = true;
    bool sawMute = false;
    for (uint32_t index = 0; index < actions->namespaces[0].actionCount; ++index)
    {
        const RedXeActionDescriptor& descriptor = actions->namespaces[0].actions[index];
        contractValid = contractValid && descriptor.sizeBytes == sizeof(RedXeActionDescriptor) &&
                        RedXeIsActionNameSyntax(descriptor.name) && RedXeActionInNamespace(descriptor.name, "zoom") &&
                        (descriptor.flags & RedXeActionFlagDeferred) != 0 &&
                        (descriptor.targetKind != RedXeActionTargetEnum || descriptor.targetOptions);
        if (std::strcmp(descriptor.name, "zoom.mute") == 0)
        {
            sawMute = true;
            ZOOM_CHECK(RedXeActions::ValidateTarget(descriptor, "toggle") == S_OK &&
                           RedXeActions::ValidateTarget(descriptor, "maybe") == E_INVALIDARG,
                       "zoom.mute takes on, off, or toggle");
        }
        if (std::strcmp(descriptor.name, "zoom.leave") == 0)
        {
            ZOOM_CHECK(RedXeActions::ValidateTarget(descriptor, "") == E_INVALIDARG &&
                           RedXeActions::ValidateTarget(descriptor, "now") == S_OK,
                       "zoom.leave is destructive and needs now");
        }
    }
    ZOOM_CHECK(contractValid && sawMute, "every descriptor is well-formed and deferred");
    ZOOM_CHECK(actionContract("builtin.other", &actions) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
               "unknown id has no action contract");

    // Create the service with an envelope, as the host does.
    RedXeZoomTestDiagnostics report{};
    report.sizeBytes = sizeof(report);
    ZOOM_CHECK(diagnostics(&report) == HRESULT_FROM_WIN32(ERROR_NOT_READY), "no service yet");
    TestHost host;
    constexpr char envelope[] =
        R"json({"plugin":{},"instance":{"clientId":"client-1","redirectPort":0,"domain":"zoom.us","displayName":"Desk","autoConnect":false}})json";
    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
    options.configurationJsonUtf8 = envelope;
    options.configurationBytes = static_cast<uint32_t>(sizeof(envelope) - 1);
    void* object = nullptr;
    ZOOM_CHECK(create(__uuidof(IRedXeService), &options, &host, Zoom::kPluginId, &object) ==
                   HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
               "redirectPort 0 rejects creation");
    constexpr char goodEnvelope[] =
        R"json({"plugin":{},"instance":{"clientId":"client-1","redirectPort":48199,"domain":"zoom.us","displayName":"Desk","autoConnect":false}})json";
    options.configurationJsonUtf8 = goodEnvelope;
    options.configurationBytes = static_cast<uint32_t>(sizeof(goodEnvelope) - 1);
    ZOOM_CHECK(create(__uuidof(IRedXeWidget), &options, &host, Zoom::kPluginId, &object) == E_NOINTERFACE,
               "the service refuses the widget IID");
    ZOOM_CHECK(SUCCEEDED(create(__uuidof(IRedXeService), &options, &host, Zoom::kPluginId, &object)) && object,
               "service creates");
    wil::com_ptr_nothrow<IRedXeService> service;
    service.attach(static_cast<IRedXeService*>(object));
    wil::com_ptr_nothrow<IRedXeDeviceWorker> worker;
    wil::com_ptr_nothrow<IRedXeActionPack> pack;
    wil::com_ptr_nothrow<IUnknown> identity;
    wil::com_ptr_nothrow<IUnknown> packIdentity;
    ZOOM_CHECK(SUCCEEDED(service.query_to(worker.put())) && SUCCEEDED(service.query_to(pack.put())) &&
                   SUCCEEDED(service.query_to(identity.put())) && SUCCEEDED(pack.query_to(packIdentity.put())) &&
                   identity.get() == packIdentity.get(),
               "the service exposes the device worker and the action pack on one identity");
    ZOOM_CHECK(SUCCEEDED(useSynthetic(TRUE)) && SUCCEEDED(seed("")), "synthetic session selected, no credential");

    RedXeServiceStartContext start{};
    start.sizeBytes = sizeof(start);
    start.flags = RedXeServiceFlagDeviceAccessDisabled;
    ZOOM_CHECK(SUCCEEDED(service->Start(&start)), "service starts without device access");
    ZOOM_CHECK(SUCCEEDED(diagnostics(&report)) && report.serviceStarted == 1 && report.deviceAccess == 0,
               "diagnostics after start");

    wil::unique_event_nothrow stop;
    wil::unique_event_nothrow wake;
    ZOOM_CHECK(SUCCEEDED(stop.create(wil::EventOptions::ManualReset)) && SUCCEEDED(wake.create()), "lane events");
    std::atomic<HRESULT> laneResult{E_PENDING};
    std::thread lane(
        [&]() noexcept
        {
            (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            laneResult.store(worker->RunDeviceWork(stop.get(), wake.get()), std::memory_order_release);
            CoUninitialize();
        });
    const auto stopLane = wil::scope_exit(
        [&]() noexcept
        {
            SetEvent(stop.get());
            if (lane.joinable())
            {
                lane.join();
            }
        });
    ZOOM_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.laneRunning == 1; }, 3000),
               "lane reports running");
    ZOOM_CHECK(report.synthetic == 1 && report.sdkAvailable == 0 && report.credentialPresent == 0,
               "synthetic session, no SDK, no credential");

    const auto execute = [&](const char* action, const char* target) noexcept
    {
        RedXeActionRequest request{};
        request.sizeBytes = sizeof(request);
        request.flags = RedXeActionRequestFlagDeviceAccessDisabled;
        request.actionUtf8 = action;
        request.targetUtf8 = target;
        return pack->Execute(&request);
    };
    RedXeActionRequest bad{};
    ZOOM_CHECK(pack->Execute(nullptr) == E_POINTER && pack->Execute(&bad) == E_INVALIDARG, "bad requests");
    ZOOM_CHECK(execute("page.next", nullptr) == E_INVALIDARG, "another namespace is refused");

    // Without a credential every action but signIn fails signed-out.
    ZOOM_CHECK(execute("zoom.mute", "toggle") == S_FALSE, "a request is deferred to the lane");
    ZOOM_CHECK(
        WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.requestsFailed == 1; }, 3000) &&
            report.lastFailure == static_cast<int32_t>(E_NOT_VALID_STATE) && host.SawEvent("zoom-signed-out"),
        "signed-out actions fail and log once");

    // Sign in: the lane starts the listener; with device access disabled no browser opens. Deliver the redirect.
    ZOOM_CHECK(execute("zoom.signIn", nullptr) == S_FALSE, "sign-in queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.signingIn == 1; }, 3000) &&
                   report.listenerPort == 48199 && report.signIns == 1 && host.actions.load() == 0,
               "the loopback listener runs on the configured port and no browser launch happened");
    WSADATA data{};
    ZOOM_CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0, "winsock for the redirect");
    const auto cleanupWinsock = wil::scope_exit([]() noexcept { WSACleanup(); });
    // The synthetic transport turns the code into the tokens; the state must match, which the test cannot know, so
    // a wrong state is delivered first and must be ignored.
    ZOOM_CHECK(SendRedirect(48199, "GET /redirect?code=abc&state=nope HTTP/1.1"), "wrong-state redirect");
    Sleep(100);
    ZOOM_CHECK(SUCCEEDED(diagnostics(&report)) && report.signingIn == 1, "a wrong state keeps signing in");
    // Sign out cancels the pending sign-in so the rest of the test can seed a credential directly.
    ZOOM_CHECK(execute("zoom.signOut", "now") == S_FALSE, "sign-out queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.signingIn == 0; }, 3000),
               "sign-out stops the listener");

    // A seeded credential connects the synthetic client at the next action; the deferred request then runs.
    ZOOM_CHECK(SUCCEEDED(seed("refresh-1")), "credential seeded");
    constexpr char applied[] =
        R"json({"clientId":"client-1","redirectPort":48199,"domain":"zoom.us","displayName":"Desk","autoConnect":false})json";
    ZOOM_CHECK(SUCCEEDED(service->ApplySettings(applied, static_cast<uint32_t>(sizeof(applied) - 1))),
               "settings re-apply");
    ZOOM_CHECK(execute("zoom.start", nullptr) == S_FALSE, "start queued");
    ZOOM_CHECK(WaitUntil(
                   [&]() noexcept
                   {
                       return SUCCEEDED(diagnostics(&report)) &&
                              report.meetingState == static_cast<uint32_t>(Zoom::MeetingState::InMeeting);
                   },
                   3000) &&
                   report.authState == static_cast<uint32_t>(Zoom::AuthState::Authenticated) &&
                   report.ipcState == static_cast<uint32_t>(Zoom::IpcState::Connected) && report.sessionStarts == 1 &&
                   report.credentialPresent == 1 && report.isHost == 1,
               "the deferred start connected and reached the meeting");
    std::array<char, 64> credential{};
    ZOOM_CHECK(SUCCEEDED(stored(credential.data(), static_cast<uint32_t>(credential.size()))) &&
                   std::string_view{credential.data()} == "refresh-1",
               "the refreshed credential is stored");

    // In-meeting actions against the synthetic client.
    const uint32_t executedBefore = report.requestsExecuted;
    ZOOM_CHECK(execute("zoom.mute", "on") == S_FALSE && execute("zoom.video", "toggle") == S_FALSE &&
                   execute("zoom.raiseHand", "toggle") == S_FALSE && execute("zoom.reaction", "clap") == S_FALSE,
               "four actions queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.requestsExecuted >= executedBefore + 4; },
                         3000) &&
                   report.audioMuted == 1 && report.videoOn == 1 && report.handRaised == 1,
               "mute on, video toggled on, hand raised");
    uint64_t meetingNumber = 0;
    uint32_t reaction = 0;
    uint32_t chatMessages = 0;
    ZOOM_CHECK(SUCCEEDED(counters(&meetingNumber, &reaction, &chatMessages)) && reaction == 1, "clap is reaction 1");
    ZOOM_CHECK(execute("zoom.mute", "toggle") == S_FALSE && execute("zoom.chat.send", "hello") == S_FALSE &&
                   execute("zoom.share", "monitor@2") == S_FALSE && execute("zoom.record", "local.start") == S_FALSE,
               "more actions queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.requestsExecuted >= executedBefore + 8; },
                         3000) &&
                   report.audioMuted == 0 && report.sharing == 1 && report.recording == 1,
               "toggle unmutes, share and record start");
    ZOOM_CHECK(SUCCEEDED(counters(&meetingNumber, &reaction, &chatMessages)) && chatMessages == 1, "chat sent");
    ZOOM_CHECK(execute("zoom.share", "stop") == S_FALSE && execute("zoom.record", "local.stop") == S_FALSE,
               "share and record stop queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.requestsExecuted >= executedBefore + 10; },
                         3000) &&
                   report.sharing == 0 && report.recording == 0,
               "share and record stopped");

    // Role refusal: a participant cannot end for all or mute everyone.
    ZOOM_CHECK(SUCCEEDED(setHost(FALSE)), "become a participant");
    const uint32_t failedBefore = report.requestsFailed;
    ZOOM_CHECK(execute("zoom.participants.muteAll", nullptr) == S_FALSE, "muteAll queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.requestsFailed == failedBefore + 1; },
                         3000) &&
                   report.lastFailure == static_cast<int32_t>(E_ACCESSDENIED),
               "a host-only action is refused for a participant");
    ZOOM_CHECK(execute("zoom.leave", "later") == S_FALSE, "leave with a wrong target queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.requestsFailed == failedBefore + 2; },
                         3000) &&
                   report.lastFailure == static_cast<int32_t>(E_INVALIDARG),
               "an unconfirmed leave is refused");
    ZOOM_CHECK(execute("zoom.leave", "now") == S_FALSE, "leave queued");
    ZOOM_CHECK(WaitUntil(
                   [&]() noexcept
                   {
                       return SUCCEEDED(diagnostics(&report)) &&
                              report.meetingState == static_cast<uint32_t>(Zoom::MeetingState::Idle);
                   },
                   3000),
               "left the meeting");

    // A dropped connection reconnects with backoff.
    const uint32_t startsBefore = report.sessionStarts;
    ZOOM_CHECK(SUCCEEDED(drop()), "drop the client connection");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.sessionStarts == startsBefore + 1; },
                         5000) &&
                   report.reconnects == 1 && report.ipcState == static_cast<uint32_t>(Zoom::IpcState::Connected),
               "the lane reconnected after the drop");

    // Join with a meeting reference, then sign out deletes the credential.
    ZOOM_CHECK(execute("zoom.join", "https://zoom.us/j/1234567890?pwd=secret") == S_FALSE, "join queued");
    ZOOM_CHECK(
        WaitUntil(
            [&]() noexcept
            { return SUCCEEDED(counters(&meetingNumber, &reaction, &chatMessages)) && meetingNumber == 1234567890ULL; },
            3000),
        "the meeting number reached the session");
    ZOOM_CHECK(execute("zoom.signOut", "now") == S_FALSE, "sign-out queued");
    ZOOM_CHECK(WaitUntil(
                   [&]() noexcept
                   {
                       return SUCCEEDED(stored(credential.data(), static_cast<uint32_t>(credential.size()))) &&
                              credential[0] == '\0';
                   },
                   3000) &&
                   SUCCEEDED(diagnostics(&report)) && report.credentialPresent == 0,
               "sign-out deleted the credential");

    // Stop: the lane returns within the drain budget and the service stops idempotently.
    SetEvent(stop.get());
    ZOOM_CHECK(WaitUntil([&]() noexcept { return laneResult.load(std::memory_order_acquire) != E_PENDING; },
                         kRedXeDeviceWorkerDrainMilliseconds) &&
                   SUCCEEDED(laneResult.load(std::memory_order_acquire)),
               "lane stopped within the drain budget");
    ZOOM_CHECK(SUCCEEDED(diagnostics(&report)) && report.laneRunning == 0, "lane reports stopped");
    ZOOM_CHECK(SUCCEEDED(service->Stop()) && SUCCEEDED(service->Stop()), "stop is idempotent");
    pack.reset();
    worker.reset();
    identity.reset();
    packIdentity.reset();
    service.reset();
    ZOOM_CHECK(diagnostics(&report) == HRESULT_FROM_WIN32(ERROR_NOT_READY), "the service is gone after release");
    return S_OK;
}
} // namespace

int wmain() noexcept
{
    // Stream progress even when redirected, so a hang names the test it happened in.
    (void)setvbuf(stdout, nullptr, _IONBF, 0);
    struct Test final
    {
        const wchar_t* name;
        HRESULT (*run)() noexcept;
    };
    constexpr std::array tests{
        Test{L"settings model", &TestSettingsModel},
        Test{L"auth material", &TestAuthMaterial},
        Test{L"loopback listener", &TestLoopbackListener},
        Test{L"shipped module", &TestShippedModule},
    };
    int failures = 0;
    for (const Test& test : tests)
    {
        std::wprintf(L"[ RUN      ] %s\n", test.name);
        const HRESULT result = test.run();
        if (FAILED(result))
        {
            ++failures;
            std::wprintf(L"[  FAILED  ] %s (0x%08X)\n", test.name, static_cast<unsigned>(result));
        }
        else
        {
            std::wprintf(L"[       OK ] %s\n", test.name);
        }
    }
    std::wprintf(failures == 0 ? L"ZoomTests passed.\n" : L"ZoomTests failed: %d.\n", failures);
    return failures == 0 ? 0 : 1;
}
