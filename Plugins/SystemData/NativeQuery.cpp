#include "NativeQuery.h"

#include "NtLayout.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <powerbase.h>
#include <winternl.h>

namespace
{
constexpr NTSTATUS kStatusInfoLengthMismatch = static_cast<NTSTATUS>(0xC0000004L);
constexpr size_t kPerformanceProbeBytes = 1024;

// Microsoft documents PROCESSOR_POWER_INFORMATION on the CallNtPowerInformation reference page and states that its
// definition "was accidentally omitted from WinNT.h" and must be declared by the caller. This is that declaration.
struct RedXeProcessorPowerInformation final
{
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
};

static_assert(sizeof(RedXeProcessorPowerInformation) == 24);

using NtQuerySystemInformationFn = NTSTATUS(NTAPI*)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);

[[nodiscard]] NtQuerySystemInformationFn ResolveNtQuerySystem() noexcept
{
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
    {
        return nullptr;
    }
    const FARPROC procedure = GetProcAddress(ntdll, "NtQuerySystemInformation");
    NtQuerySystemInformationFn function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

[[nodiscard]] bool QuerySystem(NtQuerySystemInformationFn query, SYSTEM_INFORMATION_CLASS infoClass, void* buffer,
                               ULONG length, ULONG& returned) noexcept
{
    returned = 0;
    if (!query || !buffer || length == 0)
    {
        return false;
    }
    const NTSTATUS status = query(infoClass, buffer, length, &returned);
    return status >= 0 && returned > 0 && returned <= length;
}

[[nodiscard]] uint32_t HandleToPid(HANDLE value) noexcept
{
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(value));
}

[[nodiscard]] uint64_t NonNegative(int64_t value) noexcept
{
    return value < 0 ? 0 : static_cast<uint64_t>(value);
}

// Flat logical-processor index for a (group, group-relative index) pair, matching FillProcessorIdentity's ordering.
// Windows caps a machine at 64 processor groups; RedXe additionally caps total logical processors at 1,024.
constexpr size_t kMaximumProcessorGroups = 64;

struct GroupBaseTable final
{
    std::array<uint32_t, kMaximumProcessorGroups> base{};
    uint16_t groupCount = 0;
    uint32_t total = 0;
};

[[nodiscard]] GroupBaseTable BuildGroupBases() noexcept
{
    GroupBaseTable table{};
    const WORD groups = GetActiveProcessorGroupCount();
    table.groupCount = static_cast<uint16_t>((std::min)(static_cast<size_t>(groups), table.base.size()));
    uint32_t running = 0;
    for (uint16_t group = 0; group < table.groupCount; ++group)
    {
        table.base[group] = running;
        running += GetActiveProcessorCount(group);
    }
    table.total = running;
    return table;
}

[[nodiscard]] bool FlatIndex(const GroupBaseTable& table, uint16_t group, uint32_t groupRelative,
                             uint32_t& flat) noexcept
{
    if (group >= table.groupCount)
    {
        return false;
    }
    const uint64_t candidate = static_cast<uint64_t>(table.base[group]) + groupRelative;
    if (candidate >= kRedXeMaximumLogicalProcessors)
    {
        return false;
    }
    flat = static_cast<uint32_t>(candidate);
    return true;
}

void FillProcessorIdentity(RedXeNativeCheapSample& sample) noexcept
{
    const WORD groupCount = GetActiveProcessorGroupCount();
    uint32_t logical = 0;
    for (WORD group = 0; group < groupCount && logical < sample.logical.size(); ++group)
    {
        const DWORD count = GetActiveProcessorCount(group);
        for (DWORD index = 0; index < count && logical < sample.logical.size(); ++index)
        {
            sample.logical[logical].group = group;
            sample.logical[logical].index = static_cast<uint16_t>(index);
            ++logical;
        }
    }
    sample.logicalCount = logical;
    sample.topologyValid = logical != 0;
}

