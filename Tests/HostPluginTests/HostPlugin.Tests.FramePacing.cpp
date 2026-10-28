#include "HostPlugin.Tests.FramePacing.h"
#include "RedXe/FramePacing.h"
#include "RedXe/FrameScheduler.h"

#include <array>
#include <chrono>
#include <iostream>

namespace
{
void Check(bool passed, const wchar_t* name, bool& success) noexcept
{
    std::wcout << (passed ? L"[       OK ] " : L"[  FAILED  ] ") << name << L'\n';
    success = success && passed;
}
} // namespace

void TestContinuousFramePacing(bool& success) noexcept
{
    using namespace std::chrono_literals;
    using Clock = ContinuousFramePacer::Clock;
    HostFrameState settled{true, true, false, false, false, true, false, false, false, false};
    Check(LimitContinuousFrames(ContinuousFrameRate::Thirty, settled) &&
              !LimitContinuousFrames(ContinuousFrameRate::Display, settled),
          L"the cap is optional and only limits settled continuous content", success);
    const std::array bypass{
        HostFrameState{false, true, false, false, false, true, false, false, false, false},
        HostFrameState{true, false, false, false, false, true, false, false, false, false},
        HostFrameState{true, true, true, false, false, true, false, false, false, false},
        HostFrameState{true, true, false, true, false, true, false, false, false, false},
        HostFrameState{true, true, false, false, false, false, false, false, false, false},
        HostFrameState{true, true, false, false, false, true, true, false, false, false},
        HostFrameState{true, true, false, false, false, true, false, true, false, false},
        HostFrameState{true, true, false, false, false, true, false, false, true, false},
        HostFrameState{true, true, false, false, false, true, false, false, false, true},
    };
    for (const auto& state : bypass)
        Check(!LimitContinuousFrames(ContinuousFrameRate::Thirty, state),
              L"inactive, static, invalidated, page/overlay/dock motion bypasses the cap", success);
    for (const int hz : {60, 120})
    {
        ContinuousFramePacer pacer;
        int frames = 0;
        for (int tick = 0; tick < hz * 2; ++tick)
        {
            const auto now = Clock::time_point{std::chrono::duration_cast<Clock::duration>(
                std::chrono::nanoseconds{static_cast<int64_t>(tick) * 1'000'000'000 / hz})};
            if (pacer.Remaining(now) == 0ns)
            {
                ++frames;
                pacer.Presented(now);
            }
        }
        Check(frames == 60,
              hz == 60 ? L"30 fps uses two display intervals at 60 Hz"
                       : L"30 fps uses four display intervals at 120 Hz",
              success);
    }
    ContinuousFramePacer pacer;
    const auto zero = Clock::time_point{};
    Check(pacer.Remaining(zero) == 0ns, L"the first continuous frame is immediate", success);
    pacer.Presented(zero);
    pacer.AcceptFrameLatency(7);
    Check(pacer.HasFrameLatency(7) && !pacer.HasFrameLatency(8) && !pacer.HasFrameLatency(0),
          L"buffer readiness belongs to one live swap-chain generation", success);
    Check(pacer.Remaining(zero + 10ms) > 20ms, L"an unrelated wake retains the monotonic deadline", success);
    Check(pacer.HasFrameLatency(7), L"an unrelated wake retains already-acquired buffer readiness", success);
    Check(pacer.Remaining(zero + 1s) == 0ns, L"a late frame has no queued catch-up work", success);
    pacer.Presented(zero + 1s);
    Check(!pacer.HasFrameLatency(7), L"a present consumes its buffer readiness", success);
    Check(pacer.Remaining(zero + 1s) > 30ms, L"a late present starts one fresh interval", success);
    pacer.AcceptFrameLatency(8);
    pacer.ConsumeFrameLatency();
    Check(!pacer.HasFrameLatency(8), L"a render attempt consumes readiness even without a successful present", success);
    pacer.AcceptFrameLatency(8);
    pacer.Reset();
    Check(pacer.HasFrameLatency(8) && !pacer.HasFrameLatency(9),
          L"hide, resize and pacing changes retain a permit only for the same waitable object", success);
    Check(pacer.Remaining(zero + 1s) == 0ns, L"inactive/recovery reset removes the deadline", success);

    // Exercise the production timer/message wait without showing or activating any window.
    MSG message{};
    (void)PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    ContinuousFrameWait waiter;
    bool ready = false;
    Check(waiter.Wait(0ns, INFINITE, ready) == S_OK && ready && !waiter.Handle(), L"immediate work allocates no timer",
          success);
    constexpr UINT testMessage = WM_APP + 0x703;
    Check(PostThreadMessageW(GetCurrentThreadId(), testMessage, 0, 0) != FALSE,
          L"the timer test queues its own thread message", success);
    Check(waiter.Wait(1s, INFINITE, ready) == S_OK && !ready,
          L"pending input interrupts the continuous wait before expiry", success);
    Check(PeekMessageW(&message, nullptr, testMessage, testMessage, PM_REMOVE) != FALSE,
          L"the interrupted wait preserves the message for dispatch", success);
    waiter.Cancel();
    const HANDLE reused = waiter.Handle();
    Check(waiter.Wait(1s, 0, ready) == S_OK && !ready && waiter.Handle() == reused,
          L"a diagnostic timeout requests no frame and reuses the timer", success);
    waiter.Cancel();
    const auto started = Clock::now();
    Check(waiter.Wait(ContinuousFramePacer::Period, INFINITE, ready) == S_OK && ready &&
              Clock::now() - started >= 30ms && waiter.Handle() == reused,
          L"the production wait blocks until its one-shot deadline without DXGI polling", success);
    Check(WaitForSingleObject(waiter.Handle(), 0) == WAIT_TIMEOUT,
          L"the completed timer is auto-reset and has no periodic signal", success);
    waiter.Reset();
    Check(!waiter.Handle(), L"disabling the cap releases its timer", success);
}
