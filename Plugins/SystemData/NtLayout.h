#pragma once

// RedXe-owned overlays for the records that the Windows SDK declares with `Reserved*` members.
//
// The SDK deliberately hides field names it does not want callers to depend on, but it still pins the record size and
// the position of every reserved block. These overlays give those bytes RedXe names and prove the mapping at compile
// time: every consumed member asserts its offset against the matching SDK member, so an SDK that renames, resizes, or
// reorders a reserved block breaks the build instead of silently shifting a published column.
//
// Rules that keep this admissible (see Specs/Plans/WIP/SystemDataNativeLayoutsAndAccelerators_2026-09-03.md):
//   * No third-party header is vendored or included. The build compiles against the Windows SDK only.
//   * Every consumed field has an independent runtime oracle in Tests/SystemDataTests.
//   * Records with a build-dependent tail are consumed by measured `ReturnLength`, never by `sizeof`.

#include <cstddef>
#include <cstdint>
#include <windows.h>
#include <winternl.h>

// ---------------------------------------------------------------------------------------------------------------
// SystemProcessInformation (class 5) — SDK `SYSTEM_PROCESS_INFORMATION`, 256 bytes, fixed layout.
// ---------------------------------------------------------------------------------------------------------------

struct RedXeNtProcessRecord final
{
    uint32_t nextEntryOffset;
    uint32_t numberOfThreads;
    uint64_t workingSetPrivateSize;
    uint32_t hardFaultCount;
    uint32_t numberOfThreadsHighWatermark;
    uint64_t cycleTime;
    int64_t createTime;
    int64_t userTime;
    int64_t kernelTime;
    UNICODE_STRING imageName;
    LONG basePriority;
    HANDLE uniqueProcessId;
    HANDLE inheritedFromUniqueProcessId;
    uint32_t handleCount;
    uint32_t sessionId;
    ULONG_PTR uniqueProcessKey;
    SIZE_T peakVirtualSize;
    SIZE_T virtualSize;
    uint32_t pageFaultCount;
    SIZE_T peakWorkingSetSize;
    SIZE_T workingSetSize;
    SIZE_T quotaPeakPagedPoolUsage;
    SIZE_T quotaPagedPoolUsage;
    SIZE_T quotaPeakNonPagedPoolUsage;
    SIZE_T quotaNonPagedPoolUsage;
    SIZE_T pagefileUsage;
    SIZE_T peakPagefileUsage;
    SIZE_T privatePageCount;
    int64_t readOperationCount;
    int64_t writeOperationCount;
    int64_t otherOperationCount;
    int64_t readTransferCount;
    int64_t writeTransferCount;
    int64_t otherTransferCount;
};