// Walks RelationAll once, counting cores/packages/NUMA nodes and recording the per-logical mapping. Cached: the
// result only changes when processor topology changes, which a bounded dashboard never sees mid-session.
void FillTopologyCache(RedXeNativeState& state, const GroupBaseTable& groups) noexcept
{
    RedXeNativeTopologyCache& cache = state.topology;
    DWORD required = 0;
    (void)GetLogicalProcessorInformationEx(RelationAll, nullptr, &required);
    if (required == 0 || required > state.topologyBuffer.size())
    {
        return;
    }
    DWORD size = required;
    if (GetLogicalProcessorInformationEx(
            RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(state.topologyBuffer.data()),
            &size) == FALSE)
    {
        return;
    }
    cache.coreIndex.fill(0);
    cache.packageIndex.fill(0);
    cache.numaNode.fill(0);
    cache.hasTopology.fill(false);
    uint32_t cores = 0;
    uint32_t packages = 0;
    uint32_t numa = 0;
    DWORD offset = 0;
    while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= size)
    {
        const auto* record =
            reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(state.topologyBuffer.data() + offset);
        if (record->Size == 0 || offset + record->Size > size)
        {
            return;
        }
        if (record->Relationship == RelationProcessorCore || record->Relationship == RelationProcessorPackage)
        {
            const bool isCore = record->Relationship == RelationProcessorCore;
            const uint32_t identifier = isCore ? cores : packages;
            const WORD affinityCount = isCore ? 1 : record->Processor.GroupCount;
            for (WORD entry = 0; entry < affinityCount; ++entry)
            {
                const GROUP_AFFINITY& affinity = record->Processor.GroupMask[entry];
                for (uint32_t bit = 0; bit < sizeof(KAFFINITY) * 8; ++bit)
                {
                    if ((affinity.Mask & (static_cast<KAFFINITY>(1) << bit)) == 0)
                    {
                        continue;
                    }
                    uint32_t flat = 0;
                    if (!FlatIndex(groups, affinity.Group, bit, flat))
                    {
                        continue;
                    }
                    if (isCore)
                    {
                        cache.coreIndex[flat] = identifier;
                    }
                    else
                    {
                        cache.packageIndex[flat] = identifier;
                    }
                    cache.hasTopology[flat] = true;
                }
            }
            if (isCore)
            {
                ++cores;
            }
            else
            {
                ++packages;
            }
        }
        else if (record->Relationship == RelationNumaNode || record->Relationship == RelationNumaNodeEx)
        {
            const GROUP_AFFINITY& affinity = record->NumaNode.GroupMask;
            for (uint32_t bit = 0; bit < sizeof(KAFFINITY) * 8; ++bit)
            {
                if ((affinity.Mask & (static_cast<KAFFINITY>(1) << bit)) == 0)
                {
                    continue;
                }
                uint32_t flat = 0;
                if (FlatIndex(groups, affinity.Group, bit, flat))
                {
                    cache.numaNode[flat] = record->NumaNode.NodeNumber;
                }
            }
            ++numa;
        }
        offset += record->Size;
    }
    cache.coreCount = cores;
    cache.packageCount = packages;
    cache.numaNodeCount = numa;
    cache.logicalCount = groups.total;
    cache.valid = true;
}

// CPU-set identity, efficiency class, and the dynamic parked/allocated flags. Documented Win32; no native class.
// Identity is written into the topology cache when `refreshCache` is set; parked/allocated are per-sample and always
// written straight into the sample.
void FillCpuSets(RedXeNativeState& state, RedXeNativeCheapSample& sample, const GroupBaseTable& groups,
                 bool refreshCache) noexcept
{
    RedXeNativeTopologyCache& cache = state.topology;
    if (refreshCache)
    {
        cache.hasCpuSet.fill(false);
    }
    ULONG required = 0;
    (void)GetSystemCpuSetInformation(nullptr, 0, &required, nullptr, 0);
    if (required == 0 || required > state.cpuSetBuffer.size())
    {
        return;
    }
    ULONG size = required;
    auto* buffer = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(state.cpuSetBuffer.data());
    if (GetSystemCpuSetInformation(buffer, size, &size, nullptr, 0) == FALSE)
    {
        return;
    }
    ULONG offset = 0;
    while (offset + sizeof(SYSTEM_CPU_SET_INFORMATION) <= size)
    {
        const auto* record = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(state.cpuSetBuffer.data() + offset);
        if (record->Size == 0 || offset + record->Size > size)
        {
            return;
        }
        if (record->Type == CpuSetInformation)
        {
            uint32_t flat = 0;
            if (FlatIndex(groups, record->CpuSet.Group, record->CpuSet.LogicalProcessorIndex, flat))
            {
                if (refreshCache)
                {
                    cache.cpuSetId[flat] = record->CpuSet.Id;
                    cache.efficiencyClass[flat] = record->CpuSet.EfficiencyClass;
                    cache.hasCpuSet[flat] = true;
                }
                if (flat < sample.logicalCount)
                {
                    sample.logical[flat].parked =
                        (record->CpuSet.AllFlags & SYSTEM_CPU_SET_INFORMATION_PARKED) != 0;
                    sample.logical[flat].allocated =
                        (record->CpuSet.AllFlags & SYSTEM_CPU_SET_INFORMATION_ALLOCATED) != 0;
                }
            }
        }
        offset += record->Size;
    }
}

