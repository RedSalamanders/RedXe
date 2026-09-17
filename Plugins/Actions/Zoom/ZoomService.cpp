#include "ZoomService.h"

#include "Actions/ActionTargets.h"
#include "Actions/WindowSelector.h"
#include "ZoomSynthetic.h"

#include <algorithm>
#include <cstring>
#include <new>

#include <yyjson.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace Zoom
{
namespace
{
SRWLOCK g_currentLock = SRWLOCK_INIT;
ZoomService* g_current = nullptr;

[[nodiscard]] bool ToggleWanted(std::string_view target, bool current, bool& wanted) noexcept
{
    if (target == "on")
    {
        wanted = true;
    }
    else if (target == "off")
    {
        wanted = false;
    }
    else if (target == "toggle")
    {
        wanted = !current;
    }
    else
    {
        return false;
    }
    return true;
}
} // namespace

ZoomService* ZoomService::Current() noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&g_currentLock);
    return g_current;
}

ZoomService::ZoomService(IRedXeHost* host) noexcept : _host(host)
{
    const auto guard = wil::AcquireSRWLockExclusive(&g_currentLock);
    if (!g_current)
    {
        g_current = this;
    }
}

ZoomService::~ZoomService()
{
    const auto guard = wil::AcquireSRWLockExclusive(&g_currentLock);
    if (g_current == this)
    {
        g_current = nullptr;
    }
}

void ZoomService::Log(uint32_t level, const char* eventId, const char* message, HRESULT code) noexcept
{
    (void)RedXeHostLog(_host, level, kPluginId, nullptr, eventId, message, code);
}

HRESULT ZoomService::ParseConfiguration(const char* jsonUtf8, uint32_t bytes) noexcept
{
    if (!jsonUtf8 || bytes == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    yyjson_doc* document = yyjson_read(jsonUtf8, bytes, YYJSON_READ_NOFLAG);
    if (!document)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    yyjson_val* root = yyjson_doc_get_root(document);
    yyjson_val* instance = yyjson_is_obj(root) ? yyjson_obj_get(root, "instance") : nullptr;
    Settings parsed{};
    std::array<char, 160> diagnostic{};
    const HRESULT result = instance ? ParseSettings(instance, parsed, diagnostic.data(), diagnostic.size())
                                    : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    yyjson_doc_free(document);
    if (FAILED(result))
    {
        Log(RedXeLogLevelError, "settings-rejected", diagnostic[0] != '\0' ? diagnostic.data() : "invalid envelope.",
            result);
        return result;
    }
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    _settings = parsed;
    ++_settingsGeneration;
    return S_OK;
}

HRESULT ZoomService::Start(const RedXeServiceStartContext* context) noexcept
{
    if (!context)
    {
        return E_POINTER;
    }
    if (context->sizeBytes != sizeof(RedXeServiceStartContext))
    {
        return E_INVALIDARG;
    }
    _deviceAccess.store((context->flags & RedXeServiceFlagDeviceAccessDisabled) == 0, std::memory_order_release);
    _started.store(true, std::memory_order_release);
    // The lane is not running yet (StartDeviceLane follows Start), so only the flags are published here.
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _snapshot.deviceAccess = _deviceAccess.load(std::memory_order_acquire);
        _snapshot.sdkAvailable = SdkSessionAvailable();
    }
    return S_OK;
}

HRESULT ZoomService::ApplySettings(const char* settingsJsonUtf8, uint32_t settingsBytes) noexcept
{
    if (!settingsJsonUtf8)
    {
        return E_POINTER;
    }
    if (settingsBytes == 0)
    {
        return E_INVALIDARG;
    }
    Settings parsed{};
    std::array<char, 160> diagnostic{};
    const HRESULT result = ParseSettingsJson(std::string_view(settingsJsonUtf8, settingsBytes), parsed,
                                             diagnostic.data(), diagnostic.size());
    if (FAILED(result))
    {
        Log(RedXeLogLevelWarning, "settings-rejected", diagnostic.data(), result);
        return result;
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _settings = parsed;
        ++_settingsGeneration;
    }
    WakeLane();
    return S_OK;
}

