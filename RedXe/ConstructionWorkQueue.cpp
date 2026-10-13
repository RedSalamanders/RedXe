#include "ConstructionWorkQueue.h"
#include <algorithm>
#include <system_error>

ConstructionWorkQueue::~ConstructionWorkQueue()
{
    Join();
}

HRESULT ConstructionWorkQueue::Start(Notify notify, void* context) noexcept
{
    if (Running())
        return _stop.is_signaled() ? E_UNEXPECTED : S_FALSE;
    RETURN_IF_FAILED(_stop.create(wil::EventOptions::ManualReset));
    RETURN_IF_FAILED(_ready.create());
    _notify = notify;
    _context = context;
    try
    {
        _worker = std::thread([this]() noexcept { Worker(); });
    }
    catch (const std::system_error&)
    {
        Reset();
        return E_FAIL;
    }
    catch (const std::bad_alloc&)
    {
        Reset();
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

HRESULT ConstructionWorkQueue::Enqueue(IRedXeConstructionWork* work) noexcept
{
    if (!work)
        return E_POINTER;
    if (!Running())
        return E_UNEXPECTED;
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    if (_stop.is_signaled())
        return E_UNEXPECTED;
    for (const Slot& slot : _slots)
        if (slot.work.get() == work)
            return S_FALSE;
    for (Slot& slot : _slots)
    {
        if (slot.state != State::Empty)
            continue;
        slot.work = work;
        slot.state = State::Queued;
        slot.queuedAt = GetTickCount64();
        _ready.SetEvent();
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_BUSY);
}

void ConstructionWorkQueue::Worker() noexcept
{
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const auto uninitialize = wil::scope_exit(
        [&]() noexcept
        {
            if (SUCCEEDED(apartment))
                CoUninitialize();
        });
    const HANDLE events[]{_stop.get(), _ready.get()};
    for (;;)
    {
        const DWORD wait = MsgWaitForMultipleObjectsEx(2, events, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0 + 2)
        {
            MSG message{};
            while (!_stop.is_signaled() && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                (void)DispatchMessageW(&message);
            continue;
        }
        if (wait != WAIT_OBJECT_0 + 1)
            return;
        for (;;)
        {
            Slot* running = nullptr;
            {
                const auto guard = wil::AcquireSRWLockExclusive(&_lock);
                if (_stop.is_signaled())
                    return;
                for (size_t offset = 1; offset <= Capacity; ++offset)
                {
                    const size_t i = (_lastSelected + offset) % Capacity;
                    if (_slots[i].state != State::Queued)
                        continue;
                    _lastSelected = i;
                    _slots[i].state = State::Running;
                    running = &_slots[i];
                    break;
                }
            }
            if (!running)
                break;
            // The owning slot stays pinned through Run. No worker-owned AddRef can become the final release of a
            // job containing a UI completion token or plugin objects.
            const uint64_t age = GetTickCount64() - running->queuedAt;
            HRESULT result = FAILED(apartment) ? apartment
                             : age >= WorkMilliseconds
                                 ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                                 : running->work->Run(_stop.get(), static_cast<uint32_t>(WorkMilliseconds - age));
            if (SUCCEEDED(result) && GetTickCount64() - running->queuedAt >= WorkMilliseconds)
                result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            bool notify = false;
            {
                const auto guard = wil::AcquireSRWLockExclusive(&_lock);
                running->result = result;
                running->state = State::Complete;
                notify = !_stop.is_signaled();
                if (notify)
                    _completed.store(true, std::memory_order_release);
            }
            if (notify && _notify)
                _notify(_context);
        }
    }
}

void ConstructionWorkQueue::DrainCompletions() noexcept
{
    if (!Running() || _stop.is_signaled() || !_completed.exchange(false, std::memory_order_acq_rel))
        return;
    std::array<Slot, Capacity> ready{};
    size_t count = 0;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        for (Slot& slot : _slots)
        {
            if (slot.state != State::Complete)
                continue;
            ready[count++] = std::move(slot);
            slot = Slot{};
        }
    }
    for (size_t i = 0; i < count && Running() && !_stop.is_signaled(); ++i)
        ready[i].work->Complete(ready[i].result);
}

ConstructionWorkQueue::StopResult ConstructionWorkQueue::Stop(uint32_t timeoutMilliseconds) noexcept
{
    if (!Running())
        return StopResult::Stopped;
    _stop.SetEvent();
    std::array<wil::com_ptr_nothrow<IRedXeConstructionWork>, Capacity> discarded{};
    bool inProgress = false;
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        size_t count = 0;
        for (Slot& slot : _slots)
        {
            if (slot.state == State::Running)
                inProgress = true;
            else if (slot.state != State::Empty)
            {
                discarded[count++] = std::move(slot.work);
                slot = Slot{};
            }
        }
    }
    const DWORD wait = _waitedOut ? 0 : std::min(timeoutMilliseconds, StopMilliseconds);
    if (WaitForSingleObject(_worker.native_handle(), wait) != WAIT_OBJECT_0)
    {
        // A zero wait that gave an idle thread no time to exit leaves a later stop its wait. A running indivisible
        // call, or a spent positive wait, consumes the bound once, including at repeated process shutdown.
        _waitedOut = _waitedOut || inProgress || wait != 0;
        return inProgress ? StopResult::WorkInProgress : StopResult::Exiting;
    }
    _worker.join();
    Reset();
    return StopResult::Stopped;
}

void ConstructionWorkQueue::Join() noexcept
{
    (void)Stop(0);
    if (_worker.joinable())
        _worker.join();
    Reset();
}

void ConstructionWorkQueue::Reset() noexcept
{
    _slots = {};
    _completed.store(false, std::memory_order_release);
    _notify = nullptr;
    _context = nullptr;
    _ready.reset();
    _stop.reset();
    _lastSelected = Capacity - 1;
    _waitedOut = false;
}
