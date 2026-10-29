#include "PlugInterfaces/FactoryImpl.h"
#include "RedXe/ConstructionWorkQueue.h"
#include "RedXe/DashboardHost.h"
#include "RedXe/PluginHost.h"
#include "RedXe/Renderer.h"

#include <atomic>
#include <iostream>
#include <string>

struct RendererConstructionTestAccess final
{
    static bool PreparationPending(const Renderer& renderer) noexcept
    {
        return bool(renderer._pagePreparationProbe);
    }
};

struct ConstructionWorkQueueTestAccess final
{
    static HANDLE Thread(ConstructionWorkQueue& queue) noexcept
    {
        return queue._worker.native_handle();
    }
};
struct PluginHostConstructionTestAccess final
{
    static HANDLE Thread(PluginHost& host) noexcept
    {
        return ConstructionWorkQueueTestAccess::Thread(host._constructionWork);
    }
    static size_t Modules(const PluginHost& host) noexcept
    {
        size_t count = 0;
        for (const auto& module : host._modules)
            count += module.module ? 1U : 0U;
        return count;
    }
};

namespace
{
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Lifetime final
{
    DWORD owner = GetCurrentThreadId();
    std::atomic<uint32_t> destroyed{0};
    std::atomic<bool> releasedOnOwner{true};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class Construction final : public RedXeComObject<Construction, IRedXeConstructionWork>
{
  public:
    explicit Construction(Lifetime* lifetime = nullptr) : _lifetime(lifetime)
    {
        THROW_IF_FAILED(gate.create(wil::EventOptions::ManualReset));
        THROW_IF_FAILED(entered.create(wil::EventOptions::ManualReset));
    }
    ~Construction()
    {
        if (_lifetime)
        {
            if (GetCurrentThreadId() != _lifetime->owner)
                _lifetime->releasedOnOwner.store(false);
            ++_lifetime->destroyed;
        }
    }
    HRESULT STDMETHODCALLTYPE Run(HANDLE cancel, uint32_t timeout) noexcept override
    {
        ++runs;
        worker.store(GetCurrentThreadId());
        budget.store(timeout);
        APTTYPE type = APTTYPE_CURRENT;
        APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
        apartment.store(SUCCEEDED(CoGetApartmentType(&type, &qualifier)) ? type : APTTYPE_CURRENT);
        entered.SetEvent();
        const HANDLE events[]{cancel, gate.get()};
        const DWORD wait = ignoreCancellation ? WaitForSingleObject(gate.get(), INFINITE)
                                              : WaitForMultipleObjects(2, events, FALSE, timeout);
        return ignoreCancellation && wait == WAIT_OBJECT_0 ? outcome
               : wait == WAIT_OBJECT_0                     ? HRESULT_FROM_WIN32(ERROR_CANCELLED)
               : wait == WAIT_OBJECT_0 + 1                 ? outcome
                                                           : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    void STDMETHODCALLTYPE Complete(HRESULT result) noexcept override
    {
        ++completions;
        completedOnOwner = completedOnOwner && GetCurrentThreadId() == owner;
        lastResult = result;
        if (stopOnComplete)
            (void)stopOnComplete->Stop();
    }
    wil::unique_event_nothrow gate;
    wil::unique_event_nothrow entered;
    std::atomic<uint32_t> runs{0};
    std::atomic<DWORD> worker{0};
    std::atomic<uint32_t> budget{0};
    std::atomic<APTTYPE> apartment{APTTYPE_CURRENT};
    uint32_t completions = 0;
    DWORD owner = GetCurrentThreadId();
    bool completedOnOwner = true;
    bool ignoreCancellation = false;
    HRESULT outcome = S_OK;
    HRESULT lastResult = E_PENDING;
    ConstructionWorkQueue* stopOnComplete = nullptr;

  private:
    Lifetime* _lifetime;
};
#pragma warning(pop)

wil::com_ptr_nothrow<Construction> MakeConstruction(Lifetime* lifetime = nullptr)
{
    wil::com_ptr_nothrow<Construction> result;
    result.attach(new Construction(lifetime));
    return result;
}
uint64_t Cpu(HANDLE thread) noexcept
{
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetThreadTimes(thread, &created, &exited, &kernel, &user))
        return UINT64_MAX;
    return ((uint64_t{kernel.dwHighDateTime} << 32) | kernel.dwLowDateTime) +
           ((uint64_t{user.dwHighDateTime} << 32) | user.dwLowDateTime);
}
bool Drain(ConstructionWorkQueue& queue, HANDLE notification,
           const std::array<wil::com_ptr_nothrow<Construction>, ConstructionWorkQueue::Capacity + 1>& work,
           uint32_t expected) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + 5'000;
    while (GetTickCount64() < deadline)
    {
        if (WaitForSingleObject(notification, 1000) != WAIT_OBJECT_0)
            return false;
        queue.DrainCompletions();
        uint32_t completed = 0;
        for (const auto& item : work)
            completed += item ? item->completions : 0;
        if (completed == expected)
            return true;
    }
    return false;
}
bool WaitForNotificationQuiet(ConstructionWorkQueue& queue, HANDLE notification) noexcept
{
    // Completion publication precedes the callback. Drain finite late hints before measuring steady idle.
    const ULONGLONG deadline = GetTickCount64() + 5'000;
    while (GetTickCount64() < deadline)
    {
        queue.DrainCompletions();
        const DWORD wait = WaitForSingleObject(notification, 50);
        if (wait == WAIT_TIMEOUT)
            return true;
        if (wait != WAIT_OBJECT_0 || !ResetEvent(notification))
            return false;
    }
    return false;
}
} // namespace