HRESULT ZoomService::OnHostState(const RedXeHostState* state) noexcept
{
    if (!state)
    {
        return E_POINTER;
    }
    return state->sizeBytes == sizeof(RedXeHostState) ? S_OK : E_INVALIDARG;
}

HRESULT ZoomService::Stop() noexcept
{
    // The lane already returned (StopDeviceLane precedes Stop); nothing lane-owned is touched here.
    _started.store(false, std::memory_order_release);
    return S_OK;
}

bool ZoomService::Started() const noexcept
{
    return _started.load(std::memory_order_acquire);
}

void ZoomService::WakeLane() noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    if (_wakeEvent)
    {
        SetEvent(_wakeEvent);
    }
}

void ZoomService::OnSessionChanged() noexcept
{
    WakeLane();
}

HRESULT ZoomService::Execute(const RedXeActionRequest* request) noexcept
{
    if (!request)
    {
        return E_POINTER;
    }
    if (request->sizeBytes != sizeof(RedXeActionRequest) || !request->actionUtf8 ||
        !RedXeActionInNamespace(request->actionUtf8, kActionNamespace))
    {
        return E_INVALIDARG;
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        if (_pendingCount >= _pending.size())
        {
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        }
        PendingRequest& pending = _pending[_pendingCount++];
        pending = PendingRequest{};
        strncpy_s(pending.action.data(), pending.action.size(), request->actionUtf8, _TRUNCATE);
        if (request->targetUtf8)
        {
            strncpy_s(pending.target.data(), pending.target.size(), request->targetUtf8, _TRUNCATE);
        }
        ++_snapshot.requestsQueued;
    }
    WakeLane();
    return S_FALSE;
}

void ZoomService::CopySnapshot(ServiceSnapshot& snapshot) const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    snapshot = _snapshot;
}

void ZoomService::UseSyntheticSession(bool enabled) noexcept
{
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    _syntheticRequested = enabled;
}

SyntheticSession* ZoomService::Synthetic() noexcept
{
    return _synthetic;
}

void ZoomService::SetCredentialStore(ICredentialStore* store) noexcept
{
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    _injectedStore = store;
}

void ZoomService::SetTokenTransport(ITokenTransport* transport) noexcept
{
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    _injectedTransport = transport;
}

void ZoomService::NoteFailure(HRESULT code) noexcept
{
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    _snapshot.lastFailure = code;
    ++_snapshot.requestsFailed;
}

void ZoomService::PublishSnapshot() noexcept
{
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    _snapshot.laneRunning = _laneRunning.load(std::memory_order_acquire);
    _snapshot.deviceAccess = _deviceAccess.load(std::memory_order_acquire);
    _snapshot.sdkAvailable = SdkSessionAvailable();
    _snapshot.synthetic = _synthetic != nullptr;
    _snapshot.credentialPresent = _credentialPresent;
    _snapshot.signingIn = _signingIn;
    _snapshot.listenerPort = _listener.Running() ? _listener.Port() : 0;
    _snapshot.session = _session ? _session->Snapshot() : SessionSnapshot{};
}

void ZoomService::AdoptSettings() noexcept
{
    Settings next{};
    uint32_t generation = 0;
    {
        const auto guard = wil::AcquireSRWLockShared(&_lock);
        if (_laneSettingsGeneration == _settingsGeneration)
        {
            return;
        }
        next = _settings;
        generation = _settingsGeneration;
    }
    const bool clientChanged = next.ClientId() != _laneSettings.ClientId();
    _laneSettings = next;
    _laneSettingsGeneration = generation;
    if (clientChanged)
    {
        // A different Marketplace app means different tokens: forget the session.
        SignOut(false);
    }
    // Every settings apply re-reads the stored credential (one CredRead), so a credential added while the service
    // was signed out is picked up without a restart.
    _credentialChecked = false;
    _signedOutLogged = false;
    if (_laneSettings.autoConnect)
    {
        _wantConnected = true;
    }
}