void ApplyTopologyCache(const RedXeNativeState& state, RedXeNativeCheapSample& sample) noexcept
{
    const RedXeNativeTopologyCache& cache = state.topology;
    if (!cache.valid)
    {
        return;
    }
    sample.coreCount = cache.coreCount;
    sample.packageCount = cache.packageCount;
    sample.numaNodeCount = cache.numaNodeCount;
    for (uint32_t index = 0; index < sample.logicalCount; ++index)
    {
        RedXeNativeLogicalCpu& cpu = sample.logical[index];
        cpu.coreIndex = cache.coreIndex[index];
        cpu.packageIndex = cache.packageIndex[index];
        cpu.numaNode = cache.numaNode[index];
        cpu.hasTopology = cache.hasTopology[index];
        cpu.cpuSetId = cache.cpuSetId[index];
        cpu.efficiencyClass = cache.efficiencyClass[index];
        cpu.hasCpuSet = cache.hasCpuSet[index];
    }
}

// Per-logical current/maximum frequency. One documented call covers every logical processor.
void FillProcessorFrequency(RedXeNativeState& state, RedXeNativeCheapSample& sample) noexcept
{
    if (sample.logicalCount == 0)
    {
        return;
    }
    const size_t bytes = sizeof(RedXeProcessorPowerInformation) * sample.logicalCount;
    if (bytes > state.processorPowerBuffer.size() * sizeof(uint32_t))
    {
        return;
    }
    auto* power = reinterpret_cast<RedXeProcessorPowerInformation*>(state.processorPowerBuffer.data());
    if (CallNtPowerInformation(ProcessorInformation, nullptr, 0, power, static_cast<ULONG>(bytes)) != 0)
    {
        return;
    }
    for (uint32_t index = 0; index < sample.logicalCount; ++index)
    {
        RedXeNativeLogicalCpu& cpu = sample.logical[index];
        cpu.maxMhz = power[index].MaxMhz;
        cpu.currentMhz = power[index].CurrentMhz;
        cpu.mhzLimit = power[index].MhzLimit;
        cpu.hasFrequency = power[index].MaxMhz != 0;
    }
}

void FillProcessorTimes(NtQuerySystemInformationFn query, RedXeNativeState& state,
                        RedXeNativeCheapSample& sample) noexcept
{
    auto* records = reinterpret_cast<RedXeNtProcessorPerformance*>(state.processorTimeBuffer.data());
    static_assert(sizeof(decltype(RedXeNativeState::processorTimeBuffer){}) %
                      sizeof(RedXeNtProcessorPerformance) ==
                  0);
    const ULONG capacity =
        static_cast<ULONG>(kRedXeMaximumLogicalProcessors * sizeof(RedXeNtProcessorPerformance));
    ULONG returned = 0;
    if (!QuerySystem(query, SystemProcessorPerformanceInformation, records, capacity, returned))
    {
        return;
    }
    if (returned % sizeof(RedXeNtProcessorPerformance) != 0)
    {
        return;
    }
    const uint32_t count = static_cast<uint32_t>(returned / sizeof(RedXeNtProcessorPerformance));
    if (count == 0 || count > kRedXeMaximumLogicalProcessors)
    {
        return;
    }
    if (sample.logicalCount == 0)
    {
        FillProcessorIdentity(sample);
    }
    const uint32_t limit = (std::min)(count, sample.logicalCount != 0 ? sample.logicalCount : count);
    sample.logicalCount = limit;
    for (uint32_t index = 0; index < limit; ++index)
    {
        RedXeNativeLogicalCpu& cpu = sample.logical[index];
        cpu.idleTime = NonNegative(records[index].idleTime);
        cpu.kernelTime = NonNegative(records[index].kernelTime);
        cpu.userTime = NonNegative(records[index].userTime);
        cpu.dpcTime = NonNegative(records[index].dpcTime);
        cpu.interruptTime = NonNegative(records[index].interruptTime);
        cpu.interruptCount = records[index].interruptCount;
        cpu.hasTimes = true;
    }
    sample.processorTimesValid = true;
}

