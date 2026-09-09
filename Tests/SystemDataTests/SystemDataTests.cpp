#include "../../Plugins/SystemData/SystemDataTestContract.h"
#include "PlugInterfaces/Data.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Widget.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <new>
#include <psapi.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tlhelp32.h>
#include <type_traits>
#include <utility>
#include <vector>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
static_assert(std::is_base_of_v<IUnknown, IRedXeDataSource>);
static_assert(!std::is_base_of_v<IRedXeWidget, IRedXeDataSource>);

// Mirrors kRedXeDeviceClassNpu in the plugin. Declared here rather than shared so the test asserts against the
// published contract value, not against whatever the implementation happens to define.
constexpr uint64_t kTestDeviceClassNpu = 2;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Function> [[nodiscard]] Function Resolve(HMODULE module, const char* name)
{
    const FARPROC procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    Expect(function != nullptr, "required SystemData export is missing");
    return function;
}

[[nodiscard]] std::filesystem::path PluginPath()
{
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    Expect(length != 0 && length < executable.size(), "test executable path is unavailable");
    executable.resize(length);
    return std::filesystem::path(executable).parent_path() / L"Plugins" / L"SystemData.dll";
}

[[nodiscard]] const RedXeDataSetDescriptor* FindDataSet(const RedXeDataSetDescriptor* descriptors, uint32_t count,
                                                        const char* id)
{
    for (uint32_t index = 0; index < count; ++index)
    {
        if (descriptors[index].dataSetId && RedXeAsciiEqualsIgnoreCase(descriptors[index].dataSetId, id))
        {
            return &descriptors[index];
        }
    }
    return nullptr;
}

[[nodiscard]] uint32_t FindColumn(const RedXeDataSetDescriptor& descriptor, const char* id)
{
    for (uint32_t index = 0; index < descriptor.columnCount; ++index)
    {
        if (descriptor.columns[index].columnId && RedXeAsciiEqualsIgnoreCase(descriptor.columns[index].columnId, id))
        {
            return index;
        }
    }
    throw std::runtime_error("required data column is missing");
}

[[nodiscard]] HRESULT CollectOneSnapshot(IRedXeDataSource& source, const char* dataSetId,
                                         const RedXeDataSnapshot** snapshot)
{
    if (snapshot)
    {
        *snapshot = nullptr;
    }
    if (!snapshot)
    {
        return E_POINTER;
    }
    const char* ids[] = {dataSetId};
    const RedXeDataCollectRequest request{sizeof(RedXeDataCollectRequest), ids, 1};
    const RedXeDataCollectResult* result = nullptr;
    const HRESULT collected = source.CollectSnapshots(&request, &result);
    if (FAILED(collected))
    {
        return collected;
    }
    if (!result || result->sizeBytes != sizeof(RedXeDataCollectResult) || result->snapshotCount != 1 ||
        !result->snapshots || !result->snapshots[0])
    {
        return E_UNEXPECTED;
    }
    *snapshot = result->snapshots[0];
    return S_OK;
}

void ValidateSnapshot(const RedXeDataSnapshot& snapshot, const RedXeDataSetDescriptor& descriptor)
{
    Expect(snapshot.sizeBytes == sizeof(RedXeDataSnapshot), "snapshot record size is invalid");
    Expect(snapshot.dataSetId && RedXeAsciiEqualsIgnoreCase(snapshot.dataSetId, descriptor.dataSetId),
           "snapshot dataset identity is wrong");
    Expect(snapshot.sequence != 0 && snapshot.timestampFileTime100ns != 0, "snapshot lacks sequence or timestamp");
    Expect(snapshot.rowCount <= descriptor.maximumRows, "snapshot exceeds its declared row bound");
    Expect(snapshot.columnCount == descriptor.columnCount, "snapshot column count differs from its descriptor");
    Expect(snapshot.rowCount == 0 || snapshot.rows, "non-empty snapshot has no rows");

    for (uint32_t rowIndex = 0; rowIndex < snapshot.rowCount; ++rowIndex)
    {
        const RedXeDataRow& row = snapshot.rows[rowIndex];
        Expect(row.sizeBytes == sizeof(RedXeDataRow), "row record size is invalid");
        Expect(row.values && row.valueCount == descriptor.columnCount, "row has the wrong value shape");
        for (uint32_t valueIndex = 0; valueIndex < row.valueCount; ++valueIndex)
        {
            const RedXeDataValue& value = row.values[valueIndex];
            Expect(value.sizeBytes == sizeof(RedXeDataValue), "value record size is invalid");
            Expect(value.valueType == descriptor.columns[valueIndex].valueType, "value type differs from descriptor");
            Expect(value.quality <= RedXeDataQualityInitializing, "value quality is outside the contract");
            if (value.valueType == RedXeDataValueTypeFloat64)
            {
                Expect(std::isfinite(value.float64Value), "floating-point value is not finite");
                if (value.quality == RedXeDataQualityGood &&
                    (std::strstr(descriptor.columns[valueIndex].columnId, "Percent") ||
                     std::strstr(descriptor.columns[valueIndex].columnId, "percent") ||
                     std::strstr(descriptor.columns[valueIndex].columnId, "Cpu") ||
                     std::strstr(descriptor.columns[valueIndex].columnId, "cpu")))
                {
                    Expect(value.float64Value >= 0.0 && value.float64Value <= 100.0, "percentage is outside 0-100");
                }
            }
            if (value.valueType == RedXeDataValueTypeUtf16 && value.quality == RedXeDataQualityGood)
            {
                Expect(value.utf16Value != nullptr, "good UTF-16 value has no storage");
            }
        }
    }
}

[[nodiscard]] bool ContainsAsciiIgnoreCase(const char* value, const char* needle)
{
    if (!value || !needle || needle[0] == '\0')
    {
        return false;
    }
    const size_t needleLength = std::strlen(needle);
    const size_t valueLength = std::strlen(value);
    if (needleLength > valueLength)
    {
        return false;
    }
    for (size_t start = 0; start + needleLength <= valueLength; ++start)
    {
        bool match = true;
        for (size_t index = 0; index < needleLength; ++index)
        {
            const unsigned char left = static_cast<unsigned char>(value[start + index]);
            const unsigned char right = static_cast<unsigned char>(needle[index]);
            const unsigned char leftLower =
                static_cast<unsigned char>(left >= 'A' && left <= 'Z' ? left - 'A' + 'a' : left);
            const unsigned char rightLower =
                static_cast<unsigned char>(right >= 'A' && right <= 'Z' ? right - 'A' + 'a' : right);
            if (leftLower != rightLower)
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return true;
        }
    }
    return false;
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
    Expect(K32EnumProcessModules(GetCurrentProcess(), modules.data(),
                                 static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &bytes) != FALSE,
           "module enumeration failed");
    const uint32_t count = bytes / static_cast<DWORD>(sizeof(HMODULE));
    Expect(count <= modules.size(), "loaded module count exceeded the bounded snapshot");

    std::vector<std::wstring> names;
    names.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        std::wstring name(MAX_PATH, L'\0');
        const DWORD length = K32GetModuleBaseNameW(GetCurrentProcess(), modules[index], name.data(), MAX_PATH);
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
        const bool presentBefore = std::binary_search(before.begin(), before.end(), std::wstring(name));
        const bool presentAfter = std::binary_search(after.begin(), after.end(), std::wstring(name));
        Expect(!presentAfter || presentBefore, "System Data loaded a WMI provider module");
    }
}

void RunDomainMeasurement(IRedXeDataSource& source, const RedXeDataSetDescriptor* descriptors, uint32_t count)
{
    Expect(descriptors && count != 0 && count <= RedXeDataCollectMaximumDataSets,
           "domain measurement received an invalid catalog");
    std::array<const char*, RedXeDataCollectMaximumDataSets> ids{};
    for (uint32_t index = 0; index < count; ++index)
    {
        ids[index] = descriptors[index].dataSetId;
        const RedXeDataSnapshot* ignored = nullptr;
        Expect(CollectOneSnapshot(source, ids[index], &ignored) == S_OK && ignored,
               "domain measurement warm-up collection failed");
    }

    LARGE_INTEGER frequency{};
    Expect(QueryPerformanceFrequency(&frequency) != FALSE && frequency.QuadPart != 0,
           "domain measurement timer is unavailable");
    constexpr uint32_t sampleCount = 8;
    for (uint32_t index = 0; index < count; ++index)
    {
        LARGE_INTEGER start{};
        LARGE_INTEGER finish{};
        Expect(QueryPerformanceCounter(&start) != FALSE, "domain measurement start failed");
        const RedXeDataSnapshot* snapshot = nullptr;
        for (uint32_t sample = 0; sample < sampleCount; ++sample)
        {
            Expect(CollectOneSnapshot(source, ids[index], &snapshot) == S_OK && snapshot,
                   "timed domain collection failed");
        }
        Expect(QueryPerformanceCounter(&finish) != FALSE, "domain measurement finish failed");
        const double microseconds = static_cast<double>(finish.QuadPart - start.QuadPart) * 1'000'000.0 /
                                    static_cast<double>(frequency.QuadPart) / static_cast<double>(sampleCount);
        Expect(microseconds < 5'000'000.0, "domain collection exceeded the hang budget");
        std::wcout << L"SystemData domain measurement: id=";
        if (ids[index])
        {
            for (const char* cursor = ids[index]; *cursor != '\0'; ++cursor)
            {
                std::wcout << static_cast<wchar_t>(*cursor);
            }
        }
        std::wcout << L" rows=" << (snapshot ? snapshot->rowCount : 0) << L" wall_us_per_collection=" << microseconds
                   << L'\n';
    }

    LARGE_INTEGER batchStart{};
    LARGE_INTEGER batchFinish{};
    const RedXeDataCollectRequest batchRequest{sizeof(RedXeDataCollectRequest), ids.data(), count};
    const RedXeDataCollectResult* batchResult = nullptr;
    Expect(QueryPerformanceCounter(&batchStart) != FALSE, "full-catalog measurement start failed");
    Expect(source.CollectSnapshots(&batchRequest, &batchResult) == S_OK && batchResult &&
               batchResult->snapshotCount == count,
           "full-catalog batch collection failed");
    Expect(QueryPerformanceCounter(&batchFinish) != FALSE, "full-catalog measurement finish failed");
    const double batchMicroseconds = static_cast<double>(batchFinish.QuadPart - batchStart.QuadPart) * 1'000'000.0 /
                                     static_cast<double>(frequency.QuadPart);
    Expect(batchMicroseconds < 5'000'000.0, "full-catalog batch exceeded the hang budget");
    std::wcout << L"SystemData domain measurement: id=ALL rows=" << count << L" wall_us_per_collection="
               << batchMicroseconds << L'\n';
}