void ZoomService::EnsureSession() noexcept
{
    if (_session)
    {
        return;
    }
    bool synthetic = false;
    {
        const auto guard = wil::AcquireSRWLockShared(&_lock);
        synthetic = _syntheticRequested;
    }
    if (synthetic)
    {
        auto* session = new (std::nothrow) SyntheticSession();
        _synthetic = session;
        _session.reset(session);
        return;
    }
    if (!_deviceAccess.load(std::memory_order_acquire))
    {
        // Automated host without a synthetic session: requests are counted and dropped.
        return;
    }
    _session.reset(CreateSdkSession());
    if (!_session && !_sdkUnavailableLogged)
    {
        _sdkUnavailableLogged = true;
        Log(RedXeLogLevelWarning, "zoom-sdk-unavailable",
            "the Zoom Plugin SDK runtime is not available in this build; zoom.* actions are disabled.");
    }
}

bool ZoomService::RefreshAccessToken() noexcept
{
    if (!_store || !_transport)
    {
        return false;
    }
    if (!_credentialChecked)
    {
        _credentialChecked = true;
        uint32_t bytes = 0;
        const HRESULT read =
            _store->Read(_laneSettings.ClientId(), _tokens.refreshToken.data(), _tokens.refreshToken.size(), bytes);
        _tokens.refreshBytes = SUCCEEDED(read) && read != S_FALSE ? bytes : 0;
        _credentialPresent = _tokens.refreshBytes != 0;
    }
    if (_tokens.refreshBytes == 0)
    {
        return false;
    }
    const uint64_t now = UnixNow();
    if (_tokens.accessBytes != 0 && _tokens.expiresAt > now + 60)
    {
        return true;
    }
    const HRESULT refreshed =
        RefreshTokens(*_transport, _laneSettings.Domain(), _laneSettings.ClientId(), now, _tokens);
    if (FAILED(refreshed))
    {
        Log(RedXeLogLevelWarning, "zoom-auth-failed", "the refresh token was rejected; sign in again (zoom.signIn).",
            refreshed);
        if (refreshed == E_ACCESSDENIED)
        {
            // invalid_grant: the credential is dead, keeping it would only repeat the failure.
            (void)_store->Delete(_laneSettings.ClientId());
            _tokens = TokenSet{};
            _credentialPresent = false;
        }
        return false;
    }
    if (_tokens.refreshBytes != 0)
    {
        (void)_store->Write(_laneSettings.ClientId(), _tokens.RefreshToken());
        _credentialPresent = true;
    }
    return true;
}

void ZoomService::DisconnectSession() noexcept
{
    if (_session && _sessionInitialized)
    {
        _session->Uninitialize();
    }
    _sessionInitialized = false;
    _authInFlight = false;
    _connectedSeen = false;
}

void ZoomService::SignOut(bool deleteCredential) noexcept
{
    DisconnectSession();
    _wantConnected = false;
    _tokens = TokenSet{};
    if (deleteCredential && _store)
    {
        (void)_store->Delete(_laneSettings.ClientId());
    }
    _credentialPresent = false;
    _credentialChecked = !deleteCredential ? _credentialChecked : true;
    if (_signingIn)
    {
        _listener.Stop();
        _signingIn = false;
    }
}

