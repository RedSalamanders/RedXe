#include "SystemDataPhase0.h"
#include "../../Plugins/SystemData/NtLayout.h"
#include "PlugInterfaces/Data.h"
#include "PlugInterfaces/Factory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <psapi.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tlhelp32.h>
#include <vector>
#include <windows.h>
#include <winternl.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr NTSTATUS kStatusInfoLengthMismatch = static_cast<NTSTATUS>(0xC0000004L);
constexpr size_t kMaximumLogicalProcessors = 1024;
constexpr size_t kProcessBufferStartBytes = 1024 * 1024;
constexpr size_t kProcessBufferCapBytes = 8 * 1024 * 1024;
constexpr uint32_t kCheapIterations = 64;
constexpr uint32_t kWalkIterations = 16;
constexpr uint32_t kWarmupIterations = 8;

static_assert(sizeof(void*) == 8);
static_assert(sizeof(SYSTEM_HANDLECOUNT_INFORMATION) == 12);
static_assert(sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) == 48);
static_assert(sizeof(SYSTEM_BASIC_INFORMATION) == 64);
static_assert(sizeof(SYSTEM_PERFORMANCE_INFORMATION) == 312);
static_assert(sizeof(SYSTEM_TIMEOFDAY_INFORMATION) == 48);
static_assert(sizeof(PROCESS_BASIC_INFORMATION) == 48);
static_assert(sizeof(SYSTEM_PROCESS_INFORMATION) == 256);
static_assert(sizeof(SYSTEM_THREAD_INFORMATION) == 80);
// The RedXe overlays that name the SDK reserved blocks. NtLayout.h already anchors every consumed member offset to
// the matching SDK member; these repeat the sizes here so the Phase 0 record and the plugin cannot drift apart, and
// so the ARM64 configuration proves the same layouts at compile time before ARM64 hardware is measured.
static_assert(sizeof(RedXeNtProcessRecord) == sizeof(SYSTEM_PROCESS_INFORMATION));
static_assert(sizeof(RedXeNtThreadRecord) == sizeof(SYSTEM_THREAD_INFORMATION));
static_assert(sizeof(RedXeNtProcessorPerformance) == sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION));
static_assert(sizeof(RedXeNtTimeOfDay) == sizeof(SYSTEM_TIMEOFDAY_INFORMATION));
static_assert(sizeof(RedXeNtInterruptRecord) == sizeof(SYSTEM_INTERRUPT_INFORMATION));
static_assert(sizeof(RedXeNtExceptionRecord) == sizeof(SYSTEM_EXCEPTION_INFORMATION));
static_assert(kRedXeNtPerformanceBaseBytes == sizeof(SYSTEM_PERFORMANCE_INFORMATION));
static_assert(sizeof(RedXeNtPerformance) == kRedXeNtPerformance24H2Bytes);

using NtQuerySystemInformationFn = NTSTATUS(NTAPI*)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
using NtQueryInformationProcessFn = NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Function> [[nodiscard]] Function ResolveExport(HMODULE module, const char* name)
{
    const FARPROC procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    Expect(function != nullptr, "required export is missing");
    return function;
}

[[nodiscard]] double Microseconds(const LARGE_INTEGER& start, const LARGE_INTEGER& finish,
                                  const LARGE_INTEGER& frequency)
{
    return static_cast<double>(finish.QuadPart - start.QuadPart) * 1'000'000.0 /
           static_cast<double>(frequency.QuadPart);
}

[[nodiscard]] bool CountsWithinRacyTolerance(uint64_t left, uint64_t right) noexcept
{
    const uint64_t larger = (std::max)(left, right);
    const uint64_t smaller = (std::min)(left, right);
    if (larger == 0)
    {
        return smaller == 0;
    }
    if (smaller == 0)
    {
        return false;
    }
    return (larger - smaller) * 100 <= larger * 25;
}

[[nodiscard]] bool CumulativeWithinTolerance(uint64_t left, uint64_t right) noexcept
{
    const uint64_t larger = (std::max)(left, right);
    const uint64_t smaller = (std::min)(left, right);
    if (larger == 0)
    {
        return smaller == 0;
    }
    const uint64_t pageUnits = 100000; // 10 ms of 100 ns units; ignore tiny boot-time skew
    const uint64_t absolute = larger - smaller;
    if (absolute <= pageUnits)
    {
        return true;
    }
    return absolute * 1000 <= larger; // 0.1%
}

