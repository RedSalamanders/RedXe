// The Zoom Plugin SDK for Windows session, pinned against package zoom-plugin-sdk-windows-7.1.0.2020 (x64,
// export_h/ and demo/PSDKTest/PSDKTestDlg.cpp, ShareTestDlg.cpp, ReactionTestDlg.cpp).
//
// The SDK is not committed: ThirdParty/ZoomPluginSdk/Import-ZoomSdk.ps1 copies the developer-downloaded package
// beside this project and Zoom.vcxproj defines ZOOM_PLUGIN_SDK_AVAILABLE only when that import exists. Without it,
// SdkSessionAvailable() is false, CreateSdkSession() returns null, and the service logs zoom-sdk-unavailable once;
// the synthetic session (ZoomSynthetic.cpp) still drives every test.
//
// Runtime layout: the proxy and its dependency set live in <Plugins>\ZoomSdk\, never beside RedXe.exe, because the
// package ships its own CRT copies (vcruntime140, msvcp140, ucrtbase) that must not shadow the process runtime.
// zToolSuiteIPCProxy.dll is delay-loaded; the hook below maps it from that directory with
// LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR so its dependents resolve there, exactly like PluginHost maps a plugin.
//
// Threading: InitZMToolSuite creates the proxy's message window on the calling thread (the service lane) and the
// proxy delivers listener callbacks and toolkit completions through that queue, so the lane pumps messages while a
// session exists (ZoomService::RunDeviceWork). The snapshot is still guarded by a lock because the package does
// not document the callback thread. Every toolkit method returns bool for *submission*; the typed completion
// carries the result (BaseResult::error_code), so a true return is never treated as success here.

#include "ZoomSession.h"

#if defined(ZOOM_PLUGIN_SDK_AVAILABLE)

#include <array>
#include <cstring>
#include <memory>
#include <new>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

#include <delayimp.h>

#pragma warning(push)
#pragma warning(disable : 4265 4625 4626 5026 5027)
#include "ToolSuiteProxyDef.h"
#include "ToolSuiteProxyInterface.h"
#pragma warning(pop)

namespace Zoom
{
namespace
{
using namespace ZMToolSuiteProxy;

constexpr char kProxyModuleName[] = "zToolSuiteIPCProxy.dll";
constexpr wchar_t kProxyRelativePath[] = L"ZoomSdk\\zToolSuiteIPCProxy.dll";

// Delay-load hook: maps the proxy from <this module's directory>\ZoomSdk\ so the SDK's dependency set resolves
// from there. Returning null lets the helper fall back to the ordinary search, which fails cleanly.
FARPROC WINAPI DelayLoadHook(unsigned notification, PDelayLoadInfo info) noexcept
{
    if (notification != dliNotePreLoadLibrary || !info || !info->szDll || _stricmp(info->szDll, kProxyModuleName) != 0)
    {
        return nullptr;
    }
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&DelayLoadHook), &self))
    {
        return nullptr;
    }
    std::array<wchar_t, MAX_PATH + 64> path{};
    const DWORD length = GetModuleFileNameW(self, path.data(), MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return nullptr;
    }
    wchar_t* slash = wcsrchr(path.data(), L'\\');
    if (!slash)
    {
        return nullptr;
    }
    slash[1] = L'\0';
    if (wcscat_s(path.data(), path.size(), kProxyRelativePath) != 0)
    {
        return nullptr;
    }
    return reinterpret_cast<FARPROC>(
        LoadLibraryExW(path.data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
}

[[nodiscard]] HRESULT MapError(ZMToolSuiteProxyErrors code) noexcept
{
    switch (code)
    {
    case kZMToolSuiteProxyErrorsSuccess:
        return S_OK;
    case kZMToolSuiteProxyErrorsNotImplemented:
        return E_NOTIMPL;
    case kZMToolSuiteProxyErrorsWrongUsage:
        return E_NOT_VALID_STATE;
    case kZMToolSuiteProxyErrorsInvalidArgument:
        return E_INVALIDARG;
    case kZMToolSuiteProxyErrorsNoPermission:
        return E_ACCESSDENIED;
    case kZMToolSuiteProxyErrorsRateLimitReached:
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    case kZMToolSuiteProxyErrorsOperationTimeout:
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    case kZMToolSuiteProxyErrorsVersionIncompatible:
        return HRESULT_FROM_WIN32(ERROR_PRODUCT_VERSION);
    case kZMToolSuiteProxyErrorsInternalError:
    case kZMToolSuiteProxyErrorsUnknown:
    default:
        return E_FAIL;
    }
}

[[nodiscard]] MeetingState MapMeetingStatus(ZMToolSuiteMeetingStatus status, MeetingState current) noexcept
{
    switch (status)
    {
    case MEETING_STATUS_IDLE:
    case MEETING_STATUS_ENDED:
        return MeetingState::Idle;
    case MEETING_STATUS_CONNECTING:
    case MEETING_STATUS_WAITING_FOR_HOST:
    case MEETING_STATUS_RECONNECTING:
    case MEETING_STATUS_IN_WAITING_ROOM:
        return MeetingState::Connecting;
    case MEETING_STATUS_INMEETING:
        return MeetingState::InMeeting;
    case MEETING_STATUS_DISCONNECTING:
        return MeetingState::Ending;
    case MEETING_STATUS_FAILED:
        return MeetingState::Failed;
    default:
        // Lock, unlock, webinar promotion, and breakout-room transitions do not change whether we are in a meeting.
        return current;
    }
}

// Bounded UTF-8 to UTF-16 copy; an empty or invalid input yields an empty string.
template <size_t N> void ToWide(std::string_view text, std::array<wchar_t, N>& out) noexcept
{
    out.fill(L'\0');
    if (text.empty() || text.size() > INT_MAX)
    {
        return;
    }
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                            out.data(), static_cast<int>(N - 1));
    if (written <= 0)
    {
        out.fill(L'\0');
    }
}

