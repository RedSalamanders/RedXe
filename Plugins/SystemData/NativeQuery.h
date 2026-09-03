#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <windows.h>

constexpr size_t kRedXeMaximumLogicalProcessors = 1024;
constexpr size_t kRedXeNtProcessBufferBytes = 4U * 1024U * 1024U;
constexpr size_t kRedXeMaximumNativeProcesses = 2048;
constexpr size_t kRedXeMaximumNativeThreads = 8192;
constexpr size_t kRedXeNativeImageCharacters = 260;
// Image names are staged in one shared arena rather than a fixed 260-character slot per row: a per-row slot spends
// 1 MiB to hold names that average well under 20 characters. The arena is sized for the worst case the row cap can
// produce at the observed name lengths, and a walk that would overflow it truncates the remaining names rather than
// the process list.
constexpr size_t kRedXeNativeImageArenaCharacters = 64U * 1024U;

struct RedXeNativeLogicalCpu final
{
    uint16_t group = 0;
    uint16_t index = 0;
    uint64_t idleTime = 0;
    uint64_t kernelTime = 0;
    uint64_t userTime = 0;
    uint64_t dpcTime = 0;
    uint64_t interruptTime = 0;
    uint32_t interruptCount = 0;
    uint32_t contextSwitches = 0;
    uint32_t dpcCount = 0;
    uint32_t dpcRate = 0;
    uint32_t dpcBypassCount = 0;
    uint32_t apcBypassCount = 0;
    uint32_t currentMhz = 0;
    uint32_t maxMhz = 0;
    uint32_t mhzLimit = 0;
    uint32_t cpuSetId = 0;
    uint32_t coreIndex = 0;
    uint32_t packageIndex = 0;
    uint32_t numaNode = 0;
    std::uint8_t efficiencyClass = 0;
    bool parked = false;
    bool allocated = false;
    bool hasTimes = false;
    bool hasFrequency = false;
    bool hasCpuSet = false;
    bool hasInterrupt = false;
    bool hasTopology = false;
};

struct RedXeNativeProcessRow final
{
    uint32_t processId = 0;
    uint32_t parentProcessId = 0;
    uint32_t threadCount = 0;
    uint32_t handleCount = 0;
    uint32_t sessionId = 0;
    std::int32_t basePriority = 0;
    uint32_t pageFaultCount = 0;
    uint32_t hardFaultCount = 0;
    uint64_t createTime = 0;
    uint64_t userTime = 0;
    uint64_t kernelTime = 0;
    uint64_t cycleTime = 0;
    uint64_t virtualBytes = 0;
    uint64_t peakWorkingSetBytes = 0;
    uint64_t workingSetBytes = 0;
    uint64_t workingSetPrivateBytes = 0;
    uint64_t privateBytes = 0;
    uint64_t pagefileBytes = 0;
    uint64_t ioReadBytes = 0;
    uint64_t ioWriteBytes = 0;
    uint64_t ioOtherBytes = 0;
    uint64_t ioReadOperations = 0;
    uint64_t ioWriteOperations = 0;
    uint64_t ioOtherOperations = 0;
    uint32_t imageOffset = 0;
    uint32_t imageCharacters = 0;
    bool hasParent = false;
};

struct RedXeNativeThreadRow final
{
    uint32_t processId = 0;
    uint32_t threadId = 0;
    std::int32_t priority = 0;
    std::int32_t basePriority = 0;
    uint32_t threadState = 0;
    uint32_t waitReason = 0;
    uint32_t contextSwitches = 0;
    uint64_t createTime = 0;
    uint64_t userTime = 0;
    uint64_t kernelTime = 0;
};

// Named contents of SystemPerformanceInformation, bounded by the measured ReturnLength. `readableBytes` records what
// the kernel actually returned; `hasThresholdTail` / `has24H2Tail` say which optional bands are valid this build.
struct RedXeNativeSystemPerformance final
{
    bool valid = false;
    bool hasThresholdTail = false;
    bool has24H2Tail = false;
    uint32_t readableBytes = 0;
    uint64_t idleProcessTime = 0;
    uint64_t ioReadTransferBytes = 0;
    uint64_t ioWriteTransferBytes = 0;
    uint64_t ioOtherTransferBytes = 0;
    uint32_t ioReadOperationCount = 0;
    uint32_t ioWriteOperationCount = 0;
    uint32_t ioOtherOperationCount = 0;
    uint32_t availablePages = 0;
    uint32_t committedPages = 0;
    uint32_t commitLimit = 0;
    uint32_t peakCommitment = 0;
    uint32_t pageFaultCount = 0;
    uint32_t copyOnWriteCount = 0;
    uint32_t transitionCount = 0;
    uint32_t demandZeroCount = 0;
    uint32_t pageReadCount = 0;
    uint32_t pageReadIoCount = 0;
    uint32_t dirtyPagesWriteCount = 0;
    uint32_t mappedPagesWriteCount = 0;
    uint32_t pagedPoolPages = 0;
    uint32_t nonPagedPoolPages = 0;
    uint32_t availablePagedPoolPages = 0;
    uint32_t freeSystemPtes = 0;
    uint32_t residentSystemCachePage = 0;
    uint32_t contextSwitches = 0;
    uint32_t systemCalls = 0;
    uint32_t firstLevelTbFills = 0;
    uint32_t secondLevelTbFills = 0;
    uint64_t ccTotalDirtyPages = 0;
    uint64_t ccDirtyPageThreshold = 0;
    int64_t residentAvailablePages = 0;
    uint64_t sharedCommittedPages = 0;
    uint64_t pfnDatabaseCommittedPages = 0;
    uint64_t systemPageTableCommittedPages = 0;
};

