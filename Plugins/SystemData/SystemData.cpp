#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/Data.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "SystemDataTestContract.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <new>
#include <psapi.h>
#include <tlhelp32.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr char kPluginId[] = "builtin.system-data";
constexpr char kSummaryDataSetId[] = "system.summary";
constexpr char kProcessDataSetId[] = "process.list";
constexpr char kSettingsSchema[] = R"json({"type":"object","additionalProperties":false})json";
constexpr char kSettingsDefaults[] = R"json({})json";
constexpr std::uint32_t kRecommendedIntervalMilliseconds = 2000;
constexpr std::size_t kMaximumProcesses = 2048;
constexpr std::size_t kProcessNameCharacters = 65536;
constexpr std::size_t kProcessHistoryCapacity = 4096;
constexpr std::uint32_t kSummaryColumnCount = 10;
constexpr std::uint32_t kProcessColumnCount = 7;
static_assert((kProcessHistoryCapacity & (kProcessHistoryCapacity - 1)) == 0);

constexpr RedXePluginSettingsContract kSettingsContract{
    sizeof(RedXePluginSettingsContract), kSettingsSchema, sizeof(kSettingsSchema) - 1, kSettingsDefaults,
    sizeof(kSettingsDefaults) - 1,
};

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        kPluginId,
        L"System Data",
        L"Bounded local machine and process snapshots for RedXe data consumers.",
        L"RedXe",
        L"1.0.0",
        RedXePluginCapabilityDataSource,
    },
};

constexpr std::array kSummaryColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "totalCpuPercent", L"Total CPU", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "logicalProcessorCount", L"Logical processors",
                              nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "processCount", L"Processes", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "threadCount", L"Threads", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "handleCount", L"Handles", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "totalPhysicalBytes", L"Physical memory", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "availablePhysicalBytes", L"Available memory",
                              L"bytes", RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "usedPhysicalBytes", L"Used memory", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "committedBytes", L"Committed memory", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "uptimeMilliseconds", L"Uptime", L"milliseconds",
                              RedXeDataValueTypeUInt64},
};
static_assert(kSummaryColumns.size() == kSummaryColumnCount);

constexpr std::array kProcessColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "processId", L"Process ID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "imageName", L"Image name", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "cpuPercent", L"CPU", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "workingSetBytes", L"Working set", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "privateBytes", L"Private bytes", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "threadCount", L"Threads", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "handleCount", L"Handles", nullptr,
                              RedXeDataValueTypeUInt64},
};
static_assert(kProcessColumns.size() == kProcessColumnCount);

constexpr std::array kDataSets{
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kSummaryDataSetId,
        L"System summary",
        L"CPU, memory, uptime, and operating-system object counts for the local machine.",
        kSummaryColumns.data(),
        kSummaryColumnCount,
        1,
        kRecommendedIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kProcessDataSetId,
        L"Process list",
        L"Bounded process identity, CPU, memory, thread, and handle data visible to the current user.",
        kProcessColumns.data(),
        kProcessColumnCount,
        static_cast<std::uint32_t>(kMaximumProcesses),
        kRecommendedIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
};

[[nodiscard]] std::uint64_t FileTimeValue(const FILETIME& value) noexcept
{
    ULARGE_INTEGER combined{};
    combined.LowPart = value.dwLowDateTime;
    combined.HighPart = value.dwHighDateTime;
    return combined.QuadPart;
}

[[nodiscard]] std::uint64_t CurrentTimestamp() noexcept
{
    FILETIME timestamp{};
    GetSystemTimePreciseAsFileTime(&timestamp);
    return FileTimeValue(timestamp);
}

[[nodiscard]] RedXeDataValue UInt64Value(std::uint64_t value, RedXeDataQuality quality) noexcept
{
    RedXeDataValue result{};
    result.sizeBytes = sizeof(result);
    result.valueType = RedXeDataValueTypeUInt64;
    result.quality = quality;
    result.uint64Value = value;
    return result;
}