// State shared with the SDK's completion callbacks. Completions capture a weak reference so a completion that
// arrives after the session was destroyed touches nothing.
struct Shared final
{
    mutable SRWLOCK lock = SRWLOCK_INIT;
    SessionSnapshot snapshot{};
    ISessionListener* listener = nullptr;
    unsigned int myUserId = 0;
    bool sharePaused = false;
    bool localRecording = false;
    bool cloudRecording = false;

    template <typename Mutation> void Update(Mutation mutation) noexcept
    {
        const auto guard = wil::AcquireSRWLockExclusive(&lock);
        mutation(*this);
        if (listener)
        {
            // Only signals the lane's wake event; never re-enters the session.
            listener->OnSessionChanged();
        }
    }
};

class SdkSession final : public IZoomSession, public IZMToolSuiteProxyListener
{
  public:
    SdkSession() : _shared(std::make_shared<Shared>()) {}
    ~SdkSession() override
    {
        Uninitialize();
    }
    SdkSession(const SdkSession&) = delete;
    SdkSession& operator=(const SdkSession&) = delete;

    HRESULT Initialize(ISessionListener* listener) noexcept override
    {
        if (_initialized)
        {
            return E_NOT_VALID_STATE;
        }
        const HRESULT loaded = __HrLoadAllImportsForDll(kProxyModuleName);
        if (FAILED(loaded))
        {
            return loaded;
        }
        _shared->Update(
            [listener](Shared& state)
            {
                state.snapshot = SessionSnapshot{};
                state.listener = listener;
                state.myUserId = 0;
                state.sharePaused = false;
                state.localRecording = false;
                state.cloudRecording = false;
            });
        InitZMToolSuite();
        SetToolSuiteProxyListener(this);
        _initialized = true;
        return S_OK;
    }

    void Uninitialize() noexcept override
    {
        if (!_initialized)
        {
            return;
        }
        SetToolSuiteProxyListener(nullptr);
        UninitZMToolSuite();
        _initialized = false;
        _shared->Update(
            [](Shared& state)
            {
                state.snapshot = SessionSnapshot{};
                state.listener = nullptr;
            });
    }

