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

// One process-scoped control lane. Start, Enqueue, DrainCompletions and Stop are UI-thread operations.
class ControlWorkQueue final
{
  public:
    using Notify = void (*)(void*) noexcept;
    static constexpr size_t Capacity = 16;
    ControlWorkQueue() = default;
    ~ControlWorkQueue();
    ControlWorkQueue(const ControlWorkQueue&) = delete;
    ControlWorkQueue& operator=(const ControlWorkQueue&) = delete;
    [[nodiscard]] HRESULT Start(Notify notify, void* context) noexcept;
    [[nodiscard]] HRESULT Enqueue(IRedXeControlWork* work) noexcept;
    void DrainCompletions() noexcept;
    void Stop() noexcept;
    [[nodiscard]] bool Running() const noexcept { return _worker.joinable(); }
  private:
    enum class State : uint32_t { Empty, Queued, Running, Complete };
    struct Slot final
    {
        wil::com_ptr_nothrow<IRedXeControlWork> work;
        State state = State::Empty;
        HRESULT result = S_OK;
        bool rerun = false;
        uint64_t queuedAt = 0;
    };
    void Worker() noexcept;
    SRWLOCK _lock = SRWLOCK_INIT;
    std::array<Slot, Capacity> _slots{};
    wil::unique_event_nothrow _stop;
    wil::unique_event_nothrow _ready;
    std::thread _worker;
    Notify _notify = nullptr;
    void* _context = nullptr;
    std::atomic<bool> _completed{false};
    size_t _lastSelected = Capacity - 1;
};
