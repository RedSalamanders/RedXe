#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Service.h"
#include "ZoomService.h"
#include "ZoomSynthetic.h"
#include "ZoomTestContract.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
constexpr RedXePluginSettingsContract kServiceContract{
    sizeof(RedXePluginSettingsContract), Zoom::kSettingsSchema,
    sizeof(Zoom::kSettingsSchema) - 1,   Zoom::kSettingsDefaults,
    sizeof(Zoom::kSettingsDefaults) - 1,
};

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        Zoom::kPluginId,
        L"Zoom",
        L"Drives the Zoom Workplace client through the Zoom Plugin SDK: join, leave, mute, video, share, record, "
        L"reactions, and chat as dashboard actions.",
        L"RedXe",
        L"1.0.0",
        RedXePluginCapabilityService | RedXePluginCapabilityActions,
    },
};

constexpr std::array kSettingsContracts{
    RedXeSettingsContractEntry{Zoom::kPluginId, &kServiceContract},
};

constexpr uint32_t kDescriptorSize = sizeof(RedXeActionDescriptor);
constexpr uint32_t kDeferred = RedXeActionFlagDeferred;

constexpr RedXeActionDescriptor Action(const char* name, const wchar_t* displayName, uint32_t targetKind,
                                       const wchar_t* targetSyntax, uint32_t flags = kDeferred,
                                       const char* options = nullptr, int32_t minimum = 0, int32_t maximum = 0) noexcept
{
    return RedXeActionDescriptor{kDescriptorSize, flags,   name,    displayName, targetSyntax,
                                 targetKind,      minimum, maximum, options};
}

// The published "zoom" namespace. Every action completes on the service lane against the SDK session.
constexpr std::array kZoomActions{
    Action("zoom.signIn", L"Sign in to Zoom", RedXeActionTargetNone, L""),
    Action("zoom.signOut", L"Sign out of Zoom", RedXeActionTargetEnum, L"now", kDeferred | RedXeActionFlagDestructive,
           "now"),
    Action("zoom.join", L"Join meeting", RedXeActionTargetMeeting, L"<meeting URL> or <id>[:<passcode>]"),
    Action("zoom.start", L"Start meeting", RedXeActionTargetMeeting, L"<PMI> or empty for an instant meeting",
           kDeferred | RedXeActionFlagTargetOptional),
    Action("zoom.leave", L"Leave meeting", RedXeActionTargetEnum, L"now", kDeferred | RedXeActionFlagDestructive,
           "now"),
    Action("zoom.end", L"End meeting for all", RedXeActionTargetEnum, L"now", kDeferred | RedXeActionFlagDestructive,
           "now"),
    Action("zoom.audio", L"Computer audio", RedXeActionTargetEnum, L"join or leave", kDeferred, "join|leave"),
    Action("zoom.mute", L"Mute", RedXeActionTargetEnum, L"on, off, or toggle", kDeferred, "on|off|toggle"),
    Action("zoom.video", L"Video", RedXeActionTargetEnum, L"on, off, or toggle", kDeferred, "on|off|toggle"),
    Action("zoom.share", L"Share", RedXeActionTargetEnum, L"monitor[@<monitor>], app@<window>, pause, resume, or stop",
           kDeferred | RedXeActionFlagMonitorSuffix | RedXeActionFlagWindowSuffix, "monitor|app|pause|resume|stop"),
    Action("zoom.record", L"Record", RedXeActionTargetEnum,
           L"local.start, local.stop, cloud.start, cloud.stop, pause, or resume", kDeferred,
           "local.start|local.stop|cloud.start|cloud.stop|pause|resume"),
    Action("zoom.raiseHand", L"Raise hand", RedXeActionTargetEnum, L"on, off, or toggle", kDeferred, "on|off|toggle"),
    Action("zoom.reaction", L"Reaction", RedXeActionTargetEnum, L"thumbsUp, clap, heart, joy, openMouth, or tada",
           kDeferred, Zoom::kReactionOptions),
    Action("zoom.chat.send", L"Send chat", RedXeActionTargetText, L"<text>"),
    Action("zoom.captions", L"Captions", RedXeActionTargetEnum, L"on or off", kDeferred, "on|off"),
    Action("zoom.participants.muteAll", L"Mute all participants", RedXeActionTargetNone, L""),
    Action("zoom.participants.admitAll", L"Admit waiting participants", RedXeActionTargetNone, L""),
    Action("zoom.focus", L"Front the Zoom window", RedXeActionTargetNone, L"", kDeferred | RedXeActionFlagInjectsInput),
};