static_assert(sizeof(RedXeNtProcessRecord) == sizeof(SYSTEM_PROCESS_INFORMATION));
static_assert(offsetof(RedXeNtProcessRecord, nextEntryOffset) == offsetof(SYSTEM_PROCESS_INFORMATION, NextEntryOffset));
static_assert(offsetof(RedXeNtProcessRecord, numberOfThreads) == offsetof(SYSTEM_PROCESS_INFORMATION, NumberOfThreads));
// Reserved1[48] spans workingSetPrivateSize .. kernelTime and ends exactly where ImageName begins.
static_assert(offsetof(RedXeNtProcessRecord, workingSetPrivateSize) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, Reserved1));
static_assert(offsetof(RedXeNtProcessRecord, hardFaultCount) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved1) + 8);
static_assert(offsetof(RedXeNtProcessRecord, numberOfThreadsHighWatermark) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, Reserved1) + 12);
static_assert(offsetof(RedXeNtProcessRecord, cycleTime) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved1) + 16);
static_assert(offsetof(RedXeNtProcessRecord, createTime) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved1) + 24);
static_assert(offsetof(RedXeNtProcessRecord, userTime) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved1) + 32);
static_assert(offsetof(RedXeNtProcessRecord, kernelTime) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved1) + 40);
static_assert(offsetof(RedXeNtProcessRecord, kernelTime) + 8 == offsetof(SYSTEM_PROCESS_INFORMATION, ImageName));
static_assert(offsetof(RedXeNtProcessRecord, imageName) == offsetof(SYSTEM_PROCESS_INFORMATION, ImageName));
static_assert(offsetof(RedXeNtProcessRecord, basePriority) == offsetof(SYSTEM_PROCESS_INFORMATION, BasePriority));
static_assert(offsetof(RedXeNtProcessRecord, uniqueProcessId) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, UniqueProcessId));
static_assert(offsetof(RedXeNtProcessRecord, inheritedFromUniqueProcessId) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, Reserved2));
static_assert(offsetof(RedXeNtProcessRecord, handleCount) == offsetof(SYSTEM_PROCESS_INFORMATION, HandleCount));
static_assert(offsetof(RedXeNtProcessRecord, sessionId) == offsetof(SYSTEM_PROCESS_INFORMATION, SessionId));
static_assert(offsetof(RedXeNtProcessRecord, uniqueProcessKey) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved3));
static_assert(offsetof(RedXeNtProcessRecord, peakVirtualSize) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, PeakVirtualSize));
static_assert(offsetof(RedXeNtProcessRecord, virtualSize) == offsetof(SYSTEM_PROCESS_INFORMATION, VirtualSize));
static_assert(offsetof(RedXeNtProcessRecord, pageFaultCount) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved4));
static_assert(offsetof(RedXeNtProcessRecord, peakWorkingSetSize) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, PeakWorkingSetSize));
static_assert(offsetof(RedXeNtProcessRecord, workingSetSize) == offsetof(SYSTEM_PROCESS_INFORMATION, WorkingSetSize));
static_assert(offsetof(RedXeNtProcessRecord, quotaPeakPagedPoolUsage) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, Reserved5));
static_assert(offsetof(RedXeNtProcessRecord, quotaPagedPoolUsage) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, QuotaPagedPoolUsage));
static_assert(offsetof(RedXeNtProcessRecord, quotaPeakNonPagedPoolUsage) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, Reserved6));
static_assert(offsetof(RedXeNtProcessRecord, quotaNonPagedPoolUsage) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, QuotaNonPagedPoolUsage));
static_assert(offsetof(RedXeNtProcessRecord, pagefileUsage) == offsetof(SYSTEM_PROCESS_INFORMATION, PagefileUsage));
static_assert(offsetof(RedXeNtProcessRecord, peakPagefileUsage) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, PeakPagefileUsage));
static_assert(offsetof(RedXeNtProcessRecord, privatePageCount) ==
              offsetof(SYSTEM_PROCESS_INFORMATION, PrivatePageCount));
// Reserved7[6] is the six per-process I/O counters and closes the record.
static_assert(offsetof(RedXeNtProcessRecord, readOperationCount) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved7));
static_assert(offsetof(RedXeNtProcessRecord, otherTransferCount) + 8 == sizeof(SYSTEM_PROCESS_INFORMATION));

// ---------------------------------------------------------------------------------------------------------------
// Thread records inlined after every process record — SDK `SYSTEM_THREAD_INFORMATION`, 80 bytes, fixed layout.
// StartAddress is present in the SDK but never published; RedXe does not expose thread addresses.
// ---------------------------------------------------------------------------------------------------------------

struct RedXeNtThreadRecord final
{
    int64_t kernelTime;
    int64_t userTime;
    int64_t createTime;
    uint32_t waitTime;
    PVOID startAddress;
    CLIENT_ID clientId;
    LONG priority;
    LONG basePriority;
    uint32_t contextSwitches;
    uint32_t threadState;
    uint32_t waitReason;
};

