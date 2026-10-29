#pragma once

#include <chrono>
#include <cstdint>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// One lazy auto-reset, one-shot timer, only for the optional continuous cap. DXGI readiness is acquired once before
// this wait and retained across message/diagnostic interruptions; it must never be polled during this wait.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class ContinuousFrameWait final
{
  public:
    [[nodiscard]] HRESULT Wait(std::chrono::nanoseconds delay, DWORD diagnosticTimeout, bool& ready) noexcept
    {
        ready = delay <= std::chrono::nanoseconds::zero();
        if (ready)
            return S_OK;
        if (!_timer)
        {
            _timer.reset(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                TIMER_MODIFY_STATE | SYNCHRONIZE));
            if (!_timer && GetLastError() == ERROR_INVALID_PARAMETER)
                _timer.reset(CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE));
            if (!_timer)
                return HRESULT_FROM_WIN32(GetLastError());
        }
        // Round upward to the timer's 100-ns unit, so a positive remainder never turns into zero or an absolute date.
        LARGE_INTEGER due{};
        due.QuadPart = -(delay.count() / 100 + (delay.count() % 100 != 0 ? 1 : 0));
        if (!SetWaitableTimerEx(_timer.get(), &due, 0, nullptr, nullptr, nullptr, 0))
            return HRESULT_FROM_WIN32(GetLastError());
        const HANDLE handle = _timer.get();
        const DWORD result =
            MsgWaitForMultipleObjectsEx(1, &handle, diagnosticTimeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (result == WAIT_OBJECT_0)
        {
            ready = true;
            return S_OK;
        }
        if (result == WAIT_OBJECT_0 + 1 || result == WAIT_TIMEOUT)
            return S_OK;
        return result == WAIT_FAILED ? HRESULT_FROM_WIN32(GetLastError()) : E_UNEXPECTED;
    }

    void Cancel() noexcept
    {
        if (_timer)
            (void)CancelWaitableTimer(_timer.get());
    }

    void Reset() noexcept
    {
        _timer.reset();
    }

    [[nodiscard]] HANDLE Handle() const noexcept
    {
        return _timer.get();
    }

  private:
    wil::unique_handle _timer;
};
#pragma warning(pop)

static_assert(sizeof(ContinuousFrameWait) == sizeof(HANDLE));