void FillInterruptCounts(NtQuerySystemInformationFn query, RedXeNativeState& state,
                         RedXeNativeCheapSample& sample) noexcept
{
    if (sample.logicalCount == 0)
    {
        return;
    }
    auto* records = reinterpret_cast<RedXeNtInterruptRecord*>(state.interruptBuffer.data());
    static_assert(sizeof(decltype(RedXeNativeState::interruptBuffer){}) % sizeof(RedXeNtInterruptRecord) == 0);
    const ULONG capacity = static_cast<ULONG>(kRedXeMaximumLogicalProcessors * sizeof(RedXeNtInterruptRecord));
    ULONG returned = 0;
    if (!QuerySystem(query, SystemInterruptInformation, records, capacity, returned))
    {
        return;
    }
    if (returned % sizeof(RedXeNtInterruptRecord) != 0)
    {
        return;
    }
    const uint32_t count = static_cast<uint32_t>(returned / sizeof(RedXeNtInterruptRecord));
    const uint32_t limit = (std::min)(count, sample.logicalCount);
    for (uint32_t index = 0; index < limit; ++index)
    {
        RedXeNativeLogicalCpu& cpu = sample.logical[index];
        cpu.contextSwitches = records[index].contextSwitches;
        cpu.dpcCount = records[index].dpcCount;
        cpu.dpcRate = records[index].dpcRate;
        cpu.dpcBypassCount = records[index].dpcBypassCount;
        cpu.apcBypassCount = records[index].apcBypassCount;
        cpu.hasInterrupt = true;
    }
}

