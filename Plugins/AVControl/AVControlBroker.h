#pragma once
#include "AVControlProtocol.h"
#include "ProfileTransaction.h"
#include <string>
#include <wil/resource.h>

namespace AVControl
{
// Worker-thread affine. One persistent helper per module coordinator, sleeping on its request event while idle.
// Every call has a remaining deadline and cancellation handle. A stuck driver cannot hold the RedXe worker open.
class Broker final : public BackendTransport
{
  public:
    Broker() = default;
    ~Broker();
    Broker(const Broker&) = delete;
    Broker& operator=(const Broker&) = delete;
    HRESULT Start(std::wstring_view executable, bool synthetic = false) noexcept;
    // Worker-thread only, between handshakes; copied once when the loaded-profile union changes or after restart.
    HRESULT SetPreferences(const InventoryPreferences& preferences) noexcept;
    HRESULT Execute(const BrokerCommand& command, BrokerReply& reply, HANDLE cancelEvent,
                    uint32_t timeoutMilliseconds) noexcept override;
    void Stop(uint32_t drainMilliseconds = 250) noexcept;
    [[nodiscard]] bool Running() const noexcept;
    // Stable from the first successful event creation through object destruction, including connection restarts.
    // The coordinator must drain its thread-pool wait before destroying this Broker.
    [[nodiscard]] HANDLE ChangeEvent() const noexcept
    {
        return _changed.get();
    }
    [[nodiscard]] DWORD ProcessId() const noexcept
    {
        return _process ? GetProcessId(_process.get()) : 0;
    }

  private:
    wil::unique_handle _job, _process, _mapping;
    wil::unique_event_nothrow _request, _reply, _changed;
    wil::unique_mapview_ptr<BrokerShared> _shared;
    uint32_t _sequence = 0;
};
} // namespace AVControl