// Named contents of SystemTimeOfDayInformation.
struct RedXeNativeTimeOfDay final
{
    bool valid = false;
    uint64_t bootTime = 0;
    uint64_t currentTime = 0;
    int64_t timeZoneBias = 0;
    uint32_t timeZoneId = 0;
    uint64_t bootTimeBias = 0;
    uint64_t sleepTimeBias = 0;
};

// Named contents of SystemExceptionInformation.
struct RedXeNativeExceptions final
{
    bool valid = false;
    uint32_t alignmentFixupCount = 0;
    uint32_t exceptionDispatchCount = 0;
    uint32_t floatingEmulationCount = 0;
    uint32_t byteWordEmulationCount = 0;
};

// Platform code-integrity posture from SystemCodeIntegrityInformation. Both members are SDK-named.
struct RedXeNativeCodeIntegrity final
{
    bool valid = false;
    uint32_t options = 0;
};

struct RedXeNativeCheapSample final
{
    bool handleCountsValid = false;
    bool processorTimesValid = false;
    bool topologyValid = false;
    uint32_t processCount = 0;
    uint32_t threadCount = 0;
    uint32_t handleCount = 0;
    uint32_t logicalCount = 0;
    uint32_t coreCount = 0;
    uint32_t packageCount = 0;
    uint32_t numaNodeCount = 0;
    RedXeNativeSystemPerformance performance{};
    RedXeNativeTimeOfDay timeOfDay{};
    RedXeNativeExceptions exceptions{};
    std::array<RedXeNativeLogicalCpu, kRedXeMaximumLogicalProcessors> logical{};
};

struct RedXeNativeWalkSample final
{
    bool valid = false;
    bool processesTruncated = false;
    bool threadsTruncated = false;
    uint32_t processCount = 0;
    uint32_t threadCount = 0;
    uint32_t imageArenaUsed = 0;
    std::array<RedXeNativeProcessRow, kRedXeMaximumNativeProcesses> processes{};
    std::array<RedXeNativeThreadRow, kRedXeMaximumNativeThreads> threads{};
    // Null-terminated image names referenced by RedXeNativeProcessRow::imageOffset. Valid until the next walk.
    std::array<wchar_t, kRedXeNativeImageArenaCharacters> imageArena{};
};

// Identity that only changes when processor topology changes. Cached across collections.
struct RedXeNativeTopologyCache final
{
    bool valid = false;
    uint32_t logicalCount = 0;
    uint32_t coreCount = 0;
    uint32_t packageCount = 0;
    uint32_t numaNodeCount = 0;
    std::array<uint32_t, kRedXeMaximumLogicalProcessors> coreIndex{};
    std::array<uint32_t, kRedXeMaximumLogicalProcessors> packageIndex{};
    std::array<uint32_t, kRedXeMaximumLogicalProcessors> numaNode{};
    std::array<uint32_t, kRedXeMaximumLogicalProcessors> cpuSetId{};
    std::array<std::uint8_t, kRedXeMaximumLogicalProcessors> efficiencyClass{};
    std::array<bool, kRedXeMaximumLogicalProcessors> hasTopology{};
    std::array<bool, kRedXeMaximumLogicalProcessors> hasCpuSet{};
};

struct RedXeNativeState final
{
    std::array<std::byte, kRedXeNtProcessBufferBytes> processBuffer{};
    std::array<std::byte, 64U * 1024U> topologyBuffer{};
    std::array<std::byte, 64U * 1024U> cpuSetBuffer{};
    // Reusable staging for the per-logical-processor native records. Declared with an integer element type so the
    // buffers stay naturally aligned for the records overlaid on them, and sized to an exact whole number of records
    // (48, 24, and 24 bytes respectively): NtQuerySystemInformation rejects a length for these classes that is not a
    // multiple of one record, so a merely generous buffer fails the query outright.
    std::array<uint64_t, kRedXeMaximumLogicalProcessors * 6> processorTimeBuffer{};
    std::array<uint32_t, kRedXeMaximumLogicalProcessors * 6> interruptBuffer{};
    std::array<uint32_t, kRedXeMaximumLogicalProcessors * 6> processorPowerBuffer{};
    RedXeNativeTopologyCache topology{};
};

[[nodiscard]] bool RedXeNativeQueryCheap(RedXeNativeState& state, RedXeNativeCheapSample& sample) noexcept;
[[nodiscard]] bool RedXeNativeQueryWalk(RedXeNativeState& state, RedXeNativeWalkSample& sample) noexcept;
[[nodiscard]] bool RedXeNativeQueryCodeIntegrity(RedXeNativeCodeIntegrity& integrity) noexcept;