constexpr std::array kZoomNamespaces{
    RedXeActionNamespace{sizeof(RedXeActionNamespace), static_cast<uint32_t>(kZoomActions.size()),
                         Zoom::kActionNamespace, kZoomActions.data()},
};

constexpr RedXeActionContract kZoomActionContract{
    sizeof(RedXeActionContract), static_cast<uint32_t>(kZoomNamespaces.size()), kZoomNamespaces.data()};

HRESULT CreateZoomService(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                          void** result) noexcept
{
    // The service object also carries IRedXeActionPack, but the host obtains that on the started object.
    if (interfaceId != __uuidof(IRedXeService))
    {
        return E_NOINTERFACE;
    }
    if (!host)
    {
        return E_POINTER;
    }
    if (options && options->sizeBytes != sizeof(RedXeFactoryOptions))
    {
        return E_INVALIDARG;
    }
    const char* configuration = options ? options->configurationJsonUtf8 : nullptr;
    const uint32_t configurationBytes = options ? options->configurationBytes : 0;
    if ((configuration == nullptr) != (configurationBytes == 0) ||
        configurationBytes > kRedXeMaximumFactoryConfigurationBytes)
    {
        return E_INVALIDARG;
    }
    wil::com_ptr_nothrow<Zoom::ZoomService> service;
    service.attach(new (std::nothrow) Zoom::ZoomService(host));
    if (!service)
    {
        return E_OUTOFMEMORY;
    }
    const HRESULT parsed = service->ParseConfiguration(configuration, configurationBytes);
    if (FAILED(parsed))
    {
        return parsed;
    }
    *result = static_cast<IRedXeService*>(service.detach());
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateZoomService},
};

// Test transport: every token request answers with an access token derived from the refresh token, so a seeded
// "expired" credential yields the access token the synthetic client rejects.
class FixedTokenTransport final : public Zoom::ITokenTransport
{
  public:
    HRESULT Post(std::string_view, std::string_view body, char* response, size_t capacity,
                 size_t& received) noexcept override
    {
        received = 0;
        const bool refresh = body.find("grant_type=refresh_token") != std::string_view::npos;
        const size_t tokenAt = body.find(refresh ? "refresh_token=" : "code=");
        std::string_view token =
            tokenAt == std::string_view::npos ? std::string_view{} : body.substr(tokenAt + (refresh ? 14 : 5));
        token = token.substr(0, token.find('&'));
        if (token.size() > 512)
        {
            return E_INVALIDARG;
        }
        std::array<char, 600> tokenText{};
        std::memcpy(tokenText.data(), token.data(), token.size());
        const int written =
            sprintf_s(response, capacity, R"({"access_token":"%s","refresh_token":"%s","expires_in":3600})",
                      tokenText.data(), tokenText.data());
        if (written <= 0)
        {
            return E_FAIL;
        }
        received = static_cast<size_t>(written);
        return S_OK;
    }
};

Zoom::MemoryCredentialStore g_testStore;
FixedTokenTransport g_testTransport;
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

