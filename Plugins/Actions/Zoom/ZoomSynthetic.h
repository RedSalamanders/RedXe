#pragma once

// Deterministic in-memory Zoom session. Every request succeeds immediately and updates the snapshot the way the
// SDK's callbacks would, unless a test scripted a failure. Authentication succeeds for any non-empty token whose
// text is not "expired" (which reports AuthState::Failed, as the SDK does for a rejected token), IPC connects right
// after authentication, and Join / Start reach InMeeting at once.

#include "ZoomSession.h"

#include <windows.h>

namespace Zoom
{
class SyntheticSession final : public IZoomSession
{
  public:
    SyntheticSession() = default;
    ~SyntheticSession() override;

    [[nodiscard]] HRESULT Initialize(ISessionListener* listener) noexcept override;
    void Uninitialize() noexcept override;
    [[nodiscard]] HRESULT Authenticate(std::string_view domain, std::string_view accessToken) noexcept override;
    [[nodiscard]] HRESULT Join(uint64_t meetingNumber, std::string_view passcode,
                               std::string_view displayName) noexcept override;
    [[nodiscard]] HRESULT Start(uint64_t meetingNumber) noexcept override;
    [[nodiscard]] HRESULT Leave(bool endForAll) noexcept override;
    [[nodiscard]] HRESULT SetAudioJoined(bool joined) noexcept override;
    [[nodiscard]] HRESULT SetMuted(bool muted) noexcept override;
    [[nodiscard]] HRESULT SetVideo(bool on) noexcept override;
    [[nodiscard]] HRESULT Share(ShareCommand command, uint32_t monitorIndex, HWND application) noexcept override;
    [[nodiscard]] HRESULT Record(RecordCommand command) noexcept override;
    [[nodiscard]] HRESULT SetHandRaised(bool raised) noexcept override;
    [[nodiscard]] HRESULT React(uint32_t reaction) noexcept override;
    [[nodiscard]] HRESULT SendChat(std::string_view textUtf8) noexcept override;
    [[nodiscard]] HRESULT SetCaptions(bool on) noexcept override;
    [[nodiscard]] HRESULT MuteAll() noexcept override;
    [[nodiscard]] HRESULT AdmitAll() noexcept override;
    [[nodiscard]] SessionSnapshot Snapshot() const noexcept override;

    // Test controls (any thread).
    void SetHost(bool host) noexcept;
    // Simulates the client dropping the IPC connection; the lane reconnects with backoff.
    void DropConnection() noexcept;
    // The next request fails with this HRESULT once.
    void FailNext(HRESULT error) noexcept;
    [[nodiscard]] uint32_t InitializeCount() const noexcept;
    [[nodiscard]] uint32_t UninitializeCount() const noexcept;
    [[nodiscard]] uint64_t LastMeetingNumber() const noexcept;
    [[nodiscard]] uint32_t LastReaction() const noexcept;
    [[nodiscard]] uint32_t ChatMessages() const noexcept;

  private:
    [[nodiscard]] HRESULT Submit(bool needsMeeting) noexcept;
    void Notify() noexcept;

    mutable SRWLOCK _lock = SRWLOCK_INIT;
    ISessionListener* _listener = nullptr;
    SessionSnapshot _snapshot{};
    bool _initialized = false;
    HRESULT _failNext = S_OK;
    uint32_t _initializeCount = 0;
    uint32_t _uninitializeCount = 0;
    uint64_t _lastMeetingNumber = 0;
    uint32_t _lastReaction = 0;
    uint32_t _chatMessages = 0;
};
} // namespace Zoom
