#include "ZoomSynthetic.h"

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace Zoom
{
SyntheticSession::~SyntheticSession()
{
    Uninitialize();
}

void SyntheticSession::Notify() noexcept
{
    ISessionListener* listener = nullptr;
    {
        const auto guard = wil::AcquireSRWLockShared(&_lock);
        listener = _listener;
    }
    if (listener)
    {
        listener->OnSessionChanged();
    }
}

HRESULT SyntheticSession::Initialize(ISessionListener* listener) noexcept
{
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        if (_initialized)
        {
            return E_NOT_VALID_STATE;
        }
        _initialized = true;
        _listener = listener;
        _snapshot = SessionSnapshot{};
        ++_initializeCount;
    }
    Notify();
    return S_OK;
}

void SyntheticSession::Uninitialize() noexcept
{
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    if (!_initialized)
    {
        return;
    }
    _initialized = false;
    _listener = nullptr;
    _snapshot = SessionSnapshot{};
    ++_uninitializeCount;
}

HRESULT SyntheticSession::Authenticate(std::string_view, std::string_view accessToken) noexcept
{
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        if (!_initialized)
        {
            return E_NOT_VALID_STATE;
        }
        ++_snapshot.submissions;
        ++_snapshot.completions;
        if (accessToken.empty() || accessToken == "expired")
        {
            _snapshot.auth = AuthState::Failed;
            _snapshot.ipc = IpcState::Disconnected;
            _snapshot.lastError = E_ACCESSDENIED;
        }
        else
        {
            _snapshot.auth = AuthState::Authenticated;
            _snapshot.ipc = IpcState::Connected;
            _snapshot.lastError = S_OK;
        }
    }
    Notify();
    return S_OK;
}

HRESULT SyntheticSession::Submit(bool needsMeeting) noexcept
{
    if (!_initialized)
    {
        return E_NOT_VALID_STATE;
    }
    if (_snapshot.auth != AuthState::Authenticated || _snapshot.ipc != IpcState::Connected)
    {
        return E_NOT_VALID_STATE;
    }
    if (needsMeeting && _snapshot.meeting != MeetingState::InMeeting)
    {
        return E_NOT_VALID_STATE;
    }
    ++_snapshot.submissions;
    if (_failNext != S_OK)
    {
        const HRESULT error = _failNext;
        _failNext = S_OK;
        _snapshot.lastError = error;
        ++_snapshot.completions;
        return error;
    }
    ++_snapshot.completions;
    _snapshot.lastError = S_OK;
    return S_OK;
}

HRESULT SyntheticSession::Join(uint64_t meetingNumber, std::string_view, std::string_view) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(false);
        if (SUCCEEDED(result))
        {
            _lastMeetingNumber = meetingNumber;
            _snapshot.meeting = MeetingState::InMeeting;
            _snapshot.audioJoined = true;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::Start(uint64_t meetingNumber) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(false);
        if (SUCCEEDED(result))
        {
            _lastMeetingNumber = meetingNumber;
            _snapshot.meeting = MeetingState::InMeeting;
            _snapshot.audioJoined = true;
            _snapshot.isHost = true;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::Leave(bool endForAll) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(true);
        if (SUCCEEDED(result) && endForAll && !_snapshot.isHost)
        {
            result = E_ACCESSDENIED;
            _snapshot.lastError = result;
        }
        if (SUCCEEDED(result))
        {
            _snapshot.meeting = MeetingState::Idle;
            _snapshot.audioJoined = false;
            _snapshot.audioMuted = false;
            _snapshot.videoOn = false;
            _snapshot.sharing = false;
            _snapshot.handRaised = false;
            _snapshot.recording = false;
            _snapshot.isHost = false;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::SetAudioJoined(bool joined) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(true);
        if (SUCCEEDED(result))
        {
            _snapshot.audioJoined = joined;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::SetMuted(bool muted) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(true);
        if (SUCCEEDED(result))
        {
            _snapshot.audioMuted = muted;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::SetVideo(bool on) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(true);
        if (SUCCEEDED(result))
        {
            _snapshot.videoOn = on;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::Share(ShareCommand command, uint32_t, HWND) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(true);
        if (SUCCEEDED(result))
        {
            _snapshot.sharing = command == ShareCommand::Monitor || command == ShareCommand::Application ||
                                command == ShareCommand::Resume;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::Record(RecordCommand command) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(true);
        if (SUCCEEDED(result))
        {
            _snapshot.recording = command == RecordCommand::LocalStart || command == RecordCommand::CloudStart ||
                                  command == RecordCommand::Resume;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::SetHandRaised(bool raised) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(true);
        if (SUCCEEDED(result))
        {
            _snapshot.handRaised = raised;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::React(uint32_t reaction) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(true);
        if (SUCCEEDED(result))
        {
            _lastReaction = reaction;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::SendChat(std::string_view textUtf8) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = textUtf8.empty() ? E_INVALIDARG : Submit(true);
        if (SUCCEEDED(result))
        {
            ++_chatMessages;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::SetCaptions(bool) noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(true);
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::MuteAll() noexcept
{
    HRESULT result = S_OK;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        result = Submit(true);
        if (SUCCEEDED(result) && !_snapshot.isHost)
        {
            result = E_ACCESSDENIED;
            _snapshot.lastError = result;
        }
    }
    Notify();
    return result;
}

HRESULT SyntheticSession::AdmitAll() noexcept
{
    return MuteAll();
}

SessionSnapshot SyntheticSession::Snapshot() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return _snapshot;
}

void SyntheticSession::SetHost(bool host) noexcept
{
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _snapshot.isHost = host;
    }
    Notify();
}

void SyntheticSession::DropConnection() noexcept
{
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _snapshot.ipc = IpcState::Disconnected;
        _snapshot.meeting = MeetingState::Idle;
    }
    Notify();
}

void SyntheticSession::FailNext(HRESULT error) noexcept
{
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    _failNext = error;
}

uint32_t SyntheticSession::InitializeCount() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return _initializeCount;
}

uint32_t SyntheticSession::UninitializeCount() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return _uninitializeCount;
}

uint64_t SyntheticSession::LastMeetingNumber() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return _lastMeetingNumber;
}

uint32_t SyntheticSession::LastReaction() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return _lastReaction;
}

uint32_t SyntheticSession::ChatMessages() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return _chatMessages;
}
} // namespace Zoom