[[nodiscard]] RedXeDataValue Float64Value(double value, RedXeDataQuality quality) noexcept
{
    RedXeDataValue result{};
    result.sizeBytes = sizeof(result);
    result.valueType = RedXeDataValueTypeFloat64;
    result.quality = quality;
    result.float64Value = value;
    return result;
}

[[nodiscard]] RedXeDataValue Utf16Value(const wchar_t* value, std::uint32_t characters,
                                        RedXeDataQuality quality) noexcept
{
    RedXeDataValue result{};
    result.sizeBytes = sizeof(result);
    result.valueType = RedXeDataValueTypeUtf16;
    result.quality = quality;
    result.utf16Value = value;
    result.utf16Characters = characters;
    return result;
}

[[nodiscard]] bool PageCountToBytes(SIZE_T pageCount, SIZE_T pageSize, std::uint64_t& bytes) noexcept
{
    if (pageSize != 0 && pageCount > (UINT64_MAX / pageSize))
    {
        bytes = 0;
        return false;
    }
    bytes = static_cast<std::uint64_t>(pageCount) * static_cast<std::uint64_t>(pageSize);
    return true;
}

[[nodiscard]] bool ReadSystemTimes(std::uint64_t& idle, std::uint64_t& total) noexcept
{
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
    {
        idle = 0;
        total = 0;
        return false;
    }
    idle = FileTimeValue(idleTime);
    total = FileTimeValue(kernelTime) + FileTimeValue(userTime);
    return true;
}

[[nodiscard]] double BoundedPercent(std::uint64_t numerator, std::uint64_t denominator) noexcept
{
    if (denominator == 0)
    {
        return 0.0;
    }
    return std::clamp((static_cast<double>(numerator) * 100.0) / static_cast<double>(denominator), 0.0, 100.0);
}

struct ProcessCpuSample final
{
    std::uint32_t processId = 0;
    std::uint64_t creationTime = 0;
    std::uint64_t processorTime = 0;
};