void ToLowerAscii(std::wstring& value)
{
    for (wchar_t& character : value)
    {
        if (character >= L'A' && character <= L'Z')
        {
            character = static_cast<wchar_t>(character - L'A' + L'a');
        }
    }
}

[[nodiscard]] std::vector<std::wstring> LoadedModuleNames()
{
    std::array<HMODULE, 512> modules{};
    DWORD bytes = 0;
    Expect(EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
                              &bytes) != FALSE,
           "module enumeration failed");
    const uint32_t count = bytes / static_cast<DWORD>(sizeof(HMODULE));
    Expect(count <= modules.size(), "loaded module count exceeded the bounded snapshot");

    std::vector<std::wstring> names;
    names.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        std::wstring name(MAX_PATH, L'\0');
        const DWORD length = GetModuleBaseNameW(GetCurrentProcess(), modules[index], name.data(), MAX_PATH);
        Expect(length != 0 && length < MAX_PATH, "module base name query failed");
        name.resize(length);
        ToLowerAscii(name);
        names.push_back(std::move(name));
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

void ExpectNoWmiDelta(const std::vector<std::wstring>& before, const std::vector<std::wstring>& after)
{
    constexpr std::array<std::wstring_view, 3> forbidden{L"wbemprox.dll", L"fastprox.dll", L"wbemcomn.dll"};
    for (const std::wstring_view name : forbidden)
    {
        const bool presentBefore = std::binary_search(before.begin(), before.end(), name);
        const bool presentAfter = std::binary_search(after.begin(), after.end(), name);
        Expect(!presentAfter || presentBefore, "System Data Phase 0 loaded a WMI provider module");
    }
}

[[nodiscard]] NTSTATUS QuerySystem(NtQuerySystemInformationFn query, SYSTEM_INFORMATION_CLASS informationClass,
                                   void* buffer, ULONG length, ULONG* returned)
{
    ULONG localReturned = 0;
    const NTSTATUS status = query(informationClass, buffer, length, &localReturned);
    if (returned)
    {
        *returned = localReturned;
    }
    return status;
}

struct ProcessWalkResult final
{
    uint32_t processCount = 0;
    uint64_t threadCount = 0;
    uint32_t bufferBytes = 0;
};

[[nodiscard]] ProcessWalkResult WalkProcesses(NtQuerySystemInformationFn query, std::vector<std::byte>& buffer)
{
    ULONG returned = 0;
    NTSTATUS status = kStatusInfoLengthMismatch;
    if (buffer.size() < kProcessBufferStartBytes)
    {
        buffer.resize(kProcessBufferStartBytes);
    }
    while (buffer.size() <= kProcessBufferCapBytes)
    {
        status =
            QuerySystem(query, SystemProcessInformation, buffer.data(), static_cast<ULONG>(buffer.size()), &returned);
        if (status >= 0)
        {
            break;
        }
        Expect(status == kStatusInfoLengthMismatch, "SystemProcessInformation failed with an unexpected status");
        const size_t next = buffer.size() * 2;
        Expect(next <= kProcessBufferCapBytes, "SystemProcessInformation exceeded the 8 MiB spike cap");
        buffer.resize(next);
    }
    Expect(status >= 0, "SystemProcessInformation did not succeed inside the bounded buffer");
    Expect(returned > 0 && returned <= buffer.size(), "SystemProcessInformation returned an invalid length");

    ProcessWalkResult result{};
    result.bufferBytes = returned;
    size_t offset = 0;
    for (;;)
    {
        Expect(offset + sizeof(SYSTEM_PROCESS_INFORMATION) <= returned, "process record overruns the native buffer");
        const auto* record = reinterpret_cast<const SYSTEM_PROCESS_INFORMATION*>(buffer.data() + offset);
        ++result.processCount;
        result.threadCount += record->NumberOfThreads;
        if (record->NextEntryOffset == 0)
        {
            break;
        }
        Expect(record->NextEntryOffset >= sizeof(SYSTEM_PROCESS_INFORMATION), "process NextEntryOffset is too small");
        Expect(offset + record->NextEntryOffset <= returned, "process NextEntryOffset overruns the native buffer");
        offset += record->NextEntryOffset;
    }
    return result;
}

