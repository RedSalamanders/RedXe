#include "ControlWorkQueue.h"
#include <system_error>

ControlWorkQueue::~ControlWorkQueue() { Stop(); }

HRESULT ControlWorkQueue::Start(Notify notify, void* context) noexcept
{
    if (Running()) return S_FALSE;
    RETURN_IF_FAILED(_stop.create(wil::EventOptions::ManualReset));
    RETURN_IF_FAILED(_ready.create());
    _notify = notify;
    _context = context;
    try { _worker = std::thread([this]() noexcept { Worker(); }); }
    catch (const std::system_error&) { _stop.reset(); _ready.reset(); return E_FAIL; }
    catch (const std::bad_alloc&) { _stop.reset(); _ready.reset(); return E_OUTOFMEMORY; }
    return S_OK;
}

HRESULT ControlWorkQueue::Enqueue(IRedXeControlWork* work) noexcept
{
    if (!work) return E_POINTER;
    if (!Running()) return E_UNEXPECTED;
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    for (Slot& slot : _slots)
    {
        if (slot.work.get() != work) continue;
        if (slot.state != State::Queued) slot.rerun = true;
        return S_FALSE;
    }
    for (Slot& slot : _slots)
    {
        if (slot.state != State::Empty) continue;
        slot.work = work;
        slot.state = State::Queued;
        slot.queuedAt = GetTickCount64();
        _ready.SetEvent();
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_BUSY);
}

void ControlWorkQueue::Worker() noexcept
{
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto uninitialize = wil::scope_exit([&]() noexcept { if (SUCCEEDED(apartment)) CoUninitialize(); });
    const HANDLE events[]{_stop.get(), _ready.get()};
    while (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
    {
        for (;;)
        {
            if (_stop.is_signaled()) return;
            size_t selected = Capacity;
            uint64_t queuedAt = 0;
            wil::com_ptr_nothrow<IRedXeControlWork> work;
            {
                const auto guard = wil::AcquireSRWLockExclusive(&_lock);
                for (size_t offset = 1; offset <= _slots.size(); ++offset)
                {
                    const size_t i = (_lastSelected + offset) % Capacity;
                    if (_slots[i].state != State::Queued) continue;
                    selected = i;
                    _lastSelected = i;
                    queuedAt = _slots[i].queuedAt;
                    _slots[i].state = State::Running;
                    work = _slots[i].work;
                    break;
                }
            }
            if (!work) break;
            const uint64_t age = GetTickCount64() - queuedAt;
            const HRESULT result = FAILED(apartment) ? apartment : age >= 3000 ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) :
                work->Run(_stop.get(), static_cast<uint32_t>(3000 - age));
            {
                const auto guard = wil::AcquireSRWLockExclusive(&_lock);
                _slots[selected].result = result;
                _slots[selected].state = State::Complete;
                _completed.store(true, std::memory_order_release);
            }
            if (_notify) _notify(_context);
        }
    }
}

void ControlWorkQueue::DrainCompletions() noexcept
{
    if (!_completed.exchange(false, std::memory_order_acq_rel)) return;
    std::array<Slot, Capacity> ready{};
    size_t count = 0;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        for (Slot& slot : _slots)
        {
            if (slot.state != State::Complete) continue;
            ready[count++] = std::move(slot);
            slot = Slot{};
        }
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (!Running()) break;
        ready[i].work->Complete(ready[i].result);
        if (ready[i].rerun && Running()) (void)Enqueue(ready[i].work.get());
    }
}

void ControlWorkQueue::Stop() noexcept
{
    if (_worker.joinable())
    {
        _stop.SetEvent();
        _worker.join();
    }
    // All COM references are released on the owning UI thread, after the last worker borrow is gone.
    _slots = {};
    _completed.store(false, std::memory_order_release);
    _notify = nullptr;
    _context = nullptr;
    _ready.reset();
    _stop.reset();
}