struct ProcessMemorySnapshot final
{
    uint64_t privateBytes = 0;
    uint64_t workingSetBytes = 0;
};

[[nodiscard]] ProcessMemorySnapshot QueryProcessMemory()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = static_cast<DWORD>(sizeof(counters));
    Expect(K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                   static_cast<DWORD>(sizeof(counters))) != FALSE,
           "process memory measurement failed");
    return ProcessMemorySnapshot{counters.PrivateUsage, counters.WorkingSetSize};
}

struct HeapSnapshot final
{
    uint64_t busyBlocks = 0;
    uint64_t busyBytes = 0;
};

[[nodiscard]] HeapSnapshot QueryHeapSnapshot()
{
    const DWORD heapCount = GetProcessHeaps(0, nullptr);
    Expect(heapCount != 0, "process heap enumeration failed");
    std::vector<HANDLE> heaps(heapCount);
    const DWORD actualCount = GetProcessHeaps(heapCount, heaps.data());
    Expect(actualCount != 0 && actualCount <= heapCount, "process heap enumeration changed unexpectedly");

    HeapSnapshot result{};
    for (DWORD index = 0; index < actualCount; ++index)
    {
        Expect(HeapLock(heaps[index]) != FALSE, "process heap lock failed");
        PROCESS_HEAP_ENTRY entry{};
        SetLastError(ERROR_SUCCESS);
        while (HeapWalk(heaps[index], &entry))
        {
            if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0)
            {
                ++result.busyBlocks;
                result.busyBytes += entry.cbData;
            }
        }
        const DWORD walkError = GetLastError();
        const BOOL unlocked = HeapUnlock(heaps[index]);
        Expect(unlocked != FALSE && walkError == ERROR_NO_MORE_ITEMS, "process heap walk failed");
    }
    return result;
}

[[nodiscard]] uint64_t FileTime100ns(const FILETIME& value) noexcept
{
    ULARGE_INTEGER combined{};
    combined.LowPart = value.dwLowDateTime;
    combined.HighPart = value.dwHighDateTime;
    return combined.QuadPart;
}

void RunResourceBenchmark(IRedXeSystemDataTestSource& testSource, const RedXeDataSetDescriptor& processDescriptor,
                          const SystemDataTestDiagnostics& diagnostics)
{
    Expect(diagnostics.sourceStorageBytes < 16ULL * 1024ULL * 1024ULL, "SystemData source storage exceeds 16 MiB");
    Expect(diagnostics.maximumProcessRows == processDescriptor.maximumRows,
           "test diagnostics disagree with the process row bound");
    Expect(diagnostics.sourceOwnedWorkerCount == 0 && diagnostics.sourceOwnedTimerCount == 0,
           "SystemData source unexpectedly owns a worker or timer");

    const RedXeDataSnapshot* snapshot = nullptr;
    Expect(testSource.CollectSyntheticProcessSnapshot(diagnostics.maximumProcessRows, &snapshot) == S_OK && snapshot,
           "row-cap warm-up collection failed");
    ValidateSnapshot(*snapshot, processDescriptor);
    Expect(snapshot->rowCount == diagnostics.maximumProcessRows &&
               (snapshot->flags & RedXeDataSnapshotFlagTruncated) == 0,
           "row-cap warm-up did not fill the bounded process table");
    Expect(testSource.CollectSyntheticProcessSnapshot(diagnostics.maximumProcessRows, &snapshot) == S_OK && snapshot,
           "second row-cap warm-up collection failed");

    const HeapSnapshot heapBefore = QueryHeapSnapshot();
    const ProcessMemorySnapshot memoryBefore = QueryProcessMemory();
    DWORD handlesBefore = 0;
    Expect(GetProcessHandleCount(GetCurrentProcess(), &handlesBefore) != FALSE, "pre-measurement handle query failed");
    FILETIME creationBefore{};
    FILETIME exitBefore{};
    FILETIME kernelBefore{};
    FILETIME userBefore{};
    Expect(GetProcessTimes(GetCurrentProcess(), &creationBefore, &exitBefore, &kernelBefore, &userBefore) != FALSE,
           "pre-measurement CPU query failed");
    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    LARGE_INTEGER finish{};
    Expect(QueryPerformanceFrequency(&frequency) != FALSE && QueryPerformanceCounter(&start) != FALSE,
           "high-resolution timer initialization failed");

    constexpr uint32_t collectionCount = 2;
    for (uint32_t collection = 0; collection < collectionCount; ++collection)
    {
        Expect(testSource.CollectSyntheticProcessSnapshot(diagnostics.maximumProcessRows, &snapshot) == S_OK &&
                   snapshot,
               "measured row-cap collection failed");
        ValidateSnapshot(*snapshot, processDescriptor);
        Expect(snapshot->rowCount == diagnostics.maximumProcessRows, "measured collection missed the process row cap");
    }
    Expect(QueryPerformanceCounter(&finish) != FALSE, "high-resolution timer completion failed");

    FILETIME creationAfter{};
    FILETIME exitAfter{};
    FILETIME kernelAfter{};
    FILETIME userAfter{};
    Expect(GetProcessTimes(GetCurrentProcess(), &creationAfter, &exitAfter, &kernelAfter, &userAfter) != FALSE,
           "post-measurement CPU query failed");
    DWORD handlesAfter = 0;
    Expect(GetProcessHandleCount(GetCurrentProcess(), &handlesAfter) != FALSE, "post-measurement handle query failed");
    const ProcessMemorySnapshot memoryAfter = QueryProcessMemory();
    const HeapSnapshot heapAfter = QueryHeapSnapshot();

    Expect(handlesAfter == handlesBefore, "row-cap collections leaked a process handle");
    Expect(heapAfter.busyBlocks == heapBefore.busyBlocks && heapAfter.busyBytes == heapBefore.busyBytes,
           "row-cap collections changed steady process heap usage");

    const uint64_t kernelDelta = FileTime100ns(kernelAfter) - FileTime100ns(kernelBefore);
    const uint64_t userDelta = FileTime100ns(userAfter) - FileTime100ns(userBefore);
    const double wallMicroseconds =
        static_cast<double>(finish.QuadPart - start.QuadPart) * 1'000'000.0 / static_cast<double>(frequency.QuadPart);
    const double cpuMicroseconds = static_cast<double>(kernelDelta + userDelta) / 10.0;
    const auto privateDelta =
        static_cast<std::int64_t>(memoryAfter.privateBytes) - static_cast<std::int64_t>(memoryBefore.privateBytes);
    const auto workingSetDelta = static_cast<std::int64_t>(memoryAfter.workingSetBytes) -
                                 static_cast<std::int64_t>(memoryBefore.workingSetBytes);

    constexpr uint32_t cpuCollectionCount = 64;
    FILETIME cpuCreationBefore{};
    FILETIME cpuExitBefore{};
    FILETIME cpuKernelBefore{};
    FILETIME cpuUserBefore{};
    Expect(GetProcessTimes(GetCurrentProcess(), &cpuCreationBefore, &cpuExitBefore, &cpuKernelBefore, &cpuUserBefore) !=
               FALSE,
           "CPU-probe start query failed");
    for (uint32_t collection = 0; collection < cpuCollectionCount; ++collection)
    {
        Expect(testSource.CollectSyntheticProcessSnapshot(diagnostics.maximumProcessRows, &snapshot) == S_OK &&
                   snapshot,
               "CPU-probe row-cap collection failed");
    }
    FILETIME cpuCreationAfter{};
    FILETIME cpuExitAfter{};
    FILETIME cpuKernelAfter{};
    FILETIME cpuUserAfter{};
    Expect(GetProcessTimes(GetCurrentProcess(), &cpuCreationAfter, &cpuExitAfter, &cpuKernelAfter, &cpuUserAfter) !=
               FALSE,
           "CPU-probe completion query failed");
    const uint64_t cpuProbe100ns = FileTime100ns(cpuKernelAfter) - FileTime100ns(cpuKernelBefore) +
                                   FileTime100ns(cpuUserAfter) - FileTime100ns(cpuUserBefore);
    const double measuredCpuMicrosecondsPerCollection = static_cast<double>(cpuProbe100ns) / 10.0 / cpuCollectionCount;

    std::wcout << L"SystemData Release row-cap measurement: rows=" << diagnostics.maximumProcessRows << L" collections="
               << collectionCount << L" wall_us_total=" << wallMicroseconds << L" wall_us_per_collection="
               << wallMicroseconds / collectionCount << L" process_cpu_us_total=" << cpuMicroseconds
               << L" cpu_probe_collections=" << cpuCollectionCount << L" process_cpu_us_per_collection="
               << measuredCpuMicrosecondsPerCollection << L" handles_delta="
               << static_cast<std::int64_t>(handlesAfter) - static_cast<std::int64_t>(handlesBefore)
               << L" heap_blocks_delta="
               << static_cast<std::int64_t>(heapAfter.busyBlocks) - static_cast<std::int64_t>(heapBefore.busyBlocks)
               << L" heap_bytes_delta="
               << static_cast<std::int64_t>(heapAfter.busyBytes) - static_cast<std::int64_t>(heapBefore.busyBytes)
               << L" private_bytes_delta=" << privateDelta << L" working_set_delta=" << workingSetDelta
               << L" source_storage_bytes=" << diagnostics.sourceStorageBytes << L" source_workers="
               << diagnostics.sourceOwnedWorkerCount << L" source_timers=" << diagnostics.sourceOwnedTimerCount
               << L'\n';
}

