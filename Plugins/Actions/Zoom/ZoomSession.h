#pragma once

// The Zoom Plugin SDK session as the service's lane sees it. One implementation wraps the SDK's IPC proxy
// (ZoomSdkSession.cpp, compiled only when the SDK package is imported); the other is deterministic and in-memory
// (ZoomSynthetic.cpp) so ZoomTests and automated hosts drive every state without Zoom, a network, or the SDK DLL.
//
// Threading: every method is called on the service's device lane. Listener callbacks arrive on whatever thread
// the implementation uses (the SDK's, or the caller's for the synthetic session); they copy nothing and only
// signal the lane, which then reads the snapshot under the implementation's lock.

#include <cstdint>
#include <string_view>
#include <windows.h>

namespace Zoom
{
enum class AuthState : uint8_t
{
    SignedOut = 0,
    Authenticating,
    Authenticated,
    Failed,
};

enum class IpcState : uint8_t
{
    Disconnected = 0,
    Connecting,
    Connected,
};

enum class MeetingState : uint8_t
{
    Idle = 0,
    Connecting,
    InMeeting,
    Ending,
    Failed,
};

enum class ShareCommand : uint8_t
{
    Monitor = 0,
    Application,
    Pause,
    Resume,
    Stop,
};

enum class RecordCommand : uint8_t
{
    LocalStart = 0,
    LocalStop,
    CloudStart,
    CloudStop,
    Pause,
    Resume,
};

// What the lane knows about the session; copied under the implementation's lock.
struct SessionSnapshot final
{
    AuthState auth = AuthState::SignedOut;
    IpcState ipc = IpcState::Disconnected;
    MeetingState meeting = MeetingState::Idle;
    bool audioJoined = false;
    bool audioMuted = false;
    bool videoOn = false;
    bool sharing = false;
    bool handRaised = false;
    bool recording = false;
    bool isHost = false;
    uint32_t submissions = 0;
    uint32_t completions = 0;
    HRESULT lastError = S_OK;
};

struct ISessionListener
{
    // Any thread: state changed, the lane should re-read the snapshot.
    virtual void OnSessionChanged() noexcept = 0;

  protected:
    ~ISessionListener() = default;
};

class IZoomSession
{
  public:
    virtual ~IZoomSession() = default;

    // InitZMToolSuite and listener registration. E_NOT_VALID_STATE when already initialized.
    [[nodiscard]] virtual HRESULT Initialize(ISessionListener* listener) noexcept = 0;
    // UninitZMToolSuite and listener removal; idempotent.
    virtual void Uninitialize() noexcept = 0;
    // StartToolSuiteAuth with a user OAuth access token; the result arrives through OnAuthResult.
    [[nodiscard]] virtual HRESULT Authenticate(std::string_view domain, std::string_view accessToken) noexcept = 0;

    // Pre-meeting toolkit. meetingNumber 0 starts an instant meeting.
    [[nodiscard]] virtual HRESULT Join(uint64_t meetingNumber, std::string_view passcode,
                                       std::string_view displayName) noexcept = 0;
    [[nodiscard]] virtual HRESULT Start(uint64_t meetingNumber) noexcept = 0;
    // Meeting toolkit: leave, or end for everyone (host only).
    [[nodiscard]] virtual HRESULT Leave(bool endForAll) noexcept = 0;
    // Audio and video toolkits.
    [[nodiscard]] virtual HRESULT SetAudioJoined(bool joined) noexcept = 0;
    [[nodiscard]] virtual HRESULT SetMuted(bool muted) noexcept = 0;
    [[nodiscard]] virtual HRESULT SetVideo(bool on) noexcept = 0;
    // Share toolkit: monitorIndex is 1-based for Monitor; application is the window for Application.
    [[nodiscard]] virtual HRESULT Share(ShareCommand command, uint32_t monitorIndex, HWND application) noexcept = 0;
    [[nodiscard]] virtual HRESULT Record(RecordCommand command) noexcept = 0;
    // Participants toolkit, self and host operations.
    [[nodiscard]] virtual HRESULT SetHandRaised(bool raised) noexcept = 0;
    [[nodiscard]] virtual HRESULT React(uint32_t reaction) noexcept = 0;
    [[nodiscard]] virtual HRESULT SendChat(std::string_view textUtf8) noexcept = 0;
    [[nodiscard]] virtual HRESULT SetCaptions(bool on) noexcept = 0;
    [[nodiscard]] virtual HRESULT MuteAll() noexcept = 0;
    [[nodiscard]] virtual HRESULT AdmitAll() noexcept = 0;

    [[nodiscard]] virtual SessionSnapshot Snapshot() const noexcept = 0;
};

// The reaction set the zoom.reaction enum maps to, in enum order.
inline constexpr const char* kReactionOptions = "thumbsUp|clap|heart|joy|openMouth|tada";

// Whether the SDK-backed session can be created in this build (the SDK package was imported at build time).
[[nodiscard]] bool SdkSessionAvailable() noexcept;
// Creates the SDK-backed session, or returns null when unavailable.
[[nodiscard]] IZoomSession* CreateSdkSession() noexcept;
} // namespace Zoom
