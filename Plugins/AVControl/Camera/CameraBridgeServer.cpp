#include "CameraBridgeServer.h"
#include <algorithm>
#include <wil/result.h>

namespace AVControl::Camera
{
void BridgeServer::Client::Close() noexcept
{
    if (pending && pipe)
    {
        (void)CancelIoEx(pipe.get(), &read);
        DWORD ignored = 0; (void)GetOverlappedResult(pipe.get(), &read, &ignored, TRUE);
    }
    pending = false; closeRequested = false; pipe.reset(); channel.Close(); event.reset(); read = {}; demandedAt = 0;
}
void BridgeServer::Client::ReadDemand() noexcept
{
    ResetEvent(event.get()); read = {}; read.hEvent = event.get(); unexpected = 0;
    const BOOL immediate = ReadFile(pipe.get(), &unexpected, 1, nullptr, &read);
    pending = !immediate && GetLastError() == ERROR_IO_PENDING;
    if (!pending) SetEvent(event.get());
}
uint32_t BridgeServer::DemandingConsumers() noexcept
{
    const auto lock = wil::AcquireSRWLockShared(&_lock);
    const ULONGLONG now = GetTickCount64(); uint32_t result = 0;
    for (const auto& client : _clients) if (client.pipe && client.demandedAt && now - client.demandedAt < 250) ++result;
    return result;
}
HRESULT BridgeServer::Start(BridgeIdentity identity, BridgeObserver* observer) noexcept
{
    Stop(); _identity = std::move(identity); _observer = observer; _enabled = false; _generation = 1;
    RETURN_IF_FAILED(CreateBridgeSecurity(_identity, _security));
    _stop.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    _wake.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    _accepted.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!_stop || !_wake || !_accepted) { const HRESULT result = HRESULT_FROM_WIN32(GetLastError()); Stop(); return result; }
    const HRESULT listening = Listen(true);
    if (FAILED(listening)) { Stop(); return listening; }
    _thread.reset(CreateThread(nullptr, 0, Entry, this, 0, nullptr));
    if (!_thread) { const HRESULT result = HRESULT_FROM_WIN32(GetLastError()); Stop(); return result; }
    return S_OK;
}
void BridgeServer::Stop() noexcept
{
    if (_stop) SetEvent(_stop.get());
    if (_thread) { (void)WaitForSingleObject(_thread.get(), INFINITE); _thread.reset(); }
    if (_connecting && _listener)
    {
        (void)CancelIoEx(_listener.get(), &_connect);
        DWORD ignored = 0; (void)GetOverlappedResult(_listener.get(), &_connect, &ignored, TRUE);
    }
    _connecting = false; _listener.reset();
    for (auto& client : _clients) client.Close();
    _consumers = 0; _observer = nullptr; _stop.reset(); _wake.reset(); _accepted.reset(); _security.reset();
}
HRESULT BridgeServer::Listen(bool first) noexcept
{
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), _security.get(), FALSE};
    _listener.reset(CreateNamedPipeW(_identity.pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
        (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0), PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        static_cast<DWORD>(MaximumConsumers + 1), sizeof(BridgeHello), sizeof(BridgeHello), 0, &attributes));
    if (!_listener) return HRESULT_FROM_WIN32(GetLastError());
    ResetEvent(_accepted.get()); _connect = {}; _connect.hEvent = _accepted.get();
    const BOOL connected = ConnectNamedPipe(_listener.get(), &_connect);
    const DWORD error = connected ? ERROR_SUCCESS : GetLastError();
    if (error != ERROR_SUCCESS && error != ERROR_IO_PENDING && error != ERROR_PIPE_CONNECTED) return HRESULT_FROM_WIN32(error);
    _connecting = error == ERROR_IO_PENDING;
    if (!_connecting) SetEvent(_accepted.get());
    return S_OK;
}
DWORD WINAPI BridgeServer::Entry(void* value) noexcept
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 1;
    static_cast<BridgeServer*>(value)->Run(); CoUninitialize(); return 0;
}
void BridgeServer::Run() noexcept
{
    for (;;)
    {
        std::array<HANDLE, MaximumConsumers + 3> handles{_stop.get(), _wake.get(), _accepted.get()};
        std::array<Client*, MaximumConsumers> clients{};
        DWORD count = 3;
        DWORD timeout = INFINITE;
        {
            const auto lock = wil::AcquireSRWLockShared(&_lock);
            const auto now = GetTickCount64();
            for (auto& client : _clients) if (client.pipe)
            {
                clients[count - 3] = &client; handles[count++] = client.event.get();
                if (client.demandedAt)
                {
                    const auto due = client.demandedAt + 250;
                    timeout = (std::min)(timeout, static_cast<DWORD>(due > now ? due - now : 0));
                }
            }
        }
        const DWORD wait = WaitForMultipleObjects(count, handles.data(), FALSE, timeout);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
        if (wait == WAIT_TIMEOUT)
        {
            const auto lock = wil::AcquireSRWLockExclusive(&_lock);
            const auto now = GetTickCount64();
            for (auto& client : _clients) if (client.demandedAt && now - client.demandedAt >= 250) client.demandedAt = 0;
            // This one-shot lease expiry also releases a reader which has stopped producing callbacks. Once
            // the lease is gone, the next wait is infinite unless a new consumer requests another sample.
        }
        if (wait == WAIT_OBJECT_0 + 2) Accept();
        else if (wait >= WAIT_OBJECT_0 + 3 && wait < WAIT_OBJECT_0 + count)
        {
            const auto lock = wil::AcquireSRWLockExclusive(&_lock);
            auto& client = *clients[wait - WAIT_OBJECT_0 - 3];
            DWORD received = 0;
            if (!client.closeRequested && GetOverlappedResult(client.pipe.get(), &client.read, &received, FALSE) && received == 1 && client.unexpected == 1)
            {
                client.pending = false; client.demandedAt = GetTickCount64(); client.ReadDemand();
            }
            else Drop(client); // End-of-file or an unknown control byte terminates this lease.
        }
        if (_observer) _observer->OnBridgeWork();
    }
    if (_observer) _observer->OnBridgeStopping();
}
void BridgeServer::Drop(Client& client) noexcept
{
    if (!client.pipe) return;
    client.Close(); --_consumers;
}
void BridgeServer::Accept() noexcept
{
    if (_connecting)
    {
        DWORD ignored = 0;
        if (!GetOverlappedResult(_listener.get(), &_connect, &ignored, FALSE))
        { _connecting = false; (void)Listen(false); return; }
    }
    _connecting = false;
    auto pipe = std::move(_listener);
    // Preserve ownership of the pipe namespace while creating its next listener. At capacity this connected
    // instance is closed first; four established instances still retain the namespace.
    if (_consumers >= MaximumConsumers) pipe.reset();
    if (FAILED(Listen(false))) { SetEvent(_stop.get()); return; }
    if (!pipe) return;
    BridgeHello hello;
    BridgeReply reply;
    const ULONGLONG deadline = GetTickCount64() + BridgeDeadlineMs;
    reply.status = BridgeTransfer(pipe.get(), false, &hello, sizeof(hello), deadline);
    if (SUCCEEDED(reply.status) && (hello.magic != BridgeMagic || hello.version != BridgeVersion ||
        hello.size != sizeof(hello) || hello.reserved)) reply.status = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (SUCCEEDED(reply.status)) reply.status = AuthenticateBridgeConsumer(pipe.get(), _identity);
    const auto lock = wil::AcquireSRWLockExclusive(&_lock);
    Client* candidate = nullptr;
    for (auto& client : _clients) if (!client.pipe) { candidate = &client; break; }
    if (!candidate) reply.status = HRESULT_FROM_WIN32(ERROR_TOO_MANY_SESS);
    if (candidate && SUCCEEDED(reply.status))
    {
        reply.status = candidate->channel.Open(hello.channel, _identity.synthetic);
        ChannelState state;
        if (SUCCEEDED(reply.status)) reply.status = candidate->channel.State(state);
        if (SUCCEEDED(reply.status))
        {
            if (state.revision == UINT64_MAX) reply.status = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
            else { candidate->channelRevision = state.revision + 1; reply.status = candidate->channel.SetGate({candidate->channelRevision, _enabled}); }
        }
    }
    const HRESULT sent = BridgeTransfer(pipe.get(), true, &reply, sizeof(reply), deadline);
    if (!candidate) return;
    if (FAILED(sent) || FAILED(reply.status)) { candidate->Close(); return; }
    candidate->event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!candidate->event) { candidate->Close(); return; }
    candidate->pipe = std::move(pipe); candidate->ReadDemand();
    ++_consumers;
}
HRESULT BridgeServer::SetGate(uint64_t generation, bool enabled) noexcept
{
    const auto lock = wil::AcquireSRWLockExclusive(&_lock);
    if (!generation || generation < _generation || (generation == _generation && enabled != _enabled))
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    if (generation == _generation) return S_OK;
    _generation = generation; _enabled = enabled;
    HRESULT result = S_OK;
    for (auto& client : _clients) if (client.pipe)
    {
        const HRESULT changed = client.channelRevision == UINT64_MAX ? HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW) :
            client.channel.SetGate({++client.channelRevision, enabled});
        // Keep event handles alive until the event loop observes them; it may currently be waiting on them.
        // A failed gate is reported to the caller, never acknowledged as a confirmed Off.
        if (FAILED(changed))
        {
            result = changed; client.closeRequested = true;
            (void)CancelIoEx(client.pipe.get(), &client.read); SetEvent(client.event.get());
        }
    }
    Wake(); return result;
}
HRESULT BridgeServer::Publish(uint64_t generation, std::span<const BYTE> frame, LONGLONG timestamp) noexcept
{
    const auto lock = wil::AcquireSRWLockExclusive(&_lock);
    if (!_enabled || generation != _generation) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    HRESULT result = S_OK;
    for (auto& client : _clients) if (client.pipe)
    {
        const HRESULT written = client.channel.Write(client.channelRevision, frame, timestamp);
        if (FAILED(written))
        {
            result = written; client.closeRequested = true;
            (void)CancelIoEx(client.pipe.get(), &client.read); SetEvent(client.event.get());
        }
    }
    return result;
}
} // namespace AVControl::Camera