static_assert(sizeof(RedXeNtThreadRecord) == sizeof(SYSTEM_THREAD_INFORMATION));
static_assert(offsetof(RedXeNtThreadRecord, kernelTime) == offsetof(SYSTEM_THREAD_INFORMATION, Reserved1));
static_assert(offsetof(RedXeNtThreadRecord, userTime) == offsetof(SYSTEM_THREAD_INFORMATION, Reserved1) + 8);
static_assert(offsetof(RedXeNtThreadRecord, createTime) == offsetof(SYSTEM_THREAD_INFORMATION, Reserved1) + 16);
static_assert(offsetof(RedXeNtThreadRecord, waitTime) == offsetof(SYSTEM_THREAD_INFORMATION, Reserved2));
static_assert(offsetof(RedXeNtThreadRecord, startAddress) == offsetof(SYSTEM_THREAD_INFORMATION, StartAddress));
static_assert(offsetof(RedXeNtThreadRecord, clientId) == offsetof(SYSTEM_THREAD_INFORMATION, ClientId));
static_assert(offsetof(RedXeNtThreadRecord, priority) == offsetof(SYSTEM_THREAD_INFORMATION, Priority));
static_assert(offsetof(RedXeNtThreadRecord, basePriority) == offsetof(SYSTEM_THREAD_INFORMATION, BasePriority));
static_assert(offsetof(RedXeNtThreadRecord, contextSwitches) == offsetof(SYSTEM_THREAD_INFORMATION, Reserved3));
static_assert(offsetof(RedXeNtThreadRecord, threadState) == offsetof(SYSTEM_THREAD_INFORMATION, ThreadState));
static_assert(offsetof(RedXeNtThreadRecord, waitReason) == offsetof(SYSTEM_THREAD_INFORMATION, WaitReason));

// ---------------------------------------------------------------------------------------------------------------
// SystemProcessorPerformanceInformation (class 8) — SDK `SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION`, 48 bytes.
// ---------------------------------------------------------------------------------------------------------------

struct RedXeNtProcessorPerformance final
{
    int64_t idleTime;
    int64_t kernelTime;
    int64_t userTime;
    int64_t dpcTime;
    int64_t interruptTime;
    uint32_t interruptCount;
    uint32_t spare0;
};

static_assert(sizeof(RedXeNtProcessorPerformance) == sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION));
static_assert(offsetof(RedXeNtProcessorPerformance, idleTime) ==
              offsetof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION, IdleTime));
static_assert(offsetof(RedXeNtProcessorPerformance, kernelTime) ==
              offsetof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION, KernelTime));
static_assert(offsetof(RedXeNtProcessorPerformance, userTime) ==
              offsetof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION, UserTime));
static_assert(offsetof(RedXeNtProcessorPerformance, dpcTime) ==
              offsetof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION, Reserved1));
static_assert(offsetof(RedXeNtProcessorPerformance, interruptTime) ==
              offsetof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION, Reserved1) + 8);
static_assert(offsetof(RedXeNtProcessorPerformance, interruptCount) ==
              offsetof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION, Reserved2));

// ---------------------------------------------------------------------------------------------------------------
// SystemTimeOfDayInformation (class 3) — SDK `SYSTEM_TIMEOFDAY_INFORMATION`, 48 bytes, fully covered, no tail.
// ---------------------------------------------------------------------------------------------------------------

struct RedXeNtTimeOfDay final
{
    int64_t bootTime;
    int64_t currentTime;
    int64_t timeZoneBias;
    uint32_t timeZoneId;
    uint32_t reserved;
    uint64_t bootTimeBias;
    uint64_t sleepTimeBias;
};

static_assert(sizeof(RedXeNtTimeOfDay) == sizeof(SYSTEM_TIMEOFDAY_INFORMATION));
static_assert(offsetof(RedXeNtTimeOfDay, bootTime) == offsetof(SYSTEM_TIMEOFDAY_INFORMATION, Reserved1));

// ---------------------------------------------------------------------------------------------------------------
// SystemInterruptInformation (class 23) — SDK `SYSTEM_INTERRUPT_INFORMATION`, 24 bytes per logical processor.
// ---------------------------------------------------------------------------------------------------------------

struct RedXeNtInterruptRecord final
{
    uint32_t contextSwitches;
    uint32_t dpcCount;
    uint32_t dpcRate;
    uint32_t timeIncrement;
    uint32_t dpcBypassCount;
    uint32_t apcBypassCount;
};

