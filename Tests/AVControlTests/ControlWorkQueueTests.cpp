#include "../../RedXe/ControlWorkQueue.h"
#include "PlugInterfaces/FactoryImpl.h"

#include <atomic>
#include <stdexcept>

namespace
{
uint32_t queueChecks = 0;
void Check(bool value, const char* message)
{
    ++queueChecks;
    if (!value) throw std::runtime_error(message);
}
class Work final : public RedXeComObject<Work, IRedXeControlWork>
{
  public:
    Work()
    {
        Check(SUCCEEDED(gate.create(wil::EventOptions::ManualReset)), "work gate event");
        Check(SUCCEEDED(entered.create(wil::EventOptions::ManualReset)), "work entered event");
    }
    HRESULT STDMETHODCALLTYPE Run(HANDLE cancel, uint32_t timeout) noexcept override
    {
        ++runs;
        workerThread.store(GetCurrentThreadId());
        receivedBudget.store(timeout);
        entered.SetEvent();
        const HANDLE handles[]{cancel, gate.get()};
        const DWORD result = WaitForMultipleObjects(2, handles, FALSE, timeout);
        return result == WAIT_OBJECT_0 ? HRESULT_FROM_WIN32(ERROR_CANCELLED) :
               result == WAIT_OBJECT_0 + 1 ? S_OK : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    void STDMETHODCALLTYPE Complete(HRESULT result) noexcept override
    {
        ++completions;
        completedOnOwner = completedOnOwner && GetCurrentThreadId() == ownerThread;
        lastResult = result;
    }
    wil::unique_event_nothrow gate;
    wil::unique_event_nothrow entered;
    std::atomic<uint32_t> runs{0};
    std::atomic<uint32_t> workerThread{0};
    std::atomic<uint32_t> receivedBudget{0};
    uint32_t completions = 0;
    DWORD ownerThread = GetCurrentThreadId();
    bool completedOnOwner = true;
    HRESULT lastResult = E_PENDING;
};
}

uint32_t RunControlWorkQueueTests()
{
    queueChecks = 0;
    wil::unique_event_nothrow notification;
    Check(SUCCEEDED(notification.create()), "control completion event");
    const auto notify = [](void* context) noexcept { (void)SetEvent(static_cast<HANDLE>(context)); };
    ControlWorkQueue queue;
    Check(!queue.Running(), "control worker starts lazily");
    Check(queue.Start(notify, notification.get()) == S_OK, "control worker starts");
    Check(queue.Start(notify, notification.get()) == S_FALSE, "control worker startup is idempotent");
    Check(queue.Enqueue(nullptr) == E_POINTER, "null control work rejected");
    std::array<wil::com_ptr_nothrow<Work>, ControlWorkQueue::Capacity + 1> work;
    for (auto& item : work) item.attach(new Work());
    Check(queue.Enqueue(work[0].get()) == S_OK, "first control work accepted");
    Check(WaitForSingleObject(work[0]->entered.get(), 1000) == WAIT_OBJECT_0, "control work reaches worker");
    Check(work[0]->workerThread.load() != GetCurrentThreadId(), "device work does not run on UI thread");
    Check(work[0]->receivedBudget.load() > 0 && work[0]->receivedBudget.load() <= 3000, "device work receives remaining bounded budget");
    for (size_t i = 1; i < ControlWorkQueue::Capacity; ++i)
        Check(queue.Enqueue(work[i].get()) == S_OK, "bounded control slot accepted");
    Check(queue.Enqueue(work.back().get()) == HRESULT_FROM_WIN32(ERROR_BUSY), "queue saturation is explicit");
    Check(queue.Enqueue(work[0].get()) == S_FALSE && queue.Enqueue(work[0].get()) == S_FALSE, "repeated control work coalesces");
    for (auto& item : work) item->gate.SetEvent();
    const ULONGLONG deadline = GetTickCount64() + 3000;
    uint32_t completed = 0;
    while (completed < ControlWorkQueue::Capacity + 1 && GetTickCount64() < deadline)
    {
        Check(WaitForSingleObject(notification.get(), 1000) == WAIT_OBJECT_0, "worker posts completion notification");
        queue.DrainCompletions();
        completed = 0;
        for (const auto& item : work) completed += item->completions;
    }
    Check(completed == ControlWorkQueue::Capacity + 1 && work[0]->runs.load() == 2, "coalesced work reruns exactly once");
    Check(work.back()->runs.load() == 0, "saturated work never executes");
    for (size_t i = 0; i < ControlWorkQueue::Capacity; ++i)
        Check(work[i]->completedOnOwner && work[i]->lastResult == S_OK, "completion is delivered on UI thread");
    queue.Stop();
    Check(!queue.Running(), "worker stops and drains");

    wil::com_ptr_nothrow<Work> blocked;
    blocked.attach(new Work());
    Check(queue.Start(notify, notification.get()) == S_OK && queue.Enqueue(blocked.get()) == S_OK, "queue can restart");
    Check(WaitForSingleObject(blocked->entered.get(), 1000) == WAIT_OBJECT_0, "cancel fixture enters device work");
    const ULONGLONG beforeStop = GetTickCount64();
    queue.Stop();
    Check(GetTickCount64() - beforeStop < 1000, "cancellation drains an event-blocked device unit");
    queue.DrainCompletions();
    Check(blocked->completions == 0, "shutdown never invokes stale UI completions");
    return queueChecks;
}