// Oracles for the SDK `Reserved*` members that SystemData now consumes, and for the information classes it probes by
// measured ReturnLength. Every value below is cross-checked against an independent documented Win32 API; a field that
// cannot be corroborated has no business being published, so a mismatch fails the suite rather than degrading quality.
[[nodiscard]] uint64_t ValueU64(const RedXeDataRow& row, uint32_t column)
{
    Expect(row.values && column < row.valueCount, "column index is out of range");
    Expect(row.values[column].valueType == RedXeDataValueTypeUInt64, "column is not a UInt64");
    return row.values[column].uint64Value;
}

[[nodiscard]] double ValueF64(const RedXeDataRow& row, uint32_t column)
{
    Expect(row.values && column < row.valueCount, "column index is out of range");
    Expect(row.values[column].valueType == RedXeDataValueTypeFloat64, "column is not a Float64");
    return row.values[column].float64Value;
}

[[nodiscard]] bool IsGood(const RedXeDataRow& row, uint32_t column)
{
    Expect(row.values && column < row.valueCount, "column index is out of range");
    return row.values[column].quality == RedXeDataQualityGood;
}

// Oracle for SYSTEM_PROCESS_INFORMATION::Reserved2 — the same parent PID, read through Toolhelp's named field.
[[nodiscard]] std::vector<std::pair<uint32_t, uint32_t>> ToolhelpParents()
{
    std::vector<std::pair<uint32_t, uint32_t>> parents;
    wil::unique_handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE)
    {
        snapshot.release();
        return parents;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = static_cast<DWORD>(sizeof(entry));
    for (BOOL more = Process32FirstW(snapshot.get(), &entry); more != FALSE;
         more = Process32NextW(snapshot.get(), &entry))
    {
        parents.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID);
    }
    return parents;
}

void RunNativeLayoutOracles(IRedXeDataSource& source, const RedXeDataSetDescriptor* descriptors, uint32_t count)
{
    const RedXeDataSetDescriptor* processDescriptor = FindDataSet(descriptors, count, "process.list");
    const RedXeDataSetDescriptor* threadDescriptor = FindDataSet(descriptors, count, "thread.list");
    const RedXeDataSetDescriptor* cpuLogicalDescriptor = FindDataSet(descriptors, count, "cpu.logical");
    const RedXeDataSetDescriptor* cpuSummaryDescriptor = FindDataSet(descriptors, count, "cpu.summary");
    const RedXeDataSetDescriptor* memoryDescriptor = FindDataSet(descriptors, count, "memory.summary");
    const RedXeDataSetDescriptor* summaryDescriptor = FindDataSet(descriptors, count, "system.summary");
    Expect(processDescriptor && threadDescriptor && cpuLogicalDescriptor && cpuSummaryDescriptor && memoryDescriptor &&
               summaryDescriptor,
           "oracle datasets are missing");

    // ---- class 5 process record: Reserved1, Reserved2, Reserved4, Reserved7 ------------------------------------
    const RedXeDataSnapshot* processes = nullptr;
    Expect(CollectOneSnapshot(source, "process.list", &processes) == S_OK && processes && processes->rowCount != 0,
           "process.list oracle collection failed");
    const uint32_t pidColumn = FindColumn(*processDescriptor, "processId");
    const uint32_t parentColumn = FindColumn(*processDescriptor, "parentProcessId");
    const uint32_t createColumn = FindColumn(*processDescriptor, "createTime100ns");
    const uint32_t userColumn = FindColumn(*processDescriptor, "userTime100ns");
    const uint32_t kernelColumn = FindColumn(*processDescriptor, "kernelTime100ns");
    const uint32_t readOpsColumn = FindColumn(*processDescriptor, "ioReadOperations");
    const uint32_t writeOpsColumn = FindColumn(*processDescriptor, "ioWriteOperations");
    const uint32_t readBytesColumn = FindColumn(*processDescriptor, "ioReadBytes");
    const uint32_t faultColumn = FindColumn(*processDescriptor, "pageFaultCount");
    const uint32_t hardFaultColumn = FindColumn(*processDescriptor, "hardFaultCount");
    const uint32_t privateWorkingSetColumn = FindColumn(*processDescriptor, "workingSetPrivateBytes");

    const std::vector<std::pair<uint32_t, uint32_t>> toolhelp = ToolhelpParents();
    Expect(!toolhelp.empty(), "Toolhelp parent oracle produced no rows");
    uint32_t parentComparisons = 0;
    uint32_t timeComparisons = 0;
    uint32_t ioComparisons = 0;
    for (uint32_t index = 0; index < processes->rowCount; ++index)
    {
        const RedXeDataRow& row = processes->rows[index];
        const uint32_t pid = static_cast<uint32_t>(ValueU64(row, pidColumn));

        // Reserved2 is the parent PID. Toolhelp reports the same value from a named field.
        if (IsGood(row, parentColumn))
        {
            const auto match =
                std::find_if(toolhelp.begin(), toolhelp.end(), [pid](const auto& entry) { return entry.first == pid; });
            if (match != toolhelp.end())
            {
                Expect(static_cast<uint32_t>(ValueU64(row, parentColumn)) == match->second,
                       "class-5 Reserved2 parent PID disagrees with Toolhelp");
                ++parentComparisons;
            }
        }

        // Every walked row must carry the counters that live in the reserved blocks; coverage is no longer limited
        // to processes this test can open.
        Expect(IsGood(row, faultColumn) && IsGood(row, hardFaultColumn) && IsGood(row, privateWorkingSetColumn),
               "class-5 reserved counters are unavailable on a walked row");

        wil::unique_handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
        if (!process)
        {
            continue;
        }
        FILETIME creation{};
        FILETIME exitTime{};
        FILETIME kernel{};
        FILETIME user{};
        if (GetProcessTimes(process.get(), &creation, &exitTime, &kernel, &user) != FALSE)
        {
            // Creation time never advances, so Reserved1's CreateTime must match exactly.
            Expect(ValueU64(row, createColumn) == FileTime100ns(creation),
                   "class-5 Reserved1 create time disagrees with GetProcessTimes");
            // User and kernel time advance between the snapshot and this call, so the snapshot must be no larger.
            Expect(ValueU64(row, userColumn) <= FileTime100ns(user),
                   "class-5 Reserved1 user time exceeds GetProcessTimes");
            Expect(ValueU64(row, kernelColumn) <= FileTime100ns(kernel),
                   "class-5 Reserved1 kernel time exceeds GetProcessTimes");
            ++timeComparisons;
        }
        IO_COUNTERS io{};
        if (GetProcessIoCounters(process.get(), &io) != FALSE)
        {
            Expect(ValueU64(row, readOpsColumn) <= io.ReadOperationCount &&
                       ValueU64(row, writeOpsColumn) <= io.WriteOperationCount &&
                       ValueU64(row, readBytesColumn) <= io.ReadTransferCount,
                   "class-5 Reserved7 I/O counters exceed GetProcessIoCounters");
            ++ioComparisons;
        }
        PROCESS_MEMORY_COUNTERS memory{};
        memory.cb = static_cast<DWORD>(sizeof(memory));
        if (K32GetProcessMemoryInfo(process.get(), &memory, static_cast<DWORD>(sizeof(memory))) != FALSE)
        {
            Expect(ValueU64(row, faultColumn) <= memory.PageFaultCount,
                   "class-5 Reserved4 page-fault count exceeds K32GetProcessMemoryInfo");
        }
    }
    Expect(parentComparisons >= 8 && timeComparisons >= 8 && ioComparisons >= 8,
           "process oracle did not compare enough rows to be meaningful");

    // ---- class 5 thread records: Reserved1 and Reserved3 --------------------------------------------------------
    const RedXeDataSnapshot* threads = nullptr;
    Expect(CollectOneSnapshot(source, "thread.list", &threads) == S_OK && threads && threads->rowCount != 0,
           "thread.list oracle collection failed");
    const uint32_t threadPidColumn = FindColumn(*threadDescriptor, "processId");
    const uint32_t threadCreateColumn = FindColumn(*threadDescriptor, "createTime100ns");
    const uint32_t threadUserColumn = FindColumn(*threadDescriptor, "userTime100ns");
    const uint32_t threadKernelColumn = FindColumn(*threadDescriptor, "kernelTime100ns");
    const uint32_t threadSwitchColumn = FindColumn(*threadDescriptor, "contextSwitchCount");
    // thread.list truncates at its row cap well before it reaches every process, so the oracle below cannot assume
    // any particular process appears. It instead checks each thread against the process row it belongs to, which is
    // available for every thread the snapshot did return.
    std::vector<std::pair<uint32_t, uint64_t>> processTimes;
    for (uint32_t index = 0; index < processes->rowCount; ++index)
    {
        const RedXeDataRow& row = processes->rows[index];
        processTimes.emplace_back(static_cast<uint32_t>(ValueU64(row, pidColumn)),
                                  ValueU64(row, userColumn) + ValueU64(row, kernelColumn));
    }
    uint32_t threadComparisons = 0;
    uint32_t switchingThreads = 0;
    for (uint32_t index = 0; index < threads->rowCount; ++index)
    {
        const RedXeDataRow& row = threads->rows[index];
        const uint32_t threadPid = static_cast<uint32_t>(ValueU64(row, threadPidColumn));
        Expect(IsGood(row, threadCreateColumn), "thread Reserved1 create time is unavailable");
        // Idle (PID 0) and System (PID 4) are created before the interrupt-time base exists and legitimately report
        // a zero create time. A never-scheduled thread on a later process can do the same; a thread that has
        // accumulated CPU must carry a stamp.
        const uint64_t createTime = ValueU64(row, threadCreateColumn);
        if (threadPid != 0 && threadPid != 4 && createTime == 0)
        {
            Expect(ValueU64(row, threadUserColumn) == 0 && ValueU64(row, threadKernelColumn) == 0,
                   "thread Reserved1 create time is zero for a real process");
        }
        if (ValueU64(row, threadSwitchColumn) != 0)
        {
            ++switchingThreads;
        }
        // A thread's CPU time is one component of its process's, so it can never exceed the process total. The two
        // snapshots are taken moments apart, so allow one second of drift.
        const auto owner = std::find_if(processTimes.begin(), processTimes.end(),
                                        [threadPid](const auto& entry) { return entry.first == threadPid; });
        if (owner != processTimes.end())
        {
            const uint64_t threadTime = ValueU64(row, threadUserColumn) + ValueU64(row, threadKernelColumn);
            Expect(threadTime <= owner->second + 10'000'000ULL,
                   "thread Reserved1 CPU time exceeds its owning process's CPU time");
            ++threadComparisons;
        }
    }
    Expect(switchingThreads * 2 >= threads->rowCount,
           "thread Reserved3 context switches are zero for most threads, which no running system produces");
    Expect(threadComparisons >= 32, "thread oracle did not compare enough rows to be meaningful");

    // ---- class 8 Reserved1/Reserved2, plus the workstream C frequency and CPU-set columns -----------------------
    const RedXeDataSnapshot* logical = nullptr;
    Expect(CollectOneSnapshot(source, "cpu.logical", &logical) == S_OK && logical && logical->rowCount != 0,
           "cpu.logical oracle collection failed");
    const uint32_t dpcColumn = FindColumn(*cpuLogicalDescriptor, "dpcPercent");
    const uint32_t interruptColumn = FindColumn(*cpuLogicalDescriptor, "interruptPercent");
    const uint32_t interruptCountColumn = FindColumn(*cpuLogicalDescriptor, "interruptCount");
    const uint32_t coreColumn = FindColumn(*cpuLogicalDescriptor, "coreIndex");
    const uint32_t packageColumn = FindColumn(*cpuLogicalDescriptor, "packageIndex");
    const uint32_t efficiencyColumn = FindColumn(*cpuLogicalDescriptor, "efficiencyClass");
    const uint32_t currentMhzColumn = FindColumn(*cpuLogicalDescriptor, "currentFrequencyMhz");
    const uint32_t maximumMhzColumn = FindColumn(*cpuLogicalDescriptor, "maximumFrequencyMhz");
    Expect(logical->rowCount == GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),
           "cpu.logical row count disagrees with GetActiveProcessorCount");
    std::vector<uint64_t> firstInterruptCounts;
    for (uint32_t index = 0; index < logical->rowCount; ++index)
    {
        const RedXeDataRow& row = logical->rows[index];
        Expect(IsGood(row, coreColumn) && IsGood(row, packageColumn), "cpu.logical topology mapping is unavailable");
        Expect(IsGood(row, efficiencyColumn), "cpu.logical CPU-set identity is unavailable");
        Expect(IsGood(row, currentMhzColumn) && IsGood(row, maximumMhzColumn) && ValueU64(row, maximumMhzColumn) != 0,
               "cpu.logical frequency is unavailable");
        Expect(IsGood(row, interruptCountColumn), "class-8 Reserved2 interrupt count is unavailable");
        if (IsGood(row, dpcColumn))
        {
            // DPC and interrupt time are components of kernel time, so together they cannot exceed the whole.
            Expect(ValueF64(row, dpcColumn) + ValueF64(row, interruptColumn) <= 100.0,
                   "class-8 Reserved1 DPC and interrupt time exceed 100% of the interval");
        }
        firstInterruptCounts.push_back(ValueU64(row, interruptCountColumn));
    }

    // ---- class 2 and class 3: fields that only exist because the records were probed by ReturnLength ------------
    const RedXeDataSnapshot* memory = nullptr;
    Expect(CollectOneSnapshot(source, "memory.summary", &memory) == S_OK && memory && memory->rowCount == 1,
           "memory.summary oracle collection failed");
    const uint32_t cacheColumn = FindColumn(*memoryDescriptor, "cacheBytes");
    const uint32_t totalColumn = FindColumn(*memoryDescriptor, "totalPhysicalBytes");
    Expect(IsGood(memory->rows[0], cacheColumn), "class-2 system cache size is unavailable");
    Expect(ValueU64(memory->rows[0], cacheColumn) != 0 &&
               ValueU64(memory->rows[0], cacheColumn) < ValueU64(memory->rows[0], totalColumn),
           "class-2 system cache size is not a plausible fraction of physical memory");

    const RedXeDataSnapshot* summary = nullptr;
    Expect(CollectOneSnapshot(source, "system.summary", &summary) == S_OK && summary && summary->rowCount == 1,
           "system.summary oracle collection failed");
    const uint32_t bootColumn = FindColumn(*summaryDescriptor, "bootTime100ns");
    const uint32_t uptimeColumn = FindColumn(*summaryDescriptor, "uptimeMilliseconds");
    Expect(IsGood(summary->rows[0], bootColumn), "class-3 boot time is unavailable");
    FILETIME nowFileTime{};
    GetSystemTimeAsFileTime(&nowFileTime);
    const uint64_t now = FileTime100ns(nowFileTime);
    const uint64_t boot = ValueU64(summary->rows[0], bootColumn);
    Expect(boot != 0 && boot < now, "class-3 boot time is not before the current system time");
    const uint64_t wallUptimeMs = (now - boot) / 10'000ULL;
    const uint64_t tickUptimeMs = ValueU64(summary->rows[0], uptimeColumn);
    // Wall-clock time since boot includes any time the machine spent asleep, so it can never be the smaller of the
    // two. One minute of slack absorbs clock adjustment between the snapshot and this comparison.
    Expect(wallUptimeMs + 60'000ULL >= tickUptimeMs, "class-3 boot time implies an uptime shorter than the tick count");

    // ---- second pass: counters that must advance, and rates that must become Good -------------------------------
    // Every rate below is derived from a delta over an interval, so each dataset needs two collections separated by
    // real time. Collecting twice back to back would divide a near-zero counter delta by a near-zero interval and
    // legitimately produce zero, which says nothing about whether the field works.
    const RedXeDataSnapshot* cpuSummary = nullptr;
    const uint32_t switchRateColumn = FindColumn(*cpuSummaryDescriptor, "contextSwitchRate");
    const uint32_t faultRateColumn = FindColumn(*memoryDescriptor, "pageFaultRate");
    Expect(CollectOneSnapshot(source, "cpu.summary", &cpuSummary) == S_OK && cpuSummary && cpuSummary->rowCount == 1,
           "cpu.summary priming collection failed");
    Expect(CollectOneSnapshot(source, "memory.summary", &memory) == S_OK && memory && memory->rowCount == 1,
           "memory.summary priming collection failed");

    Sleep(1200);

    Expect(CollectOneSnapshot(source, "cpu.logical", &logical) == S_OK && logical, "cpu.logical resample failed");
    for (uint32_t index = 0; index < logical->rowCount && index < firstInterruptCounts.size(); ++index)
    {
        Expect(ValueU64(logical->rows[index], interruptCountColumn) >= firstInterruptCounts[index],
               "class-8 Reserved2 interrupt count went backwards");
    }
    Expect(CollectOneSnapshot(source, "cpu.summary", &cpuSummary) == S_OK && cpuSummary && cpuSummary->rowCount == 1,
           "cpu.summary resample failed");
    Expect(IsGood(cpuSummary->rows[0], switchRateColumn) && ValueF64(cpuSummary->rows[0], switchRateColumn) > 0.0,
           "class-2 context-switch rate is unavailable or zero, which no running system produces");
    Expect(CollectOneSnapshot(source, "memory.summary", &memory) == S_OK && memory && memory->rowCount == 1,
           "memory.summary resample failed");
    Expect(IsGood(memory->rows[0], faultRateColumn) && ValueF64(memory->rows[0], faultRateColumn) > 0.0,
           "class-2 page-fault rate is unavailable or zero, which no running system produces");
}