void TestConstructionWork(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] bounded STA construction, owner responsiveness and shutdown lifetime\n";
    uint32_t checks = 0;
    const auto check = [&](bool condition, const wchar_t* name)
    {
        ++checks;
        if (!condition)
        {
            success = false;
            std::wcerr << L"FAIL construction work: " << name << L'\n';
        }
    };
    try
    {
        const HRESULT ownerApartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const auto uninitialize = wil::scope_exit(
            [&]() noexcept
            {
                if (SUCCEEDED(ownerApartment))
                    CoUninitialize();
            });
        check(SUCCEEDED(ownerApartment), L"the owner initializes its production UI apartment");
        if (FAILED(ownerApartment))
            return;
        wil::unique_event_nothrow notification;
        THROW_IF_FAILED(notification.create());
        const auto notify = [](void* context) noexcept { (void)SetEvent(static_cast<HANDLE>(context)); };
        Lifetime lifetime;
        std::array<wil::com_ptr_nothrow<Construction>, ConstructionWorkQueue::Capacity + 1> work;
        for (auto& item : work)
            item = MakeConstruction(&lifetime);
        ConstructionWorkQueue queue;
        check(!queue.Running() && queue.Enqueue(work[0].get()) == E_UNEXPECTED,
              L"cold queue owns no worker and refuses work before startup");
        check(queue.Start(notify, notification.get()) == S_OK && queue.Start(notify, notification.get()) == S_FALSE &&
                  queue.Enqueue(nullptr) == E_POINTER,
              L"lazy startup is idempotent and null work is rejected");
        const ULONGLONG start = GetTickCount64();
        check(queue.Enqueue(work[0].get()) == S_OK &&
                  WaitForSingleObject(work[0]->entered.get(), 5'000) == WAIT_OBJECT_0,
              L"first work reaches its independent lane");
        check(work[0]->worker.load() != GetCurrentThreadId() && work[0]->apartment.load() == APTTYPE_STA &&
                  work[0]->budget.load() > 0 && work[0]->budget.load() <= ConstructionWorkQueue::WorkMilliseconds,
              L"native construction runs on an STA with the remaining budget");
        for (size_t i = 1; i < ConstructionWorkQueue::Capacity; ++i)
            check(queue.Enqueue(work[i].get()) == S_OK, L"fixed construction slot accepts one immutable job");
        check(queue.Enqueue(work.back().get()) == HRESULT_FROM_WIN32(ERROR_BUSY),
              L"saturation refuses work without waiting");
        check(queue.Enqueue(work[0].get()) == S_FALSE && queue.Enqueue(work[0].get()) == S_FALSE &&
                  queue.Enqueue(work[1].get()) == S_FALSE,
              L"running and queued duplicates coalesce without a rerun");
        queue.DrainCompletions();
        check(work[0]->completions == 0 && work[0]->runs.load() == 1 &&
                  WaitForSingleObject(work[0]->gate.get(), 0) == WAIT_TIMEOUT,
              L"owner continues enqueue and completion work while construction remains blocked");
        std::wcout << L"construction_owner blocked_submission_ms=" << GetTickCount64() - start << L" queue_bytes="
                   << sizeof(ConstructionWorkQueue) << L'\n';
        for (auto& item : work)
            item->gate.SetEvent();
        check(Drain(queue, notification.get(), work, ConstructionWorkQueue::Capacity),
              L"all accepted construction results are posted and drained");
        bool exact = work.back()->runs.load() == 0;
        for (size_t i = 0; i < ConstructionWorkQueue::Capacity; ++i)
            exact = exact && work[i]->runs.load() == 1 && work[i]->completions == 1 && work[i]->completedOnOwner &&
                    work[i]->lastResult == S_OK;
        check(exact, L"each accepted object runs once and completes once on its owner");
        notification.ResetEvent();
        check(WaitForNotificationQuiet(queue, notification.get()),
              L"finite completion notifications quiesce before the steady-idle sample");
        const uint64_t idleBefore = Cpu(ConstructionWorkQueueTestAccess::Thread(queue));
        const DWORD idleWait = WaitForSingleObject(notification.get(), 250);
        const uint64_t idleAfter = Cpu(ConstructionWorkQueueTestAccess::Thread(queue));
        const bool idle = idleBefore != UINT64_MAX && idleAfter != UINT64_MAX && idleWait == WAIT_TIMEOUT &&
                          idleAfter - idleBefore <= 10'000;
        if (!idle)
            std::wcerr << L"construction_idle before_100ns=" << idleBefore << L" after_100ns=" << idleAfter << L" wait="
                       << idleWait << L" wall_ms=250 budget_100ns=10000\n";
        check(idle, L"idle STA blocks on events without a timer or periodic CPU wake");
        std::wcout << L"construction_idle cpu_100ns=" << idleAfter - idleBefore << L" wall_ms=250\n";
        check(queue.Stop() == ConstructionWorkQueue::StopResult::Stopped && !queue.Running(),
              L"idle construction lane joins and releases its fixed storage");
        for (auto& item : work)
            item.reset();
        check(lifetime.destroyed.load() == ConstructionWorkQueue::Capacity + 1 && lifetime.releasedOnOwner.load(),
              L"final job releases and their owned storage occur on the owner thread");

        {
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
            struct DelayedNotify final
            {
                wil::unique_event_nothrow entered;
                wil::unique_event_nothrow release;
                wil::unique_event_nothrow delivered;
                wil::unique_event_nothrow notification;
            }
#pragma warning(pop)
            delayed;
            THROW_IF_FAILED(delayed.entered.create());
            THROW_IF_FAILED(delayed.release.create(wil::EventOptions::ManualReset));
            THROW_IF_FAILED(delayed.delivered.create());
            THROW_IF_FAILED(delayed.notification.create(wil::EventOptions::ManualReset));
            auto delayedWork = MakeConstruction();
            delayedWork->gate.SetEvent();
            ConstructionWorkQueue delayedQueue;
            const auto releaseNotification = wil::scope_exit([&]() noexcept { delayed.release.SetEvent(); });
            const auto delayedNotify = [](void* context) noexcept
            {
                auto& value = *static_cast<DelayedNotify*>(context);
                value.entered.SetEvent();
                (void)WaitForSingleObject(value.release.get(), INFINITE);
                value.notification.SetEvent();
                value.delivered.SetEvent();
            };
            const bool entered = delayedQueue.Start(delayedNotify, &delayed) == S_OK &&
                                 delayedQueue.Enqueue(delayedWork.get()) == S_OK &&
                                 WaitForSingleObject(delayed.entered.get(), 5'000) == WAIT_OBJECT_0;
            check(entered, L"the late-notification witness pauses after publishing its result");
            if (entered)
            {
                delayedQueue.DrainCompletions();
                check(delayedWork->completions == 1 && delayedWork->lastResult == S_OK,
                      L"the owner may drain a result before its notification callback returns");
                delayed.release.SetEvent();
                check(WaitForSingleObject(delayed.delivered.get(), 5'000) == WAIT_OBJECT_0 &&
                          WaitForSingleObject(delayed.notification.get(), 0) == WAIT_OBJECT_0,
                      L"a completed and drained job can still deliver a finite late notification");
                check(WaitForNotificationQuiet(delayedQueue, delayed.notification.get()),
                      L"idle preparation consumes the late hint without another completion");
                check(delayedWork->completions == 1, L"a late notification cannot repeat UI completion");
            }
            delayed.release.SetEvent();
            check(delayedQueue.Stop() == ConstructionWorkQueue::StopResult::Stopped,
                  L"the late-notification witness joins before its callback storage is released");
        }

        Lifetime onlyHostReference;
        auto onlyHostWork = MakeConstruction(&onlyHostReference);
        onlyHostWork->gate.SetEvent();
        notification.ResetEvent();
        check(queue.Start(notify, notification.get()) == S_OK && queue.Enqueue(onlyHostWork.get()) == S_OK,
              L"the queue can own the only remaining reference to an immutable job");
        onlyHostWork.reset();
        check(WaitForSingleObject(notification.get(), 5'000) == WAIT_OBJECT_0 &&
                  onlyHostReference.destroyed.load() == 0,
              L"completed work stays pinned until its owner drains the result");
        queue.DrainCompletions();
        check(onlyHostReference.destroyed.load() == 1 && onlyHostReference.releasedOnOwner.load(),
              L"the final queue-owned reference is released on UI, never on the worker");
        check(queue.Stop() == ConstructionWorkQueue::StopResult::Stopped,
              L"sole-reference delivery leaves the lane stoppable");

        // Expiration includes time waiting behind an indivisible call; an expired queued job never enters Run.
        auto blocked = MakeConstruction();
        auto expired = MakeConstruction();
        blocked->ignoreCancellation = true;
        const auto release = wil::scope_exit([&]() noexcept { blocked->gate.SetEvent(); });
        check(queue.Start(notify, notification.get()) == S_OK && queue.Enqueue(blocked.get()) == S_OK &&
                  WaitForSingleObject(blocked->entered.get(), 5'000) == WAIT_OBJECT_0 &&
                  queue.Enqueue(expired.get()) == S_OK,
              L"restarted queue accepts a blocked and a waiting construction job");
        notification.ResetEvent();
        check(WaitForSingleObject(notification.get(), ConstructionWorkQueue::WorkMilliseconds + 30) == WAIT_TIMEOUT,
              L"uninterruptible fixture holds the worker past both jobs' budgets");
        blocked->gate.SetEvent();
        const ULONGLONG deadline = GetTickCount64() + 5'000;
        while (expired->completions == 0 && GetTickCount64() < deadline)
        {
            if (WaitForSingleObject(notification.get(), 1000) != WAIT_OBJECT_0)
                break;
            queue.DrainCompletions();
        }
        check(blocked->lastResult == HRESULT_FROM_WIN32(ERROR_TIMEOUT) && expired->runs.load() == 0 &&
                  expired->completions == 1 && expired->lastResult == HRESULT_FROM_WIN32(ERROR_TIMEOUT),
              L"late success is refused and queue age prevents expired work from starting");
        check(queue.Stop() == ConstructionWorkQueue::StopResult::Stopped, L"expired results leave the lane stoppable");

        auto cancellable = MakeConstruction();
        check(queue.Start(notify, notification.get()) == S_OK && queue.Enqueue(cancellable.get()) == S_OK &&
                  WaitForSingleObject(cancellable->entered.get(), 5'000) == WAIT_OBJECT_0,
              L"cancellation fixture is in flight");
        check(queue.Stop() == ConstructionWorkQueue::StopResult::Stopped && cancellable->completions == 0,
              L"cooperative cancellation joins and suppresses stale completion");

        auto late = MakeConstruction();
        auto discarded = MakeConstruction();
        late->ignoreCancellation = true;
        const auto releaseLate = wil::scope_exit([&]() noexcept { late->gate.SetEvent(); });
        check(queue.Start(notify, notification.get()) == S_OK && queue.Enqueue(late.get()) == S_OK &&
                  WaitForSingleObject(late->entered.get(), 5'000) == WAIT_OBJECT_0 &&
                  queue.Enqueue(discarded.get()) == S_OK,
              L"uninterruptible construction has a queued successor");
        const ULONGLONG beforeStop = GetTickCount64();
        const auto firstStop = queue.Stop(25);
        const ULONGLONG firstWait = GetTickCount64() - beforeStop;
        const ULONGLONG again = GetTickCount64();
        const auto secondStop = queue.Stop(1000);
        check(firstStop == ConstructionWorkQueue::StopResult::WorkInProgress && firstWait < 1000 &&
                  secondStop == ConstructionWorkQueue::StopResult::WorkInProgress && GetTickCount64() - again < 500 &&
                  queue.Running() && queue.Enqueue(discarded.get()) == E_UNEXPECTED &&
                  queue.Start(notify, notification.get()) == E_UNEXPECTED,
              L"a late call retains storage and consumes the shutdown wait only once");
        queue.DrainCompletions();
        check(late->completions == 0 && discarded->runs.load() == 0 && discarded->completions == 0,
              L"shutdown drops queued work and delivers no construction completion");
        late->gate.SetEvent();
        queue.Join();
        check(!queue.Running() && late->completions == 0 && discarded->runs.load() == 0,
              L"private-owner destruction can join before releasing plugin storage");

        // Completion is deliberately allowed to stop the queue. Detached later results must not call stale UI.
        auto stopping = MakeConstruction();
        stopping->stopOnComplete = &queue;
        stopping->gate.SetEvent();
        notification.ResetEvent();
        check(queue.Start(notify, notification.get()) == S_OK && queue.Enqueue(stopping.get()) == S_OK &&
                  WaitForSingleObject(notification.get(), 5'000) == WAIT_OBJECT_0,
              L"completion-stop fixture posts");
        queue.DrainCompletions();
        check(stopping->completions == 1 && !queue.Running(), L"completion may stop its owner without recursive work");

        // Exercise production host routing and module retention, independently of the standalone queue tests.
        auto hosted = MakeConstruction();
        hosted->ignoreCancellation = true;
        PluginHost host;
        check(host.QueueConstructionWork(hosted.get()) == E_ACCESSDENIED && !host.ConstructionWorkerRunning(),
              L"ordinary activation refuses deferred construction without creating a worker");
        host.SetConstructionAccessEnabled(true);
        const auto releaseHosted = wil::scope_exit([&]() noexcept { hosted->gate.SetEvent(); });
        PluginHost::ModuleView module{};
        check(SUCCEEDED(
                  host.GetPluginModule("builtin.rotating-triangle", RedXePluginCapabilityWidgetProvider, &module)) &&
                  PluginHostConstructionTestAccess::Modules(host) == 1 && !host.ConstructionWorkerRunning(),
              L"production host maps a module without starting the construction worker");
        check(host.QueueConstructionWork(nullptr) == E_POINTER && host.QueueConstructionWork(hosted.get()) == S_OK &&
                  WaitForSingleObject(hosted->entered.get(), 5'000) == WAIT_OBJECT_0,
              L"production service dispatches native construction to its STA");
        host.StopConstruction(25);
        const ULONGLONG beforeShutdown = GetTickCount64();
        host.Shutdown();
        check(GetTickCount64() - beforeShutdown < 500 && host.ConstructionWorkerRunning() &&
                  PluginHostConstructionTestAccess::Modules(host) == 1 &&
                  host.QueueConstructionWork(hosted.get()) == E_UNEXPECTED,
              L"shutdown retains mapped plugin code for a late Run and refuses new work without waiting again");
        host.AcknowledgeUiInvalidate();
        check(hosted->completions == 0, L"host shutdown suppresses pending UI delivery");
        hosted->gate.SetEvent();
        check(WaitForSingleObject(PluginHostConstructionTestAccess::Thread(host), 5'000) == WAIT_OBJECT_0,
              L"late native call exits after its retained storage is released");
        host.Shutdown();
        check(!host.ConstructionWorkerRunning() && PluginHostConstructionTestAccess::Modules(host) == 0 &&
                  hosted->completions == 0,
              L"a subsequent shutdown joins the exited lane before unloading plugin code");

        PluginHost completingHost;
        completingHost.SetConstructionAccessEnabled(true);
        wil::unique_hwnd window{CreateWindowExW(0, L"STATIC", L"construction completion", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                                nullptr, GetModuleHandleW(nullptr), nullptr)};
        check(bool(window), L"completion fixture owns a hidden message window");
        completingHost.SetUiInvalidateTarget(window.get());
        auto completedWork = MakeConstruction();
        completedWork->gate.SetEvent();
        check(completingHost.QueueConstructionWork(completedWork.get()) == S_OK && completedWork->completions == 0,
              L"production enqueue never invokes UI completion recursively");
        const ULONGLONG postDeadline = GetTickCount64() + 5'000;
        while (completedWork->completions == 0 && GetTickCount64() < postDeadline)
        {
            (void)MsgWaitForMultipleObjectsEx(0, nullptr, 1000, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE);
            MSG message{};
            while (PeekMessageW(&message, window.get(), PluginHost::kDataSnapshotInvalidateMessage,
                                PluginHost::kDataSnapshotInvalidateMessage, PM_REMOVE))
                completingHost.AcknowledgeUiInvalidate();
        }
        check(completedWork->completions == 1 && completedWork->completedOnOwner && completedWork->lastResult == S_OK,
              L"the existing coalesced host notification delivers completion on the UI thread");
        completingHost.SetUiInvalidateTarget(nullptr);
        completingHost.Shutdown();
        std::wcout << L"construction_checks=" << checks << L'\n';
    }
    catch (const std::exception& error)
    {
        success = false;
        std::cerr << "FAIL construction exception: " << error.what() << '\n';
    }
}

void TestConstructionPages(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] production Launcher construction and full-quality neighbour readiness\n";
    uint32_t checks = 0;
    const auto check = [&](bool value, const wchar_t* name)
    {
        ++checks;
        if (!value)
        {
            success = false;
            std::wcerr << L"FAIL construction page: " << name << L'\n';
        }
    };
    const HRESULT apartment = OleInitialize(nullptr);
    check(SUCCEEDED(apartment), L"owner STA initializes");
    if (FAILED(apartment))
        return;
    const auto uninitialize = wil::scope_exit([]() noexcept { OleUninitialize(); });
    try
    {
        constexpr UINT width = 800, height = 720;
        wil::unique_hwnd window{CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"STATIC", L"construction page test",
                                                WS_POPUP, -32000, -32000, width, height, nullptr, nullptr,
                                                GetModuleHandleW(nullptr), nullptr)};
        check(bool(window), L"fixture owns a hidden nonactivating window");
        if (!window)
            return;
        std::string json =
            R"({"version":{"major":5},"pages":[{"id":"current","widgets":[{"plugin":"builtin.rotating-triangle"}]},{"id":"incoming","widgets":[)";
        for (uint32_t index = 0; index < 32; ++index)
        {
            if (index)
                json += ',';
            json +=
                R"({"plugin":"builtin.launcher","shortcuts":[{"target":"https://example.com/","icon":"Settings"}]})";
        }
        json += "]}]}";
        AppSettings settings;
        HRESULT result = ParseAppSettingsJson(json, settings);
        auto& host = PluginHost::Instance();
        const bool oldPolicy = host.ConstructionAccessEnabled();
        host.SetConstructionAccessEnabled(true);
        const auto restore = wil::scope_exit([&]() noexcept { host.SetConstructionAccessEnabled(oldPolicy); });
        auto blocker = MakeConstruction();
        blocker->ignoreCancellation = true;
        const auto release = wil::scope_exit([&]() noexcept { blocker->gate.SetEvent(); });
        if (SUCCEEDED(result))
            result = host.QueueConstructionWork(blocker.get());
        check(SUCCEEDED(result) && WaitForSingleObject(blocker->entered.get(), 5000) == WAIT_OBJECT_0,
              L"one indivisible fixture call blocks the real host STA");
        if (FAILED(result))
            return;
        PluginManager currentPlugins, incomingPlugins;
        DashboardHost current, incoming;
        Renderer renderer;
        const auto cleanup = wil::scope_exit(
            [&]() noexcept
            {
                renderer.Shutdown();
                incoming.Shutdown();
                current.Shutdown();
            });
        if (SUCCEEDED(result))
            result = currentPlugins.InitializePage(settings, 0);
        if (SUCCEEDED(result))
            result = current.Initialize(currentPlugins, window.get(), width, height, USER_DEFAULT_SCREEN_DPI, false);
        if (SUCCEEDED(result))
            result = renderer.Initialize(window.get(), true, current);
        if (SUCCEEDED(result))
            result = current.SetWidgetsVisible(true);
        if (SUCCEEDED(result))
            result = incomingPlugins.InitializePage(settings, 1, &currentPlugins);
        if (SUCCEEDED(result))
            result = incoming.Initialize(incomingPlugins, window.get(), width, height, USER_DEFAULT_SCREEN_DPI, false);
        if (SUCCEEDED(result))
            result = incoming.SetHorizontalOffset(width);
        RuntimePageCallbackContext context;
        context.stageId = 0x123456;
        context.page = 1;
        if (SUCCEEDED(result))
            result = renderer.SetTransitionDashboard(&incoming, &context);
        if (SUCCEEDED(result))
            result = incoming.SetWidgetsVisible(true);
        host.SetConstructionAccessEnabled(false);
        check(SUCCEEDED(result) && !incoming.ConstructionReady() && current.ConstructionReady() &&
                  RendererConstructionTestAccess::PreparationPending(renderer),
              L"32 valid incoming Launchers wait offscreen, including bounded-queue overflow admission");
        if (FAILED(result))
            return;
        bool currentFrames = true;
        for (uint32_t index = 0; index < 3; ++index)
        {
            currentFrames = SUCCEEDED(renderer.PrepareWidgets()) &&
                            SUCCEEDED(renderer.Render(static_cast<float>(index) / 30, 1.0f / 30)) &&
                            renderer.LastFrameSuccessfulWidgetCount() >= 1 && currentFrames;
        }
        check(currentFrames && !incoming.ConstructionReady() &&
                  RendererConstructionTestAccess::PreparationPending(renderer),
              L"blocked construction leaves current GPU frames running and does not consume first preparations");
        blocker->gate.SetEvent();
        const uint64_t deadline = GetTickCount64() + 5000;
        do
        {
            host.AcknowledgeUiInvalidate();
            result = renderer.PrepareWidgets();
            if (incoming.ConstructionReady() || FAILED(result))
                break;
            (void)MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE);
        } while (GetTickCount64() < deadline);
        check(SUCCEEDED(result) && incoming.ConstructionReady() &&
                  !RendererConstructionTestAccess::PreparationPending(renderer),
              L"all 32 jobs eventually admit, complete on UI and upload before incoming readiness");
        bool everyReady = true;
        for (size_t index = 0; index < incoming.WidgetCount(); ++index)
        {
            RedXeConstructionState state = RedXeConstructionStatePending;
            auto* widget = incoming.ConstructionStateWidgetAt(index);
            everyReady = widget && widget->GetConstructionState(&state) == S_OK &&
                         state == RedXeConstructionStateReady && everyReady;
        }
        check(everyReady, L"readiness requires every placed widget, not merely the first completed job");
        check(SUCCEEDED(renderer.Render(0.2f, 1.0f / 30)) && blocker->completions == 1,
              L"posted completion and full-quality prepared composition remain valid");
        check(renderer.SetTransitionDashboard(nullptr) == S_OK, L"retirement releases borrowed preparation ownership");
        std::wcout << L"construction_page_checks=" << checks << L'\n';
    }
    catch (const std::exception& error)
    {
        success = false;
        std::cerr << "FAIL construction page exception: " << error.what() << '\n';
    }
}