void ZoomService::StepConnection(uint64_t now) noexcept
{
    if (!_session || !_wantConnected)
    {
        return;
    }
    const SessionSnapshot state = _session->Snapshot();
    if (_sessionInitialized && _connectedSeen && state.ipc == IpcState::Disconnected)
    {
        // The client went away (or was restarted): back off 1, 2, 4, 8 s, then wait for the next request.
        Log(RedXeLogLevelInfo, "zoom-disconnected", "the Zoom client connection dropped; reconnecting.");
        DisconnectSession();
        if (_reconnectStep < kReconnectBackoffSteps)
        {
            _reconnectDue = now + (1000ULL << _reconnectStep);
            ++_reconnectStep;
        }
        else
        {
            _wantConnected = false;
            return;
        }
        {
            const auto guard = wil::AcquireSRWLockExclusive(&_lock);
            ++_snapshot.reconnects;
        }
    }
    if (_sessionInitialized)
    {
        if (!_connectedSeen && state.auth == AuthState::Authenticated && state.ipc == IpcState::Connected)
        {
            _connectedSeen = true;
            _reconnectStep = 0;
            Log(RedXeLogLevelInfo, "zoom-connected", "authenticated and connected to the Zoom client.");
        }
        else if (state.auth == AuthState::Failed && _authInFlight)
        {
            _authInFlight = false;
            Log(RedXeLogLevelWarning, "zoom-auth-failed", "the Zoom client rejected the access token.",
                state.lastError);
            DisconnectSession();
            _tokens.accessBytes = 0;
            _wantConnected = false;
        }
        return;
    }
    if (now < _reconnectDue)
    {
        return;
    }
    if (!RefreshAccessToken())
    {
        if (!_signedOutLogged)
        {
            _signedOutLogged = true;
            Log(RedXeLogLevelWarning, "zoom-signed-out", "no Zoom credential; bind zoom.signIn to sign in.");
        }
        _wantConnected = false;
        return;
    }
    _signedOutLogged = false;
    const HRESULT initialized = _session->Initialize(this);
    if (FAILED(initialized))
    {
        Log(RedXeLogLevelError, "zoom-session-failed", "the Zoom Plugin SDK could not be initialized.", initialized);
        NoteFailure(initialized);
        _wantConnected = false;
        return;
    }
    _sessionInitialized = true;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        ++_snapshot.sessionStarts;
    }
    const HRESULT authenticating = _session->Authenticate(_laneSettings.Domain(), _tokens.AccessToken());
    if (FAILED(authenticating))
    {
        Log(RedXeLogLevelError, "zoom-auth-failed", "StartToolSuiteAuth failed.", authenticating);
        NoteFailure(authenticating);
        DisconnectSession();
        _wantConnected = false;
        return;
    }
    _authInFlight = true;
}

void ZoomService::BeginSignIn(uint64_t now) noexcept
{
    if (_signingIn)
    {
        return;
    }
    if (!_deviceAccess.load(std::memory_order_acquire) && !_synthetic)
    {
        return;
    }
    std::array<char, kChallengeCharacters + 1> challenge{};
    std::array<char, kMaximumUrlBytes> url{};
    if (!GenerateVerifier(_verifier.data(), _verifier.size()) || !GenerateState(_state.data(), _state.size()) ||
        !ComputeChallenge(_verifier.data(), challenge.data(), challenge.size()) ||
        !BuildAuthorizeUrl(_laneSettings.Domain(), _laneSettings.ClientId(), _laneSettings.redirectPort,
                           challenge.data(), _state.data(), url.data(), url.size()))
    {
        Log(RedXeLogLevelError, "zoom-sign-in-failed", "the PKCE material could not be generated.");
        NoteFailure(E_FAIL);
        return;
    }
    const HRESULT listening = _listener.Start(static_cast<uint16_t>(_laneSettings.redirectPort));
    if (FAILED(listening))
    {
        Log(RedXeLogLevelError, "zoom-listener-failed",
            "the loopback redirect port is unavailable; change redirectPort in settings.", listening);
        NoteFailure(listening);
        return;
    }
    _signingIn = true;
    _signInDeadline = now + kSignInTimeoutMilliseconds;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        ++_snapshot.signIns;
    }
    // The browser opens through the host's own launch action; the lane never calls the shell itself.
    if (_deviceAccess.load(std::memory_order_acquire) && _host)
    {
        RedXeActionRequest launch{};
        launch.sizeBytes = sizeof(launch);
        launch.actionUtf8 = "system.launch";
        launch.targetUtf8 = url.data();
        launch.sourcePluginId = kPluginId;
        (void)_host->RequestAction(&launch);
    }
    Log(RedXeLogLevelInfo, "zoom-sign-in-started", "waiting for the browser to redirect back to RedXe.");
}

