#pragma once

#include <windows.h>

#include <cstdint>
#include <processsnapshot.h>
#include <psapi.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace ProcessResourcesDetail
{
inline void FreeThreadSnapshot(HPSS snapshot) noexcept
{
    (void)PssFreeSnapshot(GetCurrentProcess(), snapshot);
}
using ThreadSnapshot = wil::unique_any<HPSS, decltype(&FreeThreadSnapshot), FreeThreadSnapshot>;
} // namespace ProcessResourcesDetail

struct ProcessMemorySnapshot final
{
    uint64_t privateBytes = 0;
    uint64_t workingSetBytes = 0;
    uint64_t peakCommitBytes = 0;
    uint64_t peakWorkingSetBytes = 0;
};

[[nodiscard]] inline HRESULT QueryProcessMemorySnapshot(ProcessMemorySnapshot& snapshot) noexcept
{
    snapshot = {};
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                 sizeof(counters)))
        return HRESULT_FROM_WIN32(GetLastError());
    snapshot = {counters.PrivateUsage, counters.WorkingSetSize, counters.PeakPagefileUsage,
                counters.PeakWorkingSetSize};
    return S_OK;
}

[[nodiscard]] inline HRESULT QueryProcessThreadCount(DWORD& count) noexcept
{
    count = 0;
    // Capture only this process's thread IDs, not every system thread, contexts, handles or a VA clone. Iterating
    // Toolhelp's all-system thread snapshot cost ~120 ms on the production baseline machine.
    ProcessResourcesDetail::ThreadSnapshot snapshot;
    const DWORD captured = PssCaptureSnapshot(GetCurrentProcess(), PSS_CAPTURE_THREADS, 0, snapshot.put());
    if (captured != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(captured);
    PSS_THREAD_INFORMATION information{};
    const DWORD queried =
        PssQuerySnapshot(snapshot.get(), PSS_QUERY_THREAD_INFORMATION, &information, sizeof(information));
    if (queried != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(queried);
    count = information.ThreadsCaptured;
    return S_OK;
}

// Existing host tests use zero as failure; production diagnostics additionally retain the validity bit.
[[nodiscard]] inline DWORD CountProcessThreads() noexcept
{
    DWORD count = 0;
    return SUCCEEDED(QueryProcessThreadCount(count)) ? count : 0;
}

struct ProcessResourceSnapshot final
{
    // Bits: memory, CPU time, handles, threads, modules, GDI objects, USER objects.
    uint32_t valid = 0;
    ProcessMemorySnapshot memory;
    uint64_t cpu100ns = 0;
    DWORD handles = 0, threads = 0, modules = 0, gdi = 0, user = 0;
};

[[nodiscard]] inline ProcessResourceSnapshot QueryProcessResources() noexcept
{
    ProcessResourceSnapshot result;
    if (SUCCEEDED(QueryProcessMemorySnapshot(result.memory)))
        result.valid |= 1;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    {
        result.cpu100ns = (static_cast<uint64_t>(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
                          (static_cast<uint64_t>(user.dwHighDateTime) << 32 | user.dwLowDateTime);
        result.valid |= 2;
    }
    if (GetProcessHandleCount(GetCurrentProcess(), &result.handles))
        result.valid |= 4;
    if (SUCCEEDED(QueryProcessThreadCount(result.threads)))
        result.valid |= 8;
    HMODULE firstModule = nullptr;
    DWORD moduleBytes = 0;
    // Only count modules: PSAPI returns the bytes needed for the entire list even when the supplied array is smaller.
    if (K32EnumProcessModules(GetCurrentProcess(), &firstModule, sizeof(firstModule), &moduleBytes) &&
        moduleBytes % sizeof(HMODULE) == 0)
    {
        result.modules = static_cast<DWORD>(moduleBytes / sizeof(HMODULE));
        result.valid |= 16;
    }
    SetLastError(ERROR_SUCCESS);
    result.gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    if (result.gdi != 0 || GetLastError() == ERROR_SUCCESS)
        result.valid |= 32;
    SetLastError(ERROR_SUCCESS);
    result.user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    if (result.user != 0 || GetLastError() == ERROR_SUCCESS)
        result.valid |= 64;
    return result;
}
