#pragma once
#include "CameraBridge.h"
#include <atomic>

namespace AVControl::Camera
{
class BridgeObserver
{
  public:
    virtual ~BridgeObserver() = default;
    // One MTA helper thread serializes admission, acquisition work, and final capture shutdown. Notifications
    // are coalesced. The observer reads Consumers() and its own latest intent; it must not hold a UI lock.
    virtual void OnBridgeWork() noexcept = 0;
    virtual void OnBridgeStopping() noexcept = 0;
};
// Lives only in the owned helper (or an explicit synthetic fixture). Driver stalls in observer callbacks are
// contained by the parent's process deadline. Pipe/shared-memory work itself has bounded waits and storage.
class BridgeServer final
{
  public:
    BridgeServer() = default;
    ~BridgeServer()
    {
        Stop();
    }
    HRESULT Start(BridgeIdentity identity, BridgeObserver* observer) noexcept;
    void Stop() noexcept;
    void Wake() noexcept
    {
        if (_wake)
            SetEvent(_wake.get());
    }
    [[nodiscard]] uint32_t Consumers() const noexcept
    {
        return _consumers.load();
    }
    // A connected/started stream alone is not a request for physical capture. Each sample request renews a
    // short lease. Capture callbacks check this lease and quiesce when consumers stop requesting frames.
    [[nodiscard]] uint32_t DemandingConsumers() noexcept;
    // Borrowed until Stop. CaptureReader duplicates only EVENT_MODIFY_STATE for its callback mailbox.
    [[nodiscard]] HANDLE WakeEvent() const noexcept
    {
        return _wake.get();
    }
    // Changing the generation invalidates callbacks from the old physical source, even when staying enabled.
    HRESULT SetGate(uint64_t generation, bool enabled) noexcept;
    HRESULT Publish(uint64_t generation, std::span<const BYTE> frame, LONGLONG timestamp) noexcept;

  private:
    struct Client final
    {
        FrameChannel channel;
        wil::unique_handle pipe;
        wil::unique_event event;
        OVERLAPPED read{};
        BYTE unexpected = 0;
        bool pending = false;
        bool closeRequested = false;
        uint64_t channelRevision = 1;
        ULONGLONG demandedAt = 0;
        void Close() noexcept;
        void ReadDemand() noexcept;
    };
    BridgeIdentity _identity;
    BridgeObserver* _observer = nullptr; // Borrowed until Stop joins the helper lane.
    wil::unique_handle _thread, _listener;
    wil::unique_event _stop, _wake, _accepted;
    wil::unique_hlocal _security;
    OVERLAPPED _connect{};
    bool _connecting = false;
    SRWLOCK _lock = SRWLOCK_INIT;
    std::array<Client, MaximumConsumers> _clients;
    std::atomic<uint32_t> _consumers{0};
    uint64_t _generation = 1;
    bool _enabled = false;
    static DWORD WINAPI Entry(void* value) noexcept;
    void Run() noexcept;
    HRESULT Listen(bool first) noexcept;
    void Accept() noexcept;
    void Drop(Client& client) noexcept;
};
} // namespace AVControl::Camera