// The accelerator datasets must be discoverable and collectable on every machine, including one with no NPU, where
// the honest answer is a valid zero-row snapshot rather than a missing dataset or an invented device.
void RunAcceleratorChecks(IRedXeDataSource& source, const RedXeDataSetDescriptor* descriptors, uint32_t count)
{
    const RedXeDataSetDescriptor* npuAdapterDescriptor = FindDataSet(descriptors, count, "npu.adapter");
    const RedXeDataSetDescriptor* npuEngineDescriptor = FindDataSet(descriptors, count, "npu.engine");
    const RedXeDataSetDescriptor* npuProcessDescriptor = FindDataSet(descriptors, count, "npu.process");
    const RedXeDataSetDescriptor* securityDescriptor = FindDataSet(descriptors, count, "security.posture");
    const RedXeDataSetDescriptor* powerDescriptor = FindDataSet(descriptors, count, "power.summary");
    Expect(npuAdapterDescriptor && npuEngineDescriptor && npuProcessDescriptor && securityDescriptor && powerDescriptor,
           "accelerator or posture datasets are missing from the catalog");
    Expect(npuAdapterDescriptor->maximumRows == 16 && npuEngineDescriptor->maximumRows == 128 &&
               npuProcessDescriptor->maximumRows == 512 && securityDescriptor->maximumRows == 1 &&
               (npuProcessDescriptor->flags & RedXeDataSetFlagLocalSensitive) != 0 &&
               securityDescriptor->recommendedIntervalMilliseconds == 60000,
           "accelerator or posture descriptor bounds are wrong");

    const RedXeDataSnapshot* snapshot = nullptr;
    Expect(CollectOneSnapshot(source, "npu.adapter", &snapshot) == S_OK && snapshot, "npu.adapter collection failed");
    ValidateSnapshot(*snapshot, *npuAdapterDescriptor);
    const uint32_t classColumn = FindColumn(*npuAdapterDescriptor, "deviceClass");
    const uint32_t npuUtilizationColumn = FindColumn(*npuAdapterDescriptor, "utilizationPercent");
    for (uint32_t index = 0; index < snapshot->rowCount; ++index)
    {
        const RedXeDataRow& row = snapshot->rows[index];
        // Every row in this dataset was classified as an accelerator; an unclassified adapter stays in gpu.adapter.
        Expect(row.values[classColumn].quality == RedXeDataQualityGood &&
                   row.values[classColumn].uint64Value == kTestDeviceClassNpu,
               "npu.adapter published a row that is not classified as an NPU");
        Expect(row.values[npuUtilizationColumn].quality == RedXeDataQualityUnavailable ||
                   (row.values[npuUtilizationColumn].float64Value >= 0.0 &&
                    row.values[npuUtilizationColumn].float64Value <= 100.0),
               "npu.adapter utilization is neither Unavailable nor a percentage");
    }

    Expect(CollectOneSnapshot(source, "npu.engine", &snapshot) == S_OK && snapshot, "npu.engine collection failed");
    ValidateSnapshot(*snapshot, *npuEngineDescriptor);
    Expect(CollectOneSnapshot(source, "npu.process", &snapshot) == S_OK && snapshot, "npu.process collection failed");
    ValidateSnapshot(*snapshot, *npuProcessDescriptor);

    Expect(CollectOneSnapshot(source, "security.posture", &snapshot) == S_OK && snapshot && snapshot->rowCount == 1,
           "security.posture collection failed");
    ValidateSnapshot(*snapshot, *securityDescriptor);
    const uint32_t enabledColumn = FindColumn(*securityDescriptor, "codeIntegrityEnabled");
    const uint32_t hvciColumn = FindColumn(*securityDescriptor, "memoryIntegrityEnabled");
    const uint32_t optionsColumn = FindColumn(*securityDescriptor, "codeIntegrityOptions");
    const RedXeDataRow& posture = snapshot->rows[0];
    Expect(posture.values[enabledColumn].quality == RedXeDataQualityGood,
           "security posture code-integrity state is unavailable");
    Expect(posture.values[enabledColumn].uint64Value <= 1 && posture.values[hvciColumn].uint64Value <= 1,
           "security posture flags are not 0/1 values");
    const uint64_t options = posture.values[optionsColumn].uint64Value;
    // Windows returns a ULONG bitmask. Zero is valid when the flags are disabled, and newer
    // systems may define bits RedXe does not name. Validate width and our decoded fields instead.
    Expect(posture.values[optionsColumn].quality == RedXeDataQualityGood && (options >> 32U) == 0,
           "security posture code-integrity options are not an available 32-bit record");
    Expect(posture.values[enabledColumn].uint64Value == ((options & 0x01U) != 0 ? 1U : 0U),
           "security posture code-integrity enabled flag disagrees with the raw record");
    Expect(posture.values[hvciColumn].uint64Value == ((options & 0x400U) != 0 ? 1U : 0U),
           "security posture memory-integrity enabled flag disagrees with the raw record");
    // These are configuration flags, not counters: a second read must return exactly the same value.
    const uint64_t firstOptions = options;
    Expect(CollectOneSnapshot(source, "security.posture", &snapshot) == S_OK && snapshot && snapshot->rowCount == 1,
           "security.posture resample failed");
    Expect(snapshot->rows[0].values[optionsColumn].uint64Value == firstOptions,
           "security posture changed between two reads, which means the record was misread");

    Expect(CollectOneSnapshot(source, "power.summary", &snapshot) == S_OK && snapshot && snapshot->rowCount == 1,
           "power.summary collection failed");
    const uint32_t standbyColumn = FindColumn(*powerDescriptor, "modernStandby");
    Expect(snapshot->rows[0].values[standbyColumn].quality == RedXeDataQualityUnavailable ||
               snapshot->rows[0].values[standbyColumn].uint64Value <= 1,
           "modern standby flag is neither Unavailable nor a 0/1 value");
}

