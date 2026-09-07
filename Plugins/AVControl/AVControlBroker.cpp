#include "AVControlBroker.h"
#include "AVControlProtocolValidation.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <wil/result.h>

namespace AVControl
{
Broker::~Broker()
{
    Stop();
}
bool Broker::Running() const noexcept
{
    return _process && WaitForSingleObject(_process.get(), 0) == WAIT_TIMEOUT;
}
void Broker::Stop(uint32_t drainMilliseconds) noexcept
{
    // Only this child is in the job. Closing its kill-on-close job is also the parent-crash safety net.
    _job.reset();
    if (_process)
        (void)WaitForSingleObject(_process.get(), drainMilliseconds);
    _process.reset();
    _shared.reset();
    _mapping.reset();
    _request.reset();
    _reply.reset();
}
HRESULT Broker::Start(std::wstring_view executable, bool synthetic) noexcept
{
    Stop();
    bool successful = false;
    const auto rollback = wil::scope_exit(
        [&]
        {
            if (!successful)
                Stop();
        });
    try
    {
        if (executable.empty() || executable.find(L'"') != std::wstring_view::npos ||
            executable.find(L'\0') != std::wstring_view::npos)
            return E_INVALIDARG;
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        _mapping.reset(
            CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0, sizeof(BrokerShared), nullptr));
        RETURN_LAST_ERROR_IF(!_mapping);
        _shared.reset(
            static_cast<BrokerShared*>(MapViewOfFile(_mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BrokerShared))));
        RETURN_LAST_ERROR_IF(!_shared);
        // Mapped storage contains a POD protocol, initialized once before the child receives access.
        new (_shared.get()) BrokerShared{};
        _shared->synthetic = synthetic ? 1U : 0U;
        _request.reset(CreateEventW(&security, FALSE, FALSE, nullptr));
        _reply.reset(CreateEventW(&security, FALSE, FALSE, nullptr));
        if (!_changed)
            _changed.reset(CreateEventW(&security, FALSE, FALSE, nullptr));
        if (_changed)
            _changed.ResetEvent();
        RETURN_LAST_ERROR_IF(!_request || !_reply || !_changed);
        _job.reset(CreateJobObjectW(nullptr, nullptr));
        RETURN_LAST_ERROR_IF(!_job);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit = 1;
        RETURN_IF_WIN32_BOOL_FALSE(
            SetInformationJobObject(_job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        SIZE_T bytes = 0;
        (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<std::byte> storage(bytes);
        startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        RETURN_IF_WIN32_BOOL_FALSE(InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes));
        const auto attributes = wil::scope_exit([&] { DeleteProcThreadAttributeList(startup.lpAttributeList); });
        std::array<HANDLE, 4> handles{_mapping.get(), _request.get(), _reply.get(), _changed.get()};
        RETURN_IF_WIN32_BOOL_FALSE(UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                                                             PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(),
                                                             sizeof(handles), nullptr, nullptr));
        std::wstring command = L"\"" + std::wstring(executable) + L"\" --broker " +
                               std::to_wstring(reinterpret_cast<uintptr_t>(handles[0])) + L" " +
                               std::to_wstring(reinterpret_cast<uintptr_t>(handles[1])) + L" " +
                               std::to_wstring(reinterpret_cast<uintptr_t>(handles[2])) + L" " +
                               std::to_wstring(reinterpret_cast<uintptr_t>(handles[3]));
        PROCESS_INFORMATION process{};
        const BOOL created = CreateProcessW(std::wstring(executable).c_str(), command.data(), nullptr, nullptr, TRUE,
                                            CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                            nullptr, &startup.StartupInfo, &process);
        RETURN_LAST_ERROR_IF(!created);
        _process.reset(process.hProcess);
        wil::unique_handle thread(process.hThread);
        if (!AssignProcessToJobObject(_job.get(), _process.get()))
        {
            const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            // This suspended process has not entered plugin or Windows device code; it is exclusively ours.
            (void)TerminateProcess(_process.get(), ERROR_CANCELLED);
            Stop();
            return hr;
        }
        if (ResumeThread(thread.get()) == DWORD(-1))
        {
            const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            Stop();
            return hr;
        }
        successful = true;
        return S_OK;
    }
    catch (...)
    {
        Stop();
        return E_OUTOFMEMORY;
    }
}
HRESULT Broker::SetPreferences(const InventoryPreferences& preferences) noexcept
{
    if (!_shared || !Running())
        return HRESULT_FROM_WIN32(ERROR_PROCESS_ABORTED);
    if (!ValidInventoryPreferences(preferences))
        return E_INVALIDARG;
    _shared->preferences = preferences;
    return S_OK;
}
HRESULT Broker::Execute(const BrokerCommand& command, BrokerReply& reply, HANDLE cancelEvent,
                        uint32_t timeoutMilliseconds) noexcept
{
    if (!cancelEvent || !timeoutMilliseconds || timeoutMilliseconds > 3000)
        return E_INVALIDARG;
    const auto started = GetTickCount64();
    if (!Running() || !_shared)
        return HRESULT_FROM_WIN32(ERROR_PROCESS_ABORTED);
    if (WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0)
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    _reply.ResetEvent();
    if (++_sequence == 0)
        ++_sequence;
    _shared->command = command;
    _shared->requestSequence = _sequence;
    _shared->replySequence = 0;
    MemoryBarrier();
    _request.SetEvent();
    const HANDLE events[]{cancelEvent, _process.get(), _reply.get()};
    // Leave room for draining the owned child inside the three-second call budget.
    const DWORD wait =
        WaitForMultipleObjects(3, events, FALSE, timeoutMilliseconds > 250 ? timeoutMilliseconds - 250 : 1);
    if (wait != WAIT_OBJECT_0 + 2)
    {
        const HRESULT hr = HRESULT_FROM_WIN32(wait == WAIT_OBJECT_0       ? ERROR_CANCELLED
                                              : wait == WAIT_OBJECT_0 + 1 ? ERROR_PROCESS_ABORTED
                                              : wait == WAIT_TIMEOUT      ? ERROR_TIMEOUT
                                                                          : GetLastError());
        const auto elapsed = GetTickCount64() - started;
        Stop(elapsed < timeoutMilliseconds
                 ? static_cast<uint32_t>((std::min)(uint64_t{250}, timeoutMilliseconds - elapsed))
                 : 0);
        return hr;
    }
    MemoryBarrier();
    if (_shared->magic != BrokerMagic || _shared->version != BrokerProtocolVersion ||
        _shared->sizeBytes != sizeof(BrokerShared) || _shared->replySequence != _sequence ||
        _shared->reply.result == E_PENDING || !ValidInventoryPayload(_shared->reply.inventory))
    {
        Stop();
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    reply = _shared->reply;
    if (reply.result == HRESULT_FROM_WIN32(ERROR_TIMEOUT))
    {
        const auto elapsed = GetTickCount64() - started;
        Stop(elapsed < timeoutMilliseconds
                 ? static_cast<uint32_t>((std::min)(uint64_t{250}, timeoutMilliseconds - elapsed))
                 : 0);
    }
    return reply.result;
}
} // namespace AVControl
