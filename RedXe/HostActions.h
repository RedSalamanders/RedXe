#pragma once

// Executes the host's default system, keys, and mouse actions on the UI thread (HostActionCatalog.h lists them;
// page, widget, and redxe run inside Application). Every call is synchronous, non-reentrant, allocation-free
// beyond what an API mandates, and returns within kRedXeActionExecuteBudgetMilliseconds: a launch (system.launch,
// system.open, system.run, system.taskManager) is validated and counted here and performed by the host's launch worker
// (LaunchWorker.h), never on the UI thread. With device access disabled (automated hosts) nothing is injected,
// launched, or changed: the action is validated and counted.

#include "LaunchWorker.h"
#include "PlugInterfaces/Action.h"
#include "PlugInterfaces/Host.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <windows.h>

namespace HostActions
{
// The main window, so `xeneon` monitor selectors resolve to the monitor RedXe sits on and the held-input timer has a
// target, and the host log that records a release SendInput refused. Both null before the window exists. A null
// window (the main window closes) first makes the last release attempt at anything still held, while the outgoing
// log can still record a refusal.
void SetHostWindow(HWND window, IRedXeHost* log) noexcept;

// Extra validation for default actions whose target grammar the descriptor cannot express completely
// (system.power.plan: a named plan or a GUID; keys.layout: a language tag or an 8-digit KLID). S_OK or E_INVALIDARG.
[[nodiscard]] HRESULT ValidateExtra(const RedXeActionDescriptor& descriptor, std::string_view target) noexcept;

// Executes one system, keys, or mouse action. Returns S_OK, E_INVALIDARG for a target the grammar accepted but the
// system rejected, or the Win32 failure. A launch returns S_FALSE once launches has queued it (its result is logged
// when the worker finishes), or the queue's refusal. deviceAccess false counts and performs nothing, and a launch then
// returns S_FALSE without touching launches, which may be null only then.
[[nodiscard]] HRESULT Execute(const RedXeActionDescriptor& descriptor, std::string_view target, bool deviceAccess,
                              LaunchWorker* launches) noexcept;

// Releases keys and buttons still held by keys.down / mouse.down. The main window calls OnHeldTimer for the
// one-shot deadline and for the retry of a release SendInput refused; closing the main window (SetHostWindow) and
// shutdown make one last attempt at anything still held. Only an `up` naming the held chord or button ends its hold;
// any other `up` injects its own release. An `up` matching a hold the deadline already released injects nothing and
// returns S_FALSE.
void ReleaseHeld(bool deviceAccess) noexcept;
void OnHeldTimer() noexcept;

inline constexpr uint32_t kHeldReleaseMilliseconds = 2000;
// A refused release (a UAC prompt or the lock screen owns the input desktop) keeps its hold and retries at this
// interval, at most kMaximumHeldReleaseAttempts attempts in all, then stops tracking it.
inline constexpr uint32_t kHeldReleaseRetryMilliseconds = 250;
inline constexpr uint32_t kMaximumHeldReleaseAttempts = 40;
inline constexpr UINT_PTR kHeldInputTimerId = 0x5C7;
inline constexpr uint32_t kMaximumInputBatch = 32;

// Counters for automated hosts and tests; every field counts since the last reset.
struct Counters final
{
    uint32_t executed = 0;
    uint32_t injectedInputs = 0;
    uint32_t launches = 0;
    uint32_t processes = 0;
    uint32_t powerRequests = 0;
    uint32_t heldReleases = 0;
    std::array<char, kRedXeMaximumActionNameBytes + 1> lastAction{};
};
[[nodiscard]] Counters CopyCounters() noexcept;
void ResetCounters() noexcept;

#if defined(REDXE_HOST_PLUGIN_TESTS)
// Test seam: every injection is counted and then fails with `failure`, even with device access disabled, until the
// next call passes S_OK.
void FailInjectionForTesting(HRESULT failure) noexcept;
#endif
} // namespace HostActions