void Run(bool benchmark, bool domains)
{
    const std::filesystem::path pluginPath = PluginPath();
    wil::unique_hmodule module{
        LoadLibraryExW(pluginPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    Expect(static_cast<bool>(module), "SystemData.dll could not be loaded");
    const std::vector<std::wstring> modulesBeforeCollect = LoadedModuleNames();

    const RedXeCreateFn create = Resolve<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const RedXeEnumeratePluginsFn enumerate =
        Resolve<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    const RedXeGetPluginSettingsContractFn getSettings =
        Resolve<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);

    const RedXePluginMetadata* metadata = reinterpret_cast<const RedXePluginMetadata*>(1);
    uint32_t metadataCount = 99;
    Expect(enumerate(nullptr, &metadataCount) == E_POINTER && metadataCount == 0,
           "enumeration did not clear count for a null metadata output");
    Expect(enumerate(&metadata, &metadataCount) == S_OK && metadata && metadataCount == 1,
           "SystemData metadata enumeration failed");
    Expect(RedXeAsciiEqualsIgnoreCase(metadata[0].id, "builtin.system-data") &&
               metadata[0].capabilities == RedXePluginCapabilityDataSource,
           "SystemData metadata is invalid");

    const RedXePluginSettingsContract* settings = reinterpret_cast<const RedXePluginSettingsContract*>(1);
    Expect(getSettings("missing", &settings) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !settings,
           "unknown settings lookup did not clear its output");
    Expect(getSettings("builtin.system-data", &settings) == S_OK && settings &&
               settings->sizeBytes == sizeof(RedXePluginSettingsContract),
           "SystemData settings contract is unavailable");

    void* unsupported = reinterpret_cast<void*>(1);
    Expect(create(__uuidof(IRedXeWidgetProvider), nullptr, nullptr, "builtin.system-data", &unsupported) ==
                   E_NOINTERFACE &&
               !unsupported,
           "SystemData accepted the widget-provider IID or retained an output");
    void* missing = reinterpret_cast<void*>(1);
    Expect(create(__uuidof(IRedXeDataSource), nullptr, nullptr, "missing", &missing) ==
                   HRESULT_FROM_WIN32(ERROR_NOT_FOUND) &&
               !missing,
           "SystemData accepted an unknown plugin ID or retained an output");

    void* sourceObject = nullptr;
    Expect(create(__uuidof(IRedXeDataSource), nullptr, nullptr, "builtin.system-data", &sourceObject) == S_OK &&
               sourceObject,
           "SystemData source creation failed");
    wil::com_ptr_nothrow<IRedXeDataSource> source;
    source.attach(static_cast<IRedXeDataSource*>(sourceObject));

    wil::com_ptr_nothrow<IRedXeSystemDataTestSource> testSource;
    Expect(source.query_to(testSource.put()) == S_OK && testSource, "SystemData test interface is unavailable");
    Expect(testSource->GetTestDiagnostics(nullptr) == E_POINTER, "SystemData test diagnostics accepted a null record");
    SystemDataTestDiagnostics shortDiagnostics{sizeof(uint32_t)};
    Expect(testSource->GetTestDiagnostics(&shortDiagnostics) == E_INVALIDARG,
           "SystemData test diagnostics accepted a short record");
    SystemDataTestDiagnostics diagnostics{sizeof(SystemDataTestDiagnostics)};
    Expect(testSource->GetTestDiagnostics(&diagnostics) == S_OK && diagnostics.maximumProcessRows == 2048 &&
               diagnostics.processNameCharacters == 65536,
           "SystemData test diagnostics are invalid");

    wil::com_ptr_nothrow<IUnknown> unknownFromProvider;
    wil::com_ptr_nothrow<IUnknown> unknownFromQuery;
    wil::com_ptr_nothrow<IUnknown> unknownFromTest;
    Expect(source.query_to(unknownFromProvider.put()) == S_OK &&
               source->QueryInterface(__uuidof(IRedXeDataSource), reinterpret_cast<void**>(unknownFromQuery.put())) ==
                   S_OK &&
               testSource.query_to(unknownFromTest.put()) == S_OK,
           "SystemData QueryInterface failed");
    Expect(static_cast<IUnknown*>(source.get()) == unknownFromProvider.get() &&
               unknownFromProvider.get() == unknownFromQuery.get() && unknownFromQuery.get() == unknownFromTest.get(),
           "SystemData interfaces do not share one controlling IUnknown");

    const RedXeDataSetDescriptor* descriptors = reinterpret_cast<const RedXeDataSetDescriptor*>(1);
    uint32_t descriptorCount = 99;
    Expect(source->GetDataSets(nullptr, &descriptorCount) == E_POINTER && descriptorCount == 0,
           "GetDataSets did not clear count on a null descriptor output");
    Expect(source->GetDataSets(&descriptors, &descriptorCount) == S_OK && descriptors && descriptorCount == 22,
           "SystemData descriptors are unavailable");
    for (uint32_t index = 0; index < descriptorCount; ++index)
    {
        Expect(descriptors[index].dataSetId && descriptors[index].dataSetId[0] != '\0',
               "SystemData catalog contains an empty dataset ID");
        for (uint32_t later = index + 1; later < descriptorCount; ++later)
        {
            Expect(std::strcmp(descriptors[index].dataSetId, descriptors[later].dataSetId) != 0,
                   "SystemData catalog contains a duplicate dataset ID");
        }
        for (uint32_t column = 0; column < descriptors[index].columnCount; ++column)
        {
            const char* columnId = descriptors[index].columns[column].columnId;
            Expect(
                !ContainsAsciiIgnoreCase(columnId, "macAddress") && !ContainsAsciiIgnoreCase(columnId, "ipAddress") &&
                    !ContainsAsciiIgnoreCase(columnId, "serialNumber") &&
                    !ContainsAsciiIgnoreCase(columnId, "commandLine") &&
                    !ContainsAsciiIgnoreCase(columnId, "imagePath") && !ContainsAsciiIgnoreCase(columnId, "userName") &&
                    !ContainsAsciiIgnoreCase(columnId, "physicalAddress") &&
                    !RedXeAsciiEqualsIgnoreCase(columnId, "serial") && !RedXeAsciiEqualsIgnoreCase(columnId, "ssid") &&
                    !RedXeAsciiEqualsIgnoreCase(columnId, "mac") && !RedXeAsciiEqualsIgnoreCase(columnId, "uniqueId"),
                "SystemData catalog published a prohibited identity column");
        }
    }
    const RedXeDataSetDescriptor* statusDescriptor = FindDataSet(descriptors, descriptorCount, "source.status");
    const RedXeDataSetDescriptor* summaryDescriptor = FindDataSet(descriptors, descriptorCount, "system.summary");
    const RedXeDataSetDescriptor* processDescriptor = FindDataSet(descriptors, descriptorCount, "process.list");
    const RedXeDataSetDescriptor* cpuSummaryDescriptor = FindDataSet(descriptors, descriptorCount, "cpu.summary");
    const RedXeDataSetDescriptor* cpuLogicalDescriptor = FindDataSet(descriptors, descriptorCount, "cpu.logical");
    const RedXeDataSetDescriptor* memoryDescriptor = FindDataSet(descriptors, descriptorCount, "memory.summary");
    const RedXeDataSetDescriptor* threadDescriptor = FindDataSet(descriptors, descriptorCount, "thread.list");
    const RedXeDataSetDescriptor* networkInterfaceDescriptor =
        FindDataSet(descriptors, descriptorCount, "network.interface");
    const RedXeDataSetDescriptor* networkProtocolDescriptor =
        FindDataSet(descriptors, descriptorCount, "network.protocol");
    const RedXeDataSetDescriptor* storageDiskDescriptor = FindDataSet(descriptors, descriptorCount, "storage.disk");
    const RedXeDataSetDescriptor* storageVolumeDescriptor = FindDataSet(descriptors, descriptorCount, "storage.volume");
    const RedXeDataSetDescriptor* gpuAdapterDescriptor = FindDataSet(descriptors, descriptorCount, "gpu.adapter");
    const RedXeDataSetDescriptor* gpuEngineDescriptor = FindDataSet(descriptors, descriptorCount, "gpu.engine");
    const RedXeDataSetDescriptor* gpuProcessDescriptor = FindDataSet(descriptors, descriptorCount, "gpu.process");
    const RedXeDataSetDescriptor* powerSummaryDescriptor = FindDataSet(descriptors, descriptorCount, "power.summary");
    const RedXeDataSetDescriptor* batteryDescriptor = FindDataSet(descriptors, descriptorCount, "battery.list");
    const RedXeDataSetDescriptor* thermalDescriptor = FindDataSet(descriptors, descriptorCount, "thermal.sensor");
    const RedXeDataSetDescriptor* fanDescriptor = FindDataSet(descriptors, descriptorCount, "fan.sensor");
    Expect(
        statusDescriptor && summaryDescriptor && processDescriptor && cpuSummaryDescriptor && cpuLogicalDescriptor &&
            memoryDescriptor && threadDescriptor && networkInterfaceDescriptor && networkProtocolDescriptor &&
            storageDiskDescriptor && storageVolumeDescriptor && gpuAdapterDescriptor && gpuEngineDescriptor &&
            gpuProcessDescriptor && powerSummaryDescriptor && batteryDescriptor && thermalDescriptor && fanDescriptor &&
            statusDescriptor->maximumRows == 32 && statusDescriptor->recommendedIntervalMilliseconds == 5000 &&
            (statusDescriptor->flags & RedXeDataSetFlagLocalSensitive) == 0 && statusDescriptor->columnCount == 12 &&
            summaryDescriptor->maximumRows == 1 && (summaryDescriptor->flags & RedXeDataSetFlagLocalSensitive) == 0 &&
            processDescriptor->maximumRows == 2048 && processDescriptor->columnCount == 39 &&
            summaryDescriptor->columnCount == 12 && cpuLogicalDescriptor->maximumRows == 1024 &&
            threadDescriptor->maximumRows == 8192 && networkInterfaceDescriptor->maximumRows == 256 &&
            (networkInterfaceDescriptor->flags & RedXeDataSetFlagLocalSensitive) != 0 &&
            networkInterfaceDescriptor->columnCount == 22 && networkProtocolDescriptor->maximumRows == 16 &&
            (networkProtocolDescriptor->flags & RedXeDataSetFlagLocalSensitive) == 0 &&
            storageDiskDescriptor->maximumRows == 128 && storageVolumeDescriptor->maximumRows == 256 &&
            storageVolumeDescriptor->recommendedIntervalMilliseconds == 5000 &&
            gpuAdapterDescriptor->maximumRows == 32 && gpuEngineDescriptor->maximumRows == 512 &&
            gpuProcessDescriptor->maximumRows == 2048 &&
            (gpuProcessDescriptor->flags & RedXeDataSetFlagLocalSensitive) != 0 &&
            powerSummaryDescriptor->maximumRows == 1 &&
            powerSummaryDescriptor->recommendedIntervalMilliseconds == 5000 && batteryDescriptor->maximumRows == 32 &&
            (batteryDescriptor->flags & RedXeDataSetFlagLocalSensitive) != 0 && thermalDescriptor->maximumRows == 128 &&
            thermalDescriptor->recommendedIntervalMilliseconds == 10000 && fanDescriptor->maximumRows == 128,
        "SystemData descriptor bounds are wrong");

    const RedXeDataSnapshot* snapshot = reinterpret_cast<const RedXeDataSnapshot*>(1);
    const RedXeDataCollectResult* collectResult = reinterpret_cast<const RedXeDataCollectResult*>(1);
    Expect(source->CollectSnapshots(nullptr, &collectResult) == E_INVALIDARG && !collectResult,
           "null collect request did not clear its output");
    RedXeDataCollectRequest emptyRequest{sizeof(RedXeDataCollectRequest), nullptr, 0};
    collectResult = reinterpret_cast<const RedXeDataCollectResult*>(1);
    Expect(source->CollectSnapshots(&emptyRequest, &collectResult) == E_INVALIDARG && !collectResult,
           "empty collect request did not clear its output");
    const char* duplicateIds[] = {"system.summary", "system.summary"};
    RedXeDataCollectRequest duplicateRequest{sizeof(RedXeDataCollectRequest), duplicateIds, 2};
    collectResult = reinterpret_cast<const RedXeDataCollectResult*>(1);
    Expect(source->CollectSnapshots(&duplicateRequest, &collectResult) == E_INVALIDARG && !collectResult,
           "duplicate collect request did not clear its output");
    const char* missingIds[] = {"missing"};
    RedXeDataCollectRequest missingRequest{sizeof(RedXeDataCollectRequest), missingIds, 1};
    collectResult = reinterpret_cast<const RedXeDataCollectResult*>(1);
    Expect(source->CollectSnapshots(&missingRequest, &collectResult) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) &&
               !collectResult,
           "unknown dataset collection did not clear its output");
    Expect(CollectOneSnapshot(*source, "missing", &snapshot) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !snapshot,
           "unknown dataset helper collection did not clear its output");

    Expect(CollectOneSnapshot(*source, "cpu.logical", &snapshot) == S_OK && snapshot && snapshot->rowCount != 0,
           "cpu.logical collection failed");
    ValidateSnapshot(*snapshot, *cpuLogicalDescriptor);
    const uint64_t logicalSequence = snapshot->sequence;
    const uint32_t totalPercentColumn = FindColumn(*cpuLogicalDescriptor, "totalPercent");
    const char* abortedIds[] = {"cpu.logical", "missing"};
    RedXeDataCollectRequest abortedRequest{sizeof(RedXeDataCollectRequest), abortedIds, 2};
    collectResult = reinterpret_cast<const RedXeDataCollectResult*>(1);
    Expect(source->CollectSnapshots(&abortedRequest, &collectResult) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) &&
               !collectResult,
           "a batch that fails after a live dataset must not publish snapshots");
    Expect(CollectOneSnapshot(*source, "cpu.logical", &snapshot) == S_OK && snapshot &&
               snapshot->sequence == logicalSequence + 1,
           "a failed batch must not consume a sequence number");
    ValidateSnapshot(*snapshot, *cpuLogicalDescriptor);
    bool ratesReinitialized = false;
    for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        const uint32_t quality = snapshot->rows[rowIndex].values[totalPercentColumn].quality;
        Expect(quality == RedXeDataQualityInitializing || quality == RedXeDataQualityUnavailable,
               "an aborted batch left cpu.logical rates live");
        if (quality == RedXeDataQualityInitializing)
        {
            ratesReinitialized = true;
        }
    }
    Expect(ratesReinitialized, "an aborted batch did not reinitialize cpu.logical rates");

    Expect(CollectOneSnapshot(*source, "system.summary", &snapshot) == S_OK && snapshot,
           "system summary collection failed");
    ValidateSnapshot(*snapshot, *summaryDescriptor);
    Expect(snapshot->rowCount == 1, "system summary does not contain exactly one row");
    const uint64_t firstSequence = snapshot->sequence;
    Expect(CollectOneSnapshot(*source, "system.summary", &snapshot) == S_OK && snapshot->sequence > firstSequence,
           "system summary sequence did not advance");
    ValidateSnapshot(*snapshot, *summaryDescriptor);

    const char* batchIds[] = {"system.summary", "process.list"};
    RedXeDataCollectRequest batchRequest{sizeof(RedXeDataCollectRequest), batchIds, 2};
    Expect(source->CollectSnapshots(&batchRequest, &collectResult) == S_OK && collectResult &&
               collectResult->sizeBytes == sizeof(RedXeDataCollectResult) && collectResult->snapshotCount == 2 &&
               collectResult->snapshots && collectResult->snapshots[0] && collectResult->snapshots[1] &&
               collectResult->snapshots[0]->sequence == collectResult->snapshots[1]->sequence &&
               collectResult->snapshots[0]->timestampFileTime100ns ==
                   collectResult->snapshots[1]->timestampFileTime100ns &&
               collectResult->sequence == collectResult->snapshots[0]->sequence,
           "batched collection did not share one sequence and timestamp");
    ValidateSnapshot(*collectResult->snapshots[0], *summaryDescriptor);
    ValidateSnapshot(*collectResult->snapshots[1], *processDescriptor);

    Expect(CollectOneSnapshot(*source, "source.status", &snapshot) == S_OK && snapshot,
           "source status collection failed");
    ValidateSnapshot(*snapshot, *statusDescriptor);
    Expect(snapshot->rowCount == 22, "source status does not contain one row per dataset");
    const uint32_t statusIdColumn = FindColumn(*statusDescriptor, "dataSetId");
    bool foundStatusRow = false;
    bool foundSummaryRow = false;
    bool foundProcessRow = false;
    for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        const RedXeDataValue& idValue = snapshot->rows[rowIndex].values[statusIdColumn];
        if (idValue.utf16Value && std::wcscmp(idValue.utf16Value, L"source.status") == 0)
        {
            foundStatusRow = true;
        }
        if (idValue.utf16Value && std::wcscmp(idValue.utf16Value, L"system.summary") == 0)
        {
            foundSummaryRow = true;
        }
        if (idValue.utf16Value && std::wcscmp(idValue.utf16Value, L"process.list") == 0)
        {
            foundProcessRow = true;
        }
    }
    Expect(foundStatusRow && foundSummaryRow && foundProcessRow, "source status is missing a catalog row");

    Expect(CollectOneSnapshot(*source, "process.list", &snapshot) == S_OK && snapshot,
           "process list collection failed");
    ValidateSnapshot(*snapshot, *processDescriptor);
    const uint32_t processIdColumn = FindColumn(*processDescriptor, "processId");
    const uint32_t imageNameColumn = FindColumn(*processDescriptor, "imageName");
    bool foundCurrentProcess = false;
    bool foundIdleProcess = false;
    for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        const RedXeDataRow& row = snapshot->rows[rowIndex];
        if (row.values[processIdColumn].uint64Value == GetCurrentProcessId())
        {
            foundCurrentProcess = row.values[imageNameColumn].quality == RedXeDataQualityGood &&
                                  row.values[imageNameColumn].utf16Characters != 0;
        }
        if (row.values[processIdColumn].uint64Value == 0)
        {
            foundIdleProcess = row.values[imageNameColumn].quality == RedXeDataQualityGood &&
                               row.values[imageNameColumn].utf16Characters == 19 &&
                               row.values[imageNameColumn].utf16Value &&
                               std::wcsncmp(row.values[imageNameColumn].utf16Value, L"System Idle Process", 19) == 0;
        }
    }
    Expect(foundCurrentProcess, "process list does not contain the test process");
    Expect(foundIdleProcess, "PID 0 is named System Idle Process");
    const uint64_t processSequence = snapshot->sequence;
    Expect(CollectOneSnapshot(*source, "process.list", &snapshot) == S_OK && snapshot->sequence > processSequence,
           "process list sequence did not advance");
    ValidateSnapshot(*snapshot, *processDescriptor);
    (void)FindColumn(*processDescriptor, "parentProcessId");
    Expect(CollectOneSnapshot(*source, "cpu.summary", &snapshot) == S_OK && snapshot, "cpu summary collection failed");
    ValidateSnapshot(*snapshot, *cpuSummaryDescriptor);
    Expect(snapshot->rowCount == 1, "cpu summary does not contain exactly one row");
    Expect(CollectOneSnapshot(*source, "cpu.logical", &snapshot) == S_OK && snapshot, "cpu logical collection failed");
    ValidateSnapshot(*snapshot, *cpuLogicalDescriptor);
    Expect(snapshot->rowCount != 0, "cpu logical table is empty");
    Expect(CollectOneSnapshot(*source, "memory.summary", &snapshot) == S_OK && snapshot,
           "memory summary collection failed");
    ValidateSnapshot(*snapshot, *memoryDescriptor);
    Expect(snapshot->rowCount == 1, "memory summary does not contain exactly one row");
    Expect(CollectOneSnapshot(*source, "thread.list", &snapshot) == S_OK && snapshot, "thread list collection failed");
    ValidateSnapshot(*snapshot, *threadDescriptor);
    Expect(snapshot->rowCount != 0, "thread list is empty");
    if (snapshot->rowCount == threadDescriptor->maximumRows)
    {
        Expect((snapshot->flags & RedXeDataSnapshotFlagTruncated) != 0,
               "thread list filled the row cap without Truncated");
    }
    else
    {
        Expect((snapshot->flags & RedXeDataSnapshotFlagTruncated) == 0,
               "thread list reported Truncated below the row cap");
    }

    Expect(CollectOneSnapshot(*source, "network.interface", &snapshot) == S_OK && snapshot,
           "network interface collection failed");
    ValidateSnapshot(*snapshot, *networkInterfaceDescriptor);
    Expect(snapshot->rowCount != 0, "network interface table is empty");
    const uint32_t luidColumn = FindColumn(*networkInterfaceDescriptor, "interfaceLuid");
    const uint32_t rateColumn = FindColumn(*networkInterfaceDescriptor, "inOctetRate");
    for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        Expect(snapshot->rows[rowIndex].values[luidColumn].quality == RedXeDataQualityGood,
               "network interface LUID is missing");
        if (rowIndex > 0)
        {
            Expect(snapshot->rows[rowIndex].values[luidColumn].uint64Value >
                       snapshot->rows[rowIndex - 1].values[luidColumn].uint64Value,
                   "network interfaces are not sorted by LUID");
        }
        Expect(snapshot->rows[rowIndex].values[rateColumn].quality == RedXeDataQualityInitializing,
               "first network rate sample was not Initializing");
    }
    for (uint32_t column = 0; column < networkInterfaceDescriptor->columnCount; ++column)
    {
        Expect(std::strstr(networkInterfaceDescriptor->columns[column].columnId, "mac") == nullptr &&
                   std::strstr(networkInterfaceDescriptor->columns[column].columnId, "Mac") == nullptr &&
                   std::strstr(networkInterfaceDescriptor->columns[column].columnId, "ipAddress") == nullptr,
               "network interface published a MAC or IP column");
    }
    Expect(CollectOneSnapshot(*source, "network.interface", &snapshot) == S_OK && snapshot,
           "second network interface collection failed");
    ValidateSnapshot(*snapshot, *networkInterfaceDescriptor);
    Sleep(16);
    Expect(CollectOneSnapshot(*source, "network.interface", &snapshot) == S_OK && snapshot,
           "elapsed network interface collection failed");
    ValidateSnapshot(*snapshot, *networkInterfaceDescriptor);
    const uint32_t inOctetsColumn = FindColumn(*networkInterfaceDescriptor, "inOctets");
    bool foundReadyRate = false;
    for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        if (snapshot->rows[rowIndex].values[inOctetsColumn].quality == RedXeDataQualityGood)
        {
            Expect(snapshot->rows[rowIndex].values[rateColumn].quality == RedXeDataQualityGood,
                   "network rate stayed Initializing after an elapsed sample");
            foundReadyRate = true;
        }
    }
    Expect(foundReadyRate, "no network interface with counters produced a second-sample rate");

    Expect(CollectOneSnapshot(*source, "network.protocol", &snapshot) == S_OK && snapshot,
           "network protocol collection failed");
    ValidateSnapshot(*snapshot, *networkProtocolDescriptor);
    Expect(snapshot->rowCount == 6, "network protocol table does not contain the six aggregate rows");
    const uint32_t protocolIdColumn = FindColumn(*networkProtocolDescriptor, "protocolId");
    Expect(snapshot->rows[0].values[protocolIdColumn].utf16Value &&
               std::wcscmp(snapshot->rows[0].values[protocolIdColumn].utf16Value, L"ipv4") == 0 &&
               snapshot->rows[5].values[protocolIdColumn].utf16Value &&
               std::wcscmp(snapshot->rows[5].values[protocolIdColumn].utf16Value, L"udp6") == 0,
           "network protocol row identities are wrong");

    Expect(CollectOneSnapshot(*source, "storage.disk", &snapshot) == S_OK && snapshot,
           "storage disk collection failed");
    ValidateSnapshot(*snapshot, *storageDiskDescriptor);
    (void)FindColumn(*storageDiskDescriptor, "diskNumber");
    (void)FindColumn(*storageDiskDescriptor, "performanceAvailable");

    Expect(CollectOneSnapshot(*source, "storage.volume", &snapshot) == S_OK && snapshot,
           "storage volume collection failed");
    ValidateSnapshot(*snapshot, *storageVolumeDescriptor);
    Expect(snapshot->rowCount != 0, "storage volume table is empty");
    const uint32_t volumeGuidColumn = FindColumn(*storageVolumeDescriptor, "volumeGuid");
    bool foundVolumeGuid = false;
    for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        const RedXeDataValue& guid = snapshot->rows[rowIndex].values[volumeGuidColumn];
        if (guid.quality == RedXeDataQualityGood && guid.utf16Value &&
            std::wcsncmp(guid.utf16Value, L"\\\\?\\Volume", 10) == 0)
        {
            foundVolumeGuid = true;
            break;
        }
    }
    Expect(foundVolumeGuid, "storage volume table has no volume GUID identity");

    Expect(CollectOneSnapshot(*source, "gpu.adapter", &snapshot) == S_OK && snapshot, "gpu adapter collection failed");
    ValidateSnapshot(*snapshot, *gpuAdapterDescriptor);
    Expect(snapshot->rowCount != 0, "gpu adapter table is empty");
    const uint32_t adapterLuidColumn = FindColumn(*gpuAdapterDescriptor, "adapterLuid");
    const uint32_t softwareColumn = FindColumn(*gpuAdapterDescriptor, "software");
    const uint32_t integratedColumn = FindColumn(*gpuAdapterDescriptor, "integrated");
    const uint32_t adapterUtilizationColumn = FindColumn(*gpuAdapterDescriptor, "utilizationPercent");
    const uint32_t dedicatedUsedColumn = FindColumn(*gpuAdapterDescriptor, "dedicatedUsedBytes");
    const uint32_t sharedUsedColumn = FindColumn(*gpuAdapterDescriptor, "sharedUsedBytes");
    const uint32_t deviceClassColumn = FindColumn(*gpuAdapterDescriptor, "deviceClass");
    for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        const RedXeDataRow& row = snapshot->rows[rowIndex];
        Expect(row.values[adapterLuidColumn].quality == RedXeDataQualityGood, "gpu adapter LUID is missing");
        Expect(row.values[softwareColumn].quality == RedXeDataQualityGood &&
                   (row.values[softwareColumn].uint64Value == 0 || row.values[softwareColumn].uint64Value == 1),
               "gpu adapter software flag is not a Good 0/1 value");
        // These three came from DXCore once the adapter spine was inverted, and are still absent on a host whose
        // DXCore does not implement the prerelease state items. Either outcome is correct; a Good value that is not
        // plausible is not.
        Expect(row.values[integratedColumn].quality == RedXeDataQualityUnavailable ||
                   row.values[integratedColumn].uint64Value <= 1,
               "gpu adapter integrated flag is neither Unavailable nor a 0/1 value");
        Expect(row.values[adapterUtilizationColumn].quality == RedXeDataQualityUnavailable ||
                   (row.values[adapterUtilizationColumn].float64Value >= 0.0 &&
                    row.values[adapterUtilizationColumn].float64Value <= 100.0),
               "gpu adapter utilization is neither Unavailable nor a percentage");
        Expect(row.values[dedicatedUsedColumn].quality == row.values[sharedUsedColumn].quality,
               "gpu adapter machine-wide memory use is only half available");
        Expect(row.values[deviceClassColumn].quality == RedXeDataQualityUnavailable ||
                   row.values[deviceClassColumn].uint64Value != kTestDeviceClassNpu,
               "gpu.adapter published a compute-only accelerator that belongs in npu.adapter");
        if (rowIndex > 0)
        {
            Expect(row.values[adapterLuidColumn].uint64Value >
                       snapshot->rows[rowIndex - 1].values[adapterLuidColumn].uint64Value,
                   "gpu adapters are not sorted by LUID");
        }
    }
    Expect(CollectOneSnapshot(*source, "gpu.engine", &snapshot) == S_OK && snapshot, "gpu engine collection failed");
    ValidateSnapshot(*snapshot, *gpuEngineDescriptor);
    (void)FindColumn(*gpuEngineDescriptor, "nodeOrdinal");
    const uint32_t engineUtilizationColumn = FindColumn(*gpuEngineDescriptor, "utilizationPercent");
    for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        const RedXeDataValue& utilization = snapshot->rows[rowIndex].values[engineUtilizationColumn];
        Expect(utilization.quality == RedXeDataQualityUnavailable ||
                   (utilization.float64Value >= 0.0 && utilization.float64Value <= 100.0),
               "gpu engine utilization is neither Unavailable nor a percentage");
    }
    Expect(CollectOneSnapshot(*source, "gpu.process", &snapshot) == S_OK && snapshot, "gpu process collection failed");
    ValidateSnapshot(*snapshot, *gpuProcessDescriptor);
    Expect(CollectOneSnapshot(*source, "gpu.process", &snapshot) == S_OK && snapshot,
           "second gpu process collection failed");
    ValidateSnapshot(*snapshot, *gpuProcessDescriptor);

    Expect(CollectOneSnapshot(*source, "power.summary", &snapshot) == S_OK && snapshot,
           "power summary collection failed");
    ValidateSnapshot(*snapshot, *powerSummaryDescriptor);
    Expect(snapshot->rowCount == 1, "power summary does not contain exactly one row");
    const uint32_t acColumn = FindColumn(*powerSummaryDescriptor, "acOnline");
    const uint32_t batteryPresentColumn = FindColumn(*powerSummaryDescriptor, "batteryPresent");
    Expect(snapshot->rows[0].values[acColumn].quality == RedXeDataQualityGood ||
               snapshot->rows[0].values[acColumn].quality == RedXeDataQualityUnavailable,
           "power summary AC status quality is invalid");
    const bool batteryPresentGood = snapshot->rows[0].values[batteryPresentColumn].quality == RedXeDataQualityGood;
    const uint64_t batteryPresentValue = snapshot->rows[0].values[batteryPresentColumn].uint64Value;

    Expect(CollectOneSnapshot(*source, "battery.list", &snapshot) == S_OK && snapshot,
           "battery list collection failed");
    ValidateSnapshot(*snapshot, *batteryDescriptor);
    if (batteryPresentGood && batteryPresentValue == 0)
    {
        Expect(snapshot->rowCount == 0, "AC-only host published battery rows");
    }
    for (uint32_t column = 0; column < batteryDescriptor->columnCount; ++column)
    {
        Expect(std::strstr(batteryDescriptor->columns[column].columnId, "serial") == nullptr &&
                   std::strstr(batteryDescriptor->columns[column].columnId, "Serial") == nullptr,
               "battery list published a serial-number column");
    }

    bool gpuHasTemperature = false;
    bool gpuHasFan = false;
    Expect(CollectOneSnapshot(*source, "gpu.adapter", &snapshot) == S_OK && snapshot,
           "gpu adapter collection for thermal projection failed");
    const uint32_t gpuTemperatureColumn = FindColumn(*gpuAdapterDescriptor, "temperatureC");
    const uint32_t gpuMaxFanColumn = FindColumn(*gpuAdapterDescriptor, "maxFanRpm");
    for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        if (snapshot->rows[rowIndex].values[gpuTemperatureColumn].quality == RedXeDataQualityGood)
        {
            gpuHasTemperature = true;
        }
        if (snapshot->rows[rowIndex].values[gpuMaxFanColumn].quality == RedXeDataQualityGood &&
            snapshot->rows[rowIndex].values[gpuMaxFanColumn].uint64Value != 0)
        {
            gpuHasFan = true;
        }
    }

    Expect(CollectOneSnapshot(*source, "thermal.sensor", &snapshot) == S_OK && snapshot,
           "thermal sensor collection failed");
    ValidateSnapshot(*snapshot, *thermalDescriptor);
    if (gpuHasTemperature)
    {
        Expect(snapshot->rowCount != 0, "thermal sensor table omitted a GPU temperature");
    }

    Expect(CollectOneSnapshot(*source, "fan.sensor", &snapshot) == S_OK && snapshot, "fan sensor collection failed");
    ValidateSnapshot(*snapshot, *fanDescriptor);
    if (gpuHasFan)
    {
        Expect(snapshot->rowCount != 0, "fan sensor table omitted a GPU fan with a non-zero maximum RPM");
    }

    const char* netStorageIds[] = {"network.interface", "storage.volume"};
    RedXeDataCollectRequest netStorageRequest{sizeof(RedXeDataCollectRequest), netStorageIds, 2};
    collectResult = nullptr;
    Expect(source->CollectSnapshots(&netStorageRequest, &collectResult) == S_OK && collectResult &&
               collectResult->snapshotCount == 2 &&
               collectResult->snapshots[0]->sequence == collectResult->snapshots[1]->sequence,
           "network and volume batch did not share one sequence");

    std::array<const char*, RedXeDataCollectMaximumDataSets> catalogIds{};
    for (uint32_t index = 0; index < descriptorCount; ++index)
    {
        catalogIds[index] = descriptors[index].dataSetId;
    }
    RedXeDataCollectRequest catalogRequest{sizeof(RedXeDataCollectRequest), catalogIds.data(), descriptorCount};
    collectResult = nullptr;
    Expect(source->CollectSnapshots(&catalogRequest, &collectResult) == S_OK && collectResult &&
               collectResult->snapshotCount == descriptorCount && collectResult->snapshots,
           "full-catalog batch collection failed");
    const uint64_t catalogSequence = collectResult->snapshots[0]->sequence;
    const uint64_t catalogTimestamp = collectResult->snapshots[0]->timestampFileTime100ns;
    for (uint32_t index = 0; index < descriptorCount; ++index)
    {
        Expect(collectResult->snapshots[index] && collectResult->snapshots[index]->sequence == catalogSequence &&
                   collectResult->snapshots[index]->timestampFileTime100ns == catalogTimestamp,
               "full-catalog batch did not share one sequence and timestamp");
        ValidateSnapshot(*collectResult->snapshots[index], descriptors[index]);
    }
    ExpectNoWmiDelta(modulesBeforeCollect, LoadedModuleNames());

    for (uint32_t cycle = 0; cycle < 3; ++cycle)
    {
        void* extraObject = nullptr;
        Expect(create(__uuidof(IRedXeDataSource), nullptr, nullptr, "builtin.system-data", &extraObject) == S_OK &&
                   extraObject,
               "SystemData repeated source creation failed");
        wil::com_ptr_nothrow<IRedXeDataSource> extraSource;
        extraSource.attach(static_cast<IRedXeDataSource*>(extraObject));
        const RedXeDataSnapshot* extraSnapshot = nullptr;
        Expect(CollectOneSnapshot(*extraSource, "system.summary", &extraSnapshot) == S_OK && extraSnapshot,
               "repeated SystemData source collection failed");
        extraSource.reset();
    }

    snapshot = reinterpret_cast<const RedXeDataSnapshot*>(1);
    Expect(testSource->CollectSyntheticProcessSnapshot(processDescriptor->maximumRows + 1, &snapshot) == E_INVALIDARG &&
               !snapshot,
           "SystemData row-cap test seam accepted an excessive row count or retained an output");

    RunNativeLayoutOracles(*source, descriptors, descriptorCount);
    RunAcceleratorChecks(*source, descriptors, descriptorCount);

    if (benchmark)
    {
        RunResourceBenchmark(*testSource, *processDescriptor, diagnostics);
    }
    if (domains)
    {
        RunDomainMeasurement(*source, descriptors, descriptorCount);
    }
}
} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
    try
    {
        const std::wstring_view argument = argumentCount == 2 ? arguments[1] : L"";
        const bool benchmark = argument == L"--benchmark";
        const bool domains = argument == L"--domains";
        Expect(argumentCount == 1 || benchmark || domains, "unsupported SystemDataTests argument");
        Run(benchmark, domains);
        std::wcout << L"System data tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "System data tests failed: " << error.what() << '\n';
        return 1;
    }
}