void ZoomService::CompleteSignIn(std::string_view code) noexcept
{
    _signingIn = false;
    _listener.Stop();
    if (!_transport || !_store)
    {
        return;
    }
    TokenSet tokens{};
    const HRESULT exchanged = ExchangeCode(*_transport, _laneSettings.Domain(), _laneSettings.ClientId(),
                                           _laneSettings.redirectPort, code, _verifier.data(), UnixNow(), tokens);
    _verifier.fill('\0');
    if (FAILED(exchanged))
    {
        Log(RedXeLogLevelError, "zoom-sign-in-failed", "the authorization code could not be exchanged.", exchanged);
        NoteFailure(exchanged);
        return;
    }
    if (tokens.refreshBytes == 0)
    {
        Log(RedXeLogLevelError, "zoom-sign-in-failed", "the token response carried no refresh token.");
        NoteFailure(E_UNEXPECTED);
        return;
    }
    (void)_store->Write(_laneSettings.ClientId(), tokens.RefreshToken());
    _tokens = tokens;
    _credentialPresent = true;
    _credentialChecked = true;
    _signedOutLogged = false;
    DisconnectSession();
    _wantConnected = true;
    _reconnectDue = 0;
    Log(RedXeLogLevelInfo, "zoom-sign-in-complete", "signed in; the credential is stored for this user.");
}

