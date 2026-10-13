#pragma once
#include "PlugInterfaces/Widget.h"

#include <array>
#include <atomic>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

// UI-owned storage for one lazy STA construction lane. Run borrows its slot; every final work release is on the UI
// thread. Stop is bounded even when an indivisible Shell call ignores cancellation. Keep this object and its plugin
// modules alive until Running is false; Join is only for destruction of a private owner that cannot retain storage.
class ConstructionWorkQueue final
{
  public:
    using Notify = void (*)(void*) noexcept;
    static constexpr size_t Capacity = 16;
    static constexpr uint32_t WorkMilliseconds = 3000;
    static constexpr uint32_t StopMilliseconds = 1000;
    enum class StopResult : uint8_t
    {
        Stopped,
        Exiting,
        WorkInProgress,
    };
    ConstructionWorkQueue() = default;
    ~ConstructionWorkQueue();
    ConstructionWorkQueue(const ConstructionWorkQueue&) = delete;
    ConstructionWorkQueue& operator=(const ConstructionWorkQueue&) = delete;
    [[nodiscard]] HRESULT Start(Notify notify, void* context) noexcept;
    [[nodiscard]] HRESULT Enqueue(IRedXeConstructionWork* work) noexcept;
    void DrainCompletions() noexcept;
    [[nodiscard]] StopResult Stop(uint32_t timeoutMilliseconds = StopMilliseconds) noexcept;
    void Join() noexcept;
    [[nodiscard]] bool Running() const noexcept
    {
        return _worker.joinable();
    }

  private:
#if defined(REDXE_HOST_PLUGIN_TESTS)
    friend struct ConstructionWorkQueueTestAccess;
#endif
    enum class State : uint8_t
    {
        Empty,
        Queued,
        Running,
        Complete,
    };
    struct Slot final
    {
        wil::com_ptr_nothrow<IRedXeConstructionWork> work;
        State state = State::Empty;
        HRESULT result = S_OK;
        uint64_t queuedAt = 0;
    };
    void Worker() noexcept;
    void Reset() noexcept;
    SRWLOCK _lock = SRWLOCK_INIT;
    std::array<Slot, Capacity> _slots{};
    wil::unique_event_nothrow _stop;
    wil::unique_event_nothrow _ready;
    std::thread _worker;
    Notify _notify = nullptr;
    void* _context = nullptr;
    std::atomic<bool> _completed{false};
    size_t _lastSelected = Capacity - 1;
    bool _waitedOut = false;
};
