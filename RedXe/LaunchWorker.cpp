#include "LaunchWorker.h"

#include <cstdio>
#include <cwchar>
#include <new>
#include <objbase.h>
#include <system_error>

LaunchWorker::~LaunchWorker()
{
    // Only a private host gets here with a launch still running; the process runtime is retained instead.
    if (_thread.joinable())
    {
        _stop.SetEvent();
        _thread.join();
    }
}

HRESULT LaunchWorker::Enqueue(const Request& request) noexcept
{
    if (_thread.joinable() && _stop.is_signaled())
    {
        return E_UNEXPECTED;
    }
    if (!_thread.joinable())
    {
        if (!_stop)
        {
            RETURN_IF_FAILED(_stop.create(wil::EventOptions::ManualReset));
        }
        if (!_wake)
        {
            RETURN_IF_FAILED(_wake.create());
        }
        try
        {
            _thread = std::thread([this]() noexcept { Worker(); });
        }
        catch (const std::system_error&)
        {
            return E_FAIL;
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
    }
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        if (_count == kSlots)
        {
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        }
        Slot& slot = _slots[(_head + _count) % kSlots];
        slot.request = request;
        slot.result = S_OK;
        slot.state = State::Queued;
        ++_count;
    }
    _wake.SetEvent();
    return S_OK;
}

void LaunchWorker::DrainCompletions(IRedXeHost* host) noexcept
{
    for (;;)
    {
        HRESULT result = S_OK;
        const char* failureEvent = nullptr;
        std::array<char, 64> subject{};
        {
            const auto guard = wil::AcquireSRWLockExclusive(&_lock);
            if (_count == 0 || _slots[_head].state != State::Complete)
            {
                return;
            }
            const Slot& slot = _slots[_head];
            result = slot.result;
            failureEvent = slot.request.failureEvent;
            subject = slot.request.subject;
            _slots[_head].state = State::Empty;
            _head = (_head + 1) % kSlots;
            --_count;
        }
        const bool failed = FAILED(result);
        std::array<char, kRedXeMaximumLogMessageBytes> message{};
        (void)_snprintf_s(message.data(), message.size(), _TRUNCATE, "%s %s", subject.data(),
                          failed ? "could not start its target." : "started its target.");
        (void)RedXeHostLog(host, failed ? RedXeLogLevelWarning : RedXeLogLevelDebug, nullptr, nullptr,
                           failed && failureEvent ? failureEvent : "launch-completed", message.data(), result);
    }
}

bool LaunchWorker::Stop(uint32_t timeoutMilliseconds) noexcept
{
    if (!_thread.joinable())
    {
        return true;
    }
    // The worker takes no further slot once the event is set, so queued launches are dropped and only the one in
    // progress, if any, still runs. The event is still set only when an earlier Stop timed out: the process runtime
    // shuts down again at static destruction, and that later call checks the thread without waiting out the bound
    // a second time.
    const DWORD wait = _stop.is_signaled() ? 0 : timeoutMilliseconds;
    _stop.SetEvent();
    if (WaitForSingleObject(_thread.native_handle(), wait) != WAIT_OBJECT_0)
    {
        return false;
    }
    _thread.join();
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        for (Slot& slot : _slots)
        {
            slot.state = State::Empty;
        }
        _head = 0;
        _count = 0;
    }
    _stop.ResetEvent();
    return true;
}

HRESULT LaunchWorker::Launch(Request& request) noexcept
{
    if (request.kind == Kind::Process)
    {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        wil::unique_process_information process;
        if (!CreateProcessW(request.file.data(), request.commandLine.data(), nullptr, nullptr, FALSE,
                            CREATE_DEFAULT_ERROR_MODE, nullptr,
                            request.directory[0] != L'\0' ? request.directory.data() : nullptr, &startup, &process))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }
    if (request.fileFolder && request.directory[0] == L'\0')
    {
        // A file launches with its own folder as working directory, as Launcher always did.
        const DWORD attributes = GetFileAttributesW(request.file.data());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            request.directory = request.file;
            wchar_t* slash = wcsrchr(request.directory.data(), L'\\');
            if (!slash)
            {
                slash = wcsrchr(request.directory.data(), L'/');
            }
            if (slash)
            {
                *slash = L'\0';
            }
            else
            {
                request.directory[0] = L'\0';
            }
        }
    }
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = request.shellMask;
    info.lpFile = request.file.data();
    info.lpDirectory = request.directory[0] != L'\0' ? request.directory.data() : nullptr;
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

void LaunchWorker::Worker() noexcept
{
    // The shell can hand a launch to an extension activated through COM, and asks its caller for an STA.
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const auto uninitialize = wil::scope_exit(
        [&]() noexcept
        {
            if (SUCCEEDED(apartment))
            {
                CoUninitialize();
            }
        });
    constexpr DWORD kEvents = 2;
    const std::array<HANDLE, kEvents> events{_stop.get(), _wake.get()};
    for (;;)
    {
        // An apartment thread keeps its queue moving, so the wait also returns for a message; an idle lane gets none.
        const DWORD wait =
            MsgWaitForMultipleObjectsEx(kEvents, events.data(), INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait == WAIT_OBJECT_0 + kEvents)
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                (void)DispatchMessageW(&message);
            }
            continue;
        }
        if (wait != WAIT_OBJECT_0 + 1)
        {
            return;
        }
        for (;;)
        {
            if (_stop.is_signaled())
            {
                return;
            }
            Slot* running = nullptr;
            {
                const auto guard = wil::AcquireSRWLockExclusive(&_lock);
                for (size_t offset = 0; offset < _count && !running; ++offset)
                {
                    Slot& slot = _slots[(_head + offset) % kSlots];
                    if (slot.state == State::Queued)
                    {
                        slot.state = State::Running;
                        running = &slot;
                    }
                }
            }
            if (!running)
            {
                break;
            }
            // The slot belongs to this thread until it is Complete: Enqueue writes only free slots past the tail,
            // and DrainCompletions frees only Complete ones.
            const HRESULT result = FAILED(apartment) ? apartment : _perform(running->request);
            {
                const auto guard = wil::AcquireSRWLockExclusive(&_lock);
                running->result = result;
                running->state = State::Complete;
            }
            if (_notify)
            {
                _notify(_context);
            }
        }
    }
}