void ZoomService::StepSignIn(uint64_t now) noexcept
{
    if (!_signingIn)
    {
        return;
    }
    if (now >= _signInDeadline)
    {
        _signingIn = false;
        _listener.Stop();
        Log(RedXeLogLevelWarning, "zoom-sign-in-failed", "the browser did not redirect back within five minutes.");
        NoteFailure(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        return;
    }
    std::array<char, kMaximumCodeBytes + 1> code{};
    const HRESULT pumped = _listener.Pump(_state.data(), code.data(), code.size());
    if (pumped == S_OK)
    {
        CompleteSignIn(code.data());
    }
    else if (FAILED(pumped))
    {
        _signingIn = false;
        _listener.Stop();
        Log(RedXeLogLevelError, "zoom-listener-failed", "the loopback listener failed.", pumped);
        NoteFailure(pumped);
    }
}

HRESULT ZoomService::RunVerb(std::string_view verb, std::string_view target) noexcept
{
    const SessionSnapshot state = _session->Snapshot();
    bool wanted = false;
    if (verb == "join" || verb == "start")
    {
        RedXeActions::Meeting meeting{};
        const bool hasMeeting = RedXeActions::ParseMeeting(target, meeting);
        if (verb == "join")
        {
            return hasMeeting ? _session->Join(meeting.number, meeting.passcode, _laneSettings.DisplayName())
                              : E_INVALIDARG;
        }
        return _session->Start(hasMeeting ? meeting.number : 0);
    }
    if (verb == "leave" || verb == "end")
    {
        return target == "now" ? _session->Leave(verb == "end") : E_INVALIDARG;
    }
    if (verb == "audio")
    {
        if (target != "join" && target != "leave")
        {
            return E_INVALIDARG;
        }
        return _session->SetAudioJoined(target == "join");
    }
    if (verb == "mute")
    {
        return ToggleWanted(target, state.audioMuted, wanted) ? _session->SetMuted(wanted) : E_INVALIDARG;
    }
    if (verb == "video")
    {
        return ToggleWanted(target, state.videoOn, wanted) ? _session->SetVideo(wanted) : E_INVALIDARG;
    }
    if (verb == "share")
    {
        std::string_view suffix;
        const std::string_view command = RedXeActions::SplitSuffix(target, suffix);
        uint32_t monitorIndex = 1;
        HWND application = nullptr;
        if (command == "monitor")
        {
            RedXeActions::MonitorSelector monitor{};
            if (!suffix.empty())
            {
                if (!RedXeActions::ParseMonitorSelector(suffix, false, monitor))
                {
                    return E_INVALIDARG;
                }
                monitorIndex = monitor.kind == RedXeActions::MonitorSelector::Kind::Index ? monitor.index : 1;
            }
            return _session->Share(ShareCommand::Monitor, monitorIndex, nullptr);
        }
        if (command == "app")
        {
            RedXeActions::WindowSelector window{};
            RedXeActions::SelectedWindows selected{};
            if (!RedXeActions::ParseWindowSelector(suffix, window))
            {
                return E_INVALIDARG;
            }
            if (_deviceAccess.load(std::memory_order_acquire) && RedXeActions::SelectWindows(window, false, selected))
            {
                application = selected.windows[0];
            }
            return _session->Share(ShareCommand::Application, 0, application);
        }
        if (command == "pause")
        {
            return _session->Share(ShareCommand::Pause, 0, nullptr);
        }
        if (command == "resume")
        {
            return _session->Share(ShareCommand::Resume, 0, nullptr);
        }
        if (command == "stop")
        {
            return _session->Share(ShareCommand::Stop, 0, nullptr);
        }
        return E_INVALIDARG;
    }
    if (verb == "record")
    {
        uint32_t index = 0;
        if (!RedXeActions::ParseEnum(target, "local.start|local.stop|cloud.start|cloud.stop|pause|resume", index))
        {
            return E_INVALIDARG;
        }
        return _session->Record(static_cast<RecordCommand>(index));
    }
    if (verb == "raiseHand")
    {
        return ToggleWanted(target, state.handRaised, wanted) ? _session->SetHandRaised(wanted) : E_INVALIDARG;
    }
    if (verb == "reaction")
    {
        uint32_t index = 0;
        return RedXeActions::ParseEnum(target, kReactionOptions, index) ? _session->React(index) : E_INVALIDARG;
    }
    if (verb == "chat.send")
    {
        return _session->SendChat(target);
    }
    if (verb == "captions")
    {
        if (target != "on" && target != "off")
        {
            return E_INVALIDARG;
        }
        return _session->SetCaptions(target == "on");
    }
    if (verb == "participants.muteAll")
    {
        return _session->MuteAll();
    }
    if (verb == "participants.admitAll")
    {
        return _session->AdmitAll();
    }
    if (verb == "focus")
    {
        RedXeActions::WindowSelector window{};
        window.kind = RedXeActions::WindowSelector::Kind::Executable;
        window.value = "Zoom.exe";
        RedXeActions::SelectedWindows selected{};
        const bool deviceAccess = _deviceAccess.load(std::memory_order_acquire);
        if (!deviceAccess)
        {
            return S_OK;
        }
        if (!RedXeActions::SelectWindows(window, false, selected))
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        return RedXeActions::BringToForeground(selected.windows[0], true);
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

void ZoomService::HandleRequest(const PendingRequest& request, uint64_t now) noexcept
{
    const std::string_view action{request.action.data()};
    const std::string_view target{request.target.data()};
    const std::string_view verb = action.substr(sizeof(kActionNamespace));
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _snapshot.lastAction = request.action;
    }
    if (verb == "signIn")
    {
        BeginSignIn(now);
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        ++_snapshot.requestsExecuted;
        return;
    }
    if (verb == "signOut")
    {
        if (target == "now")
        {
            SignOut(true);
            Log(RedXeLogLevelInfo, "zoom-signed-out", "signed out; the stored credential was deleted.");
            const auto guard = wil::AcquireSRWLockExclusive(&_lock);
            ++_snapshot.requestsExecuted;
        }
        else
        {
            NoteFailure(E_INVALIDARG);
        }
        return;
    }
    if (!_session)
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        ++_snapshot.requestsDropped;
        return;
    }
    // Everything else needs an authenticated, connected session; the newest request waits for it.
    const SessionSnapshot state = _session->Snapshot();
    const bool connected =
        _sessionInitialized && state.auth == AuthState::Authenticated && state.ipc == IpcState::Connected;
    if (!connected)
    {
        if (!_credentialPresent && _credentialChecked)
        {
            if (!_signedOutLogged)
            {
                _signedOutLogged = true;
                Log(RedXeLogLevelWarning, "zoom-signed-out", "no Zoom credential; bind zoom.signIn to sign in.");
            }
            NoteFailure(E_NOT_VALID_STATE);
            return;
        }
        _wantConnected = true;
        _deferred = request;
        _deferredPending = true;
        _deferredDeadline = now + kDeferredRequestMilliseconds;
        return;
    }
    const HRESULT result = RunVerb(verb, target);
    if (FAILED(result))
    {
        NoteFailure(result);
        Log(RedXeLogLevelDebug, "zoom-action-failed", "a zoom action was refused by the session.", result);
        return;
    }
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    ++_snapshot.requestsExecuted;
}

HRESULT ZoomService::RunDeviceWork(HANDLE stopEvent, HANDLE wakeEvent) noexcept
{
    if (!stopEvent || !wakeEvent)
    {
        return E_POINTER;
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _wakeEvent = wakeEvent;
        _store = _injectedStore ? _injectedStore : &_credentialManager;
        _transport = _injectedTransport ? _injectedTransport : &_winHttp;
    }
    _laneRunning.store(true, std::memory_order_release);
    Log(RedXeLogLevelInfo, "lane-started",
        _deviceAccess.load(std::memory_order_acquire) ? "zoom lane running." : "zoom lane running (no device access).");
    _laneSettingsGeneration = 0;
    AdoptSettings();
    EnsureSession();
    PublishSnapshot();
    std::array<HANDLE, 3> handles{};
    for (;;)
    {
        uint32_t handleCount = 0;
        handles[handleCount++] = stopEvent;
        handles[handleCount++] = wakeEvent;
        if (_signingIn && _listener.Event())
        {
            handles[handleCount++] = _listener.Event();
        }
        DWORD timeout = INFINITE;
        const uint64_t before = GetTickCount64();
        if (_signingIn)
        {
            timeout =
                static_cast<DWORD>(std::max<uint64_t>(1, _signInDeadline > before ? _signInDeadline - before : 1));
        }
        if (_wantConnected && !_sessionInitialized && _reconnectDue > before)
        {
            timeout = std::min(timeout, static_cast<DWORD>(_reconnectDue - before));
        }
        if (_deferredPending)
        {
            timeout = std::min(timeout, static_cast<DWORD>(std::max<uint64_t>(
                                            1, _deferredDeadline > before ? _deferredDeadline - before : 1)));
        }
        const DWORD waited = WaitForMultipleObjects(handleCount, handles.data(), FALSE, timeout);
        if (waited == WAIT_OBJECT_0)
        {
            break;
        }
        if (waited == WAIT_FAILED)
        {
            Log(RedXeLogLevelError, "lane-wait-failed", "the zoom lane wait failed.",
                HRESULT_FROM_WIN32(GetLastError()));
            break;
        }
        const uint64_t now = GetTickCount64();
        AdoptSettings();
        EnsureSession();
        std::array<PendingRequest, kMaximumPendingRequests> pending{};
        uint32_t pendingCount = 0;
        {
            const auto guard = wil::AcquireSRWLockExclusive(&_lock);
            pending = _pending;
            pendingCount = _pendingCount;
            _pendingCount = 0;
        }
        for (uint32_t index = 0; index < pendingCount; ++index)
        {
            HandleRequest(pending[index], now);
        }
        StepSignIn(now);
        StepConnection(now);
        if (_deferredPending && _session)
        {
            const SessionSnapshot state = _session->Snapshot();
            if (_sessionInitialized && state.auth == AuthState::Authenticated && state.ipc == IpcState::Connected)
            {
                const PendingRequest deferred = _deferred;
                _deferredPending = false;
                HandleRequest(deferred, now);
            }
            else if (now >= _deferredDeadline || !_wantConnected)
            {
                _deferredPending = false;
                NoteFailure(E_NOT_VALID_STATE);
            }
        }
        PublishSnapshot();
    }
    if (_signingIn)
    {
        _listener.Stop();
        _signingIn = false;
    }
    DisconnectSession();
    _session.reset();
    _synthetic = nullptr;
    _laneRunning.store(false, std::memory_order_release);
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _wakeEvent = nullptr;
        _pendingCount = 0;
    }
    PublishSnapshot();
    Log(RedXeLogLevelInfo, "lane-stopped", "zoom lane stopped.");
    return S_OK;
}
} // namespace Zoom
