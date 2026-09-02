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
    Expect(source->GetDataSets(&descriptors, &descriptorCount) == S_OK && descriptors && descriptorCount == 18,
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
            Expect(!ContainsAsciiIgnoreCase(columnId, "macAddress") &&
                       !ContainsAsciiIgnoreCase(columnId, "ipAddress") &&
                       !ContainsAsciiIgnoreCase(columnId, "serialNumber") &&
                       !ContainsAsciiIgnoreCase(columnId, "commandLine") &&
                       !ContainsAsciiIgnoreCase(columnId, "imagePath") &&
                       !ContainsAsciiIgnoreCase(columnId, "userName") &&
                       !ContainsAsciiIgnoreCase(columnId, "physicalAddress") &&
                       !RedXeAsciiEqualsIgnoreCase(columnId, "serial") &&
                       !RedXeAsciiEqualsIgnoreCase(columnId, "ssid") && !RedXeAsciiEqualsIgnoreCase(columnId, "mac") &&
                       !RedXeAsciiEqualsIgnoreCase(columnId, "uniqueId"),
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
            processDescriptor->maximumRows == 2048 && processDescriptor->columnCount == 31 &&
            cpuLogicalDescriptor->maximumRows == 1024 && threadDescriptor->maximumRows == 8192 &&
            networkInterfaceDescriptor->maximumRows == 256 &&
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
    Expect(snapshot->rowCount == 18, "source status does not contain one row per dataset");
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
    for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
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
    for (uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        const RedXeDataRow& row = snapshot->rows[rowIndex];
        Expect(row.values[adapterLuidColumn].quality == RedXeDataQualityGood, "gpu adapter LUID is missing");
        Expect(row.values[softwareColumn].quality == RedXeDataQualityGood &&
                   (row.values[softwareColumn].uint64Value == 0 || row.values[softwareColumn].uint64Value == 1),
               "gpu adapter software flag is not a Good 0/1 value");
        Expect(row.values[integratedColumn].quality == RedXeDataQualityUnavailable,
               "gpu adapter integrated flag is not Unavailable without DXCore");
        Expect(row.values[adapterUtilizationColumn].quality == RedXeDataQualityUnavailable,
               "gpu adapter utilization is not Unavailable");
        Expect(row.values[dedicatedUsedColumn].quality == RedXeDataQualityUnavailable &&
                   row.values[sharedUsedColumn].quality == RedXeDataQualityUnavailable,
               "gpu adapter machine-wide memory use is not Unavailable");
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
        Expect(snapshot->rows[rowIndex].values[engineUtilizationColumn].quality == RedXeDataQualityUnavailable,
               "gpu engine utilization is not Unavailable");
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