    HRESULT Authenticate(std::string_view domain, std::string_view accessToken) noexcept override
    {
        if (!_initialized)
        {
            return E_NOT_VALID_STATE;
        }
        if (accessToken.empty())
        {
            return E_INVALIDARG;
        }
        // The demo passes "https://zoom.us"; a bare host from the settings gets the scheme.
        std::array<wchar_t, kMaximumDomainCharacters> host{};
        ToWide(domain, host);
        _domain.fill(L'\0');
        if (domain.starts_with("http://") || domain.starts_with("https://"))
        {
            (void)wcscpy_s(_domain.data(), _domain.size(), host.data());
        }
        else
        {
            (void)wcscpy_s(_domain.data(), _domain.size(), L"https://");
            (void)wcscat_s(_domain.data(), _domain.size(), host.data());
        }
        ToWide(accessToken, _accessToken);
        ZMToolSuiteProxyAuthContext context{};
        context.accessToken = _accessToken.data();
        context.domain = _domain.data();
        _shared->Update([](Shared& state) { state.snapshot.auth = AuthState::Authenticating; });
        return StartToolSuiteAuth(context) ? S_OK : E_FAIL;
    }

    HRESULT Join(uint64_t meetingNumber, std::string_view passcode, std::string_view displayName) noexcept override
    {
        premeeting::IPreMeetingToolkit* toolkit = _initialized ? premeeting::GetPreMeetingToolkit() : nullptr;
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        ToWide(displayName, _displayName);
        ToWide(passcode, _passcode);
        JoinMeetingParam join{};
        join.meetingNumber = static_cast<long long>(meetingNumber);
        join.displayName = _displayName[0] != L'\0' ? _displayName.data() : nullptr;
        join.password = _passcode[0] != L'\0' ? _passcode.data() : nullptr;
        return Submit(toolkit->JoinMeeting(join, Completion()),
                      [](Shared& state) { state.snapshot.meeting = MeetingState::Connecting; });
    }

    HRESULT Start(uint64_t meetingNumber) noexcept override
    {
        premeeting::IPreMeetingToolkit* toolkit = _initialized ? premeeting::GetPreMeetingToolkit() : nullptr;
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        StartMeetingParam start{};
        start.meetingNumber = static_cast<long long>(meetingNumber);
        // Audio joins so the keypad user can talk at once; the camera follows the client's own setting later.
        start.isNoAudio = false;
        start.isNoVideo = true;
        return Submit(toolkit->StartMeeting(start, Completion()),
                      [](Shared& state) { state.snapshot.meeting = MeetingState::Connecting; });
    }