struct ToolhelpCounts final
{
    uint32_t processCount = 0;
    uint32_t threadCount = 0;
};

[[nodiscard]] ToolhelpCounts SnapshotToolhelp()
{
    ToolhelpCounts counts{};
    wil::unique_hfile snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD, 0)};
    Expect(static_cast<bool>(snapshot), "Toolhelp snapshot failed");

    PROCESSENTRY32W processEntry{};
    processEntry.dwSize = static_cast<DWORD>(sizeof(processEntry));
    if (Process32FirstW(snapshot.get(), &processEntry) != FALSE)
    {
        do
        {
            ++counts.processCount;
        } while (Process32NextW(snapshot.get(), &processEntry) != FALSE);
    }

    THREADENTRY32 threadEntry{};
    threadEntry.dwSize = static_cast<DWORD>(sizeof(threadEntry));
    if (Thread32First(snapshot.get(), &threadEntry) != FALSE)
    {
        do
        {
            ++counts.threadCount;
        } while (Thread32Next(snapshot.get(), &threadEntry) != FALSE);
    }
    return counts;
}

[[nodiscard]] HRESULT CollectProductionSnapshot(IRedXeDataSource& source, const char* dataSetId,
                                                const RedXeDataSnapshot** snapshot)
{
    if (snapshot)
    {
        *snapshot = nullptr;
    }
    const char* ids[] = {dataSetId};
    const RedXeDataCollectRequest request{sizeof(RedXeDataCollectRequest), ids, 1};
    const RedXeDataCollectResult* result = nullptr;
    const HRESULT collected = source.CollectSnapshots(&request, &result);
    if (FAILED(collected))
    {
        return collected;
    }
    if (!result || result->snapshotCount != 1 || !result->snapshots || !result->snapshots[0])
    {
        return E_UNEXPECTED;
    }
    *snapshot = result->snapshots[0];
    return S_OK;
}