static_assert(sizeof(RedXeNtInterruptRecord) == sizeof(SYSTEM_INTERRUPT_INFORMATION));
static_assert(offsetof(RedXeNtInterruptRecord, contextSwitches) == offsetof(SYSTEM_INTERRUPT_INFORMATION, Reserved1));

// ---------------------------------------------------------------------------------------------------------------
// SystemExceptionInformation (class 33) — SDK `SYSTEM_EXCEPTION_INFORMATION`, 16 bytes.
// ---------------------------------------------------------------------------------------------------------------

struct RedXeNtExceptionRecord final
{
    uint32_t alignmentFixupCount;
    uint32_t exceptionDispatchCount;
    uint32_t floatingEmulationCount;
    uint32_t byteWordEmulationCount;
};

static_assert(sizeof(RedXeNtExceptionRecord) == sizeof(SYSTEM_EXCEPTION_INFORMATION));
static_assert(offsetof(RedXeNtExceptionRecord, alignmentFixupCount) ==
              offsetof(SYSTEM_EXCEPTION_INFORMATION, Reserved1));

// ---------------------------------------------------------------------------------------------------------------
// SystemPerformanceInformation (class 2) — the one record with a build-dependent tail.
//
// The SDK's `Reserved1[312]` is exactly the prefix through `systemCalls`; current kernels return more. Consume by
// measured `ReturnLength` using the band constants below, never by `sizeof(RedXeNtPerformance)`.
// ---------------------------------------------------------------------------------------------------------------

struct RedXeNtPerformance final
{
    int64_t idleProcessTime;
    int64_t ioReadTransferCount;
    int64_t ioWriteTransferCount;
    int64_t ioOtherTransferCount;
    uint32_t ioReadOperationCount;
    uint32_t ioWriteOperationCount;
    uint32_t ioOtherOperationCount;
    uint32_t availablePages;
    uint32_t committedPages;
    uint32_t commitLimit;
    uint32_t peakCommitment;
    uint32_t pageFaultCount;
    uint32_t copyOnWriteCount;
    uint32_t transitionCount;
    uint32_t cacheTransitionCount;
    uint32_t demandZeroCount;
    uint32_t pageReadCount;
    uint32_t pageReadIoCount;
    uint32_t cacheReadCount;
    uint32_t cacheIoCount;
    uint32_t dirtyPagesWriteCount;
    uint32_t dirtyWriteIoCount;
    uint32_t mappedPagesWriteCount;
    uint32_t mappedWriteIoCount;
    uint32_t pagedPoolPages;
    uint32_t nonPagedPoolPages;
    uint32_t pagedPoolAllocs;
    uint32_t pagedPoolFrees;
    uint32_t nonPagedPoolAllocs;
    uint32_t nonPagedPoolFrees;
    uint32_t freeSystemPtes;
    uint32_t residentSystemCodePage;
    uint32_t totalSystemDriverPages;
    uint32_t totalSystemCodePages;
    uint32_t nonPagedPoolLookasideHits;
    uint32_t pagedPoolLookasideHits;
    uint32_t availablePagedPoolPages;
    uint32_t residentSystemCachePage;
    uint32_t residentPagedPoolPage;
    uint32_t residentSystemDriverPage;
    uint32_t ccFastReadNoWait;
    uint32_t ccFastReadWait;
    uint32_t ccFastReadResourceMiss;
    uint32_t ccFastReadNotPossible;
    uint32_t ccFastMdlReadNoWait;
    uint32_t ccFastMdlReadWait;
    uint32_t ccFastMdlReadResourceMiss;
    uint32_t ccFastMdlReadNotPossible;
    uint32_t ccMapDataNoWait;
    uint32_t ccMapDataWait;
    uint32_t ccMapDataNoWaitMiss;
    uint32_t ccMapDataWaitMiss;
    uint32_t ccPinMappedDataCount;
    uint32_t ccPinReadNoWait;
    uint32_t ccPinReadWait;
    uint32_t ccPinReadNoWaitMiss;
    uint32_t ccPinReadWaitMiss;
    uint32_t ccCopyReadNoWait;
    uint32_t ccCopyReadWait;
    uint32_t ccCopyReadNoWaitMiss;
    uint32_t ccCopyReadWaitMiss;
    uint32_t ccMdlReadNoWait;
    uint32_t ccMdlReadWait;
    uint32_t ccMdlReadNoWaitMiss;
    uint32_t ccMdlReadWaitMiss;
    uint32_t ccReadAheadIos;
    uint32_t ccLazyWriteIos;
    uint32_t ccLazyWritePages;
    uint32_t ccDataFlushes;
    uint32_t ccDataPages;
    uint32_t contextSwitches;
    uint32_t firstLevelTbFills;
    uint32_t secondLevelTbFills;
    uint32_t systemCalls;
    uint64_t ccTotalDirtyPages;
    uint64_t ccDirtyPageThreshold;
    int64_t residentAvailablePages;
    uint64_t sharedCommittedPages;
    uint64_t mdlPagesAllocated;
    uint64_t pfnDatabaseCommittedPages;
    uint64_t systemPageTableCommittedPages;
    uint64_t contiguousPagesAllocated;
};