    HRESULT Leave(bool endForAll) noexcept override
    {
        meeting::IMeetingToolkit* toolkit = InMeetingToolkit(meeting::GetMeetingToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        if (endForAll && !IsHost())
        {
            return E_ACCESSDENIED;
        }
        return Submit(endForAll ? toolkit->EndMeeting(Completion()) : toolkit->LeaveMeeting(Completion()),
                      [](Shared& state) { state.snapshot.meeting = MeetingState::Ending; });
    }

    HRESULT SetAudioJoined(bool joined) noexcept override
    {
        meeting::IAudioToolkit* toolkit = InMeetingToolkit(meeting::GetAudioToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        return Submit(joined ? toolkit->JoinAudio(Completion()) : toolkit->LeaveAudio(Completion()));
    }

    HRESULT SetMuted(bool muted) noexcept override
    {
        meeting::IAudioToolkit* toolkit = InMeetingToolkit(meeting::GetAudioToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        return Submit(muted ? toolkit->MuteMyAudio(Completion()) : toolkit->UnMuteMyAudio(Completion()));
    }

    HRESULT SetVideo(bool on) noexcept override
    {
        meeting::IVideoToolkit* toolkit = InMeetingToolkit(meeting::GetVideoToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        return Submit(on ? toolkit->UnMuteMyVideo(Completion()) : toolkit->MuteMyVideo(Completion()));
    }

    HRESULT Share(ShareCommand command, uint32_t monitorIndex, HWND application) noexcept override
    {
        meeting::IShareToolkit* toolkit = InMeetingToolkit(meeting::GetShareToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        switch (command)
        {
        case ShareCommand::Monitor:
            // StartMonitorShare takes the MONITORINFOEX device name, as the demo's ShareTestDlg passes it.
            if (!SelectMonitorDevice(monitorIndex))
            {
                return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            }
            return Submit(toolkit->StartMonitorShare(static_cast<void*>(_shareDevice.data()), Completion()));
        case ShareCommand::Application:
            if (!application)
            {
                return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            }
            return Submit(toolkit->StartAppShare(static_cast<void*>(application), Completion()));
        case ShareCommand::Pause:
        case ShareCommand::Resume:
        {
            // The SDK only toggles; pause and resume are honored against the state its callbacks report.
            bool sharing = false;
            bool paused = false;
            {
                const auto guard = wil::AcquireSRWLockShared(&_shared->lock);
                sharing = _shared->snapshot.sharing;
                paused = _shared->sharePaused;
            }
            if (!sharing)
            {
                return E_NOT_VALID_STATE;
            }
            if (paused == (command == ShareCommand::Pause))
            {
                return S_OK;
            }
            return Submit(toolkit->PauseResumeShareToggle(Completion()));
        }
        case ShareCommand::Stop:
            return Submit(toolkit->StopShare(Completion()));
        default:
            return E_INVALIDARG;
        }
    }

    HRESULT Record(RecordCommand command) noexcept override
    {
        meeting::IRecordingToolkit* toolkit = InMeetingToolkit(meeting::GetRecordingToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        bool cloud = false;
        {
            const auto guard = wil::AcquireSRWLockShared(&_shared->lock);
            cloud = _shared->cloudRecording;
        }
        switch (command)
        {
        case RecordCommand::LocalStart:
            return Submit(toolkit->StartLocalRecording(Completion()));
        case RecordCommand::LocalStop:
            return Submit(toolkit->StopLocalRecording(Completion()));
        case RecordCommand::CloudStart:
            return Submit(toolkit->StartCloudRecording(Completion()));
        case RecordCommand::CloudStop:
            return Submit(toolkit->StopCloudRecording(Completion()));
        case RecordCommand::Pause:
            return Submit(cloud ? toolkit->PauseCloudRecording(Completion())
                                : toolkit->PauseLocalRecording(Completion()));
        case RecordCommand::Resume:
            return Submit(cloud ? toolkit->ResumeCloudRecording(Completion())
                                : toolkit->ResumeLocalRecording(Completion()));
        default:
            return E_INVALIDARG;
        }
    }

    HRESULT SetHandRaised(bool raised) noexcept override
    {
        meeting::IReactionToolkit* toolkit = InMeetingToolkit(meeting::GetReactionToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        // The hand is a feedback reaction; Clear withdraws it (demo ReactionTestDlg).
        return Submit(toolkit->SendFeedbackReaction(
            raised ? IPCFeedbackReactionType_Hand : IPCFeedbackReactionType_Clear, Completion()));
    }

    HRESULT React(uint32_t reaction) noexcept override
    {
        meeting::IReactionToolkit* toolkit = InMeetingToolkit(meeting::GetReactionToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        // kReactionOptions order: thumbsUp|clap|heart|joy|openMouth|tada.
        static constexpr std::array<IPCEmojiReactionType, 6> kTypes{
            IPCEmojiReactionType_Thumbsup, IPCEmojiReactionType_Clap,      IPCEmojiReactionType_Heart,
            IPCEmojiReactionType_Joy,      IPCEmojiReactionType_Openmouth, IPCEmojiReactionType_Tada,
        };
        if (reaction >= kTypes.size())
        {
            return E_INVALIDARG;
        }
        SendEmojiReactionParam param{};
        param.type = kTypes[reaction];
        param.skinTone = IPCEmojiReactionSkinTone_Default;
        return Submit(toolkit->SendEmojiReaction(param, Completion()));
    }

    HRESULT SendChat(std::string_view textUtf8) noexcept override
    {
        meeting::IChatToolkit* toolkit = InMeetingToolkit(meeting::GetChatToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        if (textUtf8.empty())
        {
            return E_INVALIDARG;
        }
        ToWide(textUtf8, _chatText);
        ZMToolSuiteChatInfo chat{};
        chat.strContent = _chatText.data();
        chat.iUserID = 0;
        chat.eChatType = CHAT_MESSAGE_TYPE_TO_ALL;
        return Submit(toolkit->SendChat(chat, Completion()));
    }

    HRESULT SetCaptions(bool on) noexcept override
    {
        meeting::IClosedCaptionToolkit* toolkit = InMeetingToolkit(meeting::GetClosedCaptionToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        // The meeting's closed-caption feature (host-level); "on" also starts live transcription.
        const HRESULT enabled = Submit(toolkit->EnableClosedCaptionFeature(on, Completion()));
        if (FAILED(enabled) || !on)
        {
            return enabled;
        }
        return Submit(toolkit->StartLiveTranscription(Completion()));
    }

    HRESULT MuteAll() noexcept override
    {
        meeting::IAudioToolkit* toolkit = InMeetingToolkit(meeting::GetAudioToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        if (!IsHost())
        {
            return E_ACCESSDENIED;
        }
        return Submit(toolkit->MuteAllAudio(Completion()));
    }

    HRESULT AdmitAll() noexcept override
    {
        meeting::IWaitingRoomToolkit* toolkit = InMeetingToolkit(meeting::GetWaitingRoomToolkit);
        if (!toolkit)
        {
            return E_NOT_VALID_STATE;
        }
        if (!IsHost())
        {
            return E_ACCESSDENIED;
        }
        return Submit(toolkit->AdmitAllUserToMeeting(Completion()));
    }

    SessionSnapshot Snapshot() const noexcept override
    {
        const auto guard = wil::AcquireSRWLockShared(&_shared->lock);
        return _shared->snapshot;
    }

    // IZMToolSuiteProxyListener (export_h/ToolSuiteProxyInterface.h, 7.1.0.2020). Bounded copies under the lock;
    // nothing blocks.
    void OnAuthResult(ZMToolSuiteProxyAuthResult eResult) override
    {
        _shared->Update(
            [eResult](Shared& state)
            {
                state.snapshot.auth = eResult == AUTH_RESULT_SUCCESS ? AuthState::Authenticated : AuthState::Failed;
                state.snapshot.lastError = eResult == AUTH_RESULT_SUCCESS ? S_OK : E_ACCESSDENIED;
            });
    }

    void OnIPCConnectStatusChanged(ZMToolSuiteIPCConnectStatus eStatus, std::string) override
    {
        _shared->Update(
            [eStatus](Shared& state)
            {
                state.snapshot.ipc = eStatus == IPC_STATUS_CONNECTED ? IpcState::Connected : IpcState::Disconnected;
                if (eStatus != IPC_STATUS_CONNECTED)
                {
                    ResetMeeting(state);
                }
            });
    }

    void OnUserStatusChanged(ZMToolSuiteUserList vUserList, ZMToolSuiteMeetingInstance eInstance) override
    {
        if (eInstance != ZMToolSuiteMeetingInstance_Default || !vUserList.vUserList)
        {
            return;
        }
        const int count = vUserList.vUserList->GetCount();
        for (int index = 0; index < count; ++index)
        {
            const ZMToolSuiteUser user = vUserList.vUserList->GetItem(index);
            if (user.bMyself)
            {
                ApplySelf(user);
                return;
            }
        }
    }

    void OnMeetingStatusChanged(ZMToolSuiteProxy::ZMToolSuiteMeetingStatus status) override
    {
        _shared->Update(
            [status](Shared& state)
            {
                const MeetingState next = MapMeetingStatus(status, state.snapshot.meeting);
                if (next == MeetingState::Idle || next == MeetingState::Failed)
                {
                    ResetMeeting(state);
                }
                state.snapshot.meeting = next;
            });
        if (status == MEETING_STATUS_INMEETING)
        {
            RefreshSelf();
        }
    }

    void OnMeetingShareOptionsChanged(ZMToolSuiteShareOptions&) override {}

    void OnUserRaisedHand(ZMToolSuiteMeetingInstance eInstance, unsigned int iUserID) override
    {
        SetHandIfSelf(eInstance, iUserID, true);
    }

    void OnUserLowerHand(ZMToolSuiteMeetingInstance eInstance, unsigned int iUserID) override
    {
        SetHandIfSelf(eInstance, iUserID, false);
    }

    void OnRecordingStatusChanged(ZMToolSuiteMeetingInstance eInstance) override
    {
        if (eInstance != ZMToolSuiteMeetingInstance_Default || !_initialized)
        {
            return;
        }
        meeting::IRecordingToolkit* toolkit = meeting::GetRecordingToolkit(ZMToolSuiteMeetingInstance_Default);
        if (!toolkit)
        {
            return;
        }
        std::weak_ptr<Shared> weak = _shared;
        (void)toolkit->GetLocalRecordingStatus(
            [weak](const GetLocalRecordingStatsResult& result)
            {
                if (const std::shared_ptr<Shared> shared = weak.lock())
                {
                    shared->Update(
                        [&result](Shared& state)
                        {
                            state.localRecording = result.error_code == kZMToolSuiteProxyErrorsSuccess &&
                                                   (result.status == ZMToolSuiteProxyRecordStatus_Start ||
                                                    result.status == ZMToolSuiteProxyRecordStatus_Pause);
                            state.snapshot.recording = state.localRecording || state.cloudRecording;
                        });
                }
            });
        (void)toolkit->GetCloudRecordingStatus(
            [weak](const GetCmrStatsResult& result)
            {
                if (const std::shared_ptr<Shared> shared = weak.lock())
                {
                    shared->Update(
                        [&result](Shared& state)
                        {
                            state.cloudRecording = result.error_code == kZMToolSuiteProxyErrorsSuccess &&
                                                   (result.status == ZMToolSuiteProxyRecordStatus_Start ||
                                                    result.status == ZMToolSuiteProxyRecordStatus_Pause);
                            state.snapshot.recording = state.localRecording || state.cloudRecording;
                        });
                }
            });
    }

    void OnEmojiReactionReceived(unsigned int, SendEmojiReactionParam, ZMToolSuiteMeetingInstance) override {}
    void OnEmojiReactionReceivedInWebinar(IToolSuiteVector<WebinarEmojiContent>*, ZMToolSuiteMeetingInstance) override
    {
    }
    void OnEmojiFeedbackReceived(unsigned int, IPCFeedbackReactionType, ZMToolSuiteMeetingInstance) override {}
    void OnAllHandsLowered(ZMToolSuiteMeetingInstance eInstance) override
    {
        if (eInstance == ZMToolSuiteMeetingInstance_Default)
        {
            _shared->Update([](Shared& state) { state.snapshot.handRaised = false; });
        }
    }
    void OnGroupLayoutUpdate(ZMToolSuiteMeetingInstance) override {}
    void OnMyVideoStatusChange(bool bVideoOn, ZMToolSuiteMeetingInstance eInstance) override
    {
        if (eInstance == ZMToolSuiteMeetingInstance_Default)
        {
            _shared->Update([bVideoOn](Shared& state) { state.snapshot.videoOn = bVideoOn; });
        }
    }
    void OnStartShareAudio(ZMToolSuiteMeetingInstance) override {}
    void OnStopShareAudio(ZMToolSuiteMeetingInstance) override {}
    void OnStartViewAudio(unsigned int, ZMToolSuiteMeetingInstance) override {}
    void OnStopViewAudio(unsigned int, ZMToolSuiteMeetingInstance) override {}
    void OnShareSendStatusChanged(unsigned int nShareSourceUserID, bool bPaused,
                                  ZMToolSuiteMeetingInstance eInstance) override
    {
        if (eInstance == ZMToolSuiteMeetingInstance_Default)
        {
            _shared->Update(
                [nShareSourceUserID, bPaused](Shared& state)
                {
                    if (state.myUserId == 0 || nShareSourceUserID == state.myUserId)
                    {
                        state.sharePaused = bPaused;
                    }
                });
        }
    }
    void OnStartSendShare(ZMToolSuiteMeetingInstance eInstance) override
    {
        if (eInstance == ZMToolSuiteMeetingInstance_Default)
        {
            _shared->Update(
                [](Shared& state)
                {
                    state.snapshot.sharing = true;
                    state.sharePaused = false;
                });
        }
    }
    void OnStopSendShare(ZMToolSuiteMeetingInstance eInstance) override
    {
        if (eInstance == ZMToolSuiteMeetingInstance_Default)
        {
            _shared->Update(
                [](Shared& state)
                {
                    state.snapshot.sharing = false;
                    state.sharePaused = false;
                });
        }
    }
    void OnShareContentTypeChanged(ZMToolSuiteProxyShareContentType, unsigned int, ZMToolSuiteMeetingInstance) override
    {
    }
    void OnShareStatusChanged(unsigned int, ZMToolSuiteShareStatus, ZMToolSuiteMeetingInstance) override {}
    void OnSharedVideoEnded(ZMToolSuiteMeetingInstance) override {}
    void OnVideoFileSharePlayError(ZMToolSuiteProxySharePlayError, ZMToolSuiteMeetingInstance) override {}
    void OnMyAudioStatusChanged(bool bMuted, ZMToolSuiteMeetingInstance eInstance) override
    {
        if (eInstance == ZMToolSuiteMeetingInstance_Default)
        {
            _shared->Update([bMuted](Shared& state) { state.snapshot.audioMuted = bMuted; });
        }
    }
    void OnOriginalSoundStatusChange(bool, ZMToolSuiteMeetingInstance) override {}
    void OnUnreadChatCountChange(unsigned int, ZMToolSuiteMeetingInstance) override {}
    void OnChatWindowStatusChange(bool, ZMToolSuiteMeetingInstance) override {}
    void OnMeetingViewTypeChange(IPCMeetingViewType, ZMToolSuiteMeetingInstance) override {}
    void OnFullScreenStatusChange(bool, ZMToolSuitMonitorType, ZMToolSuiteMeetingInstance) override {}

  private:
    static constexpr size_t kMaximumDomainCharacters = 160;
    static constexpr size_t kMaximumTokenCharacters = 2049;
    static constexpr size_t kMaximumTextCharacters = 513;

    static void ResetMeeting(Shared& state) noexcept
    {
        state.snapshot.audioJoined = false;
        state.snapshot.audioMuted = false;
        state.snapshot.videoOn = false;
        state.snapshot.sharing = false;
        state.snapshot.handRaised = false;
        state.snapshot.recording = false;
        state.snapshot.isHost = false;
        state.myUserId = 0;
        state.sharePaused = false;
        state.localRecording = false;
        state.cloudRecording = false;
    }

    void ApplySelf(const ZMToolSuiteUser& user) noexcept
    {
        _shared->Update(
            [&user](Shared& state)
            {
                state.myUserId = user.iUserID;
                state.snapshot.isHost = user.bHost;
                state.snapshot.handRaised = user.bRaisedHand;
                state.snapshot.audioMuted = user.bAudioMuted;
                state.snapshot.videoOn = user.bVideoOn;
                state.snapshot.audioJoined = user.audioType != ZMToolSuiteAudioType_None;
            });
    }

    // GetMySelf is synchronous; the borrowed name pointer is not retained.
    void RefreshSelf() noexcept
    {
        meeting::IParticipantsToolkit* toolkit =
            _initialized ? meeting::GetParticipantsToolkit(ZMToolSuiteMeetingInstance_Default) : nullptr;
        if (!toolkit)
        {
            return;
        }
        const ZMToolSuiteUser self = toolkit->GetMySelf();
        if (self.iUserID != 0 || self.bMyself)
        {
            ApplySelf(self);
        }
    }

    void SetHandIfSelf(ZMToolSuiteMeetingInstance eInstance, unsigned int iUserID, bool raised) noexcept
    {
        if (eInstance != ZMToolSuiteMeetingInstance_Default)
        {
            return;
        }
        _shared->Update(
            [iUserID, raised](Shared& state)
            {
                if (state.myUserId != 0 && iUserID == state.myUserId)
                {
                    state.snapshot.handRaised = raised;
                }
            });
    }

    [[nodiscard]] bool IsHost() const noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&_shared->lock);
        return _shared->snapshot.isHost;
    }

    // A meeting toolkit is only used while the session reports an active meeting.
    template <typename Getter> [[nodiscard]] auto InMeetingToolkit(Getter getter) const noexcept
    {
        using Toolkit = decltype(getter(ZMToolSuiteMeetingInstance_Default));
        if (!_initialized)
        {
            return static_cast<Toolkit>(nullptr);
        }
        {
            const auto guard = wil::AcquireSRWLockShared(&_shared->lock);
            if (_shared->snapshot.meeting != MeetingState::InMeeting)
            {
                return static_cast<Toolkit>(nullptr);
            }
        }
        return getter(ZMToolSuiteMeetingInstance_Default);
    }

    // The typed completion every toolkit call takes: counts the completion and records the mapped error.
    [[nodiscard]] std::function<void(const BaseResult&)> Completion() const noexcept
    {
        std::weak_ptr<Shared> weak = _shared;
        return [weak](const BaseResult& result)
        {
            if (const std::shared_ptr<Shared> shared = weak.lock())
            {
                const HRESULT mapped = MapError(result.error_code);
                shared->Update(
                    [mapped](Shared& state)
                    {
                        ++state.snapshot.completions;
                        state.snapshot.lastError = mapped;
                    });
            }
        };
    }

    // Counts a submission; false from the toolkit means the proxy refused to even send the request.
    template <typename Mutation> HRESULT Submit(bool submitted, Mutation mutation) noexcept
    {
        if (!submitted)
        {
            return E_NOT_VALID_STATE;
        }
        _shared->Update(
            [&mutation](Shared& state)
            {
                ++state.snapshot.submissions;
                mutation(state);
            });
        return S_OK;
    }

    HRESULT Submit(bool submitted) noexcept
    {
        return Submit(submitted, [](Shared&) {});
    }

    // Copies the device name of the requested monitor (1-based EnumDisplayMonitors order; 0 means primary).
    bool SelectMonitorDevice(uint32_t monitorIndex) noexcept
    {
        struct Enumeration final
        {
            uint32_t wanted;
            uint32_t seen;
            wchar_t* device;
            size_t capacity;
            bool found;
        };
        Enumeration enumeration{monitorIndex, 0, _shareDevice.data(), _shareDevice.size(), false};
        _shareDevice.fill(L'\0');
        (void)EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM data) noexcept -> BOOL
            {
                Enumeration& scan = *reinterpret_cast<Enumeration*>(data);
                MONITORINFOEXW info{};
                info.cbSize = sizeof(info);
                if (!GetMonitorInfoW(monitor, &info))
                {
                    return TRUE;
                }
                ++scan.seen;
                const bool match =
                    scan.wanted == 0 ? (info.dwFlags & MONITORINFOF_PRIMARY) != 0 : scan.seen == scan.wanted;
                if (match)
                {
                    (void)wcscpy_s(scan.device, scan.capacity, info.szDevice);
                    scan.found = true;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&enumeration));
        return enumeration.found;
    }

    std::shared_ptr<Shared> _shared;
    bool _initialized = false;
    std::array<wchar_t, kMaximumDomainCharacters> _domain{};
    std::array<wchar_t, kMaximumTokenCharacters> _accessToken{};
    std::array<wchar_t, kMaximumTextCharacters> _displayName{};
    std::array<wchar_t, kMaximumTextCharacters> _passcode{};
    std::array<wchar_t, kMaximumTextCharacters> _chatText{};
    std::array<wchar_t, CCHDEVICENAME> _shareDevice{};
};
} // namespace

bool SdkSessionAvailable() noexcept
{
    return true;
}

IZoomSession* CreateSdkSession() noexcept
{
    return new (std::nothrow) SdkSession();
}
} // namespace Zoom

// The delay-load notify hook is looked up by the CRT helper by this exact name.
ExternC const PfnDliHook __pfnDliNotifyHook2 = Zoom::DelayLoadHook;

#else

namespace Zoom
{
bool SdkSessionAvailable() noexcept
{
    return false;
}

IZoomSession* CreateSdkSession() noexcept
{
    return nullptr;
}
} // namespace Zoom

#endif
