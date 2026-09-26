#pragma once

// Executes the host's default system, keys, and mouse actions on the UI thread (HostActionCatalog.h lists them;
// page, widget, and redxe run inside Application). Every call is synchronous, non-reentrant, allocation-free
// beyond what an API mandates, and returns within kRedXeActionExecuteBudgetMilliseconds. With device access
// disabled (automated hosts) nothing is injected, launched, or changed: the action is validated and counted.

#include "PlugInterfaces/Action.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <windows.h>

namespace HostActions
{
// The main window, so `xeneon` monitor selectors resolve to the monitor RedXe sits on. Null before the window exists.
void SetHostWindow(HWND window) noexcept;

// Extra validation for default actions whose target grammar the descriptor cannot express completely
// (system.power.plan: a named plan or a GUID; keys.layout: a language tag or an 8-digit KLID). S_OK or E_INVALIDARG.
[[nodiscard]] HRESULT ValidateExtra(const RedXeActionDescriptor& descriptor, std::string_view target) noexcept;

// Executes one system, keys, or mouse action. Returns S_OK, E_INVALIDARG for a target the grammar accepted but the
// system rejected, or the Win32 failure. deviceAccess false counts and performs nothing.
[[nodiscard]] HRESULT Execute(const RedXeActionDescriptor& descriptor, std::string_view target,
                              bool deviceAccess) noexcept;

// Releases keys and buttons still held by keys.down / mouse.down. The main window calls OnHeldTimer for the
// one-shot deadline; shutdown also releases anything still held.
void ReleaseHeld(bool deviceAccess) noexcept;
void OnHeldTimer() noexcept;

inline constexpr uint32_t kHeldReleaseMilliseconds = 2000;
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
} // namespace HostActions
