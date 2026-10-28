#pragma once
#include "AVControlProtocol.h"
#include <atomic>

namespace AVControl
{
class BackendTransport
{
  public:
    virtual ~BackendTransport() = default;
    virtual HRESULT Execute(const BrokerCommand& command, BrokerReply& reply, HANDLE cancelEvent,
                            uint32_t timeoutMilliseconds) noexcept = 0;
};
// Published on the UI thread before queueing a safety command. The transaction worker observes this without locks.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class SafetyState final
{
  public:
    void Publish(DeviceKind device, bool off) noexcept;
    [[nodiscard]] std::array<uint64_t, 3> Snapshot() const noexcept;

  private:
    std::array<std::atomic<uint64_t>, 3> _values{};
};
#pragma warning(pop)

struct ApplyOutcome final
{
    HRESULT result = E_PENDING;
    bool applied = false;
    bool rollbackComplete = true;
    bool stateKnown = true;
    bool superseded = false;
    uint32_t mutations = 0;
};
// Synchronous only on the host's control worker. Reserves one third of the total budget for compensating rollback.
// The supplied reply is caller-owned reusable storage and always holds the most recently observed backend state.
ApplyOutcome ApplyProfile(const Profile& profile, BackendTransport& backend, BrokerReply& reply,
                          const SafetyState& safety, HANDLE cancelEvent, uint32_t timeoutMilliseconds) noexcept;
} // namespace AVControl
