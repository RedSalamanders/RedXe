#include "../../Plugins/SystemData/SystemDataTestContract.h"
#include "PlugInterfaces/Data.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Widget.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <new>
#include <psapi.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
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

[[nodiscard]] const RedXeDataSetDescriptor* FindDataSet(const RedXeDataSetDescriptor* descriptors, std::uint32_t count,
                                                        const char* id)
{
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (descriptors[index].dataSetId && RedXeAsciiEqualsIgnoreCase(descriptors[index].dataSetId, id))
        {
            return &descriptors[index];
        }
    }
    return nullptr;
}

[[nodiscard]] std::uint32_t FindColumn(const RedXeDataSetDescriptor& descriptor, const char* id)
{
    for (std::uint32_t index = 0; index < descriptor.columnCount; ++index)
    {
        if (descriptor.columns[index].columnId && RedXeAsciiEqualsIgnoreCase(descriptor.columns[index].columnId, id))
        {
            return index;
        }
    }
    throw std::runtime_error("required data column is missing");
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

    for (std::uint32_t rowIndex = 0; rowIndex < snapshot.rowCount; ++rowIndex)
    {
        const RedXeDataRow& row = snapshot.rows[rowIndex];
        Expect(row.sizeBytes == sizeof(RedXeDataRow), "row record size is invalid");
        Expect(row.values && row.valueCount == descriptor.columnCount, "row has the wrong value shape");
        for (std::uint32_t valueIndex = 0; valueIndex < row.valueCount; ++valueIndex)
        {
            const RedXeDataValue& value = row.values[valueIndex];
            Expect(value.sizeBytes == sizeof(RedXeDataValue), "value record size is invalid");
            Expect(value.valueType == descriptor.columns[valueIndex].valueType, "value type differs from descriptor");
            Expect(value.quality <= RedXeDataQualityInitializing, "value quality is outside the contract");
            if (value.valueType == RedXeDataValueTypeFloat64)
            {
                Expect(std::isfinite(value.float64Value), "floating-point value is not finite");
                if (std::strstr(descriptor.columns[valueIndex].columnId, "Cpu") ||
                    std::strstr(descriptor.columns[valueIndex].columnId, "cpu"))
                {
                    Expect(value.float64Value >= 0.0 && value.float64Value <= 100.0, "CPU percentage is outside 0-100");
                }
            }
            if (value.valueType == RedXeDataValueTypeUtf16 && value.quality == RedXeDataQualityGood)
            {
                Expect(value.utf16Value != nullptr, "good UTF-16 value has no storage");
            }
        }
    }
}