extern "C" HRESULT __stdcall RedXeZoomGetTestDiagnostics(RedXeZoomTestDiagnostics* diagnostics) noexcept
{
    if (!diagnostics)
    {
        return E_POINTER;
    }
    if (diagnostics->sizeBytes != sizeof(RedXeZoomTestDiagnostics))
    {
        return E_INVALIDARG;
    }
    Zoom::ZoomService* service = Zoom::ZoomService::Current();
    *diagnostics = RedXeZoomTestDiagnostics{};
    diagnostics->sizeBytes = sizeof(RedXeZoomTestDiagnostics);
    if (!service)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    }
    Zoom::ServiceSnapshot snapshot{};
    service->CopySnapshot(snapshot);
    RedXeZoomTestDiagnostics& out = *diagnostics;
    out.serviceStarted = service->Started() ? 1U : 0U;
    out.laneRunning = snapshot.laneRunning ? 1U : 0U;
    out.deviceAccess = snapshot.deviceAccess ? 1U : 0U;
    out.sdkAvailable = snapshot.sdkAvailable ? 1U : 0U;
    out.synthetic = snapshot.synthetic ? 1U : 0U;
    out.credentialPresent = snapshot.credentialPresent ? 1U : 0U;
    out.signingIn = snapshot.signingIn ? 1U : 0U;
    out.authState = static_cast<uint32_t>(snapshot.session.auth);
    out.ipcState = static_cast<uint32_t>(snapshot.session.ipc);
    out.meetingState = static_cast<uint32_t>(snapshot.session.meeting);
    out.audioJoined = snapshot.session.audioJoined ? 1U : 0U;
    out.audioMuted = snapshot.session.audioMuted ? 1U : 0U;
    out.videoOn = snapshot.session.videoOn ? 1U : 0U;
    out.sharing = snapshot.session.sharing ? 1U : 0U;
    out.handRaised = snapshot.session.handRaised ? 1U : 0U;
    out.recording = snapshot.session.recording ? 1U : 0U;
    out.isHost = snapshot.session.isHost ? 1U : 0U;
    out.requestsQueued = snapshot.requestsQueued;
    out.requestsExecuted = snapshot.requestsExecuted;
    out.requestsFailed = snapshot.requestsFailed;
    out.requestsDropped = snapshot.requestsDropped;
    out.sessionStarts = snapshot.sessionStarts;
    out.reconnects = snapshot.reconnects;
    out.signIns = snapshot.signIns;
    out.listenerPort = snapshot.listenerPort;
    out.sessionSubmissions = snapshot.session.submissions;
    out.sessionCompletions = snapshot.session.completions;
    out.lastFailure = static_cast<int32_t>(snapshot.lastFailure);
    strncpy_s(out.lastAction, std::size(out.lastAction), snapshot.lastAction.data(), _TRUNCATE);
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeZoomUseSyntheticSession(BOOL enabled) noexcept
{
    Zoom::ZoomService* service = Zoom::ZoomService::Current();
    if (!service)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    }
    service->UseSyntheticSession(enabled != FALSE);
    service->SetCredentialStore(enabled ? &g_testStore : nullptr);
    service->SetTokenTransport(enabled ? &g_testTransport : nullptr);
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeZoomSeedCredential(const char* refreshToken) noexcept
{
    if (!refreshToken)
    {
        return E_POINTER;
    }
    if (refreshToken[0] == '\0')
    {
        return g_testStore.Delete({});
    }
    return g_testStore.Write({}, refreshToken);
}

extern "C" HRESULT __stdcall RedXeZoomSyntheticSetHost(BOOL host) noexcept
{
    Zoom::ZoomService* service = Zoom::ZoomService::Current();
    Zoom::SyntheticSession* synthetic = service ? service->Synthetic() : nullptr;
    if (!synthetic)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    }
    synthetic->SetHost(host != FALSE);
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeZoomSyntheticDropConnection() noexcept
{
    Zoom::ZoomService* service = Zoom::ZoomService::Current();
    Zoom::SyntheticSession* synthetic = service ? service->Synthetic() : nullptr;
    if (!synthetic)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    }
    synthetic->DropConnection();
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeZoomSyntheticCounters(uint64_t* meetingNumber, uint32_t* reaction,
                                                        uint32_t* chatMessages) noexcept
{
    if (!meetingNumber || !reaction || !chatMessages)
    {
        return E_POINTER;
    }
    Zoom::ZoomService* service = Zoom::ZoomService::Current();
    Zoom::SyntheticSession* synthetic = service ? service->Synthetic() : nullptr;
    if (!synthetic)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    }
    *meetingNumber = synthetic->LastMeetingNumber();
    *reaction = synthetic->LastReaction();
    *chatMessages = synthetic->ChatMessages();
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeZoomStoredCredential(char* refreshToken, uint32_t capacity) noexcept
{
    if (!refreshToken)
    {
        return E_POINTER;
    }
    if (capacity == 0)
    {
        return E_INVALIDARG;
    }
    uint32_t bytes = 0;
    const HRESULT read = g_testStore.Read({}, refreshToken, capacity, bytes);
    if (read == S_FALSE)
    {
        refreshToken[0] = '\0';
    }
    return read;
}