// Probes with an oversized buffer so the kernel reports the record size it actually has, then accepts fields band by
// band. `sizeof` is never the gate: only the measured ReturnLength is.
void FillSystemPerformance(NtQuerySystemInformationFn query, RedXeNativeCheapSample& sample) noexcept
{
    std::array<std::byte, kPerformanceProbeBytes> buffer{};
    static_assert(kPerformanceProbeBytes >= sizeof(RedXeNtPerformance));
    ULONG returned = 0;
    if (!QuerySystem(query, SystemPerformanceInformation, buffer.data(), static_cast<ULONG>(buffer.size()), returned))
    {
        return;
    }
    if (returned < kRedXeNtPerformanceBaseBytes)
    {
        return;
    }
    const auto* record = reinterpret_cast<const RedXeNtPerformance*>(buffer.data());
    RedXeNativeSystemPerformance& out = sample.performance;
    out.valid = true;
    out.readableBytes = returned;
    out.idleProcessTime = NonNegative(record->idleProcessTime);
    out.ioReadTransferBytes = NonNegative(record->ioReadTransferCount);
    out.ioWriteTransferBytes = NonNegative(record->ioWriteTransferCount);
    out.ioOtherTransferBytes = NonNegative(record->ioOtherTransferCount);
    out.ioReadOperationCount = record->ioReadOperationCount;
    out.ioWriteOperationCount = record->ioWriteOperationCount;
    out.ioOtherOperationCount = record->ioOtherOperationCount;
    out.availablePages = record->availablePages;
    out.committedPages = record->committedPages;
    out.commitLimit = record->commitLimit;
    out.peakCommitment = record->peakCommitment;
    out.pageFaultCount = record->pageFaultCount;
    out.copyOnWriteCount = record->copyOnWriteCount;
    out.transitionCount = record->transitionCount;
    out.demandZeroCount = record->demandZeroCount;
    out.pageReadCount = record->pageReadCount;
    out.pageReadIoCount = record->pageReadIoCount;
    out.dirtyPagesWriteCount = record->dirtyPagesWriteCount;
    out.mappedPagesWriteCount = record->mappedPagesWriteCount;
    out.pagedPoolPages = record->pagedPoolPages;
    out.nonPagedPoolPages = record->nonPagedPoolPages;
    out.availablePagedPoolPages = record->availablePagedPoolPages;
    out.freeSystemPtes = record->freeSystemPtes;
    out.residentSystemCachePage = record->residentSystemCachePage;
    out.contextSwitches = record->contextSwitches;
    out.systemCalls = record->systemCalls;
    out.firstLevelTbFills = record->firstLevelTbFills;
    out.secondLevelTbFills = record->secondLevelTbFills;

    if (returned >= kRedXeNtPerformanceThresholdBytes)
    {
        out.hasThresholdTail = true;
        out.ccTotalDirtyPages = record->ccTotalDirtyPages;
        out.ccDirtyPageThreshold = record->ccDirtyPageThreshold;
        out.residentAvailablePages = record->residentAvailablePages;
        out.sharedCommittedPages = record->sharedCommittedPages;
    }
    if (returned >= kRedXeNtPerformance24H2Bytes)
    {
        out.has24H2Tail = true;
        out.pfnDatabaseCommittedPages = record->pfnDatabaseCommittedPages;
        out.systemPageTableCommittedPages = record->systemPageTableCommittedPages;
    }
}

void FillTimeOfDay(NtQuerySystemInformationFn query, RedXeNativeCheapSample& sample) noexcept
{
    RedXeNtTimeOfDay record{};
    ULONG returned = 0;
    if (!QuerySystem(query, SystemTimeOfDayInformation, &record, static_cast<ULONG>(sizeof(record)), returned))
    {
        return;
    }
    if (returned != sizeof(record))
    {
        return;
    }
    RedXeNativeTimeOfDay& out = sample.timeOfDay;
    out.valid = true;
    out.bootTime = NonNegative(record.bootTime);
    out.currentTime = NonNegative(record.currentTime);
    out.timeZoneBias = record.timeZoneBias;
    out.timeZoneId = record.timeZoneId;
    out.bootTimeBias = record.bootTimeBias;
    out.sleepTimeBias = record.sleepTimeBias;
}

void FillExceptions(NtQuerySystemInformationFn query, RedXeNativeCheapSample& sample) noexcept
{
    RedXeNtExceptionRecord record{};
    ULONG returned = 0;
    if (!QuerySystem(query, SystemExceptionInformation, &record, static_cast<ULONG>(sizeof(record)), returned))
    {
        return;
    }
    if (returned != sizeof(record))
    {
        return;
    }
    RedXeNativeExceptions& out = sample.exceptions;
    out.valid = true;
    out.alignmentFixupCount = record.alignmentFixupCount;
    out.exceptionDispatchCount = record.exceptionDispatchCount;
    out.floatingEmulationCount = record.floatingEmulationCount;
    out.byteWordEmulationCount = record.byteWordEmulationCount;
}

// Copies one image name into the walk sample's shared arena and points the row at it. Returns false only when the
// source string lies outside the queried buffer, which is a malformed record; an arena that is full leaves the row
// unnamed rather than failing the walk.
[[nodiscard]] bool CopyImageName(const UNICODE_STRING& image, const std::byte* bufferBase, size_t bufferBytes,
                                 RedXeNativeWalkSample& sample, RedXeNativeProcessRow& row) noexcept
{
    row.imageCharacters = 0;
    row.imageOffset = 0;
    if (!image.Buffer || image.Length == 0)
    {
        return true;
    }
    const auto* begin = reinterpret_cast<const std::byte*>(image.Buffer);
    if (begin < bufferBase || begin + image.Length > bufferBase + bufferBytes)
    {
        return false;
    }
    const uint32_t characters = (std::min)(static_cast<uint32_t>(image.Length / sizeof(wchar_t)),
                                           static_cast<uint32_t>(kRedXeNativeImageCharacters - 1));
    if (static_cast<size_t>(sample.imageArenaUsed) + characters + 1 > sample.imageArena.size())
    {
        return true;
    }
    wchar_t* destination = sample.imageArena.data() + sample.imageArenaUsed;
    std::wmemcpy(destination, image.Buffer, characters);
    destination[characters] = L'\0';
    row.imageOffset = sample.imageArenaUsed;
    row.imageCharacters = characters;
    sample.imageArenaUsed += characters + 1;
    return true;
}
} // namespace