class SystemDataSource final : public IRedXeDataSource, public IRedXeSystemDataTestSource
{
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeDataSource))
        {
            *result = static_cast<IRedXeDataSource*>(this);
        }
        else if (interfaceId == __uuidof(IRedXeSystemDataTestSource))
        {
            *result = static_cast<IRedXeSystemDataTestSource*>(this);
        }
        else
        {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return ++_references;
    }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        const ULONG references = --_references;
        if (references == 0)
        {
            delete this;
        }
        return references;
    }

    HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors,
                                          std::uint32_t* count) noexcept override
    {
        if (descriptors)
        {
            *descriptors = nullptr;
        }
        if (count)
        {
            *count = 0;
        }
        if (!descriptors || !count)
        {
            return E_POINTER;
        }
        *descriptors = kDataSets.data();
        *count = static_cast<std::uint32_t>(kDataSets.size());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CollectSnapshot(const char* dataSetId,
                                              const RedXeDataSnapshot** snapshot) noexcept override
    {
        if (snapshot)
        {
            *snapshot = nullptr;
        }
        if (!snapshot)
        {
            return E_POINTER;
        }
        if (!dataSetId || dataSetId[0] == '\0')
        {
            return E_INVALIDARG;
        }
        if (_collecting.test_and_set(std::memory_order_acquire))
        {
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        }
        auto clearCollecting = wil::scope_exit([this] { _collecting.clear(std::memory_order_release); });

        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kSummaryDataSetId))
        {
            return CollectSummary(snapshot);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kProcessDataSetId))
        {
            return CollectProcesses(snapshot);
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    HRESULT STDMETHODCALLTYPE GetTestDiagnostics(SystemDataTestDiagnostics* diagnostics) noexcept override
    {
        if (!diagnostics)
        {
            return E_POINTER;
        }
        if (diagnostics->sizeBytes != sizeof(SystemDataTestDiagnostics))
        {
            return E_INVALIDARG;
        }
        *diagnostics = SystemDataTestDiagnostics{
            sizeof(SystemDataTestDiagnostics),
            sizeof(SystemDataSource),
            static_cast<std::uint32_t>(kMaximumProcesses),
            static_cast<std::uint32_t>(kProcessNameCharacters),
            0,
            0,
        };
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CollectSyntheticProcessSnapshot(std::uint32_t requestedRows,
                                                              const RedXeDataSnapshot** snapshot) noexcept override
    {
        if (snapshot)
        {
            *snapshot = nullptr;
        }
        if (!snapshot)
        {
            return E_POINTER;
        }
        if (requestedRows > kMaximumProcesses)
        {
            return E_INVALIDARG;
        }
        if (_collecting.test_and_set(std::memory_order_acquire))
        {
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        }
        auto clearCollecting = wil::scope_exit([this] { _collecting.clear(std::memory_order_release); });

        std::uint64_t ignoredIdle = 0;
        std::uint64_t systemTotal = 0;
        const bool hasSystemTimes = ReadSystemTimes(ignoredIdle, systemTotal);
        const std::uint64_t systemDelta = hasSystemTimes && _hasProcessSystemTotal && systemTotal > _processSystemTotal
                                              ? systemTotal - _processSystemTotal
                                              : 0;

        PROCESSENTRY32W entry{};
        entry.dwSize = static_cast<DWORD>(sizeof(entry));
        entry.th32ProcessID = GetCurrentProcessId();
        entry.cntThreads = 1;
        constexpr wchar_t kSyntheticImageName[] = L"synthetic.exe";
        static_assert(std::size(kSyntheticImageName) <= std::size(entry.szExeFile));
        std::wmemcpy(entry.szExeFile, kSyntheticImageName, std::size(kSyntheticImageName));

        std::size_t rowCount = 0;
        std::size_t nameCharacters = 0;
        while (rowCount < requestedRows)
        {
            if (!AppendProcessRow(entry, systemDelta, rowCount, nameCharacters))
            {
                return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            }
        }
        FinishProcessCollection(rowCount, false, hasSystemTimes, systemTotal, snapshot);
        return S_OK;
    }

  private:
    [[nodiscard]] HRESULT CollectSummary(const RedXeDataSnapshot** output) noexcept
    {
        PERFORMANCE_INFORMATION performance{};
        performance.cb = static_cast<DWORD>(sizeof(performance));
        const bool hasPerformance =
            K32GetPerformanceInfo(&performance, static_cast<DWORD>(sizeof(performance))) != FALSE;

        std::uint64_t idle = 0;
        std::uint64_t total = 0;
        const bool hasTimes = ReadSystemTimes(idle, total);
        RedXeDataQuality cpuQuality = RedXeDataQualityUnavailable;
        double cpuPercent = 0.0;
        if (hasTimes)
        {
            if (_hasSummaryTimes && total > _summaryTotal && idle >= _summaryIdle)
            {
                const std::uint64_t totalDelta = total - _summaryTotal;
                const std::uint64_t idleDelta = idle - _summaryIdle;
                cpuPercent = BoundedPercent(totalDelta > idleDelta ? totalDelta - idleDelta : 0, totalDelta);
                cpuQuality = RedXeDataQualityGood;
            }
            else
            {
                cpuQuality = RedXeDataQualityInitializing;
            }
            _summaryIdle = idle;
            _summaryTotal = total;
            _hasSummaryTimes = true;
        }

        const DWORD processorCount = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        std::uint64_t totalPhysicalBytes = 0;
        std::uint64_t availablePhysicalBytes = 0;
        std::uint64_t committedBytes = 0;
        const bool totalPhysicalValid =
            hasPerformance && PageCountToBytes(performance.PhysicalTotal, performance.PageSize, totalPhysicalBytes);
        const bool availablePhysicalValid =
            hasPerformance &&
            PageCountToBytes(performance.PhysicalAvailable, performance.PageSize, availablePhysicalBytes);
        const bool committedValid =
            hasPerformance && PageCountToBytes(performance.CommitTotal, performance.PageSize, committedBytes);

        _summaryValues[0] = Float64Value(cpuPercent, cpuQuality);
        _summaryValues[1] =
            UInt64Value(processorCount, processorCount == 0 ? RedXeDataQualityUnavailable : RedXeDataQualityGood);
        _summaryValues[2] = UInt64Value(hasPerformance ? performance.ProcessCount : 0,
                                        hasPerformance ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        _summaryValues[3] = UInt64Value(hasPerformance ? performance.ThreadCount : 0,
                                        hasPerformance ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        _summaryValues[4] = UInt64Value(hasPerformance ? performance.HandleCount : 0,
                                        hasPerformance ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        _summaryValues[5] =
            UInt64Value(totalPhysicalBytes, totalPhysicalValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        _summaryValues[6] = UInt64Value(availablePhysicalBytes,
                                        availablePhysicalValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        _summaryValues[7] = UInt64Value(
            totalPhysicalValid && availablePhysicalValid && totalPhysicalBytes >= availablePhysicalBytes
                ? totalPhysicalBytes - availablePhysicalBytes
                : 0,
            totalPhysicalValid && availablePhysicalValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        _summaryValues[8] =
            UInt64Value(committedBytes, committedValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        _summaryValues[9] = UInt64Value(GetTickCount64(), RedXeDataQualityGood);
        _summaryRow = RedXeDataRow{sizeof(RedXeDataRow), _summaryValues.data(), kSummaryColumnCount};
        _summarySnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kSummaryDataSetId,
            ++_sequence,
            CurrentTimestamp(),
            &_summaryRow,
            1,
            kSummaryColumnCount,
        };
        *output = &_summarySnapshot;
        return S_OK;
    }

    [[nodiscard]] const ProcessCpuSample* FindPreviousProcess(std::uint32_t processId,
                                                              std::uint64_t creationTime) const noexcept
    {
        std::size_t index = (static_cast<std::size_t>(processId) * 2654435761U ^
                             static_cast<std::size_t>(creationTime ^ (creationTime >> 32U))) &
                            (kProcessHistoryCapacity - 1);
        for (std::size_t probe = 0; probe < kProcessHistoryCapacity; ++probe)
        {
            const ProcessCpuSample& candidate = _processHistory[index];
            if (candidate.creationTime == 0)
            {
                return nullptr;
            }
            if (candidate.processId == processId && candidate.creationTime == creationTime)
            {
                return &candidate;
            }
            index = (index + 1) & (kProcessHistoryCapacity - 1);
        }
        return nullptr;
    }

    void InsertProcessHistory(const ProcessCpuSample& sample) noexcept
    {
        if (sample.creationTime == 0)
        {
            return;
        }
        std::size_t index = (static_cast<std::size_t>(sample.processId) * 2654435761U ^
                             static_cast<std::size_t>(sample.creationTime ^ (sample.creationTime >> 32U))) &
                            (kProcessHistoryCapacity - 1);
        for (std::size_t probe = 0; probe < kProcessHistoryCapacity; ++probe)
        {
            ProcessCpuSample& candidate = _processHistory[index];
            if (candidate.creationTime == 0)
            {
                candidate = sample;
                return;
            }
            index = (index + 1) & (kProcessHistoryCapacity - 1);
        }
    }

    [[nodiscard]] bool AppendProcessRow(const PROCESSENTRY32W& entry, std::uint64_t systemDelta, std::size_t& rowCount,
                                        std::size_t& nameCharacters) noexcept
    {
        std::size_t imageCharacters = 0;
        while (imageCharacters < std::size(entry.szExeFile) && entry.szExeFile[imageCharacters] != L'\0')
        {
            ++imageCharacters;
        }
        if (rowCount >= kMaximumProcesses || nameCharacters + imageCharacters + 1 > _processNames.size())
        {
            return false;
        }

        wchar_t* imageName = _processNames.data() + nameCharacters;
        std::wmemcpy(imageName, entry.szExeFile, imageCharacters);
        imageName[imageCharacters] = L'\0';
        nameCharacters += imageCharacters + 1;

        const DWORD desiredAccess = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ;
        wil::unique_handle process{OpenProcess(desiredAccess, FALSE, entry.th32ProcessID)};
        const bool canReadMemory = static_cast<bool>(process);
        if (!process)
        {
            process.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        }

        ProcessCpuSample currentCpu{};
        currentCpu.processId = entry.th32ProcessID;
        RedXeDataQuality cpuQuality = process ? RedXeDataQualityInitializing : RedXeDataQualityUnavailable;
        double cpuPercent = 0.0;
        if (process)
        {
            FILETIME creation{};
            FILETIME exit{};
            FILETIME kernel{};
            FILETIME user{};
            if (GetProcessTimes(process.get(), &creation, &exit, &kernel, &user))
            {
                currentCpu.creationTime = FileTimeValue(creation);
                currentCpu.processorTime = FileTimeValue(kernel) + FileTimeValue(user);
                const ProcessCpuSample* previous = FindPreviousProcess(currentCpu.processId, currentCpu.creationTime);
                if (previous && currentCpu.processorTime >= previous->processorTime && systemDelta != 0)
                {
                    cpuPercent = BoundedPercent(currentCpu.processorTime - previous->processorTime, systemDelta);
                    cpuQuality = RedXeDataQualityGood;
                }
            }
            else
            {
                cpuQuality = RedXeDataQualityUnavailable;
            }
        }
        _currentCpu[rowCount] = currentCpu;

        std::uint64_t workingSet = 0;
        std::uint64_t privateBytes = 0;
        RedXeDataQuality memoryQuality = RedXeDataQualityUnavailable;
        if (canReadMemory)
        {
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = static_cast<DWORD>(sizeof(memory));
            if (K32GetProcessMemoryInfo(process.get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                        static_cast<DWORD>(sizeof(memory))))
            {
                workingSet = memory.WorkingSetSize;
                privateBytes = memory.PrivateUsage;
                memoryQuality = RedXeDataQualityGood;
            }
        }

        DWORD handleCount = 0;
        const RedXeDataQuality handleQuality = process && GetProcessHandleCount(process.get(), &handleCount)
                                                   ? RedXeDataQualityGood
                                                   : RedXeDataQualityUnavailable;

        RedXeDataValue* values = _processValues.data() + rowCount * kProcessColumnCount;
        values[0] = UInt64Value(entry.th32ProcessID, RedXeDataQualityGood);
        values[1] = Utf16Value(imageName, static_cast<std::uint32_t>(imageCharacters), RedXeDataQualityGood);
        values[2] = Float64Value(cpuPercent, cpuQuality);
        values[3] = UInt64Value(workingSet, memoryQuality);
        values[4] = UInt64Value(privateBytes, memoryQuality);
        values[5] = UInt64Value(entry.cntThreads, RedXeDataQualityGood);
        values[6] = UInt64Value(handleCount, handleQuality);
        _processRows[rowCount] = RedXeDataRow{sizeof(RedXeDataRow), values, kProcessColumnCount};
        ++rowCount;
        return true;
    }

    void FinishProcessCollection(std::size_t rowCount, bool truncated, bool hasSystemTimes, std::uint64_t systemTotal,
                                 const RedXeDataSnapshot** output) noexcept
    {
        _processHistory.fill({});
        for (std::size_t index = 0; index < rowCount; ++index)
        {
            InsertProcessHistory(_currentCpu[index]);
        }
        if (hasSystemTimes)
        {
            _processSystemTotal = systemTotal;
            _hasProcessSystemTotal = true;
        }

        _processSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            truncated ? RedXeDataSnapshotFlagTruncated : RedXeDataSnapshotFlagNone,
            kProcessDataSetId,
            ++_sequence,
            CurrentTimestamp(),
            _processRows.data(),
            static_cast<std::uint32_t>(rowCount),
            kProcessColumnCount,
        };
        *output = &_processSnapshot;
    }

    [[nodiscard]] HRESULT CollectProcesses(const RedXeDataSnapshot** output) noexcept
    {
        HANDLE rawSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (rawSnapshot == INVALID_HANDLE_VALUE)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        wil::unique_handle processSnapshot{rawSnapshot};

        std::uint64_t ignoredIdle = 0;
        std::uint64_t systemTotal = 0;
        const bool hasSystemTimes = ReadSystemTimes(ignoredIdle, systemTotal);
        const std::uint64_t systemDelta = hasSystemTimes && _hasProcessSystemTotal && systemTotal > _processSystemTotal
                                              ? systemTotal - _processSystemTotal
                                              : 0;

        std::size_t rowCount = 0;
        std::size_t nameCharacters = 0;
        bool truncated = false;
        PROCESSENTRY32W entry{};
        entry.dwSize = static_cast<DWORD>(sizeof(entry));
        BOOL hasEntry = Process32FirstW(processSnapshot.get(), &entry);
        DWORD enumerationError = hasEntry ? ERROR_SUCCESS : GetLastError();
        if (!hasEntry && enumerationError != ERROR_NO_MORE_FILES)
        {
            return HRESULT_FROM_WIN32(enumerationError);
        }

        while (hasEntry)
        {
            if (!AppendProcessRow(entry, systemDelta, rowCount, nameCharacters))
            {
                truncated = true;
                break;
            }

            hasEntry = Process32NextW(processSnapshot.get(), &entry);
            enumerationError = hasEntry ? ERROR_SUCCESS : GetLastError();
            if (!hasEntry && enumerationError != ERROR_NO_MORE_FILES)
            {
                return HRESULT_FROM_WIN32(enumerationError);
            }
        }

        FinishProcessCollection(rowCount, truncated, hasSystemTimes, systemTotal, output);
        return S_OK;
    }

    std::atomic<ULONG> _references{1};
    std::atomic_flag _collecting = ATOMIC_FLAG_INIT;
    std::uint64_t _sequence = 0;
    std::uint64_t _summaryIdle = 0;
    std::uint64_t _summaryTotal = 0;
    std::uint64_t _processSystemTotal = 0;
    bool _hasSummaryTimes = false;
    bool _hasProcessSystemTotal = false;
    std::array<RedXeDataValue, kSummaryColumnCount> _summaryValues{};
    RedXeDataRow _summaryRow{};
    RedXeDataSnapshot _summarySnapshot{};
    std::array<RedXeDataValue, kMaximumProcesses * kProcessColumnCount> _processValues{};
    std::array<RedXeDataRow, kMaximumProcesses> _processRows{};
    std::array<wchar_t, kProcessNameCharacters> _processNames{};
    std::array<ProcessCpuSample, kProcessHistoryCapacity> _processHistory{};
    std::array<ProcessCpuSample, kMaximumProcesses> _currentCpu{};
    RedXeDataSnapshot _processSnapshot{};
};

static_assert(sizeof(SystemDataSource) < 1024U * 1024U);

HRESULT CreateSystemDataSource(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost*,
                               void** result) noexcept
{
    if (interfaceId != __uuidof(IRedXeDataSource))
    {
        return E_NOINTERFACE;
    }
    const HRESULT configurationResult = RedXeValidateEmptyNormalizedConfiguration(options);
    if (FAILED(configurationResult))
    {
        return configurationResult;
    }

    auto* source = new (std::nothrow) SystemDataSource();
    if (!source)
    {
        return E_OUTOFMEMORY;
    }
    *result = static_cast<IRedXeDataSource*>(source);
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateSystemDataSource},
};
} // namespace

extern "C" HRESULT __stdcall RedXeCreate(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                         const char* pluginId, void** result) noexcept
{
    return RedXeCreateFromFactoryEntries(kFactoryEntries.data(), static_cast<std::uint32_t>(kFactoryEntries.size()),
                                         interfaceId, options, host, pluginId, result);
}

extern "C" HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata, std::uint32_t* count) noexcept
{
    return RedXeEnumerateFactoryMetadata(kMetadata.data(), static_cast<std::uint32_t>(kMetadata.size()), metadata,
                                         count);
}

extern "C" HRESULT __stdcall RedXeGetPluginSettingsContract(const char* pluginId,
                                                            const RedXePluginSettingsContract** contract) noexcept
{
    return RedXeGetStaticPluginSettingsContract(kPluginId, pluginId, &kSettingsContract, contract);
}
