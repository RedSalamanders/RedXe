#pragma once

// The headless Zoom service: owns the Zoom Plugin SDK session on its device lane, signs the user in over OAuth
// PKCE, and publishes the "zoom" action namespace (IRedXeActionPack) that the lane executes against the session.
// One instance per process. The lane is the only thread that touches the session, the tokens, and the loopback
// listener; the UI thread only parses settings and queues requests.

#include "PlugInterfaces/Action.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Service.h"
#include "ZoomAuth.h"
#include "ZoomSession.h"
#include "ZoomSettings.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>
#include <windows.h>

namespace Zoom
{
inline constexpr uint32_t kMaximumPendingRequests = 4;
inline constexpr uint32_t kReconnectBackoffSteps = 4;
inline constexpr uint32_t kSignInTimeoutMilliseconds = 5 * 60 * 1000;
// An action that arrives before the session is connected waits this long for the connection.
inline constexpr uint32_t kDeferredRequestMilliseconds = 15 * 1000;
using ActionName = std::array<char, kRedXeMaximumActionNameBytes + 1>;
using TargetText = std::array<char, kRedXeMaximumActionTargetBytes + 1>;

class SyntheticSession;

// Everything the test contract and diagnostics read; copied under the service lock.
struct ServiceSnapshot final
{
    bool laneRunning = false;
    bool deviceAccess = false;
    bool sdkAvailable = false;
    bool synthetic = false;
    bool credentialPresent = false;
    bool signingIn = false;
    SessionSnapshot session{};
    uint32_t requestsQueued = 0;
    uint32_t requestsExecuted = 0;
    uint32_t requestsFailed = 0;
    uint32_t requestsDropped = 0;
    uint32_t sessionStarts = 0;
    uint32_t reconnects = 0;
    uint32_t signIns = 0;
    uint32_t listenerPort = 0;
    HRESULT lastFailure = S_OK;
    ActionName lastAction{};
};

class ZoomService final : public RedXeComObject<ZoomService, IRedXeService, IRedXeDeviceWorker, IRedXeActionPack>,
                          public ISessionListener
{
  public:
    explicit ZoomService(IRedXeHost* host) noexcept;
    ~ZoomService();

    // Parses the {"plugin":{},"instance":{...}} factory envelope. Fails without retaining anything on error.
    [[nodiscard]] HRESULT ParseConfiguration(const char* jsonUtf8, uint32_t bytes) noexcept;

    // IRedXeService (UI thread).
    HRESULT STDMETHODCALLTYPE Start(const RedXeServiceStartContext* context) noexcept override;
    HRESULT STDMETHODCALLTYPE ApplySettings(const char* settingsJsonUtf8, uint32_t settingsBytes) noexcept override;
    HRESULT STDMETHODCALLTYPE OnHostState(const RedXeHostState* state) noexcept override;
    HRESULT STDMETHODCALLTYPE Stop() noexcept override;

    // IRedXeDeviceWorker (device lane).
    HRESULT STDMETHODCALLTYPE RunDeviceWork(HANDLE stopEvent, HANDLE wakeEvent) noexcept override;

    // IRedXeActionPack (UI thread): copies the request into a bounded slot, wakes the lane, returns S_FALSE.
    HRESULT STDMETHODCALLTYPE Execute(const RedXeActionRequest* request) noexcept override;

    // ISessionListener (SDK or synthetic thread).
    void OnSessionChanged() noexcept override;

    // Test surface; any thread. The synthetic session and the injected store/transport take effect at the next
    // lane start, so tests set them before StartServices.
    void CopySnapshot(ServiceSnapshot& snapshot) const noexcept;
    void UseSyntheticSession(bool enabled) noexcept;
    [[nodiscard]] SyntheticSession* Synthetic() noexcept;
    void SetCredentialStore(ICredentialStore* store) noexcept;
    void SetTokenTransport(ITokenTransport* transport) noexcept;
    [[nodiscard]] bool Started() const noexcept;

    // Module singleton for the test contract; borrowed, may be null.
    [[nodiscard]] static ZoomService* Current() noexcept;

  private:
    struct PendingRequest final
    {
        ActionName action{};
        TargetText target{};
    };

    // Lane-side steps.
    void AdoptSettings() noexcept;
    void EnsureSession() noexcept;
    void StepConnection(uint64_t now) noexcept;
    void HandleRequest(const PendingRequest& request, uint64_t now) noexcept;
    [[nodiscard]] HRESULT RunVerb(std::string_view verb, std::string_view target) noexcept;
    void BeginSignIn(uint64_t now) noexcept;
    void StepSignIn(uint64_t now) noexcept;
    void CompleteSignIn(std::string_view code) noexcept;
    void SignOut(bool deleteCredential) noexcept;
    [[nodiscard]] bool RefreshAccessToken() noexcept;
    void DisconnectSession() noexcept;
    void PublishSnapshot() noexcept;
    void Log(uint32_t level, const char* eventId, const char* message, HRESULT code = S_OK) noexcept;
    void WakeLane() noexcept;
    void NoteFailure(HRESULT code) noexcept;

    IRedXeHost* _host;
    std::atomic<bool> _started{false};
    std::atomic<bool> _laneRunning{false};
    std::atomic<bool> _deviceAccess{true};

    // Shared between the UI thread, the lane, and readers.
    mutable SRWLOCK _lock = SRWLOCK_INIT;
    Settings _settings{};
    uint32_t _settingsGeneration = 0;
    std::array<PendingRequest, kMaximumPendingRequests> _pending{};
    uint32_t _pendingCount = 0;
    HANDLE _wakeEvent = nullptr;
    ServiceSnapshot _snapshot{};
    bool _syntheticRequested = false;
    ICredentialStore* _injectedStore = nullptr;
    ITokenTransport* _injectedTransport = nullptr;

    // Lane-owned.
    Settings _laneSettings{};
    uint32_t _laneSettingsGeneration = 0;
    std::unique_ptr<IZoomSession> _session;
    SyntheticSession* _synthetic = nullptr;
    bool _sessionInitialized = false;
    bool _wantConnected = false;
    bool _authInFlight = false;
    bool _connectedSeen = false;
    uint32_t _reconnectStep = 0;
    uint64_t _reconnectDue = 0;
    TokenSet _tokens{};
    bool _credentialPresent = false;
    bool _credentialChecked = false;
    CredentialManagerStore _credentialManager;
    WinHttpTokenTransport _winHttp;
    ICredentialStore* _store = nullptr;
    ITokenTransport* _transport = nullptr;
    // Sign-in in progress: the listener, the PKCE material, and the deadline.
    LoopbackListener _listener;
    bool _signingIn = false;
    uint64_t _signInDeadline = 0;
    std::array<char, kVerifierCharacters + 1> _verifier{};
    std::array<char, kStateCharacters + 1> _state{};
    // The one request waiting for the session to connect (the newest wins).
    PendingRequest _deferred{};
    bool _deferredPending = false;
    uint64_t _deferredDeadline = 0;
    bool _signedOutLogged = false;
    bool _sdkUnavailableLogged = false;
};
} // namespace Zoom