bool RedXeNativeQueryCheap(RedXeNativeState& state, RedXeNativeCheapSample& sample) noexcept
{
    sample = RedXeNativeCheapSample{};
    FillProcessorIdentity(sample);

    const GroupBaseTable groups = BuildGroupBases();
    const bool topologyChanged = !state.topology.valid || state.topology.logicalCount != groups.total;
    if (topologyChanged)
    {
        FillTopologyCache(state, groups);
    }
    FillCpuSets(state, sample, groups, topologyChanged);
    ApplyTopologyCache(state, sample);
    FillProcessorFrequency(state, sample);

    const NtQuerySystemInformationFn query = ResolveNtQuerySystem();
    if (!query)
    {
        return sample.topologyValid;
    }

    SYSTEM_HANDLECOUNT_INFORMATION handles{};
    ULONG returned = 0;
    if (QuerySystem(query, SystemHandleCountInformation, &handles, static_cast<ULONG>(sizeof(handles)), returned) &&
        returned == sizeof(handles))
    {
        sample.handleCountsValid = true;
        sample.processCount = handles.ProcessCount;
        sample.threadCount = handles.ThreadCount;
        sample.handleCount = handles.HandleCount;
    }

    FillProcessorTimes(query, state, sample);
    FillInterruptCounts(query, state, sample);
    FillSystemPerformance(query, sample);
    FillTimeOfDay(query, sample);
    FillExceptions(query, sample);
    return sample.handleCountsValid || sample.processorTimesValid || sample.topologyValid;
}

