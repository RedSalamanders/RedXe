#include "NativeQuery.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <winternl.h>

namespace
{
constexpr NTSTATUS kStatusInfoLengthMismatch = static_cast<NTSTATUS>(0xC0000004L);
constexpr size_t kMaximumParentMap = 4096;

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

struct ParentMapEntry final
{
    uint32_t processId = 0;
    uint32_t parentProcessId = 0;
    bool occupied = false;
};

[[nodiscard]] size_t ParentSlot(uint32_t processId) noexcept
{
    return (static_cast<size_t>(processId) * 2654435761U) & (kMaximumParentMap - 1);
}

void InsertParent(std::array<ParentMapEntry, kMaximumParentMap>& map, uint32_t processId,
                  uint32_t parentProcessId) noexcept
{
    if (processId == 0)
    {
        return;
    }
    size_t index = ParentSlot(processId);
    for (size_t probe = 0; probe < kMaximumParentMap; ++probe)
    {
        ParentMapEntry& entry = map[index];
        if (!entry.occupied || entry.processId == processId)
        {
            entry.processId = processId;
            entry.parentProcessId = parentProcessId;
            entry.occupied = true;
            return;
        }
        index = (index + 1) & (kMaximumParentMap - 1);
    }
}

[[nodiscard]] bool FindParent(const std::array<ParentMapEntry, kMaximumParentMap>& map, uint32_t processId,
                              uint32_t& parentProcessId) noexcept
{
    size_t index = ParentSlot(processId);
    for (size_t probe = 0; probe < kMaximumParentMap; ++probe)
    {
        const ParentMapEntry& entry = map[index];
        if (!entry.occupied)
        {
            return false;
        }
        if (entry.processId == processId)
        {
            parentProcessId = entry.parentProcessId;
            return true;
        }
        index = (index + 1) & (kMaximumParentMap - 1);
    }
    return false;
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

void FillTopologyCounts(RedXeNativeState& state, RedXeNativeCheapSample& sample) noexcept
{
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
        if (record->Relationship == RelationProcessorCore)
        {
            ++cores;
        }
        else if (record->Relationship == RelationProcessorPackage)
        {
            ++packages;
        }
        else if (record->Relationship == RelationNumaNode || record->Relationship == RelationNumaNodeEx)
        {
            ++numa;
        }
        offset += record->Size;
    }
    sample.coreCount = cores;
    sample.packageCount = packages;
    sample.numaNodeCount = numa;
}

void FillProcessorTimes(NtQuerySystemInformationFn query, RedXeNativeCheapSample& sample) noexcept
{
    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION records[kRedXeMaximumLogicalProcessors]{};
    ULONG returned = 0;
    if (!QuerySystem(query, SystemProcessorPerformanceInformation, records, static_cast<ULONG>(sizeof(records)),
                     returned))
    {
        return;
    }
    if (returned % sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) != 0)
    {
        return;
    }
    const uint32_t count = static_cast<uint32_t>(returned / sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION));
    if (count == 0 || count > sample.logical.size())
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
        sample.logical[index].idleTime = static_cast<uint64_t>(records[index].IdleTime.QuadPart);
        sample.logical[index].kernelTime = static_cast<uint64_t>(records[index].KernelTime.QuadPart);
        sample.logical[index].userTime = static_cast<uint64_t>(records[index].UserTime.QuadPart);
        sample.logical[index].hasTimes = true;
    }
    sample.processorTimesValid = true;
}

[[nodiscard]] bool CopyImageName(const UNICODE_STRING& image, const std::byte* bufferBase, size_t bufferBytes,
                                 RedXeNativeProcessRow& row) noexcept
{
    row.imageCharacters = 0;
    row.imageName[0] = L'\0';
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
    std::wmemcpy(row.imageName, image.Buffer, characters);
    row.imageName[characters] = L'\0';
    row.imageCharacters = characters;
    return true;
}

[[nodiscard]] bool WalkBasicParents(const std::byte* buffer, ULONG returned,
                                    std::array<ParentMapEntry, kMaximumParentMap>& parents) noexcept
{
    size_t offset = 0;
    for (;;)
    {
        if (offset + sizeof(SYSTEM_BASICPROCESS_INFORMATION) > returned)
        {
            return false;
        }
        const auto* record = reinterpret_cast<const SYSTEM_BASICPROCESS_INFORMATION*>(buffer + offset);
        InsertParent(parents, HandleToPid(record->UniqueProcessId), HandleToPid(record->InheritedFromUniqueProcessId));
        if (record->NextEntryOffset == 0)
        {
            return true;
        }
        if (record->NextEntryOffset < sizeof(SYSTEM_BASICPROCESS_INFORMATION) ||
            offset + record->NextEntryOffset > returned)
        {
            return false;
        }
        offset += record->NextEntryOffset;
    }
}
} // namespace

