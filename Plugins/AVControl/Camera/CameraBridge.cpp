#include "CameraBridge.h"
#include <aclapi.h>
#include <algorithm>
#include <mferror.h>
#include <sddl.h>
#include <wil/result.h>

#pragma comment(lib, "advapi32.lib")

namespace AVControl::Camera
{
namespace
{
HRESULT TokenMatches(HANDLE token, PSID owner, bool serviceOnly) noexcept
{
    alignas(TOKEN_USER) std::array<BYTE, sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE> data{};
    DWORD needed = 0;
    RETURN_IF_WIN32_BOOL_FALSE(
        GetTokenInformation(token, TokenUser, data.data(), static_cast<DWORD>(data.size()), &needed));
    const auto user = reinterpret_cast<TOKEN_USER*>(data.data());
    if (!serviceOnly && EqualSid(user->User.Sid, owner))
        return S_OK;
    if (!IsWellKnownSid(user->User.Sid, WinLocalServiceSid))
        return E_ACCESSDENIED;
    DWORD session = UINT_MAX;
    RETURN_IF_WIN32_BOOL_FALSE(GetTokenInformation(token, TokenSessionId, &session, sizeof(session), &needed));
    return session == 0 ? S_OK : E_ACCESSDENIED;
}
} // namespace
HRESULT MakeBridgeIdentity(PCWSTR ownerSid, BridgeIdentity& result, const GUID* isolatedTest) noexcept
{
    if (!ownerSid || wcsnlen_s(ownerSid, 192) >= 192)
        return E_INVALIDARG;
    wil::unique_hlocal sid;
    RETURN_IF_WIN32_BOOL_FALSE(ConvertStringSidToSidW(ownerSid, sid.put()));
    wil::unique_hlocal_string canonical;
    RETURN_IF_WIN32_BOOL_FALSE(ConvertSidToStringSidW(sid.get(), canonical.put()));
    try
    {
        BridgeIdentity next;
        next.ownerSid = canonical.get();
        next.pipeName = L"\\\\.\\pipe\\RedXe.Camera.v1." + next.ownerSid;
        if (isolatedTest)
        {
            wchar_t guid[40]{};
            if (!StringFromGUID2(*isolatedTest, guid, static_cast<int>(std::size(guid))))
                return E_INVALIDARG;
            next.pipeName += L".Test.";
            next.pipeName += guid;
            next.synthetic = true;
        }
        if (next.pipeName.size() >= 256)
            return E_INVALIDARG;
        result = std::move(next);
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}
HRESULT CreateBridgeSecurity(const BridgeIdentity& identity, wil::unique_hlocal& security) noexcept
{
    try
    {
        // Explicit owner also works when the owner launches an elevated helper. Client validation reads this
        // kernel-controlled SID; an unrelated ordinary user cannot create an object owned by this principal.
        const std::wstring sddl =
            L"O:" + identity.ownerSid + L"D:P(A;;GA;;;SY)(A;;GA;;;LS)(A;;GA;;;" + identity.ownerSid + L")";
        RETURN_IF_WIN32_BOOL_FALSE(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                                                        security.put(), nullptr));
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}
HRESULT AuthenticateBridgeServer(HANDLE pipe, const BridgeIdentity& identity) noexcept
{
    wil::unique_hlocal expected;
    RETURN_IF_WIN32_BOOL_FALSE(ConvertStringSidToSidW(identity.ownerSid.c_str(), expected.put()));
    PSID owner = nullptr;
    wil::unique_hlocal security;
    const DWORD error = GetSecurityInfo(pipe, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr,
                                        nullptr, reinterpret_cast<PSECURITY_DESCRIPTOR*>(security.put()));
    if (error)
        return HRESULT_FROM_WIN32(error);
    return owner && EqualSid(owner, expected.get()) ? S_OK : E_ACCESSDENIED;
}
HRESULT AuthenticateBridgeConsumer(HANDLE pipe, const BridgeIdentity& identity) noexcept
{
    // Identification is sufficient and does not grant the helper authority to act as Local Service.
    RETURN_IF_WIN32_BOOL_FALSE(ImpersonateNamedPipeClient(pipe));
    const auto revert = wil::scope_exit(
        []
        {
            if (!RevertToSelf())
                std::terminate();
        });
    wil::unique_handle token;
    RETURN_IF_WIN32_BOOL_FALSE(OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, token.put()));
    wil::unique_hlocal owner;
    RETURN_IF_WIN32_BOOL_FALSE(ConvertStringSidToSidW(identity.ownerSid.c_str(), owner.put()));
    return TokenMatches(token.get(), owner.get(), !identity.synthetic);
}
HRESULT BridgeTransfer(HANDLE pipe, bool write, void* bytes, DWORD size, ULONGLONG deadline) noexcept
{
    if (!pipe || pipe == INVALID_HANDLE_VALUE || !bytes || !size)
        return E_INVALIDARG;
    wil::unique_event event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event)
        return HRESULT_FROM_WIN32(GetLastError());
    DWORD total = 0;
    while (total < size)
    {
        if (GetTickCount64() >= deadline)
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        ResetEvent(event.get());
        OVERLAPPED operation{};
        operation.hEvent = event.get();
        const BOOL immediate =
            write ? WriteFile(pipe, static_cast<BYTE*>(bytes) + total, size - total, nullptr, &operation)
                  : ReadFile(pipe, static_cast<BYTE*>(bytes) + total, size - total, nullptr, &operation);
        if (!immediate && GetLastError() != ERROR_IO_PENDING)
            return HRESULT_FROM_WIN32(GetLastError());
        if (!immediate)
        {
            const ULONGLONG now = GetTickCount64();
            const DWORD remaining =
                static_cast<DWORD>(std::min<ULONGLONG>(deadline > now ? deadline - now : 0, BridgeDeadlineMs));
            if (WaitForSingleObject(event.get(), remaining) != WAIT_OBJECT_0)
            {
                (void)CancelIoEx(pipe, &operation);
                DWORD ignored = 0;
                (void)GetOverlappedResult(pipe, &operation, &ignored, TRUE);
                return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            }
        }
        DWORD transferred = 0;
        RETURN_IF_WIN32_BOOL_FALSE(GetOverlappedResult(pipe, &operation, &transferred, FALSE));
        if (!transferred || transferred > size - total)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        total += transferred;
    }
    return S_OK;
}
HRESULT BridgeFrameProvider::Start() noexcept
{
    Stop();
    RETURN_IF_FAILED(_channel.Create(_identity.ownerSid.c_str(), _identity.synthetic));
    _started = true;
    _retryAt = 0;
    // Starting a consumer while RedXe is absent is valid: the stable route produces neutral frames.
    (void)Connect();
    return S_OK;
}
void BridgeFrameProvider::Disconnect() noexcept
{
    if (_demandPending && _pipe)
    {
        (void)CancelIoEx(_pipe.get(), &_demand);
        DWORD ignored = 0;
        (void)GetOverlappedResult(_pipe.get(), &_demand, &ignored, TRUE);
    }
    _demandPending = false;
    _demandEvent.reset();
    _demand = {};
    _pipe.reset();
    ChannelState state;
    if (SUCCEEDED(_channel.State(state)) && state.revision != UINT64_MAX)
        (void)_channel.SetGate({state.revision + 1, false});
    _retryAt = GetTickCount64() + 1000;
}
void BridgeFrameProvider::Stop() noexcept
{
    _channel.EndRead();
    Disconnect();
    _channel.Close();
    _started = false;
    _retryAt = 0;
}
HRESULT BridgeFrameProvider::Connect() noexcept
{
    _retryAt = GetTickCount64() + 1000;
    _pipe.reset(CreateFileW(_identity.pipeName.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                            nullptr));
    if (!_pipe)
        return HRESULT_FROM_WIN32(GetLastError());
    auto fail = wil::scope_exit([&] { Disconnect(); });
    RETURN_IF_FAILED(AuthenticateBridgeServer(_pipe.get(), _identity));
    BridgeHello hello;
    hello.channel = _channel.Names();
    BridgeReply reply;
    const ULONGLONG deadline = GetTickCount64() + BridgeDeadlineMs;
    RETURN_IF_FAILED(BridgeTransfer(_pipe.get(), true, &hello, sizeof(hello), deadline));
    RETURN_IF_FAILED(BridgeTransfer(_pipe.get(), false, &reply, sizeof(reply), deadline));
    if (reply.magic != BridgeMagic || reply.version != BridgeVersion || reply.size != sizeof(reply))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    RETURN_IF_FAILED(reply.status);
    _demandEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!_demandEvent)
        return HRESULT_FROM_WIN32(GetLastError());
    _demand = {};
    _demand.hEvent = _demandEvent.get();
    fail.release();
    return S_OK;
}
HRESULT BridgeFrameProvider::Demand() noexcept
{
    DWORD written = 0;
    if (_demandPending)
    {
        if (!GetOverlappedResult(_pipe.get(), &_demand, &written, FALSE))
            return GetLastError() == ERROR_IO_INCOMPLETE ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        _demandPending = false;
        if (written != 1)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    ResetEvent(_demandEvent.get());
    _demand = {};
    _demand.hEvent = _demandEvent.get();
    if (WriteFile(_pipe.get(), &_demandByte, 1, nullptr, &_demand))
        return S_OK;
    if (GetLastError() != ERROR_IO_PENDING)
        return HRESULT_FROM_WIN32(GetLastError());
    _demandPending = true;
    return S_OK;
}
HRESULT BridgeFrameProvider::Fill(BYTE* bytes, DWORD capacity, LONG pitch, LONGLONG timestamp) noexcept
{
    if (!_started)
        return MF_E_SHUTDOWN;
    if (_pipe)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(_pipe.get(), nullptr, 0, nullptr, &available, nullptr) || available)
            Disconnect();
    }
    if (!_pipe && GetTickCount64() >= _retryAt)
        (void)Connect();
    if (_pipe && FAILED(Demand()))
        Disconnect();
    // A failed transport cannot replay the previous producer image, even if its shared gate was contended.
    // The media source replaces a failed fill with a complete neutral frame.
    return _pipe ? _channel.Read(bytes, capacity, pitch, timestamp) : HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE);
}
} // namespace AVControl::Camera