bool RedXeNativeQueryWalk(RedXeNativeState& state, RedXeNativeWalkSample& sample) noexcept
{
    sample.valid = false;
    sample.processesTruncated = false;
    sample.threadsTruncated = false;
    sample.processCount = 0;
    sample.threadCount = 0;
    sample.imageArenaUsed = 0;
    const NtQuerySystemInformationFn query = ResolveNtQuerySystem();
    if (!query)
    {
        return false;
    }

    ULONG returned = 0;
    size_t length = 1024U * 1024U;
    NTSTATUS status = kStatusInfoLengthMismatch;
    while (length <= state.processBuffer.size())
    {
        status = query(SystemProcessInformation, state.processBuffer.data(), static_cast<ULONG>(length), &returned);
        if (status >= 0)
        {
            break;
        }
        if (status != kStatusInfoLengthMismatch)
        {
            return false;
        }
        const size_t next = length * 2;
        if (next > state.processBuffer.size())
        {
            return false;
        }
        length = next;
    }
    if (status < 0 || returned == 0 || returned > length)
    {
        return false;
    }

    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo);

    size_t offset = 0;
    for (;;)
    {
        if (offset + sizeof(RedXeNtProcessRecord) > returned)
        {
            return false;
        }
        const auto* record = reinterpret_cast<const RedXeNtProcessRecord*>(state.processBuffer.data() + offset);
        const size_t threadBytes = static_cast<size_t>(record->numberOfThreads) * sizeof(RedXeNtThreadRecord);
        if (offset + sizeof(RedXeNtProcessRecord) + threadBytes > returned)
        {
            return false;
        }
        const auto* threads = reinterpret_cast<const RedXeNtThreadRecord*>(state.processBuffer.data() + offset +
                                                                          sizeof(RedXeNtProcessRecord));

        if (sample.processCount < sample.processes.size())
        {
            RedXeNativeProcessRow& row = sample.processes[sample.processCount];
            row.processId = HandleToPid(record->uniqueProcessId);
            row.parentProcessId = HandleToPid(record->inheritedFromUniqueProcessId);
            row.hasParent = true;
            row.threadCount = record->numberOfThreads;
            row.handleCount = record->handleCount;
            row.sessionId = record->sessionId;
            row.basePriority = record->basePriority;
            row.pageFaultCount = record->pageFaultCount;
            row.hardFaultCount = record->hardFaultCount;
            row.createTime = NonNegative(record->createTime);
            row.userTime = NonNegative(record->userTime);
            row.kernelTime = NonNegative(record->kernelTime);
            row.cycleTime = record->cycleTime;
            row.virtualBytes = record->virtualSize;
            row.peakWorkingSetBytes = record->peakWorkingSetSize;
            row.workingSetBytes = record->workingSetSize;
            row.workingSetPrivateBytes = record->workingSetPrivateSize;
            row.pagefileBytes = record->pagefileUsage;
            row.ioReadBytes = NonNegative(record->readTransferCount);
            row.ioWriteBytes = NonNegative(record->writeTransferCount);
            row.ioOtherBytes = NonNegative(record->otherTransferCount);
            row.ioReadOperations = NonNegative(record->readOperationCount);
            row.ioWriteOperations = NonNegative(record->writeOperationCount);
            row.ioOtherOperations = NonNegative(record->otherOperationCount);
            // Rows are reused across walks without clearing the whole array, so every field must be written on every
            // pass. This one is conditional, so it is zeroed first rather than left holding a previous process's value.
            row.privateBytes = 0;
            if (systemInfo.dwPageSize != 0 && record->privatePageCount <= (UINT64_MAX / systemInfo.dwPageSize))
            {
                row.privateBytes =
                    static_cast<uint64_t>(record->privatePageCount) * static_cast<uint64_t>(systemInfo.dwPageSize);
            }
            if (!CopyImageName(record->imageName, state.processBuffer.data(), returned, sample, row))
            {
                return false;
            }
            ++sample.processCount;
        }
        else
        {
            sample.processesTruncated = true;
        }

        for (uint32_t threadIndex = 0; threadIndex < record->numberOfThreads; ++threadIndex)
        {
            if (sample.threadCount < sample.threads.size())
            {
                const RedXeNtThreadRecord& thread = threads[threadIndex];
                RedXeNativeThreadRow& row = sample.threads[sample.threadCount];
                row.processId = HandleToPid(thread.clientId.UniqueProcess);
                row.threadId = HandleToPid(thread.clientId.UniqueThread);
                row.priority = thread.priority;
                row.basePriority = thread.basePriority;
                row.threadState = thread.threadState;
                row.waitReason = thread.waitReason;
                row.contextSwitches = thread.contextSwitches;
                row.createTime = NonNegative(thread.createTime);
                row.userTime = NonNegative(thread.userTime);
                row.kernelTime = NonNegative(thread.kernelTime);
                ++sample.threadCount;
            }
            else
            {
                sample.threadsTruncated = true;
                break;
            }
        }

        if (record->nextEntryOffset == 0)
        {
            sample.valid = true;
            return true;
        }
        if (record->nextEntryOffset < sizeof(RedXeNtProcessRecord) || offset + record->nextEntryOffset > returned)
        {
            return false;
        }
        offset += record->nextEntryOffset;
    }
}

bool RedXeNativeQueryCodeIntegrity(RedXeNativeCodeIntegrity& integrity) noexcept
{
    integrity = RedXeNativeCodeIntegrity{};
    const NtQuerySystemInformationFn query = ResolveNtQuerySystem();
    if (!query)
    {
        return false;
    }
    SYSTEM_CODEINTEGRITY_INFORMATION record{};
    record.Length = static_cast<ULONG>(sizeof(record));
    ULONG returned = 0;
    if (!QuerySystem(query, SystemCodeIntegrityInformation, &record, static_cast<ULONG>(sizeof(record)), returned))
    {
        return false;
    }
    if (returned != sizeof(record))
    {
        return false;
    }
    integrity.valid = true;
    integrity.options = record.CodeIntegrityOptions;
    return true;
}