bool RedXeNativeQueryCheap(RedXeNativeState& state, RedXeNativeCheapSample& sample) noexcept
{
    std::memset(&sample, 0, sizeof(sample));
    FillProcessorIdentity(sample);
    FillTopologyCounts(state, sample);

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

    FillProcessorTimes(query, sample);
    return sample.handleCountsValid || sample.processorTimesValid || sample.topologyValid;
}

bool RedXeNativeQueryWalk(RedXeNativeState& state, RedXeNativeWalkSample& sample) noexcept
{
    std::memset(&sample, 0, sizeof(sample));
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

    std::array<ParentMapEntry, kMaximumParentMap> parents{};
    ULONG basicReturned = 0;
    if (QuerySystem(query, SystemBasicProcessInformation, state.basicProcessBuffer.data(),
                    static_cast<ULONG>(state.basicProcessBuffer.size()), basicReturned))
    {
        (void)WalkBasicParents(state.basicProcessBuffer.data(), basicReturned, parents);
    }

    SYSTEM_INFO systemInfo{};
    GetNativeSystemInfo(&systemInfo);

    size_t offset = 0;
    for (;;)
    {
        if (offset + sizeof(SYSTEM_PROCESS_INFORMATION) > returned)
        {
            return false;
        }
        const auto* record = reinterpret_cast<const SYSTEM_PROCESS_INFORMATION*>(state.processBuffer.data() + offset);
        const size_t threadBytes = static_cast<size_t>(record->NumberOfThreads) * sizeof(SYSTEM_THREAD_INFORMATION);
        if (offset + sizeof(SYSTEM_PROCESS_INFORMATION) + threadBytes > returned)
        {
            return false;
        }
        const auto* threads = reinterpret_cast<const SYSTEM_THREAD_INFORMATION*>(state.processBuffer.data() + offset +
                                                                                 sizeof(SYSTEM_PROCESS_INFORMATION));

        if (sample.processCount < sample.processes.size())
        {
            RedXeNativeProcessRow& row = sample.processes[sample.processCount];
            row.processId = HandleToPid(record->UniqueProcessId);
            row.threadCount = record->NumberOfThreads;
            row.handleCount = record->HandleCount;
            row.sessionId = record->SessionId;
            row.basePriority = record->BasePriority;
            row.peakVirtualBytes = record->PeakVirtualSize;
            row.virtualBytes = record->VirtualSize;
            row.peakWorkingSetBytes = record->PeakWorkingSetSize;
            row.workingSetBytes = record->WorkingSetSize;
            if (systemInfo.dwPageSize != 0 && record->PrivatePageCount <= (UINT64_MAX / systemInfo.dwPageSize))
            {
                row.privateBytes =
                    static_cast<uint64_t>(record->PrivatePageCount) * static_cast<uint64_t>(systemInfo.dwPageSize);
            }
            if (!CopyImageName(record->ImageName, state.processBuffer.data(), returned, row))
            {
                return false;
            }
            uint32_t parent = 0;
            row.hasParent = FindParent(parents, row.processId, parent);
            row.parentProcessId = parent;
            ++sample.processCount;
        }
        else
        {
            sample.processesTruncated = true;
        }

        for (ULONG threadIndex = 0; threadIndex < record->NumberOfThreads; ++threadIndex)
        {
            if (sample.threadCount < sample.threads.size())
            {
                const SYSTEM_THREAD_INFORMATION& thread = threads[threadIndex];
                RedXeNativeThreadRow& row = sample.threads[sample.threadCount];
                row.processId = HandleToPid(thread.ClientId.UniqueProcess);
                row.threadId = HandleToPid(thread.ClientId.UniqueThread);
                row.priority = thread.Priority;
                row.basePriority = thread.BasePriority;
                row.threadState = thread.ThreadState;
                row.waitReason = thread.WaitReason;
                ++sample.threadCount;
            }
            else
            {
                sample.threadsTruncated = true;
                break;
            }
        }

        if (record->NextEntryOffset == 0)
        {
            sample.valid = true;
            return true;
        }
        if (record->NextEntryOffset < sizeof(SYSTEM_PROCESS_INFORMATION) || offset + record->NextEntryOffset > returned)
        {
            return false;
        }
        offset += record->NextEntryOffset;
    }
}