// Acceptance bands. A field is readable only when the measured ReturnLength covers its end offset.
constexpr uint32_t kRedXeNtPerformanceBaseBytes = 312;      // through systemCalls; equals the SDK reserved block
constexpr uint32_t kRedXeNtPerformanceThresholdBytes = 344; // + ccTotalDirtyPages .. sharedCommittedPages
constexpr uint32_t kRedXeNtPerformance24H2Bytes = 376;      // + mdlPagesAllocated .. contiguousPagesAllocated

static_assert(offsetof(RedXeNtPerformance, systemCalls) + 4 == kRedXeNtPerformanceBaseBytes);
static_assert(kRedXeNtPerformanceBaseBytes == sizeof(SYSTEM_PERFORMANCE_INFORMATION));
static_assert(offsetof(RedXeNtPerformance, ccTotalDirtyPages) == kRedXeNtPerformanceBaseBytes);
static_assert(offsetof(RedXeNtPerformance, sharedCommittedPages) + 8 == kRedXeNtPerformanceThresholdBytes);
static_assert(offsetof(RedXeNtPerformance, mdlPagesAllocated) == kRedXeNtPerformanceThresholdBytes);
static_assert(sizeof(RedXeNtPerformance) == kRedXeNtPerformance24H2Bytes);

// ---------------------------------------------------------------------------------------------------------------
// SystemCodeIntegrityInformation (class 103) — the SDK already names both members. Only the bit meanings come from
// Microsoft's published DDI documentation, so no overlay is required; these are RedXe names for documented bits.
// ---------------------------------------------------------------------------------------------------------------

constexpr uint32_t kRedXeCodeIntegrityEnabled = 0x00000001;
constexpr uint32_t kRedXeCodeIntegrityTestSign = 0x00000002;
constexpr uint32_t kRedXeCodeIntegrityUmciEnabled = 0x00000004;
constexpr uint32_t kRedXeCodeIntegrityUmciAuditMode = 0x00000008;
constexpr uint32_t kRedXeCodeIntegrityUmciExclusionPaths = 0x00000010;
constexpr uint32_t kRedXeCodeIntegrityTestBuild = 0x00000020;
constexpr uint32_t kRedXeCodeIntegrityPreproductionBuild = 0x00000040;
constexpr uint32_t kRedXeCodeIntegrityDebugModeEnabled = 0x00000080;
constexpr uint32_t kRedXeCodeIntegrityFlightBuild = 0x00000100;
constexpr uint32_t kRedXeCodeIntegrityFlightingEnabled = 0x00000200;
constexpr uint32_t kRedXeCodeIntegrityHvciKmciEnabled = 0x00000400;
constexpr uint32_t kRedXeCodeIntegrityHvciKmciAuditMode = 0x00000800;
constexpr uint32_t kRedXeCodeIntegrityHvciKmciStrictMode = 0x00001000;
constexpr uint32_t kRedXeCodeIntegrityHvciIumEnabled = 0x00002000;
constexpr uint32_t kRedXeCodeIntegrityWhqlEnforcement = 0x00004000;
constexpr uint32_t kRedXeCodeIntegrityWhqlAuditMode = 0x00008000;