void MeasureProductionSnapshots()
{
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    Expect(length != 0 && length < executable.size(), "Phase 0 executable path is unavailable");
    executable.resize(length);
    const std::filesystem::path pluginPath =
        std::filesystem::path(executable).parent_path() / L"Plugins" / L"SystemData.dll";

    wil::unique_hmodule module{
        LoadLibraryExW(pluginPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    Expect(static_cast<bool>(module), "SystemData.dll could not be loaded for production timing");

    const RedXeCreateFn create = ResolveExport<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    void* sourceObject = nullptr;
    Expect(create(__uuidof(IRedXeDataSource), nullptr, nullptr, "builtin.system-data", &sourceObject) == S_OK &&
               sourceObject,
           "SystemData source creation failed");
    wil::com_ptr_nothrow<IRedXeDataSource> source;
    source.attach(static_cast<IRedXeDataSource*>(sourceObject));

    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    LARGE_INTEGER finish{};
    Expect(QueryPerformanceFrequency(&frequency) != FALSE, "QPC frequency failed");

    const RedXeDataSnapshot* snapshot = nullptr;
    Expect(QueryPerformanceCounter(&start) != FALSE, "QPC start failed");
    Expect(CollectProductionSnapshot(*source, "system.summary", &snapshot) == S_OK && snapshot,
           "system.summary collect failed");
    Expect(QueryPerformanceCounter(&finish) != FALSE, "QPC finish failed");
    std::wcout << L"prod_system.summary_us=" << Microseconds(start, finish, frequency) << L'\n';

    Expect(QueryPerformanceCounter(&start) != FALSE, "QPC start failed");
    Expect(CollectProductionSnapshot(*source, "process.list", &snapshot) == S_OK && snapshot,
           "process.list collect failed");
    Expect(QueryPerformanceCounter(&finish) != FALSE, "QPC finish failed");
    std::wcout << L"prod_process.list_us=" << Microseconds(start, finish, frequency) << L" rows=" << snapshot->rowCount
               << L'\n';
}

void Run()
{
    static_assert(sizeof(void*) == 8, "Phase 0 spike records 64-bit layouts");
    std::wcout << L"SystemData Phase 0 spike sizeof PROCESS_BASIC_INFORMATION=" << sizeof(PROCESS_BASIC_INFORMATION)
               << L" SYSTEM_PROCESS_INFORMATION=" << sizeof(SYSTEM_PROCESS_INFORMATION)
               << L" SYSTEM_THREAD_INFORMATION=" << sizeof(SYSTEM_THREAD_INFORMATION)
               << L" SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION=" << sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)
               << L" SYSTEM_HANDLECOUNT_INFORMATION=" << sizeof(SYSTEM_HANDLECOUNT_INFORMATION)
               << L" SYSTEM_BASIC_INFORMATION=" << sizeof(SYSTEM_BASIC_INFORMATION)
               << L" SYSTEM_PERFORMANCE_INFORMATION=" << sizeof(SYSTEM_PERFORMANCE_INFORMATION) << L'\n';

    const std::vector<std::wstring> modulesBefore = LoadedModuleNames();
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    Expect(ntdll != nullptr, "ntdll.dll is not mapped");
    const NtQuerySystemInformationFn querySystem =
        ResolveExport<NtQuerySystemInformationFn>(ntdll, "NtQuerySystemInformation");
    const NtQueryInformationProcessFn queryProcess =
        ResolveExport<NtQueryInformationProcessFn>(ntdll, "NtQueryInformationProcess");

    LARGE_INTEGER frequency{};
    Expect(QueryPerformanceFrequency(&frequency) != FALSE, "QPC frequency failed");

    SYSTEM_HANDLECOUNT_INFORMATION handleCounts{};
    ULONG returned = 0;
    Expect(QuerySystem(querySystem, SystemHandleCountInformation, &handleCounts,
                       static_cast<ULONG>(sizeof(handleCounts)), &returned) >= 0,
           "SystemHandleCountInformation failed");
    Expect(returned == sizeof(handleCounts), "SystemHandleCountInformation returned an unexpected size");

    PERFORMANCE_INFORMATION performance{};
    performance.cb = static_cast<DWORD>(sizeof(performance));
    Expect(K32GetPerformanceInfo(&performance, performance.cb) != FALSE, "K32GetPerformanceInfo failed");

    std::vector<std::byte> processBuffer;
    const ProcessWalkResult walk = WalkProcesses(querySystem, processBuffer);
    const ToolhelpCounts toolhelp = SnapshotToolhelp();

    std::wcout << L"cheap_handlecount process=" << handleCounts.ProcessCount << L" thread=" << handleCounts.ThreadCount
               << L" handle=" << handleCounts.HandleCount << L'\n';
    std::wcout << L"k32 process=" << performance.ProcessCount << L" thread=" << performance.ThreadCount << L" handle="
               << performance.HandleCount << L'\n';
    std::wcout << L"walk process=" << walk.processCount << L" thread=" << walk.threadCount << L" buffer_bytes="
               << walk.bufferBytes << L'\n';
    std::wcout << L"toolhelp process=" << toolhelp.processCount << L" thread=" << toolhelp.threadCount << L'\n';

    Expect(CountsWithinRacyTolerance(handleCounts.ProcessCount, performance.ProcessCount),
           "process count disagrees between SystemHandleCountInformation and K32GetPerformanceInfo");
    Expect(CountsWithinRacyTolerance(handleCounts.ThreadCount, performance.ThreadCount),
           "thread count disagrees between SystemHandleCountInformation and K32GetPerformanceInfo");
    Expect(CountsWithinRacyTolerance(handleCounts.HandleCount, performance.HandleCount),
           "handle count disagrees between SystemHandleCountInformation and K32GetPerformanceInfo");
    Expect(CountsWithinRacyTolerance(handleCounts.ProcessCount, walk.processCount),
           "process count disagrees between SystemHandleCountInformation and SystemProcessInformation");
    Expect(CountsWithinRacyTolerance(handleCounts.ThreadCount, walk.threadCount),
           "thread count disagrees between SystemHandleCountInformation and SystemProcessInformation");
    Expect(CountsWithinRacyTolerance(handleCounts.ProcessCount, toolhelp.processCount),
           "process count disagrees between SystemHandleCountInformation and Toolhelp");
    Expect(CountsWithinRacyTolerance(handleCounts.ThreadCount, toolhelp.threadCount),
           "thread count disagrees between SystemHandleCountInformation and Toolhelp");

    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION processors[kMaximumLogicalProcessors]{};
    const DWORD activeProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    Expect(activeProcessors != 0 && activeProcessors <= kMaximumLogicalProcessors, "active processor count is invalid");
    returned = 0;
    Expect(QuerySystem(querySystem, SystemProcessorPerformanceInformation, processors,
                       static_cast<ULONG>(sizeof(processors[0]) * activeProcessors), &returned) >= 0,
           "SystemProcessorPerformanceInformation failed");
    Expect(returned == sizeof(processors[0]) * activeProcessors,
           "SystemProcessorPerformanceInformation length does not match the active processor count");

    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    Expect(GetSystemTimes(&idleTime, &kernelTime, &userTime) != FALSE, "GetSystemTimes failed");
    ULARGE_INTEGER idle{};
    ULARGE_INTEGER kernel{};
    ULARGE_INTEGER user{};
    idle.LowPart = idleTime.dwLowDateTime;
    idle.HighPart = idleTime.dwHighDateTime;
    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = kernelTime.dwHighDateTime;
    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = userTime.dwHighDateTime;

    uint64_t nativeIdle = 0;
    uint64_t nativeKernel = 0;
    uint64_t nativeUser = 0;
    for (DWORD index = 0; index < activeProcessors; ++index)
    {
        nativeIdle += static_cast<uint64_t>(processors[index].IdleTime.QuadPart);
        nativeKernel += static_cast<uint64_t>(processors[index].KernelTime.QuadPart);
        nativeUser += static_cast<uint64_t>(processors[index].UserTime.QuadPart);
    }
    std::wcout << L"cpu active_logical=" << activeProcessors << L" native_idle=" << nativeIdle << L" native_kernel="
               << nativeKernel << L" native_user=" << nativeUser << L" gst_idle=" << idle.QuadPart << L" gst_kernel="
               << kernel.QuadPart << L" gst_user=" << user.QuadPart << L'\n';
    Expect(CumulativeWithinTolerance(nativeIdle, idle.QuadPart), "idle time disagrees with GetSystemTimes");
    Expect(CumulativeWithinTolerance(nativeKernel, kernel.QuadPart), "kernel time disagrees with GetSystemTimes");
    Expect(CumulativeWithinTolerance(nativeUser, user.QuadPart), "user time disagrees with GetSystemTimes");

    SYSTEM_BASIC_INFORMATION basic{};
    returned = 0;
    Expect(QuerySystem(querySystem, SystemBasicInformation, &basic, static_cast<ULONG>(sizeof(basic)), &returned) >= 0,
           "SystemBasicInformation failed");
    std::wcout << L"basic returned=" << returned << L" NumberOfProcessors="
               << static_cast<int>(basic.NumberOfProcessors) << L'\n';
    Expect(basic.NumberOfProcessors > 0, "SystemBasicInformation reported zero processors");
    if (activeProcessors <= 127)
    {
        Expect(static_cast<DWORD>(basic.NumberOfProcessors) == activeProcessors,
               "SystemBasicInformation.NumberOfProcessors does not match GetActiveProcessorCount");
    }

    // The earlier record of "SystemPerformanceInformation returns 312 bytes" was an artifact of passing
    // sizeof(SYSTEM_PERFORMANCE_INFORMATION) as the length: the SDK struct is only the reserved prefix, so the kernel
    // could never report more. Probing with an oversized buffer is what actually measures the record.
    std::array<std::byte, 1024> performanceProbe{};
    returned = 0;
    const NTSTATUS performanceStatus = QuerySystem(querySystem, SystemPerformanceInformation, performanceProbe.data(),
                                                   static_cast<ULONG>(performanceProbe.size()), &returned);
    std::wcout << L"SystemPerformanceInformation status=" << performanceStatus << L" probe_bytes="
               << performanceProbe.size() << L" measured_return_length=" << returned << L" sdk_sizeof="
               << sizeof(SYSTEM_PERFORMANCE_INFORMATION) << L" redxe_overlay_sizeof=" << sizeof(RedXeNtPerformance)
               << L" band=";
    if (returned >= kRedXeNtPerformance24H2Bytes)
    {
        std::wcout << L"24h2\n";
    }
    else if (returned >= kRedXeNtPerformanceThresholdBytes)
    {
        std::wcout << L"threshold\n";
    }
    else if (returned >= kRedXeNtPerformanceBaseBytes)
    {
        std::wcout << L"base\n";
    }
    else
    {
        std::wcout << L"short\n";
    }
    Expect(performanceStatus >= 0 && returned >= kRedXeNtPerformanceBaseBytes,
           "SystemPerformanceInformation did not return at least the base band");

    // Short-buffer fixture: one byte under the SDK reserved block must be rejected, never silently truncated.
    ULONG shortReturned = 0;
    const NTSTATUS shortStatus =
        QuerySystem(querySystem, SystemPerformanceInformation, performanceProbe.data(),
                    static_cast<ULONG>(sizeof(SYSTEM_PERFORMANCE_INFORMATION) - 1), &shortReturned);
    std::wcout << L"SystemPerformanceInformation short_buffer_status=" << shortStatus << L" returned=" << shortReturned
               << L'\n';
    Expect(shortStatus < 0 || shortReturned < sizeof(SYSTEM_PERFORMANCE_INFORMATION),
           "a short SystemPerformanceInformation buffer reported a full-length result");

    SYSTEM_TIMEOFDAY_INFORMATION timeOfDay{};
    returned = 0;
    Expect(QuerySystem(querySystem, SystemTimeOfDayInformation, &timeOfDay, static_cast<ULONG>(sizeof(timeOfDay)),
                       &returned) >= 0,
           "SystemTimeOfDayInformation failed");
    std::wcout << L"SystemTimeOfDayInformation returned=" << returned << L" sdk_sizeof=" << sizeof(timeOfDay)
               << L" redxe_overlay_sizeof=" << sizeof(RedXeNtTimeOfDay) << L'\n';
    Expect(returned == sizeof(RedXeNtTimeOfDay),
           "SystemTimeOfDayInformation return length does not match the RedXe overlay");

    // The remaining fixed-size classes are fully covered by their SDK reserved blocks, so an exact-length return is
    // a complete version gate for each.
    std::array<std::byte, 256> interruptProbe{};
    ULONG interruptReturned = 0;
    const ULONG interruptLength =
        static_cast<ULONG>(sizeof(RedXeNtInterruptRecord) * (activeProcessors == 0 ? 1 : activeProcessors));
    const NTSTATUS interruptStatus = interruptLength <= interruptProbe.size()
                                         ? QuerySystem(querySystem, SystemInterruptInformation, interruptProbe.data(),
                                                       interruptLength, &interruptReturned)
                                         : static_cast<NTSTATUS>(0);
    std::wcout << L"SystemInterruptInformation status=" << interruptStatus << L" returned=" << interruptReturned
               << L" record_sizeof=" << sizeof(RedXeNtInterruptRecord) << L" processors=" << activeProcessors << L'\n';
    Expect(interruptReturned == 0 || interruptReturned % sizeof(RedXeNtInterruptRecord) == 0,
           "SystemInterruptInformation return length is not a whole number of records");

    RedXeNtExceptionRecord exceptions{};
    ULONG exceptionReturned = 0;
    const NTSTATUS exceptionStatus = QuerySystem(querySystem, SystemExceptionInformation, &exceptions,
                                                 static_cast<ULONG>(sizeof(exceptions)), &exceptionReturned);
    std::wcout << L"SystemExceptionInformation status=" << exceptionStatus << L" returned=" << exceptionReturned
               << L" record_sizeof=" << sizeof(RedXeNtExceptionRecord) << L'\n';

    // A length that is not a whole number of per-processor records must be rejected rather than partially filled.
    // This is the failure that made the first interrupt and processor-time queries silently return nothing.
    ULONG raggedReturned = 0;
    const NTSTATUS raggedStatus =
        QuerySystem(querySystem, SystemProcessorPerformanceInformation, performanceProbe.data(),
                    static_cast<ULONG>(sizeof(RedXeNtProcessorPerformance) + 1), &raggedReturned);
    std::wcout << L"SystemProcessorPerformanceInformation ragged_length_status=" << raggedStatus << L" returned="
               << raggedReturned << L'\n';

    std::vector<std::byte> basicProcessBuffer(kProcessBufferStartBytes);
    ULONG basicReturned = 0;
    NTSTATUS basicProcessStatus = kStatusInfoLengthMismatch;
    while (basicProcessBuffer.size() <= kProcessBufferCapBytes)
    {
        basicProcessStatus = QuerySystem(querySystem, SystemBasicProcessInformation, basicProcessBuffer.data(),
                                         static_cast<ULONG>(basicProcessBuffer.size()), &basicReturned);
        if (basicProcessStatus >= 0 || basicProcessStatus != kStatusInfoLengthMismatch)
        {
            break;
        }
        const size_t next = basicProcessBuffer.size() * 2;
        if (next > kProcessBufferCapBytes)
        {
            break;
        }
        basicProcessBuffer.resize(next);
    }
    uint32_t basicProcessCount = 0;
    if (basicProcessStatus >= 0 && basicReturned >= sizeof(SYSTEM_BASICPROCESS_INFORMATION))
    {
        size_t offset = 0;
        for (;;)
        {
            Expect(offset + sizeof(SYSTEM_BASICPROCESS_INFORMATION) <= basicReturned,
                   "basic process record overruns the native buffer");
            const auto* record =
                reinterpret_cast<const SYSTEM_BASICPROCESS_INFORMATION*>(basicProcessBuffer.data() + offset);
            ++basicProcessCount;
            if (record->NextEntryOffset == 0)
            {
                break;
            }
            Expect(offset + record->NextEntryOffset <= basicReturned,
                   "basic process NextEntryOffset overruns the buffer");
            offset += record->NextEntryOffset;
        }
    }
    std::wcout << L"SystemBasicProcessInformation status=" << basicProcessStatus << L" returned=" << basicReturned
               << L" processes=" << basicProcessCount << L'\n';
    if (basicProcessStatus >= 0)
    {
        Expect(CountsWithinRacyTolerance(basicProcessCount, walk.processCount),
               "SystemBasicProcessInformation process count disagrees with SystemProcessInformation");
    }

    PROCESS_BASIC_INFORMATION processBasic{};
    returned = 0;
    Expect(queryProcess(GetCurrentProcess(), ProcessBasicInformation, &processBasic,
                        static_cast<ULONG>(sizeof(processBasic)), &returned) >= 0,
           "ProcessBasicInformation failed");
    Expect(returned == sizeof(processBasic), "ProcessBasicInformation returned an unexpected size");
    Expect(processBasic.UniqueProcessId == GetCurrentProcessId(), "ProcessBasicInformation PID does not match");
    Expect(processBasic.PebBaseAddress != nullptr, "ProcessBasicInformation PEB pointer was null");

    ULONG_PTR wow64 = 0;
    returned = 0;
    Expect(queryProcess(GetCurrentProcess(), ProcessWow64Information, &wow64, static_cast<ULONG>(sizeof(wow64)),
                        &returned) >= 0,
           "ProcessWow64Information failed");
    ULONG breakOnTermination = 0;
    returned = 0;
    Expect(queryProcess(GetCurrentProcess(), ProcessBreakOnTermination, &breakOnTermination,
                        static_cast<ULONG>(sizeof(breakOnTermination)), &returned) >= 0,
           "ProcessBreakOnTermination failed");
    std::wcout << L"self wow64=" << wow64 << L" break_on_termination=" << breakOnTermination << L'\n';

    uint32_t queriedProcesses = 0;
    uint32_t deniedProcesses = 0;
    wil::unique_hfile processSnapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    Expect(static_cast<bool>(processSnapshot), "process Toolhelp snapshot failed");
    PROCESSENTRY32W entry{};
    entry.dwSize = static_cast<DWORD>(sizeof(entry));
    LARGE_INTEGER qipStart{};
    LARGE_INTEGER qipFinish{};
    Expect(QueryPerformanceCounter(&qipStart) != FALSE, "QPC start failed");
    if (Process32FirstW(processSnapshot.get(), &entry) != FALSE)
    {
        do
        {
            wil::unique_handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
            if (!process)
            {
                ++deniedProcesses;
                continue;
            }
            PROCESS_BASIC_INFORMATION info{};
            ULONG_PTR wow = 0;
            ULONG critical = 0;
            if (queryProcess(process.get(), ProcessBasicInformation, &info, static_cast<ULONG>(sizeof(info)), nullptr) <
                    0 ||
                queryProcess(process.get(), ProcessWow64Information, &wow, static_cast<ULONG>(sizeof(wow)), nullptr) <
                    0 ||
                queryProcess(process.get(), ProcessBreakOnTermination, &critical, static_cast<ULONG>(sizeof(critical)),
                             nullptr) < 0)
            {
                ++deniedProcesses;
                continue;
            }
            ++queriedProcesses;
        } while (Process32NextW(processSnapshot.get(), &entry) != FALSE);
    }
    Expect(QueryPerformanceCounter(&qipFinish) != FALSE, "QPC finish failed");
    std::wcout << L"qip_live queried=" << queriedProcesses << L" denied=" << deniedProcesses << L" us="
               << Microseconds(qipStart, qipFinish, frequency) << L'\n';

    SYSTEM_HANDLECOUNT_INFORMATION ignoreHandles{};
    for (uint32_t warmup = 0; warmup < kWarmupIterations; ++warmup)
    {
        Expect(QuerySystem(querySystem, SystemHandleCountInformation, &ignoreHandles,
                           static_cast<ULONG>(sizeof(ignoreHandles)), nullptr) >= 0,
               "cheap warmup failed");
        (void)WalkProcesses(querySystem, processBuffer);
    }

    LARGE_INTEGER cheapStart{};
    LARGE_INTEGER cheapFinish{};
    Expect(QueryPerformanceCounter(&cheapStart) != FALSE, "QPC start failed");
    for (uint32_t iteration = 0; iteration < kCheapIterations; ++iteration)
    {
        Expect(QuerySystem(querySystem, SystemHandleCountInformation, &ignoreHandles,
                           static_cast<ULONG>(sizeof(ignoreHandles)), nullptr) >= 0,
               "cheap sample failed");
        Expect(QuerySystem(querySystem, SystemProcessorPerformanceInformation, processors,
                           static_cast<ULONG>(sizeof(processors[0]) * activeProcessors), nullptr) >= 0,
               "cpu sample failed");
        PERFORMANCE_INFORMATION k32{};
        k32.cb = static_cast<DWORD>(sizeof(k32));
        Expect(K32GetPerformanceInfo(&k32, k32.cb) != FALSE, "K32 sample failed");
    }
    Expect(QueryPerformanceCounter(&cheapFinish) != FALSE, "QPC finish failed");

    LARGE_INTEGER walkStart{};
    LARGE_INTEGER walkFinish{};
    Expect(QueryPerformanceCounter(&walkStart) != FALSE, "QPC start failed");
    for (uint32_t iteration = 0; iteration < kWalkIterations; ++iteration)
    {
        (void)WalkProcesses(querySystem, processBuffer);
    }
    Expect(QueryPerformanceCounter(&walkFinish) != FALSE, "QPC finish failed");

    const double cheapUs = Microseconds(cheapStart, cheapFinish, frequency) / kCheapIterations;
    const double walkUs = Microseconds(walkStart, walkFinish, frequency) / kWalkIterations;
    std::wcout << L"cheap_cpu_mem_totals_us=" << cheapUs << L" process_walk_us=" << walkUs << L" ratio_walk_over_cheap="
               << (cheapUs > 0.0 ? walkUs / cheapUs : 0.0) << L'\n';
    Expect(cheapUs < walkUs, "cheap totals path was not cheaper than SystemProcessInformation");

    MeasureProductionSnapshots();
    SpikePhase0Surfaces();
    ExpectNoWmiDelta(modulesBefore, LoadedModuleNames());
    std::wcout << L"gate_cheap_totals=keep\n";
    std::wcout << L"gate_nt_cpu=keep\n";
    std::wcout << L"System data Phase 0 spikes passed.\n";
}
} // namespace

int wmain()
{
    try
    {
        Run();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "System data Phase 0 spikes failed: " << error.what() << '\n';
        return 1;
    }
}