struct ProcessMemorySnapshot final
{
    std::uint64_t privateBytes = 0;
    std::uint64_t workingSetBytes = 0;
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
    std::uint64_t busyBlocks = 0;
    std::uint64_t busyBytes = 0;
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

[[nodiscard]] std::uint64_t FileTime100ns(const FILETIME& value) noexcept
{
    ULARGE_INTEGER combined{};
    combined.LowPart = value.dwLowDateTime;
    combined.HighPart = value.dwHighDateTime;
    return combined.QuadPart;
}

void RunResourceBenchmark(IRedXeSystemDataTestSource& testSource, const RedXeDataSetDescriptor& processDescriptor,
                          const SystemDataTestDiagnostics& diagnostics)
{
    Expect(diagnostics.sourceStorageBytes < 1024ULL * 1024ULL, "SystemData source storage exceeds 1 MiB");
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

    constexpr std::uint32_t collectionCount = 2;
    for (std::uint32_t collection = 0; collection < collectionCount; ++collection)
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

    const std::uint64_t kernelDelta = FileTime100ns(kernelAfter) - FileTime100ns(kernelBefore);
    const std::uint64_t userDelta = FileTime100ns(userAfter) - FileTime100ns(userBefore);
    const double wallMicroseconds =
        static_cast<double>(finish.QuadPart - start.QuadPart) * 1'000'000.0 / static_cast<double>(frequency.QuadPart);
    const double cpuMicroseconds = static_cast<double>(kernelDelta + userDelta) / 10.0;
    const auto privateDelta =
        static_cast<std::int64_t>(memoryAfter.privateBytes) - static_cast<std::int64_t>(memoryBefore.privateBytes);
    const auto workingSetDelta = static_cast<std::int64_t>(memoryAfter.workingSetBytes) -
                                 static_cast<std::int64_t>(memoryBefore.workingSetBytes);

    constexpr std::uint32_t cpuCollectionCount = 64;
    FILETIME cpuCreationBefore{};
    FILETIME cpuExitBefore{};
    FILETIME cpuKernelBefore{};
    FILETIME cpuUserBefore{};
    Expect(GetProcessTimes(GetCurrentProcess(), &cpuCreationBefore, &cpuExitBefore, &cpuKernelBefore, &cpuUserBefore) !=
               FALSE,
           "CPU-probe start query failed");
    for (std::uint32_t collection = 0; collection < cpuCollectionCount; ++collection)
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
    const std::uint64_t cpuProbe100ns = FileTime100ns(cpuKernelAfter) - FileTime100ns(cpuKernelBefore) +
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

void Run(bool benchmark)
{
    const std::filesystem::path pluginPath = PluginPath();
    wil::unique_hmodule module{
        LoadLibraryExW(pluginPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    Expect(static_cast<bool>(module), "SystemData.dll could not be loaded");

    const RedXeCreateFn create = Resolve<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const RedXeEnumeratePluginsFn enumerate =
        Resolve<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    const RedXeGetPluginSettingsContractFn getSettings =
        Resolve<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);

    const RedXePluginMetadata* metadata = reinterpret_cast<const RedXePluginMetadata*>(1);
    std::uint32_t metadataCount = 99;
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
    SystemDataTestDiagnostics shortDiagnostics{sizeof(std::uint32_t)};
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
    std::uint32_t descriptorCount = 99;
    Expect(source->GetDataSets(nullptr, &descriptorCount) == E_POINTER && descriptorCount == 0,
           "GetDataSets did not clear count on a null descriptor output");
    Expect(source->GetDataSets(&descriptors, &descriptorCount) == S_OK && descriptors && descriptorCount == 2,
           "SystemData descriptors are unavailable");
    const RedXeDataSetDescriptor* summaryDescriptor = FindDataSet(descriptors, descriptorCount, "system.summary");
    const RedXeDataSetDescriptor* processDescriptor = FindDataSet(descriptors, descriptorCount, "process.list");
    Expect(summaryDescriptor && processDescriptor && summaryDescriptor->maximumRows == 1 &&
               processDescriptor->maximumRows == 2048,
           "SystemData descriptor bounds are wrong");

    const RedXeDataSnapshot* snapshot = reinterpret_cast<const RedXeDataSnapshot*>(1);
    Expect(source->CollectSnapshot("missing", &snapshot) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !snapshot,
           "unknown dataset collection did not clear its output");
    Expect(source->CollectSnapshot("system.summary", &snapshot) == S_OK && snapshot,
           "system summary collection failed");
    ValidateSnapshot(*snapshot, *summaryDescriptor);
    Expect(snapshot->rowCount == 1, "system summary does not contain exactly one row");
    const std::uint64_t firstSequence = snapshot->sequence;
    Expect(source->CollectSnapshot("system.summary", &snapshot) == S_OK && snapshot->sequence > firstSequence,
           "system summary sequence did not advance");
    ValidateSnapshot(*snapshot, *summaryDescriptor);

    Expect(source->CollectSnapshot("process.list", &snapshot) == S_OK && snapshot, "process list collection failed");
    ValidateSnapshot(*snapshot, *processDescriptor);
    const std::uint32_t processIdColumn = FindColumn(*processDescriptor, "processId");
    const std::uint32_t imageNameColumn = FindColumn(*processDescriptor, "imageName");
    bool foundCurrentProcess = false;
    for (std::uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        const RedXeDataRow& row = snapshot->rows[rowIndex];
        if (row.values[processIdColumn].uint64Value == GetCurrentProcessId())
        {
            foundCurrentProcess = row.values[imageNameColumn].quality == RedXeDataQualityGood &&
                                  row.values[imageNameColumn].utf16Characters != 0;
            break;
        }
    }
    Expect(foundCurrentProcess, "process list does not contain the test process");
    const std::uint64_t processSequence = snapshot->sequence;
    Expect(source->CollectSnapshot("process.list", &snapshot) == S_OK && snapshot->sequence > processSequence,
           "process list sequence did not advance");
    ValidateSnapshot(*snapshot, *processDescriptor);
    snapshot = reinterpret_cast<const RedXeDataSnapshot*>(1);
    Expect(testSource->CollectSyntheticProcessSnapshot(processDescriptor->maximumRows + 1, &snapshot) == E_INVALIDARG &&
               !snapshot,
           "SystemData row-cap test seam accepted an excessive row count or retained an output");

    if (benchmark)
    {
        RunResourceBenchmark(*testSource, *processDescriptor, diagnostics);
    }
}
} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
    try
    {
        const bool benchmark = argumentCount == 2 && std::wstring_view(arguments[1]) == L"--benchmark";
        Expect(argumentCount == 1 || benchmark, "unsupported SystemDataTests argument");
        Run(benchmark);
        std::wcout << L"System data tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "System data tests failed: " << error.what() << '\n';
        return 1;
    }
}
