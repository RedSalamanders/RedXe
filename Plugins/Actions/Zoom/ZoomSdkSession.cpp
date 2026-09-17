// The Zoom Plugin SDK for Windows session (package zoom-plugin-sdk-windows-7.1.0.2020, x64).
//
// The SDK is not committed: ThirdParty/ZoomPluginSdk/Import-ZoomSdk.ps1 copies the developer-downloaded package
// beside this project and Zoom.vcxproj defines ZOOM_PLUGIN_SDK_AVAILABLE only when that import exists. Without it,
// SdkSessionAvailable() is false, CreateSdkSession() returns null, and the service logs zoom-sdk-unavailable once;
// the synthetic session (ZoomSynthetic.cpp) still drives every test.
//
// What is written below follows the names Zoom's documentation quotes: InitZMToolSuite, ZMToolSuiteProxyAuthContext,
// StartToolSuiteAuth, SetToolSuiteProxyListener, IZMToolSuiteProxyListener with OnAuthResult /
// OnIPCConnectStatusChanged / OnMeetingStatusChanged, UninitZMToolSuite, premeeting::GetPreMeetingToolkit()->
// JoinMeeting(JoinMeetingParam, callback), meeting::GetMeetingToolkit(instance), GetAudioToolkit(),
// GetVideoToolkit(), GetShareToolkit(instance) with CanStartShare and StartAppShare(void*, callback), the
// participants / recording / reaction / chat / captions toolkits, and ZMToolSuiteMeetingInstance_Default. Zoom
// states that the public docs cover fewer members than the headers, so the exact callback parameter types and the
// per-toolkit method names are pinned from <arch>/export_h/ and demo/PSDKTestDlg.cpp when the package is imported:
// every place that needs pinning is marked ZOOM_SDK_PIN and the build fails there until it is done, so the
// scaffold can never ship half-pinned.

#include "ZoomSession.h"

#if defined(ZOOM_PLUGIN_SDK_AVAILABLE)

#include <atomic>
#include <cstring>
#include <new>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// Delay-loaded so a deployment without zToolSuiteIPCProxy.dll fails at Initialize with a logged HRESULT rather
// than at module map. The imports resolve once on the service lane.
#include <delayimp.h>

#include "ToolSuiteProxyDef.h"
#include "ToolSuiteProxyInterface.h"

namespace Zoom
{
namespace
{
constexpr char kProxyModule[] = "zToolSuiteIPCProxy.dll";

// ZOOM_SDK_PIN: the listener's pure virtual set and each callback's parameter types come from
// export_h/ToolSuiteProxyInterface.h. The three documented callbacks are declared with placeholder parameter
// types below; replace them with the header's signatures and remove the #error.
#error ZOOM_SDK_PIN: pin IZMToolSuiteProxyListener callback signatures from export_h before building with the SDK.

class SdkSession final : public IZoomSession, public IZMToolSuiteProxyListener
{
  public:
    SdkSession() = default;
    ~SdkSession() override
    {
        Uninitialize();
    }

    HRESULT Initialize(ISessionListener* listener) noexcept override
    {
        if (_initialized)
        {
            return E_NOT_VALID_STATE;
        }
        const HRESULT loaded = __HrLoadAllImportsForDll(kProxyModule);
        if (FAILED(loaded))
        {
            return loaded;
        }
        _listener = listener;
        if (!InitZMToolSuite())
        {
            return E_FAIL;
        }
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
        _listener = nullptr;
        Update([](SessionSnapshot& state) { state = SessionSnapshot{}; });
    }

    HRESULT Authenticate(std::string_view domain, std::string_view accessToken) noexcept override
    {
        if (!_initialized)
        {
            return E_NOT_VALID_STATE;
        }
        ZMToolSuiteProxyAuthContext context{};
        // ZOOM_SDK_PIN: field names (domain, token) per ToolSuiteProxyDef.h.
        (void)domain;
        (void)accessToken;
        Update([](SessionSnapshot& state) { state.auth = AuthState::Authenticating; });
        return StartToolSuiteAuth(context) ? S_OK : E_FAIL;
    }

    HRESULT Join(uint64_t meetingNumber, std::string_view, std::string_view) noexcept override
    {
        JoinMeetingParam join{};
        join.meetingNumber = static_cast<long long>(meetingNumber);
        // ZOOM_SDK_PIN: displayName / passcode members and the completion callback type.
        Update(
            [](SessionSnapshot& state)
            {
                ++state.submissions;
                state.meeting = MeetingState::Connecting;
            });
        return premeeting::GetPreMeetingToolkit()->JoinMeeting(join, nullptr) ? S_OK : E_FAIL;
    }

    // ZOOM_SDK_PIN: the remaining toolkit calls (Start, Leave, SetAudioJoined, SetMuted, SetVideo, Share,
    // Record, SetHandRaised, React, SendChat, SetCaptions, MuteAll, AdmitAll) map one-to-one to the toolkits
    // named in the file header; each returns E_NOTIMPL until pinned.
    HRESULT Start(uint64_t) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT Leave(bool) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT SetAudioJoined(bool) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT SetMuted(bool) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT SetVideo(bool) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT Share(ShareCommand, uint32_t, HWND) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT Record(RecordCommand) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT SetHandRaised(bool) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT React(uint32_t) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT SendChat(std::string_view) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT SetCaptions(bool) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT MuteAll() noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT AdmitAll() noexcept override
    {
        return E_NOTIMPL;
    }

    SessionSnapshot Snapshot() const noexcept override
    {
        const auto guard = wil::AcquireSRWLockShared(&_lock);
        return _snapshot;
    }

    // IZMToolSuiteProxyListener. Callbacks arrive on the SDK's thread: copy bounded fields under the lock, wake the
    // lane, never block.
    // ZOOM_SDK_PIN: parameter types.
    void OnAuthResult(int result) override
    {
        Update([result](SessionSnapshot& state)
               { state.auth = result == AUTH_RESULT_SUCCESS ? AuthState::Authenticated : AuthState::Failed; });
    }
    void OnIPCConnectStatusChanged(int status) override
    {
        Update([status](SessionSnapshot& state)
               { state.ipc = status == IPC_STATUS_CONNECTED ? IpcState::Connected : IpcState::Disconnected; });
    }
    void OnMeetingStatusChanged(int status) override
    {
        Update([status](SessionSnapshot& state)
               { state.meeting = status == MEETING_STATUS_INMEETING ? MeetingState::InMeeting : MeetingState::Idle; });
    }

  private:
    template <typename Mutation> void Update(Mutation mutation) noexcept
    {
        {
            const auto guard = wil::AcquireSRWLockExclusive(&_lock);
            mutation(_snapshot);
        }
        if (_listener)
        {
            _listener->OnSessionChanged();
        }
    }

    mutable SRWLOCK _lock = SRWLOCK_INIT;
    SessionSnapshot _snapshot{};
    ISessionListener* _listener = nullptr;
    bool _initialized = false;
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
