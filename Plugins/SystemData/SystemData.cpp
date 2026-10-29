#define REDXE_PLUGIN_EXPORTS
#include "GpuQuery.h"
#include "NativeQuery.h"
#include "NetStorage.h"
#include "NtLayout.h"
#include "PlugInterfaces/Data.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PowerSensors.h"
#include "SystemData.Tests.Contract.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <new>
#include <psapi.h>
#include <realtimeapiset.h>
#include <span>
#include <tlhelp32.h>
#include <type_traits>
#include <wow64apiset.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr char kPluginId[] = "builtin.system-data";
constexpr char kStatusDataSetId[] = "source.status";
constexpr char kSummaryDataSetId[] = "system.summary";
constexpr char kCpuSummaryDataSetId[] = "cpu.summary";
constexpr char kCpuLogicalDataSetId[] = "cpu.logical";
constexpr char kMemoryDataSetId[] = "memory.summary";
constexpr char kProcessDataSetId[] = "process.list";
constexpr char kProcessActivityDataSetId[] = "process.activity";
constexpr char kThreadDataSetId[] = "thread.list";
constexpr char kNetworkInterfaceDataSetId[] = "network.interface";
constexpr char kNetworkProtocolDataSetId[] = "network.protocol";
constexpr char kStorageDiskDataSetId[] = "storage.disk";
constexpr char kStorageVolumeDataSetId[] = "storage.volume";
constexpr char kGpuAdapterDataSetId[] = "gpu.adapter";
constexpr char kGpuEngineDataSetId[] = "gpu.engine";
constexpr char kGpuProcessDataSetId[] = "gpu.process";
constexpr char kPowerSummaryDataSetId[] = "power.summary";
constexpr char kBatteryListDataSetId[] = "battery.list";
constexpr char kThermalSensorDataSetId[] = "thermal.sensor";
constexpr char kFanSensorDataSetId[] = "fan.sensor";
constexpr char kNpuAdapterDataSetId[] = "npu.adapter";
constexpr char kNpuEngineDataSetId[] = "npu.engine";
constexpr char kNpuProcessDataSetId[] = "npu.process";
constexpr char kSecurityDataSetId[] = "security.posture";
constexpr char kSettingsSchema[] = R"json({"type":"object","additionalProperties":false})json";
constexpr char kSettingsDefaults[] = R"json({})json";
constexpr uint32_t kRecommendedIntervalMilliseconds = 2000;
constexpr uint32_t kCheapIntervalMilliseconds = 1000;
constexpr uint32_t kStatusIntervalMilliseconds = 5000;
constexpr uint32_t kVolumeIntervalMilliseconds = 5000;
constexpr uint32_t kPowerIntervalMilliseconds = 5000;
constexpr uint32_t kSensorIntervalMilliseconds = 10000;
constexpr size_t kMaximumProcesses = kRedXeMaximumNativeProcesses;
constexpr size_t kMaximumThreads = kRedXeMaximumNativeThreads;
constexpr size_t kProcessNameCharacters = 65536;
constexpr size_t kProcessHistoryCapacity = 4096;
constexpr size_t kThreadHistoryCapacity = 16384;
constexpr uint32_t kStatusColumnCount = 12;
constexpr uint32_t kSummaryColumnCount = 12;
constexpr uint32_t kCpuSummaryColumnCount = 14;
constexpr uint32_t kCpuLogicalColumnCount = 19;
constexpr uint32_t kMemoryColumnCount = 12;
constexpr uint32_t kProcessColumnCount = 39;
constexpr uint32_t kProcessActivityColumnCount = 8;
constexpr uint32_t kThreadColumnCount = 12;
constexpr uint32_t kNetworkInterfaceColumnCount = 22;
constexpr uint32_t kNetworkProtocolColumnCount = 11;
constexpr uint32_t kStorageDiskColumnCount = 16;
constexpr uint32_t kStorageVolumeColumnCount = 8;
constexpr uint32_t kGpuAdapterColumnCount = 22;
constexpr uint32_t kGpuEngineColumnCount = 9;
constexpr uint32_t kGpuProcessColumnCount = 6;
constexpr uint32_t kPowerSummaryColumnCount = 13;
constexpr uint32_t kBatteryColumnCount = 13;
constexpr uint32_t kThermalColumnCount = 6;
constexpr uint32_t kFanColumnCount = 6;
constexpr uint32_t kNpuAdapterColumnCount = 15;
constexpr uint32_t kNpuEngineColumnCount = 8;
constexpr uint32_t kNpuProcessColumnCount = 6;
constexpr uint32_t kSecurityColumnCount = 11;
constexpr uint32_t kNpuAdapterMaximumRows = 16;
constexpr uint32_t kNpuEngineMaximumRows = 128;
constexpr uint32_t kNpuProcessMaximumRows = 512;
constexpr uint32_t kSecurityIntervalMilliseconds = 60000;
constexpr uint32_t kThermalMaximumRows = 128;
constexpr uint32_t kFanMaximumRows = 128;
constexpr uint32_t kStatusMaximumRows = 32;
constexpr uint64_t kAvailabilityAvailable = 0;
constexpr uint64_t kBackendNative = 1;
constexpr uint64_t kBackendWin32 = 2;
constexpr uint64_t kBackendCounter = 3;
constexpr uint64_t kLastResultOk = 0;
constexpr uint64_t kLastResultTruncated = 1;
constexpr uint64_t kLastResultFailed = 2;
static_assert((kProcessHistoryCapacity & (kProcessHistoryCapacity - 1)) == 0);
static_assert((kThreadHistoryCapacity & (kThreadHistoryCapacity - 1)) == 0);
static_assert(kThreadHistoryCapacity >= kRedXeMaximumNativeThreads * 2, "keep the open-addressed load factor at 0.5");

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

constexpr std::array kStatusColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "dataSetId", L"Dataset", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "availability", L"Availability", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "backendTier", L"Backend tier", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "backendName", L"Backend", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "backendVersion", L"Backend version", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "fallbackActive", L"Fallback active", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "lastResult", L"Last result", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "lastSuccessFileTime100ns", L"Last success", L"100ns",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "lastDurationMicroseconds", L"Last duration",
                              L"microseconds", RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "failureCount", L"Failures", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "truncationCount", L"Truncations", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "retryFileTime100ns", L"Retry at", L"100ns",
                              RedXeDataValueTypeUInt64},
};
static_assert(kStatusColumns.size() == kStatusColumnCount);

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
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "bootTime100ns", L"Boot time", L"100ns",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "sleepMilliseconds", L"Time asleep", L"milliseconds",
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
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "parentProcessId", L"Parent PID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "sessionId", L"Session", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "userTime100ns", L"User time", L"100ns",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "kernelTime100ns", L"Kernel time", L"100ns",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "cpuUserPercent", L"User CPU", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "cpuKernelPercent", L"Kernel CPU", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "peakWorkingSetBytes", L"Peak working set", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "virtualBytes", L"Virtual bytes", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "ioReadBytes", L"I/O read", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "ioWriteBytes", L"I/O write", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "ioOtherBytes", L"I/O other", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "ioReadRate", L"I/O read rate", L"bytes per second",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "ioWriteRate", L"I/O write rate", L"bytes per second",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "pageFaultCount", L"Page faults", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "pageFaultRate", L"Page fault rate", L"per second",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "createTime100ns", L"Create time", L"100ns",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "cycleCount", L"Cycles", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "basePriority", L"Base priority", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "affinityGroup", L"Affinity group", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "affinityMask", L"Affinity mask", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "wow64", L"WOW64", nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "nativeArchitecture", L"Architecture", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "subsystem", L"Subsystem", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "criticalProcess", L"Critical", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "hardFaultCount", L"Hard faults", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "workingSetPrivateBytes", L"Private working set",
                              L"bytes", RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "pagefileBytes", L"Pagefile", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "ioReadOperations", L"I/O reads", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "ioWriteOperations", L"I/O writes", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "ioOtherOperations", L"I/O other", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "efficiencyMode", L"Efficiency mode", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "throttlingControlMask", L"Throttling controls",
                              nullptr, RedXeDataValueTypeUInt64},
};
static_assert(kProcessColumns.size() == kProcessColumnCount);

constexpr std::array kCpuSummaryColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "totalPercent", L"Total CPU", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "userPercent", L"User", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "kernelPercent", L"Kernel", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "idlePercent", L"Idle", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "dpcPercent", L"DPC", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "interruptPercent", L"Interrupt", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "logicalProcessorCount", L"Logical processors",
                              nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "coreCount", L"Cores", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "packageCount", L"Packages", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "numaNodeCount", L"NUMA nodes", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "currentFrequencyMhz", L"Current frequency", L"MHz",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "maximumFrequencyMhz", L"Maximum frequency", L"MHz",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "contextSwitchRate", L"Context switches",
                              L"per second", RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "interruptRate", L"Interrupts", L"per second",
                              RedXeDataValueTypeFloat64},
};
static_assert(kCpuSummaryColumns.size() == kCpuSummaryColumnCount);

constexpr std::array kCpuLogicalColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "processorGroup", L"Group", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "groupRelativeIndex", L"Index", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "cpuSetId", L"CPU set", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "coreIndex", L"Core", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "packageIndex", L"Package", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "numaNode", L"NUMA", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "efficiencyClass", L"Efficiency class", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "parked", L"Parked", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "allocated", L"Allocated", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "totalPercent", L"Total", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "userPercent", L"User", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "kernelPercent", L"Kernel", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "idlePercent", L"Idle", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "dpcPercent", L"DPC", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "interruptPercent", L"Interrupt", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "interruptCount", L"Interrupt count", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "interruptRate", L"Interrupt rate", L"per second",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "currentFrequencyMhz", L"Current frequency", L"MHz",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "maximumFrequencyMhz", L"Maximum frequency", L"MHz",
                              RedXeDataValueTypeUInt64},
};
static_assert(kCpuLogicalColumns.size() == kCpuLogicalColumnCount);

constexpr std::array kMemoryColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "totalPhysicalBytes", L"Physical memory", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "availablePhysicalBytes", L"Available memory",
                              L"bytes", RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "usedPhysicalBytes", L"Used memory", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "commitBytes", L"Commit", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "commitLimitBytes", L"Commit limit", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "commitPeakBytes", L"Commit peak", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "cacheBytes", L"Cache", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "pagedPoolBytes", L"Paged pool", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "nonPagedPoolBytes", L"Nonpaged pool", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "pageFaultRate", L"Page faults", L"per second",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "pageInRate", L"Page in", L"per second",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "pageOutRate", L"Page out", L"per second",
                              RedXeDataValueTypeFloat64},
};
static_assert(kMemoryColumns.size() == kMemoryColumnCount);

constexpr std::array kThreadColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "processId", L"Process ID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "createTime100ns", L"Create time", L"100ns",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "threadId", L"Thread ID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "userTime100ns", L"User time", L"100ns",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "kernelTime100ns", L"Kernel time", L"100ns",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "cpuPercent", L"CPU", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "priority", L"Priority", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "basePriority", L"Base priority", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "threadState", L"State", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "waitReason", L"Wait reason", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "contextSwitchCount", L"Context switches", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "contextSwitchRate", L"Context switch rate",
                              L"per second", RedXeDataValueTypeFloat64},
};
static_assert(kThreadColumns.size() == kThreadColumnCount);

constexpr std::array kNetworkInterfaceColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "interfaceLuid", L"Interface LUID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "alias", L"Alias", nullptr, RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "description", L"Description", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "type", L"Type", nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "operationalStatus", L"Operational status", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "mediaConnectState", L"Media state", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "mtu", L"MTU", L"bytes", RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "receiveLinkSpeed", L"Receive speed",
                              L"bits per second", RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "transmitLinkSpeed", L"Transmit speed",
                              L"bits per second", RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "inOctets", L"Bytes in", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "outOctets", L"Bytes out", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "inOctetRate", L"Receive rate", L"bytes per second",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "outOctetRate", L"Transmit rate", L"bytes per second",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "inUcastPkts", L"Unicast in", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "outUcastPkts", L"Unicast out", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "inNUcastPkts", L"Non-unicast in", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "outNUcastPkts", L"Non-unicast out", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "inErrors", L"Errors in", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "outErrors", L"Errors out", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "inDiscards", L"Discards in", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "outDiscards", L"Discards out", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "utilizationPercent", L"Utilization", L"percent",
                              RedXeDataValueTypeFloat64},
};
static_assert(kNetworkInterfaceColumns.size() == kNetworkInterfaceColumnCount);

constexpr std::array kNetworkProtocolColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "protocolId", L"Protocol", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "addressFamily", L"Address family", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "inCount", L"In", nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "outCount", L"Out", nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "inRate", L"In rate", L"per second",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "outRate", L"Out rate", L"per second",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "errorCount", L"Errors", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "discardCount", L"Discards", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "retransmitCount", L"Retransmits", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "currentEstablished", L"Established", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "resetCount", L"Resets", nullptr,
                              RedXeDataValueTypeUInt64},
};
static_assert(kNetworkProtocolColumns.size() == kNetworkProtocolColumnCount);

constexpr std::array kStorageDiskColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "diskNumber", L"Disk", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "displayName", L"Name", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "busType", L"Bus", nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "mediaKind", L"Media", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "capacityBytes", L"Capacity", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "activePercent", L"Active", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "idlePercent", L"Idle", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "queueDepth", L"Queue depth", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "readBytesPerSecond", L"Read bytes",
                              L"bytes per second", RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "writeBytesPerSecond", L"Write bytes",
                              L"bytes per second", RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "readOpsPerSecond", L"Read operations", L"per second",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "writeOpsPerSecond", L"Write operations",
                              L"per second", RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "splitCount", L"Splits", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "avgReadLatencyMs", L"Read latency", L"milliseconds",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "avgWriteLatencyMs", L"Write latency", L"milliseconds",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "performanceAvailable", L"Performance", nullptr,
                              RedXeDataValueTypeUInt64},
};
static_assert(kStorageDiskColumns.size() == kStorageDiskColumnCount);

constexpr std::array kStorageVolumeColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "volumeGuid", L"Volume GUID", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "displayName", L"Mount", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "fileSystem", L"File system", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "totalBytes", L"Capacity", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "freeBytes", L"Free", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "extentCount", L"Extents", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "firstDiskNumber", L"Disk", nullptr,
                              RedXeDataValueTypeUInt64},
    // The name Explorer shows before "(X:)": the volume label, else a drive-type stand-in such as "Local Disk".
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "label", L"Label", nullptr, RedXeDataValueTypeUtf16},
};
static_assert(kStorageVolumeColumns.size() == kStorageVolumeColumnCount);

constexpr std::array kGpuAdapterColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "adapterLuid", L"Adapter LUID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "displayName", L"Name", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "vendorId", L"Vendor", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "deviceId", L"Device", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "software", L"Software", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "integrated", L"Integrated", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "dedicatedBytes", L"Dedicated", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "sharedBytes", L"Shared", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "utilizationPercent", L"Utilization", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "dedicatedUsedBytes", L"Dedicated used", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "sharedUsedBytes", L"Shared used", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "memoryClockHz", L"Memory clock", L"hertz",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "maxMemoryClockHz", L"Max memory clock", L"hertz",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "powerPercent", L"Power", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "temperatureC", L"Temperature", L"celsius",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "warningTemperatureC", L"Warning temperature",
                              L"celsius", RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "maxTemperatureC", L"Maximum temperature", L"celsius",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "fanRpm", L"Fan", L"rpm", RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "maxFanRpm", L"Max fan", L"rpm",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "deviceClass", L"Device class", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "computeOnly", L"Compute only", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "physicalAdapterCount", L"Physical adapters", nullptr,
                              RedXeDataValueTypeUInt64},
};
static_assert(kGpuAdapterColumns.size() == kGpuAdapterColumnCount);

constexpr std::array kGpuEngineColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "adapterLuid", L"Adapter LUID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "physicalAdapterIndex", L"Physical adapter", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "nodeOrdinal", L"Node", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "engineClass", L"Class", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "friendlyName", L"Name", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "currentFrequencyHz", L"Frequency", L"hertz",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "maxFrequencyHz", L"Max frequency", L"hertz",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "voltageMv", L"Voltage", L"millivolts",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "utilizationPercent", L"Utilization", L"percent",
                              RedXeDataValueTypeFloat64},
};
static_assert(kGpuEngineColumns.size() == kGpuEngineColumnCount);

constexpr std::array kGpuProcessColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "processId", L"Process ID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "adapterLuid", L"Adapter LUID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "physicalAdapterIndex", L"Physical adapter", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "engineOrdinal", L"Engine", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "engineType", L"Engine type", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "utilizationPercent", L"Utilization", L"percent",
                              RedXeDataValueTypeFloat64},
};
static_assert(kGpuProcessColumns.size() == kGpuProcessColumnCount);

constexpr std::array kPowerSummaryColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "acOnline", L"AC online", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "batteryPresent", L"Battery present", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "charging", L"Charging", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "batterySaver", L"Battery saver", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "chargePercent", L"Charge", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "remainingSeconds", L"Remaining", L"seconds",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "fullLifeSeconds", L"Full life", L"seconds",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "systemS3", L"S3", nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "systemS4", L"S4", nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "hiberFilePresent", L"Hibernation file", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "thermalControl", L"Thermal control", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "modernStandby", L"Modern standby", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "modernStandbyConnected", L"Connected standby",
                              nullptr, RedXeDataValueTypeUInt64},
};
static_assert(kPowerSummaryColumns.size() == kPowerSummaryColumnCount);

constexpr std::array kBatteryColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "deviceId", L"Device", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "deviceName", L"Name", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "chemistry", L"Chemistry", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "designedCapacity", L"Designed capacity", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "fullChargedCapacity", L"Full capacity", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "currentCapacity", L"Current capacity", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "rateMw", L"Rate", L"milliwatts",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "voltageMv", L"Voltage", L"millivolts",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "temperatureC", L"Temperature", L"celsius",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "relativeCapacity", L"Relative units", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "cycleCount", L"Cycles", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "estimatedTimeSeconds", L"Estimated time", L"seconds",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "powerState", L"Power state", nullptr,
                              RedXeDataValueTypeUInt64},
};
static_assert(kBatteryColumns.size() == kBatteryColumnCount);

constexpr std::array kThermalColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "sensorId", L"Sensor", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "deviceKind", L"Kind", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "displayName", L"Name", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "temperatureC", L"Temperature", L"celsius",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "warningTemperatureC", L"Warning temperature",
                              L"celsius", RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "criticalTemperatureC", L"Critical temperature",
                              L"celsius", RedXeDataValueTypeFloat64},
};
static_assert(kThermalColumns.size() == kThermalColumnCount);

constexpr std::array kFanColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "sensorId", L"Sensor", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "deviceKind", L"Kind", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "displayName", L"Name", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "fanRpm", L"Fan", L"rpm", RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "maxFanRpm", L"Max fan", L"rpm",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "active", L"Active", nullptr,
                              RedXeDataValueTypeUInt64},
};
static_assert(kFanColumns.size() == kFanColumnCount);

// Accelerator datasets. The graphics datasets keep their frozen IDs, columns, and meaning: a viewer that says "GPU"
// must not silently begin listing an NPU, so compute-only devices get their own family instead.
constexpr std::array kNpuAdapterColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "adapterLuid", L"Adapter LUID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "displayName", L"Name", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "vendorId", L"Vendor", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "deviceId", L"Device", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "deviceClass", L"Device class", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "computeOnly", L"Compute only", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "integrated", L"Integrated", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "physicalAdapterCount", L"Physical adapters", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "engineCount", L"Engines", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "dedicatedBytes", L"Dedicated memory", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "sharedBytes", L"Shared memory", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "usedDedicatedBytes", L"Dedicated in use", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "usedSharedBytes", L"Shared in use", L"bytes",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "utilizationPercent", L"Utilization", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "temperatureC", L"Temperature", L"celsius",
                              RedXeDataValueTypeFloat64},
};
static_assert(kNpuAdapterColumns.size() == kNpuAdapterColumnCount);

constexpr std::array kNpuEngineColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "adapterLuid", L"Adapter LUID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "physicalAdapterIndex", L"Physical adapter", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "engineIndex", L"Engine", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "engineName", L"Engine name", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "utilizationPercent", L"Utilization", L"percent",
                              RedXeDataValueTypeFloat64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "currentFrequencyHz", L"Current frequency", L"hertz",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "maximumFrequencyHz", L"Maximum frequency", L"hertz",
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "voltageMv", L"Voltage", L"millivolts",
                              RedXeDataValueTypeUInt64},
};
static_assert(kNpuEngineColumns.size() == kNpuEngineColumnCount);

constexpr std::array kNpuProcessColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "processId", L"Process ID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "adapterLuid", L"Adapter LUID", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "physicalAdapterIndex", L"Physical adapter", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "engineOrdinal", L"Engine", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "engineType", L"Engine type", nullptr,
                              RedXeDataValueTypeUtf16},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "utilizationPercent", L"Utilization", L"percent",
                              RedXeDataValueTypeFloat64},
};
static_assert(kNpuProcessColumns.size() == kNpuProcessColumnCount);

// Platform security posture. One row, read once per minute: these are configuration flags, not counters, and a value
// that changes sample to sample would mean the record was misread rather than that the machine changed.
constexpr std::array kSecurityColumns{
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "codeIntegrityEnabled", L"Code integrity", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "testSigning", L"Test signing", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "userModeCodeIntegrity", L"User-mode code integrity",
                              nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "memoryIntegrityEnabled", L"Memory integrity", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "memoryIntegrityStrict", L"Memory integrity strict",
                              nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "isolatedUserMode", L"Isolated user mode", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "whqlEnforcement", L"WHQL enforcement", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "kernelDebugMode", L"Kernel debug mode", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "flightSigning", L"Flight signing", nullptr,
                              RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "codeIntegrityOptions", L"Code integrity options",
                              nullptr, RedXeDataValueTypeUInt64},
    RedXeDataColumnDescriptor{sizeof(RedXeDataColumnDescriptor), "firmwareVirtualization",
                              L"Virtualization firmware support", nullptr, RedXeDataValueTypeUInt64},
};
static_assert(kSecurityColumns.size() == kSecurityColumnCount);

constexpr std::array kDataSets{
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kStatusDataSetId,
        L"Source status",
        L"Per-dataset availability, selected backend, and last collection diagnostics.",
        kStatusColumns.data(),
        kStatusColumnCount,
        kStatusMaximumRows,
        kStatusIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kSummaryDataSetId,
        L"System summary",
        L"CPU, memory, uptime, and operating-system object counts for the local machine.",
        kSummaryColumns.data(),
        kSummaryColumnCount,
        1,
        kCheapIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kCpuSummaryDataSetId,
        L"CPU summary",
        L"Total CPU rates, topology counts, and frequency aggregates for the local machine.",
        kCpuSummaryColumns.data(),
        kCpuSummaryColumnCount,
        1,
        kCheapIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kCpuLogicalDataSetId,
        L"Logical processors",
        L"Per-logical-processor identity, rates, and topology metadata.",
        kCpuLogicalColumns.data(),
        kCpuLogicalColumnCount,
        static_cast<uint32_t>(kRedXeMaximumLogicalProcessors),
        kCheapIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kMemoryDataSetId,
        L"Memory summary",
        L"Physical, commit, and pool memory for the local machine.",
        kMemoryColumns.data(),
        kMemoryColumnCount,
        1,
        kCheapIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kProcessDataSetId,
        L"Process list",
        L"Bounded process identity, CPU, memory, thread, and handle data visible to the current user.",
        kProcessColumns.data(),
        kProcessColumnCount,
        static_cast<uint32_t>(kMaximumProcesses),
        kRecommendedIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kProcessActivityDataSetId,
        L"Process activity",
        L"Bounded process identity, CPU, memory, thread, handle, and parent data from one shared sample.",
        kProcessColumns.data(),
        kProcessActivityColumnCount,
        static_cast<uint32_t>(kMaximumProcesses),
        kRecommendedIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kThreadDataSetId,
        L"Thread list",
        L"Bounded thread identity, priority, and state from the same process sample.",
        kThreadColumns.data(),
        kThreadColumnCount,
        static_cast<uint32_t>(kMaximumThreads),
        kRecommendedIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kNetworkInterfaceDataSetId,
        L"Network interfaces",
        L"Bounded IP Helper interface identity, link state, and octet rates without MAC or IP addresses.",
        kNetworkInterfaceColumns.data(),
        kNetworkInterfaceColumnCount,
        static_cast<uint32_t>(kRedXeMaximumNetworkInterfaces),
        kCheapIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kNetworkProtocolDataSetId,
        L"Network protocols",
        L"IPv4, IPv6, TCP, and UDP aggregate counters without endpoint tables.",
        kNetworkProtocolColumns.data(),
        kNetworkProtocolColumnCount,
        static_cast<uint32_t>(kRedXeMaximumNetworkProtocols),
        kCheapIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kStorageDiskDataSetId,
        L"Physical disks",
        L"Bounded physical-disk identity and IOCTL_DISK_PERFORMANCE activity; counters stay unavailable without IOCTL.",
        kStorageDiskColumns.data(),
        kStorageDiskColumnCount,
        static_cast<uint32_t>(kRedXeMaximumStorageDisks),
        kCheapIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kStorageVolumeDataSetId,
        L"Volumes",
        L"Volume GUID, mount, filesystem, capacity, and disk extents without copied whole-disk activity.",
        kStorageVolumeColumns.data(),
        kStorageVolumeColumnCount,
        static_cast<uint32_t>(kRedXeMaximumStorageVolumes),
        kVolumeIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kGpuAdapterDataSetId,
        L"GPU adapters",
        L"DXGI adapter identity and capacity joined to D3DKMT sensors by LUID. No D3D device is created.",
        kGpuAdapterColumns.data(),
        kGpuAdapterColumnCount,
        static_cast<uint32_t>(kRedXeMaximumGpuAdapters),
        kCheapIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kGpuEngineDataSetId,
        L"GPU engines",
        L"D3DKMT nodes per adapter LUID. Utilization stays unavailable without a proven node running-time field.",
        kGpuEngineColumns.data(),
        kGpuEngineColumnCount,
        static_cast<uint32_t>(kRedXeMaximumGpuEngines),
        kCheapIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kGpuProcessDataSetId,
        L"GPU processes",
        L"GPU Engine counter instances parsed as pid/LUID/engine. Cap 2,048 truncates under load.",
        kGpuProcessColumns.data(),
        kGpuProcessColumnCount,
        static_cast<uint32_t>(kRedXeMaximumGpuProcesses),
        kRecommendedIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kPowerSummaryDataSetId,
        L"Power summary",
        L"AC/DC state, aggregate battery flags, and cached sleep capabilities. Unknown sentinels stay unavailable.",
        kPowerSummaryColumns.data(),
        kPowerSummaryColumnCount,
        1,
        kPowerIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kBatteryListDataSetId,
        L"Batteries",
        L"Present battery devices from SetupAPI. Serial numbers are not published. Empty on AC-only desktops.",
        kBatteryColumns.data(),
        kBatteryColumnCount,
        static_cast<uint32_t>(kRedXeMaximumBatteries),
        kPowerIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kThermalSensorDataSetId,
        L"Thermal sensors",
        L"Re-projected GPU, storage, and battery temperatures plus ACPI zones with a usable tenths-Kelvin reading.",
        kThermalColumns.data(),
        kThermalColumnCount,
        kThermalMaximumRows,
        kSensorIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kFanSensorDataSetId,
        L"Fans",
        L"GPU fan RPM from D3DKMT when MaxFanRpm is non-zero. Generic fan devices do not invent motherboard RPM.",
        kFanColumns.data(),
        kFanColumnCount,
        kFanMaximumRows,
        kSensorIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kNpuAdapterDataSetId,
        L"NPU adapters",
        L"Compute-only accelerators enumerated through D3DKMT and classified by DXCore. Zero rows on a machine with "
        L"no NPU; never a fabricated device.",
        kNpuAdapterColumns.data(),
        kNpuAdapterColumnCount,
        kNpuAdapterMaximumRows,
        kCheapIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kNpuEngineDataSetId,
        L"NPU engines",
        L"Per-engine identity, frequency, and DXCore running-time utilization for compute-only accelerators.",
        kNpuEngineColumns.data(),
        kNpuEngineColumnCount,
        kNpuEngineMaximumRows,
        kCheapIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kNpuProcessDataSetId,
        L"NPU processes",
        L"Per-process accelerator utilization for compute-only adapters. Empty when no process is using an NPU.",
        kNpuProcessColumns.data(),
        kNpuProcessColumnCount,
        kNpuProcessMaximumRows,
        kRecommendedIntervalMilliseconds,
        RedXeDataSetFlagTable | RedXeDataSetFlagLocalSensitive,
    },
    RedXeDataSetDescriptor{
        sizeof(RedXeDataSetDescriptor),
        kSecurityDataSetId,
        L"Security posture",
        L"Platform code-integrity and virtualization-based security state. Configuration flags, not counters.",
        kSecurityColumns.data(),
        kSecurityColumnCount,
        1,
        kSecurityIntervalMilliseconds,
        RedXeDataSetFlagTable,
    },
};
static_assert(kDataSets.size() <= kStatusMaximumRows);
static_assert(kDataSets.size() <= RedXeDataCollectMaximumDataSets);

struct DatasetBackendInfo final
{
    const wchar_t* dataSetIdWide;
    uint64_t backendTier;
    const wchar_t* backendName;
    const wchar_t* backendVersion;
};

constexpr std::array kDatasetBackends{
    DatasetBackendInfo{L"source.status", 0, L"internal", L"1"},
    DatasetBackendInfo{L"system.summary", kBackendNative, L"nt-handlecount", L"1"},
    DatasetBackendInfo{L"cpu.summary", kBackendNative, L"nt-processor-performance", L"1"},
    DatasetBackendInfo{L"cpu.logical", kBackendNative, L"nt-processor-performance", L"1"},
    DatasetBackendInfo{L"memory.summary", kBackendWin32, L"k32-perfinfo", L"1"},
    DatasetBackendInfo{L"process.list", kBackendNative, L"nt-system-process", L"1"},
    DatasetBackendInfo{L"process.activity", kBackendNative, L"nt-system-process", L"1"},
    DatasetBackendInfo{L"thread.list", kBackendNative, L"nt-system-process", L"1"},
    DatasetBackendInfo{L"network.interface", kBackendWin32, L"iphelper-ifentry2", L"1"},
    DatasetBackendInfo{L"network.protocol", kBackendWin32, L"iphelper-stats", L"1"},
    DatasetBackendInfo{L"storage.disk", kBackendWin32, L"ioctl-disk-performance", L"1"},
    DatasetBackendInfo{L"storage.volume", kBackendWin32, L"find-volume", L"1"},
    DatasetBackendInfo{L"gpu.adapter", kBackendWin32, L"dxgi-d3dkmt", L"1"},
    DatasetBackendInfo{L"gpu.engine", kBackendWin32, L"d3dkmt-node", L"1"},
    DatasetBackendInfo{L"gpu.process", kBackendCounter, L"pdh-gpu-engine", L"1"},
    DatasetBackendInfo{L"power.summary", kBackendWin32, L"system-power-status", L"1"},
    DatasetBackendInfo{L"battery.list", kBackendWin32, L"ioctl-battery", L"1"},
    DatasetBackendInfo{L"thermal.sensor", kBackendWin32, L"gpu-storage-battery-acpi", L"1"},
    DatasetBackendInfo{L"fan.sensor", kBackendWin32, L"d3dkmt-gpu-fan", L"1"},
    DatasetBackendInfo{L"npu.adapter", kBackendWin32, L"d3dkmt-dxcore", L"1"},
    DatasetBackendInfo{L"npu.engine", kBackendWin32, L"dxcore-engine-runtime", L"1"},
    DatasetBackendInfo{L"npu.process", kBackendCounter, L"pdh-gpu-engine", L"1"},
    DatasetBackendInfo{L"security.posture", kBackendNative, L"nt-code-integrity", L"1"},
};
static_assert(kDatasetBackends.size() == kDataSets.size());

struct DatasetRuntimeStatus final
{
    uint64_t lastResult = kLastResultOk;
    uint64_t lastSuccessFileTime100ns = 0;
    uint64_t lastDurationMicroseconds = 0;
    uint64_t failureCount = 0;
    uint64_t truncationCount = 0;
    uint64_t retryFileTime100ns = 0;
};

[[nodiscard]] uint32_t Utf16Length(const wchar_t* value) noexcept
{
    if (!value)
    {
        return 0;
    }
    uint32_t length = 0;
    while (value[length] != L'\0')
    {
        ++length;
    }
    return length;
}

void AppendWide(wchar_t* destination, size_t destinationCount, size_t& length, const wchar_t* source) noexcept
{
    if (!destination || destinationCount == 0 || !source)
    {
        return;
    }
    while (source[0] != L'\0' && length + 1 < destinationCount)
    {
        destination[length] = *source;
        ++length;
        ++source;
    }
    destination[length] = L'\0';
}

void AppendDecimal(wchar_t* destination, size_t destinationCount, size_t& length, uint64_t value) noexcept
{
    wchar_t digits[20]{};
    size_t digitCount = 0;
    do
    {
        digits[digitCount] = static_cast<wchar_t>(L'0' + (value % 10));
        value /= 10;
        ++digitCount;
    } while (value != 0 && digitCount < std::size(digits));
    while (digitCount > 0 && length + 1 < destinationCount)
    {
        --digitCount;
        destination[length] = digits[digitCount];
        ++length;
    }
    if (destination && destinationCount != 0)
    {
        destination[length] = L'\0';
    }
}

void AppendHex64(wchar_t* destination, size_t destinationCount, size_t& length, uint64_t value) noexcept
{
    for (int shift = 60; shift >= 0 && length + 1 < destinationCount; shift -= 4)
    {
        const unsigned digit = static_cast<unsigned>((value >> shift) & 0xF);
        destination[length] = static_cast<wchar_t>(digit < 10 ? L'0' + digit : L'a' + (digit - 10));
        ++length;
    }
    if (destination && destinationCount != 0)
    {
        destination[length] = L'\0';
    }
}

[[nodiscard]] size_t FindDataSetIndex(const char* dataSetId) noexcept
{
    if (!dataSetId)
    {
        return kDataSets.size();
    }
    for (size_t index = 0; index < kDataSets.size(); ++index)
    {
        if (RedXeAsciiEqualsIgnoreCase(kDataSets[index].dataSetId, dataSetId))
        {
            return index;
        }
    }
    return kDataSets.size();
}

[[nodiscard]] uint64_t FileTimeValue(const FILETIME& value) noexcept
{
    ULARGE_INTEGER combined{};
    combined.LowPart = value.dwLowDateTime;
    combined.HighPart = value.dwHighDateTime;
    return combined.QuadPart;
}

[[nodiscard]] uint64_t CurrentTimestamp() noexcept
{
    FILETIME timestamp{};
    GetSystemTimePreciseAsFileTime(&timestamp);
    return FileTimeValue(timestamp);
}

[[nodiscard]] RedXeDataValue UInt64Value(uint64_t value, RedXeDataQuality quality) noexcept
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

[[nodiscard]] RedXeDataValue Utf16Value(const wchar_t* value, uint32_t characters, RedXeDataQuality quality) noexcept
{
    RedXeDataValue result{};
    result.sizeBytes = sizeof(result);
    result.valueType = RedXeDataValueTypeUtf16;
    result.quality = quality;
    result.utf16Value = value;
    result.utf16Characters = characters;
    return result;
}

// Delta between two samples of a 32-bit kernel counter, tolerating the single wrap the counter can perform between
// two collections. A counter that moved backwards by more than one wrap is treated as a reset and reported as zero.
[[nodiscard]] uint64_t Delta32(uint32_t current, uint32_t previous) noexcept
{
    return current >= previous ? static_cast<uint64_t>(current - previous)
                               : (static_cast<uint64_t>(UINT32_MAX) - previous) + current + 1ULL;
}

// Converts a counter delta and an elapsed span in 100 ns units into a per-second rate.
[[nodiscard]] double PerSecond(uint64_t delta, uint64_t elapsed100ns) noexcept
{
    if (elapsed100ns == 0)
    {
        return 0.0;
    }
    return (static_cast<double>(delta) * 10'000'000.0) / static_cast<double>(elapsed100ns);
}

[[nodiscard]] bool PageCountToBytes(SIZE_T pageCount, SIZE_T pageSize, uint64_t& bytes) noexcept
{
    if (pageSize != 0 && pageCount > (UINT64_MAX / pageSize))
    {
        bytes = 0;
        return false;
    }
    bytes = static_cast<uint64_t>(pageCount) * static_cast<uint64_t>(pageSize);
    return true;
}

[[nodiscard]] bool ReadSystemTimes(uint64_t& idle, uint64_t& total) noexcept
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

[[nodiscard]] double BoundedPercent(uint64_t numerator, uint64_t denominator) noexcept
{
    if (denominator == 0)
    {
        return 0.0;
    }
    return std::clamp((static_cast<double>(numerator) * 100.0) / static_cast<double>(denominator), 0.0, 100.0);
}

struct LogicalCpuTimes final
{
    uint64_t idle = 0;
    uint64_t kernel = 0;
    uint64_t user = 0;
    uint64_t dpc = 0;
    uint64_t interrupt = 0;
    uint32_t interruptCount = 0;
};

struct ProcessCpuSample final
{
    uint32_t processId = 0;
    uint64_t creationTime = 0;
    uint64_t processorTime = 0;
    uint64_t userTime = 0;
    uint64_t kernelTime = 0;
    uint64_t ioReadBytes = 0;
    uint64_t ioWriteBytes = 0;
    uint64_t ioOtherBytes = 0;
    uint64_t pageFaults = 0;
};

struct ThreadCpuSample final
{
    uint32_t threadId = 0;
    uint32_t contextSwitches = 0;
    uint64_t createTime = 0;
    uint64_t processorTime = 0;
};

[[nodiscard]] size_t ThreadSlot(uint32_t threadId, uint64_t createTime) noexcept
{
    return (static_cast<size_t>(threadId) * 2654435761U ^ static_cast<size_t>(createTime ^ (createTime >> 32U))) &
           (kThreadHistoryCapacity - 1);
}

// The control object and every lazy family share one reservation. Blocks are page-aligned;
// their C++ lifetime begins only after commitment, and nontrivial owners end before releasing the region.
constexpr size_t kSourceArenaBytes = 16U * 1024U * 1024U;
constexpr size_t kSourceControlRegionBytes = 64U * 1024U;
constexpr size_t kSourcePageBytes = 4096;
constexpr size_t SourcePageRound(size_t bytes) noexcept
{
    return (bytes + kSourcePageBytes - 1) & ~(kSourcePageBytes - 1);
}
enum class SourceFamily : uint32_t
{
    Cheap,
    Walk,
    ThreadScratch,
    Process,
    ProcessValues,
    ActivityViews,
    Thread,
    Logical,
    Net,
    Gpu,
    Power,
    Status,
    Summary,
    CpuSummary,
    Memory,
    NetworkInterface,
    NetworkProtocol,
    StorageDisk,
    StorageVolume,
    GpuAdapter,
    GpuEngine,
    GpuProcess,
    PowerSummary,
    Battery,
    Thermal,
    Fan,
    NpuAdapter,
    NpuEngine,
    NpuProcess,
    Security,
    Count,
};
constexpr uint64_t SourceFamilyBit(SourceFamily family) noexcept
{
    return uint64_t{1} << static_cast<uint32_t>(family);
}
struct SourceCheapStorage final
{
    RedXeNativeState _nativeState{};
    RedXeNativeCheapSample _cheap{};
};
struct SourceWalkStorage final
{
    RedXeNativeWalkSample _walk{};
    std::array<std::byte, kRedXeNtProcessBufferBytes> buffer{};
};
struct SourceThreadScratchStorage final
{
    std::array<RedXeNativeThreadRow, kMaximumThreads> rows{};
};
struct SourceProcessStorage final
{
    std::array<RedXeDataRow, kMaximumProcesses> _processRows{};
    std::array<wchar_t, kProcessNameCharacters> _processNames{};
    std::array<ProcessCpuSample, kProcessHistoryCapacity> _processHistory{};
    std::array<ProcessCpuSample, kMaximumProcesses> _currentCpu{};
};
using SourceProcessValuesStorage = std::array<RedXeDataValue, kMaximumProcesses * kProcessColumnCount>;
struct SourceActivityViewsStorage final
{
    std::array<RedXeDataRow, kMaximumProcesses> _processActivityRows{};
};
struct SourceThreadStorage final
{
    std::array<ThreadCpuSample, kThreadHistoryCapacity> _threadHistory{};
    std::array<RedXeDataValue, kMaximumThreads * kThreadColumnCount> _threadValues{};
    std::array<RedXeDataRow, kMaximumThreads> _threadRows{};
};
struct SourceLogicalStorage final
{
    std::array<LogicalCpuTimes, kRedXeMaximumLogicalProcessors> _previousLogical{};
    std::array<RedXeDataValue, kRedXeMaximumLogicalProcessors * kCpuLogicalColumnCount> _cpuLogicalValues{};
    std::array<RedXeDataRow, kRedXeMaximumLogicalProcessors> _cpuLogicalRows{};
};
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct SourceNetStorage final
{
    RedXeNetStorageState _netStorage{};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct SourceGpuStorage final
{
    RedXeGpuState _gpu{};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct SourcePowerStorage final
{
    RedXePowerState _power{};
};
#pragma warning(pop)

struct SourceStatusStorage final
{
    std::array<RedXeDataValue, kStatusMaximumRows * kStatusColumnCount> _statusValues{};
    std::array<RedXeDataRow, kStatusMaximumRows> _statusRows{};
};
struct SourceSummaryStorage final
{
    std::array<RedXeDataValue, kSummaryColumnCount> _summaryValues{};
    RedXeDataRow _summaryRow{};
};
struct SourceCpuSummaryStorage final
{
    std::array<RedXeDataValue, kCpuSummaryColumnCount> _cpuSummaryValues{};
    RedXeDataRow _cpuSummaryRow{};
};
struct SourceMemoryStorage final
{
    std::array<RedXeDataValue, kMemoryColumnCount> _memoryValues{};
    RedXeDataRow _memoryRow{};
};
struct SourceNetworkInterfaceStorage final
{
    std::array<RedXeDataValue, kRedXeMaximumNetworkInterfaces * kNetworkInterfaceColumnCount> _networkInterfaceValues{};
    std::array<RedXeDataRow, kRedXeMaximumNetworkInterfaces> _networkInterfaceRows{};
};
struct SourceNetworkProtocolStorage final
{
    std::array<RedXeDataValue, kRedXeNetworkProtocolCount * kNetworkProtocolColumnCount> _networkProtocolValues{};
    std::array<RedXeDataRow, kRedXeNetworkProtocolCount> _networkProtocolRows{};
};
struct SourceStorageDiskStorage final
{
    std::array<RedXeDataValue, kRedXeMaximumStorageDisks * kStorageDiskColumnCount> _storageDiskValues{};
    std::array<RedXeDataRow, kRedXeMaximumStorageDisks> _storageDiskRows{};
};
struct SourceStorageVolumeStorage final
{
    std::array<RedXeDataValue, kRedXeMaximumStorageVolumes * kStorageVolumeColumnCount> _storageVolumeValues{};
    std::array<RedXeDataRow, kRedXeMaximumStorageVolumes> _storageVolumeRows{};
};
struct SourceGpuAdapterStorage final
{
    std::array<RedXeDataValue, kRedXeMaximumGpuAdapters * kGpuAdapterColumnCount> _gpuAdapterValues{};
    std::array<RedXeDataRow, kRedXeMaximumGpuAdapters> _gpuAdapterRows{};
};
struct SourceGpuEngineStorage final
{
    std::array<RedXeDataValue, kRedXeMaximumGpuEngines * kGpuEngineColumnCount> _gpuEngineValues{};
    std::array<RedXeDataRow, kRedXeMaximumGpuEngines> _gpuEngineRows{};
};
struct SourceGpuProcessStorage final
{
    std::array<RedXeDataValue, kRedXeMaximumGpuProcesses * kGpuProcessColumnCount> _gpuProcessValues{};
    std::array<RedXeDataRow, kRedXeMaximumGpuProcesses> _gpuProcessRows{};
};
struct SourcePowerSummaryStorage final
{
    std::array<RedXeDataValue, kPowerSummaryColumnCount> _powerSummaryValues{};
    RedXeDataRow _powerSummaryRow{};
};
struct SourceBatteryStorage final
{
    std::array<RedXeDataValue, kRedXeMaximumBatteries * kBatteryColumnCount> _batteryValues{};
    std::array<RedXeDataRow, kRedXeMaximumBatteries> _batteryRows{};
};
struct SourceThermalStorage final
{
    std::array<std::array<wchar_t, 96>, kThermalMaximumRows> _thermalSensorIds{};
    std::array<RedXeDataValue, kThermalMaximumRows * kThermalColumnCount> _thermalValues{};
    std::array<RedXeDataRow, kThermalMaximumRows> _thermalRows{};
};
struct SourceFanStorage final
{
    std::array<std::array<wchar_t, 96>, kFanMaximumRows> _fanSensorIds{};
    std::array<RedXeDataValue, kFanMaximumRows * kFanColumnCount> _fanValues{};
    std::array<RedXeDataRow, kFanMaximumRows> _fanRows{};
};
struct SourceNpuAdapterStorage final
{
    std::array<RedXeDataValue, kNpuAdapterMaximumRows * kNpuAdapterColumnCount> _npuAdapterValues{};
    std::array<RedXeDataRow, kNpuAdapterMaximumRows> _npuAdapterRows{};
};
struct SourceNpuEngineStorage final
{
    std::array<RedXeDataValue, kNpuEngineMaximumRows * kNpuEngineColumnCount> _npuEngineValues{};
    std::array<RedXeDataRow, kNpuEngineMaximumRows> _npuEngineRows{};
};
struct SourceNpuProcessStorage final
{
    std::array<RedXeDataValue, kNpuProcessMaximumRows * kNpuProcessColumnCount> _npuProcessValues{};
    std::array<RedXeDataRow, kNpuProcessMaximumRows> _npuProcessRows{};
};
struct SourceSecurityStorage final
{
    std::array<RedXeDataValue, kSecurityColumnCount> _securityValues{};
    RedXeDataRow _securityRow{};
};
template <typename T> struct alignas(kSourcePageBytes) SourceStorageBlock final
{
    std::byte bytes[SourcePageRound(sizeof(T))];
};
struct SourceStorageLayout final
{
    SourceStorageBlock<SourceCheapStorage> Cheap;
    SourceStorageBlock<SourceWalkStorage> Walk;
    SourceStorageBlock<SourceThreadScratchStorage> ThreadScratch;
    SourceStorageBlock<SourceProcessStorage> Process;
    SourceStorageBlock<SourceProcessValuesStorage> ProcessValues;
    SourceStorageBlock<SourceActivityViewsStorage> ActivityViews;
    SourceStorageBlock<SourceThreadStorage> Thread;
    SourceStorageBlock<SourceLogicalStorage> Logical;
    SourceStorageBlock<SourceNetStorage> Net;
    SourceStorageBlock<SourceGpuStorage> Gpu;
    SourceStorageBlock<SourcePowerStorage> Power;
    SourceStorageBlock<SourceStatusStorage> Status;
    SourceStorageBlock<SourceSummaryStorage> Summary;
    SourceStorageBlock<SourceCpuSummaryStorage> CpuSummary;
    SourceStorageBlock<SourceMemoryStorage> Memory;
    SourceStorageBlock<SourceNetworkInterfaceStorage> NetworkInterface;
    SourceStorageBlock<SourceNetworkProtocolStorage> NetworkProtocol;
    SourceStorageBlock<SourceStorageDiskStorage> StorageDisk;
    SourceStorageBlock<SourceStorageVolumeStorage> StorageVolume;
    SourceStorageBlock<SourceGpuAdapterStorage> GpuAdapter;
    SourceStorageBlock<SourceGpuEngineStorage> GpuEngine;
    SourceStorageBlock<SourceGpuProcessStorage> GpuProcess;
    SourceStorageBlock<SourcePowerSummaryStorage> PowerSummary;
    SourceStorageBlock<SourceBatteryStorage> Battery;
    SourceStorageBlock<SourceThermalStorage> Thermal;
    SourceStorageBlock<SourceFanStorage> Fan;
    SourceStorageBlock<SourceNpuAdapterStorage> NpuAdapter;
    SourceStorageBlock<SourceNpuEngineStorage> NpuEngine;
    SourceStorageBlock<SourceNpuProcessStorage> NpuProcess;
    SourceStorageBlock<SourceSecurityStorage> Security;
};
constexpr size_t kSourceStorageBytes = kSourceControlRegionBytes + sizeof(SourceStorageLayout);
static_assert(kSourceStorageBytes < kSourceArenaBytes);

template <SourceFamily> struct SourceStorageType;
template <> struct SourceStorageType<SourceFamily::Cheap>
{
    using Type = SourceCheapStorage;
};
template <> struct SourceStorageType<SourceFamily::Walk>
{
    using Type = SourceWalkStorage;
};
template <> struct SourceStorageType<SourceFamily::ThreadScratch>
{
    using Type = SourceThreadScratchStorage;
};
template <> struct SourceStorageType<SourceFamily::Process>
{
    using Type = SourceProcessStorage;
};
template <> struct SourceStorageType<SourceFamily::ProcessValues>
{
    using Type = SourceProcessValuesStorage;
};
template <> struct SourceStorageType<SourceFamily::ActivityViews>
{
    using Type = SourceActivityViewsStorage;
};
template <> struct SourceStorageType<SourceFamily::Thread>
{
    using Type = SourceThreadStorage;
};
template <> struct SourceStorageType<SourceFamily::Logical>
{
    using Type = SourceLogicalStorage;
};
template <> struct SourceStorageType<SourceFamily::Net>
{
    using Type = SourceNetStorage;
};
template <> struct SourceStorageType<SourceFamily::Gpu>
{
    using Type = SourceGpuStorage;
};
template <> struct SourceStorageType<SourceFamily::Power>
{
    using Type = SourcePowerStorage;
};
template <> struct SourceStorageType<SourceFamily::Status>
{
    using Type = SourceStatusStorage;
};
template <> struct SourceStorageType<SourceFamily::Summary>
{
    using Type = SourceSummaryStorage;
};
template <> struct SourceStorageType<SourceFamily::CpuSummary>
{
    using Type = SourceCpuSummaryStorage;
};
template <> struct SourceStorageType<SourceFamily::Memory>
{
    using Type = SourceMemoryStorage;
};
template <> struct SourceStorageType<SourceFamily::NetworkInterface>
{
    using Type = SourceNetworkInterfaceStorage;
};
template <> struct SourceStorageType<SourceFamily::NetworkProtocol>
{
    using Type = SourceNetworkProtocolStorage;
};
template <> struct SourceStorageType<SourceFamily::StorageDisk>
{
    using Type = SourceStorageDiskStorage;
};
template <> struct SourceStorageType<SourceFamily::StorageVolume>
{
    using Type = SourceStorageVolumeStorage;
};
template <> struct SourceStorageType<SourceFamily::GpuAdapter>
{
    using Type = SourceGpuAdapterStorage;
};
template <> struct SourceStorageType<SourceFamily::GpuEngine>
{
    using Type = SourceGpuEngineStorage;
};
template <> struct SourceStorageType<SourceFamily::GpuProcess>
{
    using Type = SourceGpuProcessStorage;
};
template <> struct SourceStorageType<SourceFamily::PowerSummary>
{
    using Type = SourcePowerSummaryStorage;
};
template <> struct SourceStorageType<SourceFamily::Battery>
{
    using Type = SourceBatteryStorage;
};
template <> struct SourceStorageType<SourceFamily::Thermal>
{
    using Type = SourceThermalStorage;
};
template <> struct SourceStorageType<SourceFamily::Fan>
{
    using Type = SourceFanStorage;
};
template <> struct SourceStorageType<SourceFamily::NpuAdapter>
{
    using Type = SourceNpuAdapterStorage;
};
template <> struct SourceStorageType<SourceFamily::NpuEngine>
{
    using Type = SourceNpuEngineStorage;
};
template <> struct SourceStorageType<SourceFamily::NpuProcess>
{
    using Type = SourceNpuProcessStorage;
};
template <> struct SourceStorageType<SourceFamily::Security>
{
    using Type = SourceSecurityStorage;
};
struct SourceStorageSlot final
{
    size_t offset;
    size_t bytes;
    void (*construct)(void*) noexcept;
    void (*destroy)(void*) noexcept;
};
template <typename T> constexpr SourceStorageSlot MakeSourceSlot(size_t offset) noexcept
{
    static_assert(std::is_nothrow_default_constructible_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>);
    return SourceStorageSlot{
        offset,
        SourcePageRound(sizeof(T)),
        [](void* address) noexcept { std::construct_at(static_cast<T*>(address)); },
        [](void* address) noexcept { std::destroy_at(std::launder(static_cast<T*>(address))); },
    };
}
constexpr std::array kSourceSlots{
    MakeSourceSlot<SourceCheapStorage>(offsetof(SourceStorageLayout, Cheap)),
    MakeSourceSlot<SourceWalkStorage>(offsetof(SourceStorageLayout, Walk)),
    MakeSourceSlot<SourceThreadScratchStorage>(offsetof(SourceStorageLayout, ThreadScratch)),
    MakeSourceSlot<SourceProcessStorage>(offsetof(SourceStorageLayout, Process)),
    SourceStorageSlot{offsetof(SourceStorageLayout, ProcessValues), SourcePageRound(sizeof(SourceProcessValuesStorage)),
                      nullptr, nullptr},
    MakeSourceSlot<SourceActivityViewsStorage>(offsetof(SourceStorageLayout, ActivityViews)),
    MakeSourceSlot<SourceThreadStorage>(offsetof(SourceStorageLayout, Thread)),
    MakeSourceSlot<SourceLogicalStorage>(offsetof(SourceStorageLayout, Logical)),
    MakeSourceSlot<SourceNetStorage>(offsetof(SourceStorageLayout, Net)),
    MakeSourceSlot<SourceGpuStorage>(offsetof(SourceStorageLayout, Gpu)),
    MakeSourceSlot<SourcePowerStorage>(offsetof(SourceStorageLayout, Power)),
    MakeSourceSlot<SourceStatusStorage>(offsetof(SourceStorageLayout, Status)),
    MakeSourceSlot<SourceSummaryStorage>(offsetof(SourceStorageLayout, Summary)),
    MakeSourceSlot<SourceCpuSummaryStorage>(offsetof(SourceStorageLayout, CpuSummary)),
    MakeSourceSlot<SourceMemoryStorage>(offsetof(SourceStorageLayout, Memory)),
    MakeSourceSlot<SourceNetworkInterfaceStorage>(offsetof(SourceStorageLayout, NetworkInterface)),
    MakeSourceSlot<SourceNetworkProtocolStorage>(offsetof(SourceStorageLayout, NetworkProtocol)),
    MakeSourceSlot<SourceStorageDiskStorage>(offsetof(SourceStorageLayout, StorageDisk)),
    MakeSourceSlot<SourceStorageVolumeStorage>(offsetof(SourceStorageLayout, StorageVolume)),
    MakeSourceSlot<SourceGpuAdapterStorage>(offsetof(SourceStorageLayout, GpuAdapter)),
    MakeSourceSlot<SourceGpuEngineStorage>(offsetof(SourceStorageLayout, GpuEngine)),
    MakeSourceSlot<SourceGpuProcessStorage>(offsetof(SourceStorageLayout, GpuProcess)),
    MakeSourceSlot<SourcePowerSummaryStorage>(offsetof(SourceStorageLayout, PowerSummary)),
    MakeSourceSlot<SourceBatteryStorage>(offsetof(SourceStorageLayout, Battery)),
    MakeSourceSlot<SourceThermalStorage>(offsetof(SourceStorageLayout, Thermal)),
    MakeSourceSlot<SourceFanStorage>(offsetof(SourceStorageLayout, Fan)),
    MakeSourceSlot<SourceNpuAdapterStorage>(offsetof(SourceStorageLayout, NpuAdapter)),
    MakeSourceSlot<SourceNpuEngineStorage>(offsetof(SourceStorageLayout, NpuEngine)),
    MakeSourceSlot<SourceNpuProcessStorage>(offsetof(SourceStorageLayout, NpuProcess)),
    MakeSourceSlot<SourceSecurityStorage>(offsetof(SourceStorageLayout, Security)),
};
static_assert(kSourceSlots.size() == static_cast<size_t>(SourceFamily::Count));

struct SourceStorageRequirement final
{
    const char* dataSetId;
    uint64_t families;
};
constexpr std::array kSourceRequirements{
    SourceStorageRequirement{kStatusDataSetId, SourceFamilyBit(SourceFamily::Status)},
    SourceStorageRequirement{kSummaryDataSetId,
                             SourceFamilyBit(SourceFamily::Cheap) | SourceFamilyBit(SourceFamily::Summary)},
    SourceStorageRequirement{kCpuSummaryDataSetId,
                             SourceFamilyBit(SourceFamily::Cheap) | SourceFamilyBit(SourceFamily::CpuSummary)},
    SourceStorageRequirement{kCpuLogicalDataSetId,
                             SourceFamilyBit(SourceFamily::Cheap) | SourceFamilyBit(SourceFamily::Logical)},
    SourceStorageRequirement{kMemoryDataSetId,
                             SourceFamilyBit(SourceFamily::Cheap) | SourceFamilyBit(SourceFamily::Memory)},
    SourceStorageRequirement{kProcessDataSetId, SourceFamilyBit(SourceFamily::Walk) |
                                                    SourceFamilyBit(SourceFamily::Process) |
                                                    SourceFamilyBit(SourceFamily::ProcessValues)},
    SourceStorageRequirement{kProcessActivityDataSetId, SourceFamilyBit(SourceFamily::Walk) |
                                                            SourceFamilyBit(SourceFamily::Process) |
                                                            SourceFamilyBit(SourceFamily::ProcessValues)},
    SourceStorageRequirement{kThreadDataSetId, SourceFamilyBit(SourceFamily::Walk) |
                                                   SourceFamilyBit(SourceFamily::ThreadScratch) |
                                                   SourceFamilyBit(SourceFamily::Thread)},
    SourceStorageRequirement{kNetworkInterfaceDataSetId,
                             SourceFamilyBit(SourceFamily::Net) | SourceFamilyBit(SourceFamily::NetworkInterface)},
    SourceStorageRequirement{kNetworkProtocolDataSetId,
                             SourceFamilyBit(SourceFamily::Net) | SourceFamilyBit(SourceFamily::NetworkProtocol)},
    SourceStorageRequirement{kStorageDiskDataSetId,
                             SourceFamilyBit(SourceFamily::Net) | SourceFamilyBit(SourceFamily::StorageDisk)},
    SourceStorageRequirement{kStorageVolumeDataSetId,
                             SourceFamilyBit(SourceFamily::Net) | SourceFamilyBit(SourceFamily::StorageVolume)},
    SourceStorageRequirement{kGpuAdapterDataSetId,
                             SourceFamilyBit(SourceFamily::Gpu) | SourceFamilyBit(SourceFamily::GpuAdapter)},
    SourceStorageRequirement{kGpuEngineDataSetId,
                             SourceFamilyBit(SourceFamily::Gpu) | SourceFamilyBit(SourceFamily::GpuEngine)},
    SourceStorageRequirement{kGpuProcessDataSetId,
                             SourceFamilyBit(SourceFamily::Gpu) | SourceFamilyBit(SourceFamily::GpuProcess)},
    SourceStorageRequirement{kPowerSummaryDataSetId,
                             SourceFamilyBit(SourceFamily::Power) | SourceFamilyBit(SourceFamily::PowerSummary)},
    SourceStorageRequirement{kBatteryListDataSetId,
                             SourceFamilyBit(SourceFamily::Power) | SourceFamilyBit(SourceFamily::Battery)},
    SourceStorageRequirement{kThermalSensorDataSetId,
                             SourceFamilyBit(SourceFamily::Power) | SourceFamilyBit(SourceFamily::Net) |
                                 SourceFamilyBit(SourceFamily::Gpu) | SourceFamilyBit(SourceFamily::Thermal)},
    SourceStorageRequirement{kFanSensorDataSetId, SourceFamilyBit(SourceFamily::Power) |
                                                      SourceFamilyBit(SourceFamily::Gpu) |
                                                      SourceFamilyBit(SourceFamily::Fan)},
    SourceStorageRequirement{kNpuAdapterDataSetId,
                             SourceFamilyBit(SourceFamily::Gpu) | SourceFamilyBit(SourceFamily::NpuAdapter)},
    SourceStorageRequirement{kNpuEngineDataSetId,
                             SourceFamilyBit(SourceFamily::Gpu) | SourceFamilyBit(SourceFamily::NpuEngine)},
    SourceStorageRequirement{kNpuProcessDataSetId,
                             SourceFamilyBit(SourceFamily::Gpu) | SourceFamilyBit(SourceFamily::NpuProcess)},
    SourceStorageRequirement{kSecurityDataSetId, SourceFamilyBit(SourceFamily::Security)},
};
static_assert(kSourceRequirements.size() == kDataSets.size());

std::atomic<uint32_t> gSourceAllocationFault{0};
std::atomic<uint32_t> gSourceArenaCount{0};
std::atomic<uint64_t> gSourceCommittedBytes{0};
std::atomic<uint32_t> gSourceGpuOwners{0};
std::atomic<uint32_t> gSourceNetOwners{0};
std::atomic<uint32_t> gSourcePowerOwners{0};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class SystemDataSource final : public RedXeComObject<SystemDataSource, IRedXeDataSource, IRedXeSystemDataTestSource>
{
  public:
    SystemDataSource() noexcept : _arenaCommittedBytes(SourcePageRound(sizeof(SystemDataSource))) {}

    ~SystemDataSource() noexcept
    {
        for (size_t index = 0; index < kSourceSlots.size(); ++index)
        {
            if ((_arenaCommittedFamilies & (uint64_t{1} << index)) != 0 && kSourceSlots[index].destroy)
                kSourceSlots[index].destroy(ArenaAddress(kSourceSlots[index].offset));
        }
        if (HasStorage(SourceFamily::Gpu))
            gSourceGpuOwners.fetch_sub(1, std::memory_order_relaxed);
        if (HasStorage(SourceFamily::Net))
            gSourceNetOwners.fetch_sub(1, std::memory_order_relaxed);
        if (HasStorage(SourceFamily::Power))
            gSourcePowerOwners.fetch_sub(1, std::memory_order_relaxed);
        gSourceCommittedBytes.fetch_sub(_arenaCommittedBytes, std::memory_order_relaxed);
        gSourceArenaCount.fetch_sub(1, std::memory_order_relaxed);
    }

    // RedXeComObject's final delete returns this self-owned reservation only after typed destruction.
    static void* operator new(size_t bytes, const std::nothrow_t&) noexcept
    {
        const uint32_t fault = gSourceAllocationFault.exchange(0, std::memory_order_relaxed);
        if (fault == 1 || bytes > kSourceControlRegionBytes)
            return nullptr;
        wil::unique_virtualalloc_ptr<void> region{
            VirtualAlloc(nullptr, kSourceArenaBytes, MEM_RESERVE, PAGE_READWRITE)};
        if (!region || fault == 2 || !VirtualAlloc(region.get(), SourcePageRound(bytes), MEM_COMMIT, PAGE_READWRITE))
            return nullptr;
        gSourceArenaCount.fetch_add(1, std::memory_order_relaxed);
        gSourceCommittedBytes.fetch_add(SourcePageRound(bytes), std::memory_order_relaxed);
        return region.release(); // Transfer to the COM lifetime; operator delete adopts it back into WIL.
    }

    static void operator delete(void* address) noexcept
    {
        wil::unique_virtualalloc_ptr<void> region{address};
    }
    static void operator delete(void* address, const std::nothrow_t&) noexcept
    {
        operator delete(address);
    }

    HRESULT STDMETHODCALLTYPE FailArenaCommitForTest(uint32_t successfulNewCommitsBeforeFailure) noexcept override
    {
        if (_collecting.test_and_set(std::memory_order_acquire))
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        auto clearCollecting = wil::scope_exit([this] { _collecting.clear(std::memory_order_release); });
        _arenaFailureAfter = successfulNewCommitsBeforeFailure;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors, uint32_t* count) noexcept override
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
        *count = static_cast<uint32_t>(kDataSets.size());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CollectSnapshots(const RedXeDataCollectRequest* request,
                                               const RedXeDataCollectResult** result) noexcept override
    {
        if (result)
        {
            *result = nullptr;
        }
        if (!result)
        {
            return E_POINTER;
        }
        if (!request || request->sizeBytes != sizeof(RedXeDataCollectRequest) || !request->dataSetIds ||
            request->dataSetCount == 0 || request->dataSetCount > RedXeDataCollectMaximumDataSets)
        {
            return E_INVALIDARG;
        }
        for (uint32_t index = 0; index < request->dataSetCount; ++index)
        {
            if (!request->dataSetIds[index] || request->dataSetIds[index][0] == '\0')
            {
                return E_INVALIDARG;
            }
            for (uint32_t previous = 0; previous < index; ++previous)
            {
                if (RedXeAsciiEqualsIgnoreCase(request->dataSetIds[previous], request->dataSetIds[index]))
                {
                    return E_INVALIDARG;
                }
            }
        }
        if (_collecting.test_and_set(std::memory_order_acquire))
        {
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        }
        auto clearCollecting = wil::scope_exit([this] { _collecting.clear(std::memory_order_release); });

        const HRESULT storageReady = PrepareStorage(*request);
        if (FAILED(storageReady))
            return storageReady;

        ExternalRateHistory external{};
        CaptureExternalRateHistory(external);
        PrepareBatchSamples(*request);
        const uint64_t timestamp = CurrentTimestamp();
        const uint64_t sequence = _sequence + 1;
        LARGE_INTEGER frequency{};
        if (QueryPerformanceFrequency(&frequency) == FALSE || frequency.QuadPart == 0)
        {
            RestoreExternalRateHistory(external);
            return E_FAIL;
        }

        bool mutatedInternalRates = false;
        for (uint32_t index = 0; index < request->dataSetCount; ++index)
        {
            LARGE_INTEGER start{};
            LARGE_INTEGER finish{};
            (void)QueryPerformanceCounter(&start);
            const RedXeDataSnapshot* snapshot = nullptr;
            const HRESULT collected = CollectOne(request->dataSetIds[index], timestamp, sequence, &snapshot);
            (void)QueryPerformanceCounter(&finish);
            const uint64_t elapsedTicks =
                finish.QuadPart >= start.QuadPart ? static_cast<uint64_t>(finish.QuadPart - start.QuadPart) : 0;
            const uint64_t durationUs = elapsedTicks * 1'000'000ULL / static_cast<uint64_t>(frequency.QuadPart);
            if (FAILED(collected) || !snapshot)
            {
                if (collected != HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
                {
                    RecordStatus(request->dataSetIds[index], kLastResultFailed, durationUs, timestamp);
                }
                RestoreExternalRateHistory(external);
                if (mutatedInternalRates)
                {
                    DiscardInternalRateHistory();
                }
                return FAILED(collected) ? collected : E_FAIL;
            }
            if (DatasetCommitsInternalRateHistory(request->dataSetIds[index]))
            {
                mutatedInternalRates = true;
            }
            const uint64_t lastResult =
                (snapshot->flags & RedXeDataSnapshotFlagTruncated) != 0 ? kLastResultTruncated : kLastResultOk;
            RecordStatus(request->dataSetIds[index], lastResult, durationUs, timestamp);
            _batchSnapshots[index] = snapshot;
        }

        ++_sequence;
        _batchResult = RedXeDataCollectResult{
            sizeof(RedXeDataCollectResult), request->dataSetCount, sequence, timestamp, _batchSnapshots.data(),
        };
        *result = &_batchResult;
        return S_OK;
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
            kSourceStorageBytes,
            static_cast<uint32_t>(kMaximumProcesses),
            static_cast<uint32_t>(kProcessNameCharacters),
            0,
            0,
            _processCollections,
            _processHandleOpens,
            _processHistoryCommits,
            _processValueStride,
            _processBackend,
            kSourceArenaBytes,
            _arenaCommittedBytes,
            _arenaCommittedFamilies,
            this,
            _arenaCommitAttempts,
            static_cast<uint32_t>(sizeof(SystemDataSource)),
        };
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetNextProcessWalkForTest(const RedXeNativeWalkSample* sample,
                                                        uint64_t systemTotal100ns) noexcept override
    {
        if (!sample)
            return E_POINTER;
        if (sample->processCount > sample->processes.size() || sample->threadCount > sample->threads.size() ||
            sample->threadCount > kMaximumThreads || (sample->threadCount != 0 && !sample->threads.data()) ||
            sample->imageArenaUsed > sample->imageArena.size())
            return E_INVALIDARG;
        for (uint32_t index = 0; index < sample->processCount; ++index)
        {
            const auto& row = sample->processes[index];
            if (row.imageOffset >= sample->imageArenaUsed ||
                row.imageCharacters >= sample->imageArenaUsed - row.imageOffset ||
                sample->imageArena[row.imageOffset + row.imageCharacters] != L'\0')
                return E_INVALIDARG;
        }
        if (_collecting.test_and_set(std::memory_order_acquire))
            return HRESULT_FROM_WIN32(ERROR_BUSY);
        auto clearCollecting = wil::scope_exit([this] { _collecting.clear(std::memory_order_release); });
        uint64_t required = SourceFamilyBit(SourceFamily::Walk);
        if (sample->threadCount != 0)
            required |= SourceFamilyBit(SourceFamily::ThreadScratch);
        const HRESULT ready = PrepareStorageMask(required, 0);
        if (FAILED(ready))
            return ready;
        auto& walk = Storage<SourceFamily::Walk>()._walk;
        walk = *sample;
        if (sample->threadCount != 0)
        {
            auto& scratch = Storage<SourceFamily::ThreadScratch>().rows;
            std::copy_n(sample->threads.begin(), sample->threadCount, scratch.begin());
            walk.threads = scratch;
        }
        else
            walk.threads = {};

        _processTestSystemTotal = systemTotal100ns;
        _walkTestPending = true;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CollectSyntheticProcessSnapshot(uint32_t requestedRows,
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

        const HRESULT ready = PrepareStorageMask(
            SourceFamilyBit(SourceFamily::Process) | SourceFamilyBit(SourceFamily::ProcessValues), kProcessColumnCount);
        if (FAILED(ready))
            return ready;
        _processSystemTimesForTest = false;
        uint64_t ignoredIdle = 0;
        uint64_t systemTotal = 0;
        const bool hasSystemTimes = ReadSystemTimes(ignoredIdle, systemTotal);
        const uint64_t systemDelta = hasSystemTimes && _hasProcessSystemTotal && systemTotal > _processSystemTotal
                                         ? systemTotal - _processSystemTotal
                                         : 0;

        _needProcessDetails = true;
        _needProcessActivity = false;
        _processValueStride = kProcessColumnCount;
        _processBackend = kBackendWin32;
        ++_processCollections;
        PROCESSENTRY32W entry{};
        entry.dwSize = static_cast<DWORD>(sizeof(entry));
        entry.th32ProcessID = GetCurrentProcessId();
        entry.cntThreads = 1;
        constexpr wchar_t kSyntheticImageName[] = L"synthetic.exe";
        static_assert(std::size(kSyntheticImageName) <= std::size(entry.szExeFile));
        std::wmemcpy(entry.szExeFile, kSyntheticImageName, std::size(kSyntheticImageName));

        size_t rowCount = 0;
        size_t nameCharacters = 0;
        while (rowCount < requestedRows)
        {
            if (!AppendProcessRow(entry, systemDelta, rowCount, nameCharacters))
            {
                return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            }
        }
        FinishProcessCollection(rowCount, false, hasSystemTimes, systemTotal, ++_sequence, CurrentTimestamp(),
                                snapshot);
        return S_OK;
    }

  private:
    [[nodiscard]] void* ArenaAddress(size_t offset) noexcept
    {
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(this) + kSourceControlRegionBytes + offset);
    }
    [[nodiscard]] const void* ArenaAddress(size_t offset) const noexcept
    {
        return reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(this) + kSourceControlRegionBytes + offset);
    }
    [[nodiscard]] bool HasStorage(SourceFamily family) const noexcept
    {
        return (_arenaCommittedFamilies & SourceFamilyBit(family)) != 0;
    }
    template <SourceFamily Family> [[nodiscard]] typename SourceStorageType<Family>::Type& Storage() noexcept
    {
        return *std::launder(static_cast<typename SourceStorageType<Family>::Type*>(
            ArenaAddress(kSourceSlots[static_cast<size_t>(Family)].offset)));
    }
    template <SourceFamily Family>
    [[nodiscard]] const typename SourceStorageType<Family>::Type& Storage() const noexcept
    {
        return *std::launder(static_cast<const typename SourceStorageType<Family>::Type*>(
            ArenaAddress(kSourceSlots[static_cast<size_t>(Family)].offset)));
    }
    [[nodiscard]] std::span<RedXeDataValue> ProcessValues() noexcept
    {
        return {_processValuesBase, _processValueCapacity};
    }

    [[nodiscard]] HRESULT CommitStorage(SourceFamily family, uint32_t processColumns) noexcept
    {
        const auto& slot = kSourceSlots[static_cast<size_t>(family)];
        const bool values = family == SourceFamily::ProcessValues;
        const size_t valueCount = kMaximumProcesses * processColumns;
        if (HasStorage(family) && (!values || valueCount <= _processValueCapacity))
            return S_OK;
        ++_arenaCommitAttempts;
        if (_arenaFailureAfter != UINT32_MAX)
        {
            if (_arenaFailureAfter == 0)
            {
                _arenaFailureAfter = UINT32_MAX;
                return E_OUTOFMEMORY;
            }
            --_arenaFailureAfter;
        }
        const size_t bytes = values ? SourcePageRound(valueCount * sizeof(RedXeDataValue)) : slot.bytes;
        const size_t priorBytes = values ? SourcePageRound(_processValueCapacity * sizeof(RedXeDataValue)) : 0;
        auto* address = static_cast<std::byte*>(ArenaAddress(slot.offset));
        if (!VirtualAlloc(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(address) + priorBytes),
                          bytes - priorBytes, MEM_COMMIT, PAGE_READWRITE))
            return E_OUTOFMEMORY;
        if (values)
        {
            static_assert(std::is_trivially_default_constructible_v<RedXeDataValue>);
            // Non-allocating placement array new has no cookie. Default initialization preserves existing bits;
            // Windows supplies zero pages for the suffix. No uncommitted element's lifetime is started.
            _processValuesBase = ::new (address) RedXeDataValue[valueCount];
            _processValueCapacity = static_cast<uint32_t>(valueCount);
        }
        else
            slot.construct(address);
        _arenaCommittedFamilies |= SourceFamilyBit(family);
        _arenaCommittedBytes += bytes - priorBytes;
        gSourceCommittedBytes.fetch_add(bytes - priorBytes, std::memory_order_relaxed);
        if (family == SourceFamily::Gpu)
            gSourceGpuOwners.fetch_add(1, std::memory_order_relaxed);
        if (family == SourceFamily::Net)
            gSourceNetOwners.fetch_add(1, std::memory_order_relaxed);
        if (family == SourceFamily::Power)
            gSourcePowerOwners.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
    }

    [[nodiscard]] HRESULT PrepareStorage(const RedXeDataCollectRequest& request) noexcept
    {
        uint64_t required = 0;
        bool details = false;
        bool activity = false;
        for (uint32_t index = 0; index < request.dataSetCount; ++index)
        {
            const char* id = request.dataSetIds[index];
            details = details || RedXeAsciiEqualsIgnoreCase(id, kProcessDataSetId);
            activity = activity || RedXeAsciiEqualsIgnoreCase(id, kProcessActivityDataSetId);
            for (const auto& requirement : kSourceRequirements)
                if (RedXeAsciiEqualsIgnoreCase(id, requirement.dataSetId))
                {
                    required |= requirement.families;
                    break;
                }
        }
        if (details && activity)
            required |= SourceFamilyBit(SourceFamily::ActivityViews);
        return PrepareStorageMask(required, details ? kProcessColumnCount : kProcessActivityColumnCount);
    }

    [[nodiscard]] HRESULT PrepareStorageMask(uint64_t required, uint32_t processColumns) noexcept
    {
        for (uint32_t index = 0; index < static_cast<uint32_t>(SourceFamily::Count); ++index)
            if ((required & (uint64_t{1} << index)) != 0)
            {
                const HRESULT committed = CommitStorage(static_cast<SourceFamily>(index), processColumns);
                if (FAILED(committed))
                    return committed;
            }
        return S_OK;
    }

    struct ExternalRateHistory final
    {
        std::array<RedXeAcceleratorEngineSample, kRedXeMaximumGpuEngines> previousEngines{};
        uint32_t previousEngineCount = 0;
        uint64_t previousEngineTimestamp100ns = 0;
        bool pdhHasPriorCollect = false;
        std::array<RedXeNetworkInterfacePrevious, kRedXeMaximumNetworkInterfaces> previousInterfaces{};
        uint32_t previousInterfaceCount = 0;
        std::array<RedXeNetworkProtocolPrevious, kRedXeNetworkProtocolCount> previousProtocols{};
        std::array<std::int64_t, kRedXeMaximumStorageDisks> diskPreviousQpc{};
        std::array<bool, kRedXeMaximumStorageDisks> diskHasPrevious{};
    };

    [[nodiscard]] static bool DatasetCommitsInternalRateHistory(const char* dataSetId) noexcept
    {
        return RedXeAsciiEqualsIgnoreCase(dataSetId, kSummaryDataSetId) ||
               RedXeAsciiEqualsIgnoreCase(dataSetId, kCpuSummaryDataSetId) ||
               RedXeAsciiEqualsIgnoreCase(dataSetId, kCpuLogicalDataSetId) ||
               RedXeAsciiEqualsIgnoreCase(dataSetId, kMemoryDataSetId) ||
               RedXeAsciiEqualsIgnoreCase(dataSetId, kProcessDataSetId) ||
               RedXeAsciiEqualsIgnoreCase(dataSetId, kProcessActivityDataSetId) ||
               RedXeAsciiEqualsIgnoreCase(dataSetId, kThreadDataSetId);
    }

    void CaptureExternalRateHistory(ExternalRateHistory& history) const noexcept
    {
        if (HasStorage(SourceFamily::Gpu))
        {
            history.previousEngines = Storage<SourceFamily::Gpu>()._gpu.previousEngines;
            history.previousEngineCount = Storage<SourceFamily::Gpu>()._gpu.previousEngineCount;
            history.previousEngineTimestamp100ns = Storage<SourceFamily::Gpu>()._gpu.previousEngineTimestamp100ns;
            history.pdhHasPriorCollect = Storage<SourceFamily::Gpu>()._gpu.pdhHasPriorCollect;
        }
        if (HasStorage(SourceFamily::Net))
        {
            history.previousInterfaces = Storage<SourceFamily::Net>()._netStorage.previousInterfaces;
            history.previousInterfaceCount = Storage<SourceFamily::Net>()._netStorage.previousInterfaceCount;
            history.previousProtocols = Storage<SourceFamily::Net>()._netStorage.previousProtocols;
            for (uint32_t index = 0; index < kRedXeMaximumStorageDisks; ++index)
            {
                history.diskPreviousQpc[index] = Storage<SourceFamily::Net>()._netStorage.diskCache[index].previousQpc;
                history.diskHasPrevious[index] =
                    Storage<SourceFamily::Net>()._netStorage.diskCache[index].hasPreviousPerformance;
            }
        }
    }

    void RestoreExternalRateHistory(const ExternalRateHistory& history) noexcept
    {
        if (HasStorage(SourceFamily::Gpu))
        {
            Storage<SourceFamily::Gpu>()._gpu.previousEngines = history.previousEngines;
            Storage<SourceFamily::Gpu>()._gpu.previousEngineCount = history.previousEngineCount;
            Storage<SourceFamily::Gpu>()._gpu.previousEngineTimestamp100ns = history.previousEngineTimestamp100ns;
            Storage<SourceFamily::Gpu>()._gpu.pdhHasPriorCollect = history.pdhHasPriorCollect;
        }
        if (HasStorage(SourceFamily::Net))
        {
            Storage<SourceFamily::Net>()._netStorage.previousInterfaces = history.previousInterfaces;
            Storage<SourceFamily::Net>()._netStorage.previousInterfaceCount = history.previousInterfaceCount;
            Storage<SourceFamily::Net>()._netStorage.previousProtocols = history.previousProtocols;
            for (uint32_t index = 0; index < kRedXeMaximumStorageDisks; ++index)
            {
                Storage<SourceFamily::Net>()._netStorage.diskCache[index].previousQpc = history.diskPreviousQpc[index];
                Storage<SourceFamily::Net>()._netStorage.diskCache[index].hasPreviousPerformance =
                    history.diskHasPrevious[index];
            }
        }
    }

    void DiscardInternalRateHistory() noexcept
    {
        _hasSummaryTimes = false;
        _hasProcessSystemTotal = false;
        _hasCpuSummaryTimes = false;
        _hasCpuSummaryTimestamp = false;
        _hasCpuSummaryContextSwitches = false;
        _hasCpuLogicalTimestamp = false;
        _hasPreviousLogical = false;
        _previousLogicalCount = 0;
        _hasMemoryTimestamp = false;
        _hasMemoryCounters = false;
        _hasThreadTimestamp = false;
        _hasThreadSystemTotal = false;
        if (HasStorage(SourceFamily::Thread))
            Storage<SourceFamily::Thread>()._threadHistory.fill({});
        if (HasStorage(SourceFamily::Process))
            Storage<SourceFamily::Process>()._processHistory.fill({});
    }

    void PrepareBatchSamples(const RedXeDataCollectRequest& request) noexcept
    {
        _cheapReady = false;
        _walkReady = false;
        _processReady = false;
        _needProcessDetails = false;
        _needProcessActivity = false;
        _processSystemTimesForTest = false;
        _gpuAdaptersReady = false;
        _gpuEnginesReady = false;
        _gpuProcessesReady = false;
        _disksReady = false;
        _diskTempsReady = false;
        _powerReady = false;
        _batteriesReady = false;
        _acpiReady = false;
        _fanPresenceReady = false;
        bool needCheap = false;
        bool needWalk = false;
        bool needThreads = false;
        bool needGpuAdapters = false;
        bool needGpuEngines = false;
        bool needGpuProcesses = false;
        bool needDisks = false;
        bool needDiskTemps = false;
        bool needPower = false;
        bool needBatteries = false;
        bool needAcpi = false;
        bool needFanPresence = false;
        for (uint32_t index = 0; index < request.dataSetCount; ++index)
        {
            const char* id = request.dataSetIds[index];
            needThreads = needThreads || RedXeAsciiEqualsIgnoreCase(id, kThreadDataSetId);
            if (RedXeAsciiEqualsIgnoreCase(id, kSummaryDataSetId) ||
                RedXeAsciiEqualsIgnoreCase(id, kCpuSummaryDataSetId) ||
                RedXeAsciiEqualsIgnoreCase(id, kCpuLogicalDataSetId) ||
                RedXeAsciiEqualsIgnoreCase(id, kMemoryDataSetId))
            {
                needCheap = true;
            }
            if (RedXeAsciiEqualsIgnoreCase(id, kProcessDataSetId))
                _needProcessDetails = true;
            if (RedXeAsciiEqualsIgnoreCase(id, kProcessActivityDataSetId))
                _needProcessActivity = true;
            if (_needProcessDetails || _needProcessActivity || RedXeAsciiEqualsIgnoreCase(id, kThreadDataSetId))
                needWalk = true;
            if (RedXeAsciiEqualsIgnoreCase(id, kGpuAdapterDataSetId) ||
                RedXeAsciiEqualsIgnoreCase(id, kThermalSensorDataSetId) ||
                RedXeAsciiEqualsIgnoreCase(id, kFanSensorDataSetId) ||
                RedXeAsciiEqualsIgnoreCase(id, kNpuAdapterDataSetId) ||
                RedXeAsciiEqualsIgnoreCase(id, kNpuEngineDataSetId) ||
                RedXeAsciiEqualsIgnoreCase(id, kNpuProcessDataSetId))
            {
                needGpuAdapters = true;
            }
            // The graphics and accelerator datasets read the same D3DKMT node walk and the same counter query, so a
            // batch that asks for both pays for one sample, not two.
            if (RedXeAsciiEqualsIgnoreCase(id, kGpuEngineDataSetId) ||
                RedXeAsciiEqualsIgnoreCase(id, kNpuEngineDataSetId))
            {
                needGpuEngines = true;
            }
            if (RedXeAsciiEqualsIgnoreCase(id, kGpuProcessDataSetId) ||
                RedXeAsciiEqualsIgnoreCase(id, kNpuProcessDataSetId))
            {
                needGpuProcesses = true;
            }
            if (RedXeAsciiEqualsIgnoreCase(id, kStorageDiskDataSetId) ||
                RedXeAsciiEqualsIgnoreCase(id, kThermalSensorDataSetId))
            {
                needDisks = true;
            }
            if (RedXeAsciiEqualsIgnoreCase(id, kThermalSensorDataSetId))
            {
                needDiskTemps = true;
                needBatteries = true;
                needAcpi = true;
            }
            if (RedXeAsciiEqualsIgnoreCase(id, kPowerSummaryDataSetId))
            {
                needPower = true;
            }
            if (RedXeAsciiEqualsIgnoreCase(id, kBatteryListDataSetId))
            {
                needBatteries = true;
            }
            if (RedXeAsciiEqualsIgnoreCase(id, kFanSensorDataSetId))
            {
                needFanPresence = true;
            }
        }
        if (needCheap)
        {
            _cheapReady = RedXeNativeQueryCheap(Storage<SourceFamily::Cheap>()._nativeState,
                                                Storage<SourceFamily::Cheap>()._cheap);
        }
        if (needWalk)
        {
            auto& walk = Storage<SourceFamily::Walk>()._walk;
            walk.threads = needThreads ? std::span<RedXeNativeThreadRow>(Storage<SourceFamily::ThreadScratch>().rows)
                                       : std::span<RedXeNativeThreadRow>{};
            if (!needThreads)
                walk.threadCount = 0;
            if (_walkTestPending)
            {
                _walkReady = Storage<SourceFamily::Walk>()._walk.valid;
                _walkTestPending = false;
                _processSystemTimesForTest = true;
            }
            else
                _walkReady = RedXeNativeQueryWalk(Storage<SourceFamily::Walk>().buffer, walk);
        }
        if (needGpuAdapters)
        {
            RedXeGpuSampleAdapters(Storage<SourceFamily::Gpu>()._gpu);
            _gpuAdaptersReady = true;
        }
        if (needGpuEngines)
        {
            RedXeGpuSampleEngines(Storage<SourceFamily::Gpu>()._gpu);
            _gpuEnginesReady = true;
        }
        if (needGpuProcesses)
        {
            RedXeGpuSampleProcesses(Storage<SourceFamily::Gpu>()._gpu);
            _gpuProcessesReady = true;
        }
        if (needDisks)
        {
            RedXeNetStorageSampleDisks(Storage<SourceFamily::Net>()._netStorage);
            _disksReady = true;
        }
        if (needDiskTemps)
        {
            RedXeNetStorageSampleDiskTemperatures(Storage<SourceFamily::Net>()._netStorage);
            _diskTempsReady = true;
        }
        if (needPower)
        {
            RedXePowerSampleSummary(Storage<SourceFamily::Power>()._power);
            _powerReady = true;
        }
        if (needBatteries)
        {
            RedXePowerSampleBatteries(Storage<SourceFamily::Power>()._power);
            _batteriesReady = true;
        }
        if (needAcpi)
        {
            RedXePowerSampleAcpiThermals(Storage<SourceFamily::Power>()._power);
            _acpiReady = true;
        }
        if (needFanPresence)
        {
            RedXePowerSampleFanPresence(Storage<SourceFamily::Power>()._power);
            _fanPresenceReady = true;
        }
    }

    [[nodiscard]] HRESULT CollectOne(const char* dataSetId, uint64_t timestamp, uint64_t sequence,
                                     const RedXeDataSnapshot** output) noexcept
    {
        *output = nullptr;
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kStatusDataSetId))
        {
            return CollectStatus(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kSummaryDataSetId))
        {
            return CollectSummary(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kCpuSummaryDataSetId))
        {
            return CollectCpuSummary(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kCpuLogicalDataSetId))
        {
            return CollectCpuLogical(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kMemoryDataSetId))
        {
            return CollectMemory(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kProcessDataSetId) ||
            RedXeAsciiEqualsIgnoreCase(dataSetId, kProcessActivityDataSetId))
        {
            return CollectProcesses(dataSetId, timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kThreadDataSetId))
        {
            return CollectThreads(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kNetworkInterfaceDataSetId))
        {
            return CollectNetworkInterfaces(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kNetworkProtocolDataSetId))
        {
            return CollectNetworkProtocols(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kStorageDiskDataSetId))
        {
            return CollectStorageDisks(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kStorageVolumeDataSetId))
        {
            return CollectStorageVolumes(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kGpuAdapterDataSetId))
        {
            return CollectGpuAdapters(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kGpuEngineDataSetId))
        {
            return CollectGpuEngines(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kGpuProcessDataSetId))
        {
            return CollectGpuProcesses(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kPowerSummaryDataSetId))
        {
            return CollectPowerSummary(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kBatteryListDataSetId))
        {
            return CollectBatteries(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kThermalSensorDataSetId))
        {
            return CollectThermals(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kNpuAdapterDataSetId))
        {
            return CollectNpuAdapters(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kNpuEngineDataSetId))
        {
            return CollectNpuEngines(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kNpuProcessDataSetId))
        {
            return CollectNpuProcesses(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kSecurityDataSetId))
        {
            return CollectSecurityPosture(timestamp, sequence, output);
        }
        if (RedXeAsciiEqualsIgnoreCase(dataSetId, kFanSensorDataSetId))
        {
            return CollectFans(timestamp, sequence, output);
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    void RecordStatus(const char* dataSetId, uint64_t lastResult, uint64_t durationMicroseconds,
                      uint64_t timestamp) noexcept
    {
        const size_t index = FindDataSetIndex(dataSetId);
        if (index >= _status.size())
        {
            return;
        }
        DatasetRuntimeStatus& status = _status[index];
        status.lastResult = lastResult;
        status.lastDurationMicroseconds = durationMicroseconds;
        if (lastResult == kLastResultFailed)
        {
            ++status.failureCount;
        }
        else
        {
            status.lastSuccessFileTime100ns = timestamp;
        }
        if (lastResult == kLastResultTruncated)
        {
            ++status.truncationCount;
        }
    }

    [[nodiscard]] HRESULT CollectStatus(uint64_t timestamp, uint64_t sequence,
                                        const RedXeDataSnapshot** output) noexcept
    {
        const uint32_t rowCount = static_cast<uint32_t>(kDataSets.size());
        for (uint32_t row = 0; row < rowCount; ++row)
        {
            const DatasetBackendInfo& backend = kDatasetBackends[row];
            const DatasetRuntimeStatus& status = _status[row];
            RedXeDataValue* values =
                Storage<SourceFamily::Status>()._statusValues.data() + (static_cast<size_t>(row) * kStatusColumnCount);
            values[0] = Utf16Value(backend.dataSetIdWide, Utf16Length(backend.dataSetIdWide), RedXeDataQualityGood);
            values[1] = UInt64Value(kAvailabilityAvailable, RedXeDataQualityGood);
            values[2] = UInt64Value(backend.backendTier, RedXeDataQualityGood);
            values[3] = Utf16Value(backend.backendName, Utf16Length(backend.backendName), RedXeDataQualityGood);
            values[4] = Utf16Value(backend.backendVersion, Utf16Length(backend.backendVersion), RedXeDataQualityGood);
            values[5] = UInt64Value(0, RedXeDataQualityGood);
            values[6] = UInt64Value(status.lastResult, RedXeDataQualityGood);
            values[7] =
                UInt64Value(status.lastSuccessFileTime100ns,
                            status.lastSuccessFileTime100ns != 0 ? RedXeDataQualityGood : RedXeDataQualityInitializing);
            values[8] = UInt64Value(status.lastDurationMicroseconds, RedXeDataQualityGood);
            values[9] = UInt64Value(status.failureCount, RedXeDataQualityGood);
            values[10] = UInt64Value(status.truncationCount, RedXeDataQualityGood);
            values[11] = UInt64Value(status.retryFileTime100ns, RedXeDataQualityGood);
            Storage<SourceFamily::Status>()._statusRows[row] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kStatusColumnCount};
        }
        _statusSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kStatusDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::Status>()._statusRows.data(),
            rowCount,
            kStatusColumnCount,
        };
        *output = &_statusSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectSummary(uint64_t timestamp, uint64_t sequence,
                                         const RedXeDataSnapshot** output) noexcept
    {
        PERFORMANCE_INFORMATION performance{};
        performance.cb = static_cast<DWORD>(sizeof(performance));
        const bool hasPerformance =
            K32GetPerformanceInfo(&performance, static_cast<DWORD>(sizeof(performance))) != FALSE;

        uint64_t idle = 0;
        uint64_t total = 0;
        const bool hasTimes = ReadSystemTimes(idle, total);
        RedXeDataQuality cpuQuality = RedXeDataQualityUnavailable;
        double cpuPercent = 0.0;
        if (hasTimes)
        {
            if (_hasSummaryTimes && total > _summaryTotal && idle >= _summaryIdle)
            {
                const uint64_t totalDelta = total - _summaryTotal;
                const uint64_t idleDelta = idle - _summaryIdle;
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
        uint64_t totalPhysicalBytes = 0;
        uint64_t availablePhysicalBytes = 0;
        uint64_t committedBytes = 0;
        const bool totalPhysicalValid =
            hasPerformance && PageCountToBytes(performance.PhysicalTotal, performance.PageSize, totalPhysicalBytes);
        const bool availablePhysicalValid =
            hasPerformance &&
            PageCountToBytes(performance.PhysicalAvailable, performance.PageSize, availablePhysicalBytes);
        const bool committedValid =
            hasPerformance && PageCountToBytes(performance.CommitTotal, performance.PageSize, committedBytes);

        const bool hasCounts =
            (_cheapReady && Storage<SourceFamily::Cheap>()._cheap.handleCountsValid) || hasPerformance;
        const uint32_t processCount = _cheapReady && Storage<SourceFamily::Cheap>()._cheap.handleCountsValid
                                          ? Storage<SourceFamily::Cheap>()._cheap.processCount
                                          : (hasPerformance ? performance.ProcessCount : 0);
        const uint32_t threadCount = _cheapReady && Storage<SourceFamily::Cheap>()._cheap.handleCountsValid
                                         ? Storage<SourceFamily::Cheap>()._cheap.threadCount
                                         : (hasPerformance ? performance.ThreadCount : 0);
        const uint32_t handleCount = _cheapReady && Storage<SourceFamily::Cheap>()._cheap.handleCountsValid
                                         ? Storage<SourceFamily::Cheap>()._cheap.handleCount
                                         : (hasPerformance ? performance.HandleCount : 0);
        Storage<SourceFamily::Summary>()._summaryValues[0] = Float64Value(cpuPercent, cpuQuality);
        Storage<SourceFamily::Summary>()._summaryValues[1] =
            UInt64Value(processorCount, processorCount == 0 ? RedXeDataQualityUnavailable : RedXeDataQualityGood);
        Storage<SourceFamily::Summary>()._summaryValues[2] =
            UInt64Value(processCount, hasCounts ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Summary>()._summaryValues[3] =
            UInt64Value(threadCount, hasCounts ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Summary>()._summaryValues[4] =
            UInt64Value(handleCount, hasCounts ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Summary>()._summaryValues[5] =
            UInt64Value(totalPhysicalBytes, totalPhysicalValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Summary>()._summaryValues[6] = UInt64Value(
            availablePhysicalBytes, availablePhysicalValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Summary>()._summaryValues[7] = UInt64Value(
            totalPhysicalValid && availablePhysicalValid && totalPhysicalBytes >= availablePhysicalBytes
                ? totalPhysicalBytes - availablePhysicalBytes
                : 0,
            totalPhysicalValid && availablePhysicalValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Summary>()._summaryValues[8] =
            UInt64Value(committedBytes, committedValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Summary>()._summaryValues[9] = UInt64Value(GetTickCount64(), RedXeDataQualityGood);
        const RedXeNativeTimeOfDay& timeOfDay = Storage<SourceFamily::Cheap>()._cheap.timeOfDay;
        const bool hasTimeOfDay = _cheapReady && timeOfDay.valid;
        Storage<SourceFamily::Summary>()._summaryValues[10] =
            UInt64Value(timeOfDay.bootTime, hasTimeOfDay ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Summary>()._summaryValues[11] = UInt64Value(
            timeOfDay.sleepTimeBias / 10000ULL, hasTimeOfDay ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Summary>()._summaryRow = RedXeDataRow{
            sizeof(RedXeDataRow), Storage<SourceFamily::Summary>()._summaryValues.data(), kSummaryColumnCount};
        _summarySnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kSummaryDataSetId,
            sequence,
            timestamp,
            &Storage<SourceFamily::Summary>()._summaryRow,
            1,
            kSummaryColumnCount,
        };
        *output = &_summarySnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectCpuSummary(uint64_t timestamp, uint64_t sequence,
                                            const RedXeDataSnapshot** output) noexcept
    {
        double totalPercent = 0.0;
        double userPercent = 0.0;
        double kernelPercent = 0.0;
        double idlePercent = 0.0;
        double dpcPercent = 0.0;
        double interruptPercent = 0.0;
        double interruptRate = 0.0;
        RedXeDataQuality rateQuality = RedXeDataQualityUnavailable;
        RedXeDataQuality dpcQuality = RedXeDataQualityUnavailable;
        uint64_t elapsed100ns = 0;
        if (_hasCpuSummaryTimestamp && timestamp > _cpuSummaryTimestamp)
        {
            elapsed100ns = timestamp - _cpuSummaryTimestamp;
        }
        if (_cheapReady && Storage<SourceFamily::Cheap>()._cheap.processorTimesValid &&
            Storage<SourceFamily::Cheap>()._cheap.logicalCount != 0)
        {
            uint64_t idle = 0;
            uint64_t kernel = 0;
            uint64_t user = 0;
            uint64_t dpc = 0;
            uint64_t interrupt = 0;
            uint64_t interruptCount = 0;
            for (uint32_t index = 0; index < Storage<SourceFamily::Cheap>()._cheap.logicalCount; ++index)
            {
                idle += Storage<SourceFamily::Cheap>()._cheap.logical[index].idleTime;
                kernel += Storage<SourceFamily::Cheap>()._cheap.logical[index].kernelTime;
                user += Storage<SourceFamily::Cheap>()._cheap.logical[index].userTime;
                dpc += Storage<SourceFamily::Cheap>()._cheap.logical[index].dpcTime;
                interrupt += Storage<SourceFamily::Cheap>()._cheap.logical[index].interruptTime;
                interruptCount += Storage<SourceFamily::Cheap>()._cheap.logical[index].interruptCount;
            }
            if (_hasCpuSummaryTimes && kernel >= _cpuSummaryKernel && user >= _cpuSummaryUser &&
                idle >= _cpuSummaryIdle)
            {
                const uint64_t kernelDelta = kernel - _cpuSummaryKernel;
                const uint64_t userDelta = user - _cpuSummaryUser;
                const uint64_t idleDelta = idle - _cpuSummaryIdle;
                const uint64_t totalDelta = kernelDelta + userDelta;
                if (totalDelta != 0 && kernelDelta >= idleDelta)
                {
                    idlePercent = BoundedPercent(idleDelta, totalDelta);
                    userPercent = BoundedPercent(userDelta, totalDelta);
                    kernelPercent = BoundedPercent(kernelDelta - idleDelta, totalDelta);
                    totalPercent = BoundedPercent(totalDelta - idleDelta, totalDelta);
                    rateQuality = RedXeDataQualityGood;
                    if (dpc >= _cpuSummaryDpc && interrupt >= _cpuSummaryInterrupt)
                    {
                        dpcPercent = BoundedPercent(dpc - _cpuSummaryDpc, totalDelta);
                        interruptPercent = BoundedPercent(interrupt - _cpuSummaryInterrupt, totalDelta);
                        dpcQuality = RedXeDataQualityGood;
                    }
                }
                if (elapsed100ns != 0 && interruptCount >= _cpuSummaryInterruptCount)
                {
                    interruptRate = PerSecond(interruptCount - _cpuSummaryInterruptCount, elapsed100ns);
                }
            }
            else
            {
                rateQuality = RedXeDataQualityInitializing;
                dpcQuality = RedXeDataQualityInitializing;
            }
            _cpuSummaryIdle = idle;
            _cpuSummaryKernel = kernel;
            _cpuSummaryUser = user;
            _cpuSummaryDpc = dpc;
            _cpuSummaryInterrupt = interrupt;
            _cpuSummaryInterruptCount = interruptCount;
            _hasCpuSummaryTimes = true;
        }

        // Aggregate frequency across logical processors: current is the mean of reporting processors, maximum is the
        // fastest specified clock so a hybrid part reports its performance-core ceiling rather than an average.
        uint64_t frequencySum = 0;
        uint32_t frequencyCount = 0;
        uint32_t maximumMhz = 0;
        if (_cheapReady)
        {
            for (uint32_t index = 0; index < Storage<SourceFamily::Cheap>()._cheap.logicalCount; ++index)
            {
                const RedXeNativeLogicalCpu& cpu = Storage<SourceFamily::Cheap>()._cheap.logical[index];
                if (!cpu.hasFrequency)
                {
                    continue;
                }
                frequencySum += cpu.currentMhz;
                ++frequencyCount;
                maximumMhz = (std::max)(maximumMhz, cpu.maxMhz);
            }
        }
        const RedXeDataQuality frequencyQuality =
            frequencyCount != 0 ? RedXeDataQualityGood : RedXeDataQualityUnavailable;

        double contextSwitchRate = 0.0;
        RedXeDataQuality contextSwitchQuality = RedXeDataQualityUnavailable;
        if (_cheapReady && Storage<SourceFamily::Cheap>()._cheap.performance.valid)
        {
            const uint32_t switches = Storage<SourceFamily::Cheap>()._cheap.performance.contextSwitches;
            if (_hasCpuSummaryContextSwitches && elapsed100ns != 0)
            {
                contextSwitchRate = PerSecond(Delta32(switches, _cpuSummaryContextSwitches), elapsed100ns);
                contextSwitchQuality = RedXeDataQualityGood;
            }
            else
            {
                contextSwitchQuality = RedXeDataQualityInitializing;
            }
            _cpuSummaryContextSwitches = switches;
            _hasCpuSummaryContextSwitches = true;
        }
        _cpuSummaryTimestamp = timestamp;
        _hasCpuSummaryTimestamp = true;

        const DWORD logical = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[0] = Float64Value(totalPercent, rateQuality);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[1] = Float64Value(userPercent, rateQuality);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[2] = Float64Value(kernelPercent, rateQuality);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[3] = Float64Value(idlePercent, rateQuality);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[4] = Float64Value(dpcPercent, dpcQuality);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[5] = Float64Value(interruptPercent, dpcQuality);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[6] =
            UInt64Value(logical, logical == 0 ? RedXeDataQualityUnavailable : RedXeDataQualityGood);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[7] = UInt64Value(
            Storage<SourceFamily::Cheap>()._cheap.coreCount,
            _cheapReady && Storage<SourceFamily::Cheap>()._cheap.coreCount != 0 ? RedXeDataQualityGood
                                                                                : RedXeDataQualityUnavailable);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[8] = UInt64Value(
            Storage<SourceFamily::Cheap>()._cheap.packageCount,
            _cheapReady && Storage<SourceFamily::Cheap>()._cheap.packageCount != 0 ? RedXeDataQualityGood
                                                                                   : RedXeDataQualityUnavailable);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[9] = UInt64Value(
            Storage<SourceFamily::Cheap>()._cheap.numaNodeCount,
            _cheapReady && Storage<SourceFamily::Cheap>()._cheap.numaNodeCount != 0 ? RedXeDataQualityGood
                                                                                    : RedXeDataQualityUnavailable);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[10] =
            UInt64Value(frequencyCount != 0 ? frequencySum / frequencyCount : 0, frequencyQuality);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[11] = UInt64Value(maximumMhz, frequencyQuality);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[12] =
            Float64Value(contextSwitchRate, contextSwitchQuality);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryValues[13] = Float64Value(interruptRate, rateQuality);
        Storage<SourceFamily::CpuSummary>()._cpuSummaryRow = RedXeDataRow{
            sizeof(RedXeDataRow), Storage<SourceFamily::CpuSummary>()._cpuSummaryValues.data(), kCpuSummaryColumnCount};
        _cpuSummarySnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kCpuSummaryDataSetId,
            sequence,
            timestamp,
            &Storage<SourceFamily::CpuSummary>()._cpuSummaryRow,
            1,
            kCpuSummaryColumnCount,
        };
        *output = &_cpuSummarySnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectCpuLogical(uint64_t timestamp, uint64_t sequence,
                                            const RedXeDataSnapshot** output) noexcept
    {
        const uint32_t count = _cheapReady ? Storage<SourceFamily::Cheap>()._cheap.logicalCount : 0;
        const bool countChanged = count != _previousLogicalCount;
        uint64_t elapsed100ns = 0;
        if (_hasCpuLogicalTimestamp && timestamp > _cpuLogicalTimestamp)
        {
            elapsed100ns = timestamp - _cpuLogicalTimestamp;
        }
        for (uint32_t index = 0; index < count; ++index)
        {
            const RedXeNativeLogicalCpu& cpu = Storage<SourceFamily::Cheap>()._cheap.logical[index];
            RedXeDataValue* values = Storage<SourceFamily::Logical>()._cpuLogicalValues.data() +
                                     (static_cast<size_t>(index) * kCpuLogicalColumnCount);
            RedXeDataQuality rateQuality = RedXeDataQualityUnavailable;
            RedXeDataQuality dpcQuality = RedXeDataQualityUnavailable;
            double totalPercent = 0.0;
            double userPercent = 0.0;
            double kernelPercent = 0.0;
            double idlePercent = 0.0;
            double dpcPercent = 0.0;
            double interruptPercent = 0.0;
            double interruptRate = 0.0;
            const bool haveHistory = !countChanged && index < _previousLogicalCount && _hasPreviousLogical;
            if (cpu.hasTimes && haveHistory)
            {
                const LogicalCpuTimes& previous = Storage<SourceFamily::Logical>()._previousLogical[index];
                if (cpu.kernelTime >= previous.kernel && cpu.userTime >= previous.user && cpu.idleTime >= previous.idle)
                {
                    const uint64_t kernelDelta = cpu.kernelTime - previous.kernel;
                    const uint64_t userDelta = cpu.userTime - previous.user;
                    const uint64_t idleDelta = cpu.idleTime - previous.idle;
                    const uint64_t totalDelta = kernelDelta + userDelta;
                    if (totalDelta != 0 && kernelDelta >= idleDelta)
                    {
                        idlePercent = BoundedPercent(idleDelta, totalDelta);
                        userPercent = BoundedPercent(userDelta, totalDelta);
                        kernelPercent = BoundedPercent(kernelDelta - idleDelta, totalDelta);
                        totalPercent = BoundedPercent(totalDelta - idleDelta, totalDelta);
                        rateQuality = RedXeDataQualityGood;
                        if (cpu.dpcTime >= previous.dpc && cpu.interruptTime >= previous.interrupt)
                        {
                            dpcPercent = BoundedPercent(cpu.dpcTime - previous.dpc, totalDelta);
                            interruptPercent = BoundedPercent(cpu.interruptTime - previous.interrupt, totalDelta);
                            dpcQuality = RedXeDataQualityGood;
                        }
                    }
                }
                if (elapsed100ns != 0)
                {
                    interruptRate = PerSecond(Delta32(cpu.interruptCount, previous.interruptCount), elapsed100ns);
                }
            }
            else if (cpu.hasTimes)
            {
                rateQuality = RedXeDataQualityInitializing;
                dpcQuality = RedXeDataQualityInitializing;
            }
            const RedXeDataQuality topologyQuality =
                cpu.hasTopology ? RedXeDataQualityGood : RedXeDataQualityUnavailable;
            const RedXeDataQuality cpuSetQuality = cpu.hasCpuSet ? RedXeDataQualityGood : RedXeDataQualityUnavailable;
            const RedXeDataQuality frequencyQuality =
                cpu.hasFrequency ? RedXeDataQualityGood : RedXeDataQualityUnavailable;
            values[0] = UInt64Value(cpu.group, RedXeDataQualityGood);
            values[1] = UInt64Value(cpu.index, RedXeDataQualityGood);
            values[2] = UInt64Value(cpu.cpuSetId, cpuSetQuality);
            values[3] = UInt64Value(cpu.coreIndex, topologyQuality);
            values[4] = UInt64Value(cpu.packageIndex, topologyQuality);
            values[5] = UInt64Value(cpu.numaNode, topologyQuality);
            values[6] = UInt64Value(cpu.efficiencyClass, cpuSetQuality);
            values[7] = UInt64Value(cpu.parked ? 1 : 0, cpuSetQuality);
            values[8] = UInt64Value(cpu.allocated ? 1 : 0, cpuSetQuality);
            values[9] = Float64Value(totalPercent, rateQuality);
            values[10] = Float64Value(userPercent, rateQuality);
            values[11] = Float64Value(kernelPercent, rateQuality);
            values[12] = Float64Value(idlePercent, rateQuality);
            values[13] = Float64Value(dpcPercent, dpcQuality);
            values[14] = Float64Value(interruptPercent, dpcQuality);
            values[15] =
                UInt64Value(cpu.interruptCount, cpu.hasTimes ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[16] = Float64Value(interruptRate, rateQuality);
            values[17] = UInt64Value(cpu.currentMhz, frequencyQuality);
            values[18] = UInt64Value(cpu.maxMhz, frequencyQuality);
            Storage<SourceFamily::Logical>()._cpuLogicalRows[index] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kCpuLogicalColumnCount};
            Storage<SourceFamily::Logical>()._previousLogical[index] = LogicalCpuTimes{
                cpu.idleTime, cpu.kernelTime, cpu.userTime, cpu.dpcTime, cpu.interruptTime, cpu.interruptCount};
        }
        _previousLogicalCount = count;
        _hasPreviousLogical = count != 0;
        _cpuLogicalTimestamp = timestamp;
        _hasCpuLogicalTimestamp = true;
        _cpuLogicalSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kCpuLogicalDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::Logical>()._cpuLogicalRows.data(),
            count,
            kCpuLogicalColumnCount,
        };
        *output = &_cpuLogicalSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectMemory(uint64_t timestamp, uint64_t sequence,
                                        const RedXeDataSnapshot** output) noexcept
    {
        PERFORMANCE_INFORMATION performance{};
        performance.cb = static_cast<DWORD>(sizeof(performance));
        const bool hasPerformance =
            K32GetPerformanceInfo(&performance, static_cast<DWORD>(sizeof(performance))) != FALSE;
        uint64_t totalPhysical = 0;
        uint64_t availablePhysical = 0;
        uint64_t commit = 0;
        uint64_t commitLimit = 0;
        uint64_t commitPeak = 0;
        uint64_t paged = 0;
        uint64_t nonPaged = 0;
        const bool totalValid =
            hasPerformance && PageCountToBytes(performance.PhysicalTotal, performance.PageSize, totalPhysical);
        const bool availableValid =
            hasPerformance && PageCountToBytes(performance.PhysicalAvailable, performance.PageSize, availablePhysical);
        const bool commitValid =
            hasPerformance && PageCountToBytes(performance.CommitTotal, performance.PageSize, commit);
        const bool limitValid =
            hasPerformance && PageCountToBytes(performance.CommitLimit, performance.PageSize, commitLimit);
        const bool peakValid =
            hasPerformance && PageCountToBytes(performance.CommitPeak, performance.PageSize, commitPeak);
        const bool pagedValid =
            hasPerformance && PageCountToBytes(performance.KernelPaged, performance.PageSize, paged);
        const bool nonPagedValid =
            hasPerformance && PageCountToBytes(performance.KernelNonpaged, performance.PageSize, nonPaged);
        Storage<SourceFamily::Memory>()._memoryValues[0] =
            UInt64Value(totalPhysical, totalValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Memory>()._memoryValues[1] =
            UInt64Value(availablePhysical, availableValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Memory>()._memoryValues[2] = UInt64Value(
            totalValid && availableValid && totalPhysical >= availablePhysical ? totalPhysical - availablePhysical : 0,
            totalValid && availableValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Memory>()._memoryValues[3] =
            UInt64Value(commit, commitValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Memory>()._memoryValues[4] =
            UInt64Value(commitLimit, limitValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Memory>()._memoryValues[5] =
            UInt64Value(commitPeak, peakValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        // Cache size and the fault rates come from the named SystemPerformanceInformation record; the page size is
        // still taken from K32GetPerformanceInfo, which is the documented oracle for the counts above.
        const RedXeNativeSystemPerformance& native = Storage<SourceFamily::Cheap>()._cheap.performance;
        const bool hasNative = _cheapReady && native.valid;
        uint64_t cacheBytes = 0;
        const bool cacheValid = hasNative && hasPerformance &&
                                PageCountToBytes(native.residentSystemCachePage, performance.PageSize, cacheBytes);
        double pageFaultRate = 0.0;
        double pageInRate = 0.0;
        double pageOutRate = 0.0;
        RedXeDataQuality faultQuality = RedXeDataQualityUnavailable;
        if (hasNative)
        {
            uint64_t elapsed100ns = 0;
            if (_hasMemoryTimestamp && timestamp > _memoryTimestamp)
            {
                elapsed100ns = timestamp - _memoryTimestamp;
            }
            if (_hasMemoryCounters && elapsed100ns != 0)
            {
                pageFaultRate = PerSecond(Delta32(native.pageFaultCount, _memoryPageFaults), elapsed100ns);
                pageInRate = PerSecond(Delta32(native.pageReadCount, _memoryPageReads), elapsed100ns);
                pageOutRate = PerSecond(Delta32(native.dirtyPagesWriteCount, _memoryDirtyWrites) +
                                            Delta32(native.mappedPagesWriteCount, _memoryMappedWrites),
                                        elapsed100ns);
                faultQuality = RedXeDataQualityGood;
            }
            else
            {
                faultQuality = RedXeDataQualityInitializing;
            }
            _memoryPageFaults = native.pageFaultCount;
            _memoryPageReads = native.pageReadCount;
            _memoryDirtyWrites = native.dirtyPagesWriteCount;
            _memoryMappedWrites = native.mappedPagesWriteCount;
            _hasMemoryCounters = true;
            _memoryTimestamp = timestamp;
            _hasMemoryTimestamp = true;
        }
        Storage<SourceFamily::Memory>()._memoryValues[6] =
            UInt64Value(cacheBytes, cacheValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Memory>()._memoryValues[7] =
            UInt64Value(paged, pagedValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Memory>()._memoryValues[8] =
            UInt64Value(nonPaged, nonPagedValid ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Memory>()._memoryValues[9] = Float64Value(pageFaultRate, faultQuality);
        Storage<SourceFamily::Memory>()._memoryValues[10] = Float64Value(pageInRate, faultQuality);
        Storage<SourceFamily::Memory>()._memoryValues[11] = Float64Value(pageOutRate, faultQuality);
        Storage<SourceFamily::Memory>()._memoryRow = RedXeDataRow{
            sizeof(RedXeDataRow), Storage<SourceFamily::Memory>()._memoryValues.data(), kMemoryColumnCount};
        _memorySnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kMemoryDataSetId,
            sequence,
            timestamp,
            &Storage<SourceFamily::Memory>()._memoryRow,
            1,
            kMemoryColumnCount,
        };
        *output = &_memorySnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectThreads(uint64_t timestamp, uint64_t sequence,
                                         const RedXeDataSnapshot** output) noexcept
    {
        const uint32_t count = _walkReady && Storage<SourceFamily::Walk>()._walk.valid
                                   ? Storage<SourceFamily::Walk>()._walk.threadCount
                                   : 0;
        uint64_t elapsed100ns = 0;
        if (_hasThreadTimestamp && timestamp > _threadTimestamp)
        {
            elapsed100ns = timestamp - _threadTimestamp;
        }
        // One system-wide processor-time delta normalises every thread's CPU percentage, matching how process rows
        // are derived, so a thread and its process agree on what 100% means.
        uint64_t systemTotal = 0;
        uint64_t systemIdle = 0;
        const bool hasSystemTimes = ReadSystemTimes(systemIdle, systemTotal);
        uint64_t systemDelta = 0;
        if (hasSystemTimes && _hasThreadSystemTotal && systemTotal > _threadSystemTotal)
        {
            systemDelta = systemTotal - _threadSystemTotal;
        }
        for (uint32_t index = 0; index < count; ++index)
        {
            const RedXeNativeThreadRow& thread = Storage<SourceFamily::Walk>()._walk.threads[index];
            RedXeDataValue* values = Storage<SourceFamily::Thread>()._threadValues.data() +
                                     (static_cast<size_t>(index) * kThreadColumnCount);
            const uint64_t processorTime = thread.userTime + thread.kernelTime;
            double cpuPercent = 0.0;
            double contextSwitchRate = 0.0;
            RedXeDataQuality rateQuality = RedXeDataQualityInitializing;
            const ThreadCpuSample* previous = FindPreviousThread(thread.threadId, thread.createTime);
            if (previous && systemDelta != 0 && processorTime >= previous->processorTime)
            {
                cpuPercent = BoundedPercent(processorTime - previous->processorTime, systemDelta);
                rateQuality = RedXeDataQualityGood;
            }
            if (previous && elapsed100ns != 0)
            {
                contextSwitchRate = PerSecond(Delta32(thread.contextSwitches, previous->contextSwitches), elapsed100ns);
            }
            values[0] = UInt64Value(thread.processId, RedXeDataQualityGood);
            values[1] = UInt64Value(thread.createTime, RedXeDataQualityGood);
            values[2] = UInt64Value(thread.threadId, RedXeDataQualityGood);
            values[3] = UInt64Value(thread.userTime, RedXeDataQualityGood);
            values[4] = UInt64Value(thread.kernelTime, RedXeDataQualityGood);
            values[5] = Float64Value(cpuPercent, rateQuality);
            values[6] = UInt64Value(static_cast<uint64_t>(thread.priority), RedXeDataQualityGood);
            values[7] = UInt64Value(static_cast<uint64_t>(thread.basePriority), RedXeDataQualityGood);
            values[8] = UInt64Value(thread.threadState, RedXeDataQualityGood);
            values[9] = UInt64Value(thread.waitReason, RedXeDataQualityGood);
            values[10] = UInt64Value(thread.contextSwitches, RedXeDataQualityGood);
            values[11] = Float64Value(contextSwitchRate, rateQuality);
            Storage<SourceFamily::Thread>()._threadRows[index] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kThreadColumnCount};
        }
        StoreThreadHistory(count);
        if (hasSystemTimes)
        {
            _threadSystemTotal = systemTotal;
            _hasThreadSystemTotal = true;
        }
        _threadTimestamp = timestamp;
        _hasThreadTimestamp = true;
        _threadSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            (_walkReady && Storage<SourceFamily::Walk>()._walk.threadsTruncated) ? RedXeDataSnapshotFlagTruncated
                                                                                 : RedXeDataSnapshotFlagNone,
            kThreadDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::Thread>()._threadRows.data(),
            count,
            kThreadColumnCount,
        };
        *output = &_threadSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectNetworkInterfaces(uint64_t timestamp, uint64_t sequence,
                                                   const RedXeDataSnapshot** output) noexcept
    {
        RedXeNetStorageSampleInterfaces(Storage<SourceFamily::Net>()._netStorage);
        const uint32_t count = Storage<SourceFamily::Net>()._netStorage.interfaceCount;
        for (uint32_t index = 0; index < count; ++index)
        {
            const RedXeNetworkInterfaceRow& row = Storage<SourceFamily::Net>()._netStorage.interfaces[index];
            RedXeDataValue* values = Storage<SourceFamily::NetworkInterface>()._networkInterfaceValues.data() +
                                     (static_cast<size_t>(index) * kNetworkInterfaceColumnCount);
            const RedXeDataQuality counterQuality =
                row.hasCounters ? RedXeDataQualityGood : RedXeDataQualityUnavailable;
            const RedXeDataQuality rateQuality = row.ratesReady ? RedXeDataQualityGood : RedXeDataQualityInitializing;
            values[0] = UInt64Value(row.luid, RedXeDataQualityGood);
            values[1] = Utf16Value(row.alias, Utf16Length(row.alias), RedXeDataQualityGood);
            values[2] = Utf16Value(row.description, Utf16Length(row.description), RedXeDataQualityGood);
            values[3] = UInt64Value(row.type, RedXeDataQualityGood);
            values[4] = UInt64Value(row.operationalStatus, counterQuality);
            values[5] = UInt64Value(row.mediaConnectState, counterQuality);
            values[6] = UInt64Value(row.mtu, counterQuality);
            values[7] = UInt64Value(row.receiveLinkSpeed, counterQuality);
            values[8] = UInt64Value(row.transmitLinkSpeed, counterQuality);
            values[9] = UInt64Value(row.inOctets, counterQuality);
            values[10] = UInt64Value(row.outOctets, counterQuality);
            values[11] = Float64Value(row.inOctetRate, rateQuality);
            values[12] = Float64Value(row.outOctetRate, rateQuality);
            values[13] = UInt64Value(row.inUcastPkts, counterQuality);
            values[14] = UInt64Value(row.outUcastPkts, counterQuality);
            values[15] = UInt64Value(row.inNUcastPkts, counterQuality);
            values[16] = UInt64Value(row.outNUcastPkts, counterQuality);
            values[17] = UInt64Value(row.inErrors, counterQuality);
            values[18] = UInt64Value(row.outErrors, counterQuality);
            values[19] = UInt64Value(row.inDiscards, counterQuality);
            values[20] = UInt64Value(row.outDiscards, counterQuality);
            values[21] = Float64Value(row.utilizationPercent,
                                      row.utilizationReady ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            Storage<SourceFamily::NetworkInterface>()._networkInterfaceRows[index] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kNetworkInterfaceColumnCount};
        }
        _networkInterfaceSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            Storage<SourceFamily::Net>()._netStorage.interfacesTruncated ? RedXeDataSnapshotFlagTruncated
                                                                         : RedXeDataSnapshotFlagNone,
            kNetworkInterfaceDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::NetworkInterface>()._networkInterfaceRows.data(),
            count,
            kNetworkInterfaceColumnCount,
        };
        *output = &_networkInterfaceSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectNetworkProtocols(uint64_t timestamp, uint64_t sequence,
                                                  const RedXeDataSnapshot** output) noexcept
    {
        RedXeNetStorageSampleProtocols(Storage<SourceFamily::Net>()._netStorage);
        for (uint32_t index = 0; index < kRedXeNetworkProtocolCount; ++index)
        {
            const RedXeNetworkProtocolRow& row = Storage<SourceFamily::Net>()._netStorage.protocols[index];
            RedXeDataValue* values = Storage<SourceFamily::NetworkProtocol>()._networkProtocolValues.data() +
                                     (static_cast<size_t>(index) * kNetworkProtocolColumnCount);
            const RedXeDataQuality countQuality = row.hasCounts ? RedXeDataQualityGood : RedXeDataQualityUnavailable;
            const RedXeDataQuality rateQuality = row.ratesReady ? RedXeDataQualityGood : RedXeDataQualityInitializing;
            values[0] = Utf16Value(row.protocolId, Utf16Length(row.protocolId), RedXeDataQualityGood);
            values[1] = UInt64Value(row.addressFamily, RedXeDataQualityGood);
            values[2] = UInt64Value(row.inCount, countQuality);
            values[3] = UInt64Value(row.outCount, countQuality);
            values[4] = Float64Value(row.inRate, rateQuality);
            values[5] = Float64Value(row.outRate, rateQuality);
            values[6] = UInt64Value(row.errorCount, countQuality);
            values[7] = UInt64Value(row.discardCount, countQuality);
            values[8] = UInt64Value(row.retransmitCount,
                                    row.hasRetransmit ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[9] = UInt64Value(row.currentEstablished,
                                    row.hasEstablished ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[10] = UInt64Value(row.resetCount, row.hasReset ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            Storage<SourceFamily::NetworkProtocol>()._networkProtocolRows[index] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kNetworkProtocolColumnCount};
        }
        _networkProtocolSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kNetworkProtocolDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::NetworkProtocol>()._networkProtocolRows.data(),
            kRedXeNetworkProtocolCount,
            kNetworkProtocolColumnCount,
        };
        *output = &_networkProtocolSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectStorageDisks(uint64_t timestamp, uint64_t sequence,
                                              const RedXeDataSnapshot** output) noexcept
    {
        if (!_disksReady)
        {
            RedXeNetStorageSampleDisks(Storage<SourceFamily::Net>()._netStorage);
            _disksReady = true;
        }
        const uint32_t count = Storage<SourceFamily::Net>()._netStorage.diskCount;
        for (uint32_t index = 0; index < count; ++index)
        {
            const RedXeStorageDiskRow& row = Storage<SourceFamily::Net>()._netStorage.disks[index];
            RedXeDataValue* values = Storage<SourceFamily::StorageDisk>()._storageDiskValues.data() +
                                     (static_cast<size_t>(index) * kStorageDiskColumnCount);
            const RedXeDataQuality rateQuality = row.ratesReady       ? RedXeDataQualityGood
                                                 : row.hasPerformance ? RedXeDataQualityInitializing
                                                                      : RedXeDataQualityUnavailable;
            const RedXeDataQuality perfQuality =
                row.hasPerformance ? RedXeDataQualityGood : RedXeDataQualityUnavailable;
            values[0] = UInt64Value(row.diskNumber, RedXeDataQualityGood);
            values[1] = Utf16Value(row.displayName, Utf16Length(row.displayName),
                                   row.hasIdentity ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[2] = UInt64Value(row.busType, row.hasIdentity ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[3] =
                UInt64Value(row.mediaKind, row.mediaKind != 0 ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[4] =
                UInt64Value(row.capacityBytes, row.hasCapacity ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[5] = Float64Value(row.activePercent, row.ratesReady ? RedXeDataQualityGood : rateQuality);
            values[6] = Float64Value(row.idlePercent, row.ratesReady ? RedXeDataQualityGood : rateQuality);
            values[7] = UInt64Value(row.queueDepth, perfQuality);
            values[8] = Float64Value(row.readBytesPerSecond, rateQuality);
            values[9] = Float64Value(row.writeBytesPerSecond, rateQuality);
            values[10] = Float64Value(row.readOpsPerSecond, rateQuality);
            values[11] = Float64Value(row.writeOpsPerSecond, rateQuality);
            values[12] = UInt64Value(row.splitCount, row.ratesReady ? RedXeDataQualityGood : rateQuality);
            values[13] =
                Float64Value(row.avgReadLatencyMs, row.hasLatency ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[14] = Float64Value(row.avgWriteLatencyMs,
                                      row.hasLatency ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[15] = UInt64Value(row.hasPerformance ? 1 : 0, RedXeDataQualityGood);
            Storage<SourceFamily::StorageDisk>()._storageDiskRows[index] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kStorageDiskColumnCount};
        }
        _storageDiskSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kStorageDiskDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::StorageDisk>()._storageDiskRows.data(),
            count,
            kStorageDiskColumnCount,
        };
        *output = &_storageDiskSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectStorageVolumes(uint64_t timestamp, uint64_t sequence,
                                                const RedXeDataSnapshot** output) noexcept
    {
        RedXeNetStorageSampleVolumes(Storage<SourceFamily::Net>()._netStorage);
        const uint32_t count = Storage<SourceFamily::Net>()._netStorage.volumeCount;
        for (uint32_t index = 0; index < count; ++index)
        {
            const RedXeStorageVolumeRow& row = Storage<SourceFamily::Net>()._netStorage.volumes[index];
            RedXeDataValue* values = Storage<SourceFamily::StorageVolume>()._storageVolumeValues.data() +
                                     (static_cast<size_t>(index) * kStorageVolumeColumnCount);
            values[0] = Utf16Value(row.volumeGuid, Utf16Length(row.volumeGuid), RedXeDataQualityGood);
            values[1] = Utf16Value(row.displayName, Utf16Length(row.displayName), RedXeDataQualityGood);
            values[2] = Utf16Value(row.fileSystem, Utf16Length(row.fileSystem),
                                   row.fileSystem[0] != L'\0' ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[3] =
                UInt64Value(row.totalBytes, row.hasCapacity ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[4] =
                UInt64Value(row.freeBytes, row.hasCapacity ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[5] = UInt64Value(row.extentCount, RedXeDataQualityGood);
            values[6] =
                UInt64Value(row.firstDiskNumber, row.hasFirstDisk ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[7] = Utf16Value(row.label, Utf16Length(row.label),
                                   row.label[0] != L'\0' ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            Storage<SourceFamily::StorageVolume>()._storageVolumeRows[index] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kStorageVolumeColumnCount};
        }
        _storageVolumeSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            Storage<SourceFamily::Net>()._netStorage.volumesTruncated ? RedXeDataSnapshotFlagTruncated
                                                                      : RedXeDataSnapshotFlagNone,
            kStorageVolumeDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::StorageVolume>()._storageVolumeRows.data(),
            count,
            kStorageVolumeColumnCount,
        };
        *output = &_storageVolumeSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectGpuAdapters(uint64_t timestamp, uint64_t sequence,
                                             const RedXeDataSnapshot** output) noexcept
    {
        if (!_gpuAdaptersReady)
        {
            RedXeGpuSampleAdapters(Storage<SourceFamily::Gpu>()._gpu);
            _gpuAdaptersReady = true;
        }
        // Graphics rows only. Compute-only accelerators go to npu.adapter so a viewer labelled "GPU" keeps its
        // meaning; a row whose class could not be established stays here rather than being reclassified on a guess.
        uint32_t count = 0;
        for (uint32_t source = 0; source < Storage<SourceFamily::Gpu>()._gpu.adapterCount; ++source)
        {
            const RedXeGpuAdapterRow& row = Storage<SourceFamily::Gpu>()._gpu.adapters[source];
            if (row.deviceClass == kRedXeDeviceClassNpu)
            {
                continue;
            }
            const uint32_t index = count;
            ++count;
            RedXeDataValue* values = Storage<SourceFamily::GpuAdapter>()._gpuAdapterValues.data() +
                                     (static_cast<size_t>(index) * kGpuAdapterColumnCount);
            values[0] = UInt64Value(row.adapterLuid, RedXeDataQualityGood);
            values[1] = Utf16Value(row.displayName, Utf16Length(row.displayName), RedXeDataQualityGood);
            values[2] = UInt64Value(row.vendorId, RedXeDataQualityGood);
            values[3] = UInt64Value(row.deviceId, RedXeDataQualityGood);
            values[4] = UInt64Value(row.software, RedXeDataQualityGood);
            values[5] =
                UInt64Value(row.integrated, row.hasIntegrated ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[6] = UInt64Value(row.dedicatedBytes, RedXeDataQualityGood);
            values[7] = UInt64Value(row.sharedBytes, RedXeDataQualityGood);
            values[8] = Float64Value(row.utilizationPercent,
                                     row.hasUtilization ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[9] = UInt64Value(row.usedDedicatedBytes,
                                    row.hasMemoryUsage ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[10] = UInt64Value(row.usedSharedBytes,
                                     row.hasMemoryUsage ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[11] =
                UInt64Value(row.memoryClockHz, row.hasPerf ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[12] =
                UInt64Value(row.maxMemoryClockHz, row.hasPerf ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[13] = Float64Value(std::clamp(row.powerPercent, 0.0, 100.0),
                                      row.hasPerf ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[14] =
                Float64Value(row.temperatureC, row.hasTemperature ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[15] = Float64Value(row.warningTemperatureC,
                                      row.hasTemperature ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[16] = Float64Value(row.maxTemperatureC,
                                      row.hasTemperature ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[17] = UInt64Value(row.fanRpm, row.hasFan ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[18] = UInt64Value(row.maxFanRpm, row.hasFan ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[19] =
                UInt64Value(row.deviceClass, row.hasDeviceClass ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[20] =
                UInt64Value(row.computeOnly, row.hasAdapterType ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[21] =
                UInt64Value(row.physicalAdapterCount,
                            row.physicalAdapterCount != 0 ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            Storage<SourceFamily::GpuAdapter>()._gpuAdapterRows[index] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kGpuAdapterColumnCount};
        }
        _gpuAdapterSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kGpuAdapterDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::GpuAdapter>()._gpuAdapterRows.data(),
            count,
            kGpuAdapterColumnCount,
        };
        *output = &_gpuAdapterSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectGpuEngines(uint64_t timestamp, uint64_t sequence,
                                            const RedXeDataSnapshot** output) noexcept
    {
        if (!_gpuEnginesReady)
        {
            RedXeGpuSampleEngines(Storage<SourceFamily::Gpu>()._gpu);
            _gpuEnginesReady = true;
        }
        const uint32_t count = Storage<SourceFamily::Gpu>()._gpu.engineCount;
        for (uint32_t index = 0; index < count; ++index)
        {
            const RedXeGpuEngineRow& row = Storage<SourceFamily::Gpu>()._gpu.engines[index];
            RedXeDataValue* values = Storage<SourceFamily::GpuEngine>()._gpuEngineValues.data() +
                                     (static_cast<size_t>(index) * kGpuEngineColumnCount);
            values[0] = UInt64Value(row.adapterLuid, RedXeDataQualityGood);
            values[1] = UInt64Value(row.physicalAdapterIndex, RedXeDataQualityGood);
            values[2] = UInt64Value(row.nodeOrdinal, RedXeDataQualityGood);
            values[3] = UInt64Value(row.engineClass, row.hasClass ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[4] = Utf16Value(row.friendlyName, Utf16Length(row.friendlyName),
                                   row.friendlyName[0] != L'\0' ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[5] = UInt64Value(row.currentFrequencyHz,
                                    row.hasFrequency ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[6] =
                UInt64Value(row.maxFrequencyHz, row.hasFrequency ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[7] =
                UInt64Value(row.voltageMv, row.hasFrequency ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[8] = Float64Value(0.0, RedXeDataQualityUnavailable);
            Storage<SourceFamily::GpuEngine>()._gpuEngineRows[index] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kGpuEngineColumnCount};
        }
        _gpuEngineSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            Storage<SourceFamily::Gpu>()._gpu.enginesTruncated ? RedXeDataSnapshotFlagTruncated
                                                               : RedXeDataSnapshotFlagNone,
            kGpuEngineDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::GpuEngine>()._gpuEngineRows.data(),
            count,
            kGpuEngineColumnCount,
        };
        *output = &_gpuEngineSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectGpuProcesses(uint64_t timestamp, uint64_t sequence,
                                              const RedXeDataSnapshot** output) noexcept
    {
        if (!_gpuProcessesReady)
        {
            RedXeGpuSampleProcesses(Storage<SourceFamily::Gpu>()._gpu);
            _gpuProcessesReady = true;
        }
        const uint32_t count = Storage<SourceFamily::Gpu>()._gpu.processCount;
        for (uint32_t index = 0; index < count; ++index)
        {
            const RedXeGpuProcessRow& row = Storage<SourceFamily::Gpu>()._gpu.processes[index];
            RedXeDataValue* values = Storage<SourceFamily::GpuProcess>()._gpuProcessValues.data() +
                                     (static_cast<size_t>(index) * kGpuProcessColumnCount);
            values[0] = UInt64Value(row.processId, RedXeDataQualityGood);
            values[1] = UInt64Value(row.adapterLuid, RedXeDataQualityGood);
            values[2] = UInt64Value(row.physicalAdapterIndex, RedXeDataQualityGood);
            values[3] = UInt64Value(row.engineOrdinal, RedXeDataQualityGood);
            values[4] = Utf16Value(row.engineType, Utf16Length(row.engineType),
                                   row.engineType[0] != L'\0' ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[5] = Float64Value(row.utilizationPercent,
                                     row.hasUtilization ? RedXeDataQualityGood : RedXeDataQualityInitializing);
            Storage<SourceFamily::GpuProcess>()._gpuProcessRows[index] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kGpuProcessColumnCount};
        }
        _gpuProcessSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            Storage<SourceFamily::Gpu>()._gpu.processesTruncated ? RedXeDataSnapshotFlagTruncated
                                                                 : RedXeDataSnapshotFlagNone,
            kGpuProcessDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::GpuProcess>()._gpuProcessRows.data(),
            count,
            kGpuProcessColumnCount,
        };
        *output = &_gpuProcessSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectPowerSummary(uint64_t timestamp, uint64_t sequence,
                                              const RedXeDataSnapshot** output) noexcept
    {
        if (!_powerReady)
        {
            RedXePowerSampleSummary(Storage<SourceFamily::Power>()._power);
            _powerReady = true;
        }
        const RedXePowerSummaryRow& row = Storage<SourceFamily::Power>()._power.summary;
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[0] =
            UInt64Value(row.acOnline, row.hasAc ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[1] =
            UInt64Value(row.batteryPresent, row.hasBatteryPresent ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[2] =
            UInt64Value(row.charging, row.hasCharging ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[3] =
            UInt64Value(row.batterySaver, row.hasSaver ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[4] =
            Float64Value(row.chargePercent, row.hasChargePercent ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[5] =
            UInt64Value(row.remainingSeconds, row.hasRemaining ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[6] =
            UInt64Value(row.fullLifeSeconds, row.hasFullLife ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[7] =
            UInt64Value(row.systemS3, row.hasCapabilities ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[8] =
            UInt64Value(row.systemS4, row.hasCapabilities ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[9] =
            UInt64Value(row.hiberFilePresent, row.hasCapabilities ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[10] =
            UInt64Value(row.thermalControl, row.hasCapabilities ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[11] =
            UInt64Value(row.modernStandby, row.hasCapabilities ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryValues[12] = UInt64Value(
            row.modernStandbyConnected, row.hasCapabilities ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::PowerSummary>()._powerSummaryRow =
            RedXeDataRow{sizeof(RedXeDataRow), Storage<SourceFamily::PowerSummary>()._powerSummaryValues.data(),
                         kPowerSummaryColumnCount};
        _powerSummarySnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kPowerSummaryDataSetId,
            sequence,
            timestamp,
            &Storage<SourceFamily::PowerSummary>()._powerSummaryRow,
            1,
            kPowerSummaryColumnCount,
        };
        *output = &_powerSummarySnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectBatteries(uint64_t timestamp, uint64_t sequence,
                                           const RedXeDataSnapshot** output) noexcept
    {
        if (!_batteriesReady)
        {
            RedXePowerSampleBatteries(Storage<SourceFamily::Power>()._power);
            _batteriesReady = true;
        }
        const uint32_t count = Storage<SourceFamily::Power>()._power.batteryCount;
        for (uint32_t index = 0; index < count; ++index)
        {
            const RedXeBatteryRow& row = Storage<SourceFamily::Power>()._power.batteries[index];
            RedXeDataValue* values = Storage<SourceFamily::Battery>()._batteryValues.data() +
                                     (static_cast<size_t>(index) * kBatteryColumnCount);
            values[0] = Utf16Value(row.deviceId, Utf16Length(row.deviceId), RedXeDataQualityGood);
            values[1] = Utf16Value(row.deviceName, Utf16Length(row.deviceName),
                                   row.deviceName[0] != L'\0' ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[2] = Utf16Value(row.chemistry, Utf16Length(row.chemistry),
                                   row.chemistry[0] != L'\0' ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[3] =
                UInt64Value(row.designedCapacity, row.hasDesigned ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[4] = UInt64Value(row.fullChargedCapacity,
                                    row.hasFullCharged ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[5] =
                UInt64Value(row.currentCapacity, row.hasCurrent ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[6] = Float64Value(row.rateMw, row.hasRate ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[7] = UInt64Value(row.voltageMv, row.hasVoltage ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[8] =
                Float64Value(row.temperatureC, row.hasTemperature ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[9] =
                UInt64Value(row.relativeCapacity, row.hasRelative ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[10] =
                UInt64Value(row.cycleCount, row.hasCycleCount ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[11] = UInt64Value(row.estimatedTimeSeconds,
                                     row.hasEstimatedTime ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[12] =
                UInt64Value(row.powerState, row.hasPowerState ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            Storage<SourceFamily::Battery>()._batteryRows[index] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kBatteryColumnCount};
        }
        _batterySnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            Storage<SourceFamily::Power>()._power.batteriesTruncated ? RedXeDataSnapshotFlagTruncated
                                                                     : RedXeDataSnapshotFlagNone,
            kBatteryListDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::Battery>()._batteryRows.data(),
            count,
            kBatteryColumnCount,
        };
        *output = &_batterySnapshot;
        return S_OK;
    }

    [[nodiscard]] bool AppendThermalRow(const wchar_t* sensorId, uint64_t deviceKind, const wchar_t* displayName,
                                        double temperatureC, double warningC, bool hasWarning, double criticalC,
                                        bool hasCritical, uint32_t& count) noexcept
    {
        if (count >= kThermalMaximumRows)
        {
            return false;
        }
        RedXeDataValue* values =
            Storage<SourceFamily::Thermal>()._thermalValues.data() + (static_cast<size_t>(count) * kThermalColumnCount);
        values[0] = Utf16Value(sensorId, Utf16Length(sensorId), RedXeDataQualityGood);
        values[1] = UInt64Value(deviceKind, RedXeDataQualityGood);
        values[2] =
            Utf16Value(displayName, Utf16Length(displayName),
                       displayName && displayName[0] != L'\0' ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        values[3] = Float64Value(temperatureC, RedXeDataQualityGood);
        values[4] = Float64Value(warningC, hasWarning ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        values[5] = Float64Value(criticalC, hasCritical ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        Storage<SourceFamily::Thermal>()._thermalRows[count] =
            RedXeDataRow{sizeof(RedXeDataRow), values, kThermalColumnCount};
        ++count;
        return true;
    }

    [[nodiscard]] HRESULT CollectThermals(uint64_t timestamp, uint64_t sequence,
                                          const RedXeDataSnapshot** output) noexcept
    {
        if (!_gpuAdaptersReady)
        {
            RedXeGpuSampleAdapters(Storage<SourceFamily::Gpu>()._gpu);
            _gpuAdaptersReady = true;
        }
        if (!_disksReady)
        {
            RedXeNetStorageSampleDisks(Storage<SourceFamily::Net>()._netStorage);
            _disksReady = true;
        }
        if (!_diskTempsReady)
        {
            RedXeNetStorageSampleDiskTemperatures(Storage<SourceFamily::Net>()._netStorage);
            _diskTempsReady = true;
        }
        if (!_batteriesReady)
        {
            RedXePowerSampleBatteries(Storage<SourceFamily::Power>()._power);
            _batteriesReady = true;
        }
        if (!_acpiReady)
        {
            RedXePowerSampleAcpiThermals(Storage<SourceFamily::Power>()._power);
            _acpiReady = true;
        }

        uint32_t count = 0;
        bool truncated = Storage<SourceFamily::Net>()._netStorage.diskTemperaturesTruncated ||
                         Storage<SourceFamily::Power>()._power.acpiThermalsTruncated;
        for (uint32_t index = 0; index < Storage<SourceFamily::Gpu>()._gpu.adapterCount; ++index)
        {
            const RedXeGpuAdapterRow& row = Storage<SourceFamily::Gpu>()._gpu.adapters[index];
            if (!row.hasTemperature)
            {
                continue;
            }
            wchar_t* sensorId = Storage<SourceFamily::Thermal>()._thermalSensorIds[count].data();
            size_t length = 0;
            sensorId[0] = L'\0';
            AppendWide(sensorId, Storage<SourceFamily::Thermal>()._thermalSensorIds[count].size(), length, L"gpu-");
            AppendHex64(sensorId, Storage<SourceFamily::Thermal>()._thermalSensorIds[count].size(), length,
                        row.adapterLuid);
            if (!AppendThermalRow(sensorId, kRedXeThermalKindGpu, row.displayName, row.temperatureC,
                                  row.warningTemperatureC, row.hasTemperature, row.maxTemperatureC, row.hasTemperature,
                                  count))
            {
                truncated = true;
                break;
            }
        }
        for (uint32_t index = 0;
             index < Storage<SourceFamily::Net>()._netStorage.diskTemperatureCount && count < kThermalMaximumRows;
             ++index)
        {
            const RedXeStorageTemperatureRow& row = Storage<SourceFamily::Net>()._netStorage.diskTemperatures[index];
            wchar_t* sensorId = Storage<SourceFamily::Thermal>()._thermalSensorIds[count].data();
            size_t length = 0;
            sensorId[0] = L'\0';
            AppendWide(sensorId, Storage<SourceFamily::Thermal>()._thermalSensorIds[count].size(), length, L"disk-");
            AppendDecimal(sensorId, Storage<SourceFamily::Thermal>()._thermalSensorIds[count].size(), length,
                          row.diskNumber);
            AppendWide(sensorId, Storage<SourceFamily::Thermal>()._thermalSensorIds[count].size(), length, L"-");
            AppendDecimal(sensorId, Storage<SourceFamily::Thermal>()._thermalSensorIds[count].size(), length,
                          row.sensorIndex);
            if (!AppendThermalRow(sensorId, kRedXeThermalKindStorage, row.displayName, row.temperatureC,
                                  row.warningTemperatureC, row.hasWarning, row.criticalTemperatureC, row.hasCritical,
                                  count))
            {
                truncated = true;
                break;
            }
        }
        for (uint32_t index = 0;
             index < Storage<SourceFamily::Power>()._power.batteryCount && count < kThermalMaximumRows; ++index)
        {
            const RedXeBatteryRow& row = Storage<SourceFamily::Power>()._power.batteries[index];
            if (!row.hasTemperature)
            {
                continue;
            }
            if (!AppendThermalRow(row.deviceId, kRedXeThermalKindBattery,
                                  row.deviceName[0] != L'\0' ? row.deviceName : row.deviceId, row.temperatureC, 0.0,
                                  false, 0.0, false, count))
            {
                truncated = true;
                break;
            }
        }
        for (uint32_t index = 0;
             index < Storage<SourceFamily::Power>()._power.acpiThermalCount && count < kThermalMaximumRows; ++index)
        {
            const RedXeAcpiThermalRow& row = Storage<SourceFamily::Power>()._power.acpiThermals[index];
            if (!AppendThermalRow(row.sensorId, kRedXeThermalKindAcpi, row.displayName, row.temperatureC,
                                  row.warningTemperatureC, row.hasWarning, row.criticalTemperatureC, row.hasCritical,
                                  count))
            {
                truncated = true;
                break;
            }
        }
        _thermalSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            truncated ? RedXeDataSnapshotFlagTruncated : RedXeDataSnapshotFlagNone,
            kThermalSensorDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::Thermal>()._thermalRows.data(),
            count,
            kThermalColumnCount,
        };
        *output = &_thermalSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectFans(uint64_t timestamp, uint64_t sequence, const RedXeDataSnapshot** output) noexcept
    {
        if (!_gpuAdaptersReady)
        {
            RedXeGpuSampleAdapters(Storage<SourceFamily::Gpu>()._gpu);
            _gpuAdaptersReady = true;
        }
        if (!_fanPresenceReady)
        {
            RedXePowerSampleFanPresence(Storage<SourceFamily::Power>()._power);
            _fanPresenceReady = true;
        }
        uint32_t count = 0;
        bool truncated = false;
        for (uint32_t index = 0; index < Storage<SourceFamily::Gpu>()._gpu.adapterCount; ++index)
        {
            const RedXeGpuAdapterRow& row = Storage<SourceFamily::Gpu>()._gpu.adapters[index];
            if (!row.hasFan)
            {
                continue;
            }
            if (count >= kFanMaximumRows)
            {
                truncated = true;
                break;
            }
            wchar_t* sensorId = Storage<SourceFamily::Fan>()._fanSensorIds[count].data();
            size_t length = 0;
            sensorId[0] = L'\0';
            AppendWide(sensorId, Storage<SourceFamily::Fan>()._fanSensorIds[count].size(), length, L"gpu-fan-");
            AppendHex64(sensorId, Storage<SourceFamily::Fan>()._fanSensorIds[count].size(), length, row.adapterLuid);
            RedXeDataValue* values =
                Storage<SourceFamily::Fan>()._fanValues.data() + (static_cast<size_t>(count) * kFanColumnCount);
            values[0] = Utf16Value(sensorId, Utf16Length(sensorId), RedXeDataQualityGood);
            values[1] = UInt64Value(kRedXeThermalKindGpu, RedXeDataQualityGood);
            values[2] = Utf16Value(row.displayName, Utf16Length(row.displayName), RedXeDataQualityGood);
            values[3] = UInt64Value(row.fanRpm, RedXeDataQualityGood);
            values[4] = UInt64Value(row.maxFanRpm, RedXeDataQualityGood);
            values[5] = UInt64Value(row.fanRpm != 0 ? 1 : 0, RedXeDataQualityGood);
            Storage<SourceFamily::Fan>()._fanRows[count] = RedXeDataRow{sizeof(RedXeDataRow), values, kFanColumnCount};
            ++count;
        }
        (void)Storage<SourceFamily::Power>()._power.fanInterfaceCount;
        _fanSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            truncated ? RedXeDataSnapshotFlagTruncated : RedXeDataSnapshotFlagNone,
            kFanSensorDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::Fan>()._fanRows.data(),
            count,
            kFanColumnCount,
        };
        *output = &_fanSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectNpuAdapters(uint64_t timestamp, uint64_t sequence,
                                             const RedXeDataSnapshot** output) noexcept
    {
        if (!_gpuAdaptersReady)
        {
            RedXeGpuSampleAdapters(Storage<SourceFamily::Gpu>()._gpu);
            _gpuAdaptersReady = true;
        }
        uint32_t count = 0;
        for (uint32_t source = 0;
             source < Storage<SourceFamily::Gpu>()._gpu.adapterCount && count < kNpuAdapterMaximumRows; ++source)
        {
            const RedXeGpuAdapterRow& row = Storage<SourceFamily::Gpu>()._gpu.adapters[source];
            if (row.deviceClass != kRedXeDeviceClassNpu)
            {
                continue;
            }
            RedXeDataValue* values = Storage<SourceFamily::NpuAdapter>()._npuAdapterValues.data() +
                                     (static_cast<size_t>(count) * kNpuAdapterColumnCount);
            const RedXeDataQuality dxgiQuality = row.hasDxgi ? RedXeDataQualityGood : RedXeDataQualityUnavailable;
            values[0] = UInt64Value(row.adapterLuid, RedXeDataQualityGood);
            values[1] = Utf16Value(row.displayName, Utf16Length(row.displayName),
                                   row.displayName[0] != L'\0' ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[2] = UInt64Value(row.vendorId, dxgiQuality);
            values[3] = UInt64Value(row.deviceId, dxgiQuality);
            values[4] = UInt64Value(row.deviceClass, RedXeDataQualityGood);
            values[5] =
                UInt64Value(row.computeOnly, row.hasAdapterType ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[6] =
                UInt64Value(row.integrated, row.hasIntegrated ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[7] = UInt64Value(row.physicalAdapterCount,
                                    row.physicalAdapterCount != 0 ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[8] =
                UInt64Value(row.engineCount, row.hasEngineCount ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[9] = UInt64Value(row.dedicatedBytes, dxgiQuality);
            values[10] = UInt64Value(row.sharedBytes, dxgiQuality);
            values[11] = UInt64Value(row.usedDedicatedBytes,
                                     row.hasMemoryUsage ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[12] = UInt64Value(row.usedSharedBytes,
                                     row.hasMemoryUsage ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[13] = Float64Value(row.utilizationPercent,
                                      row.hasUtilization ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[14] =
                Float64Value(row.temperatureC, row.hasTemperature ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            Storage<SourceFamily::NpuAdapter>()._npuAdapterRows[count] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kNpuAdapterColumnCount};
            ++count;
        }
        _npuAdapterSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kNpuAdapterDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::NpuAdapter>()._npuAdapterRows.data(),
            count,
            kNpuAdapterColumnCount,
        };
        *output = &_npuAdapterSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectNpuEngines(uint64_t timestamp, uint64_t sequence,
                                            const RedXeDataSnapshot** output) noexcept
    {
        if (!_gpuEnginesReady)
        {
            RedXeGpuSampleEngines(Storage<SourceFamily::Gpu>()._gpu);
            _gpuEnginesReady = true;
        }
        uint32_t count = 0;
        bool truncated = false;
        for (uint32_t source = 0; source < Storage<SourceFamily::Gpu>()._gpu.engineCount; ++source)
        {
            const RedXeGpuEngineRow& row = Storage<SourceFamily::Gpu>()._gpu.engines[source];
            if (RedXeGpuDeviceClassForLuid(Storage<SourceFamily::Gpu>()._gpu, row.adapterLuid) != kRedXeDeviceClassNpu)
            {
                continue;
            }
            if (count >= kNpuEngineMaximumRows)
            {
                truncated = true;
                break;
            }
            RedXeDataValue* values = Storage<SourceFamily::NpuEngine>()._npuEngineValues.data() +
                                     (static_cast<size_t>(count) * kNpuEngineColumnCount);
            values[0] = UInt64Value(row.adapterLuid, RedXeDataQualityGood);
            values[1] = UInt64Value(row.physicalAdapterIndex, RedXeDataQualityGood);
            values[2] = UInt64Value(row.nodeOrdinal, RedXeDataQualityGood);
            values[3] = Utf16Value(row.friendlyName, Utf16Length(row.friendlyName),
                                   row.hasClass ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[4] = Float64Value(row.utilizationPercent,
                                     row.hasUtilization ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[5] = UInt64Value(row.currentFrequencyHz,
                                    row.hasFrequency ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[6] =
                UInt64Value(row.maxFrequencyHz, row.hasFrequency ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[7] =
                UInt64Value(row.voltageMv, row.hasFrequency ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            Storage<SourceFamily::NpuEngine>()._npuEngineRows[count] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kNpuEngineColumnCount};
            ++count;
        }
        _npuEngineSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            truncated ? RedXeDataSnapshotFlagTruncated : RedXeDataSnapshotFlagNone,
            kNpuEngineDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::NpuEngine>()._npuEngineRows.data(),
            count,
            kNpuEngineColumnCount,
        };
        *output = &_npuEngineSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectNpuProcesses(uint64_t timestamp, uint64_t sequence,
                                              const RedXeDataSnapshot** output) noexcept
    {
        if (!_gpuProcessesReady)
        {
            RedXeGpuSampleProcesses(Storage<SourceFamily::Gpu>()._gpu);
            _gpuProcessesReady = true;
        }
        uint32_t count = 0;
        bool truncated = false;
        for (uint32_t source = 0; source < Storage<SourceFamily::Gpu>()._gpu.processCount; ++source)
        {
            const RedXeGpuProcessRow& row = Storage<SourceFamily::Gpu>()._gpu.processes[source];
            if (RedXeGpuDeviceClassForLuid(Storage<SourceFamily::Gpu>()._gpu, row.adapterLuid) != kRedXeDeviceClassNpu)
            {
                continue;
            }
            if (count >= kNpuProcessMaximumRows)
            {
                truncated = true;
                break;
            }
            RedXeDataValue* values = Storage<SourceFamily::NpuProcess>()._npuProcessValues.data() +
                                     (static_cast<size_t>(count) * kNpuProcessColumnCount);
            values[0] = UInt64Value(row.processId, RedXeDataQualityGood);
            values[1] = UInt64Value(row.adapterLuid, RedXeDataQualityGood);
            values[2] = UInt64Value(row.physicalAdapterIndex, RedXeDataQualityGood);
            values[3] = UInt64Value(row.engineOrdinal, RedXeDataQualityGood);
            values[4] = Utf16Value(row.engineType, Utf16Length(row.engineType), RedXeDataQualityGood);
            values[5] = Float64Value(row.utilizationPercent,
                                     row.hasUtilization ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            Storage<SourceFamily::NpuProcess>()._npuProcessRows[count] =
                RedXeDataRow{sizeof(RedXeDataRow), values, kNpuProcessColumnCount};
            ++count;
        }
        _npuProcessSnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            truncated ? RedXeDataSnapshotFlagTruncated : RedXeDataSnapshotFlagNone,
            kNpuProcessDataSetId,
            sequence,
            timestamp,
            Storage<SourceFamily::NpuProcess>()._npuProcessRows.data(),
            count,
            kNpuProcessColumnCount,
        };
        *output = &_npuProcessSnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectSecurityPosture(uint64_t timestamp, uint64_t sequence,
                                                 const RedXeDataSnapshot** output) noexcept
    {
        RedXeNativeCodeIntegrity integrity{};
        const bool hasIntegrity = RedXeNativeQueryCodeIntegrity(integrity);
        const RedXeDataQuality quality = hasIntegrity ? RedXeDataQualityGood : RedXeDataQualityUnavailable;
        const auto bit = [&](uint32_t mask) -> uint64_t { return (integrity.options & mask) != 0 ? 1U : 0U; };
        Storage<SourceFamily::Security>()._securityValues[0] = UInt64Value(bit(kRedXeCodeIntegrityEnabled), quality);
        Storage<SourceFamily::Security>()._securityValues[1] = UInt64Value(bit(kRedXeCodeIntegrityTestSign), quality);
        Storage<SourceFamily::Security>()._securityValues[2] =
            UInt64Value(bit(kRedXeCodeIntegrityUmciEnabled), quality);
        Storage<SourceFamily::Security>()._securityValues[3] =
            UInt64Value(bit(kRedXeCodeIntegrityHvciKmciEnabled), quality);
        Storage<SourceFamily::Security>()._securityValues[4] =
            UInt64Value(bit(kRedXeCodeIntegrityHvciKmciStrictMode), quality);
        Storage<SourceFamily::Security>()._securityValues[5] =
            UInt64Value(bit(kRedXeCodeIntegrityHvciIumEnabled), quality);
        Storage<SourceFamily::Security>()._securityValues[6] =
            UInt64Value(bit(kRedXeCodeIntegrityWhqlEnforcement), quality);
        Storage<SourceFamily::Security>()._securityValues[7] =
            UInt64Value(bit(kRedXeCodeIntegrityDebugModeEnabled), quality);
        Storage<SourceFamily::Security>()._securityValues[8] =
            UInt64Value(bit(kRedXeCodeIntegrityFlightingEnabled), quality);
        Storage<SourceFamily::Security>()._securityValues[9] = UInt64Value(integrity.options, quality);
        // Documented processor-feature probe, independent of the native class above.
        Storage<SourceFamily::Security>()._securityValues[10] =
            UInt64Value(IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED) != FALSE ? 1U : 0U, RedXeDataQualityGood);
        Storage<SourceFamily::Security>()._securityRow = RedXeDataRow{
            sizeof(RedXeDataRow), Storage<SourceFamily::Security>()._securityValues.data(), kSecurityColumnCount};
        _securitySnapshot = RedXeDataSnapshot{
            sizeof(RedXeDataSnapshot),
            RedXeDataSnapshotFlagNone,
            kSecurityDataSetId,
            sequence,
            timestamp,
            &Storage<SourceFamily::Security>()._securityRow,
            1,
            kSecurityColumnCount,
        };
        *output = &_securitySnapshot;
        return S_OK;
    }

    // Thread rate history. Rebuilt wholesale from the current walk on every thread collection, so a departed thread
    // never leaves a stale entry and the table never needs eviction.
    [[nodiscard]] const ThreadCpuSample* FindPreviousThread(uint32_t threadId, uint64_t createTime) const noexcept
    {
        if (createTime == 0)
        {
            return nullptr;
        }
        size_t index = ThreadSlot(threadId, createTime);
        for (size_t probe = 0; probe < kThreadHistoryCapacity; ++probe)
        {
            const ThreadCpuSample& candidate = Storage<SourceFamily::Thread>()._threadHistory[index];
            if (candidate.createTime == 0)
            {
                return nullptr;
            }
            if (candidate.threadId == threadId && candidate.createTime == createTime)
            {
                return &candidate;
            }
            index = (index + 1) & (kThreadHistoryCapacity - 1);
        }
        return nullptr;
    }

    void StoreThreadHistory(uint32_t count) noexcept
    {
        Storage<SourceFamily::Thread>()._threadHistory.fill({});
        for (uint32_t entry = 0; entry < count; ++entry)
        {
            const RedXeNativeThreadRow& thread = Storage<SourceFamily::Walk>()._walk.threads[entry];
            if (thread.createTime == 0)
            {
                continue;
            }
            size_t index = ThreadSlot(thread.threadId, thread.createTime);
            for (size_t probe = 0; probe < kThreadHistoryCapacity; ++probe)
            {
                ThreadCpuSample& candidate = Storage<SourceFamily::Thread>()._threadHistory[index];
                if (candidate.createTime == 0)
                {
                    candidate.threadId = thread.threadId;
                    candidate.createTime = thread.createTime;
                    candidate.processorTime = thread.userTime + thread.kernelTime;
                    candidate.contextSwitches = thread.contextSwitches;
                    break;
                }
                index = (index + 1) & (kThreadHistoryCapacity - 1);
            }
        }
    }

    [[nodiscard]] const ProcessCpuSample* FindPreviousProcess(uint32_t processId, uint64_t creationTime) const noexcept
    {
        size_t index =
            (static_cast<size_t>(processId) * 2654435761U ^ static_cast<size_t>(creationTime ^ (creationTime >> 32U))) &
            (kProcessHistoryCapacity - 1);
        for (size_t probe = 0; probe < kProcessHistoryCapacity; ++probe)
        {
            const ProcessCpuSample& candidate = Storage<SourceFamily::Process>()._processHistory[index];
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
        size_t index = (static_cast<size_t>(sample.processId) * 2654435761U ^
                        static_cast<size_t>(sample.creationTime ^ (sample.creationTime >> 32U))) &
                       (kProcessHistoryCapacity - 1);
        for (size_t probe = 0; probe < kProcessHistoryCapacity; ++probe)
        {
            ProcessCpuSample& candidate = Storage<SourceFamily::Process>()._processHistory[index];
            if (candidate.creationTime == 0)
            {
                candidate = sample;
                return;
            }
            index = (index + 1) & (kProcessHistoryCapacity - 1);
        }
    }

    // Fills the columns beyond the seven identity/summary values. `native` is the bulk SystemProcessInformation row
    // when the walk backend produced this process; when it is present the times, faults, I/O, and cycle counters come
    // straight out of the walked buffer, and the transient handle is only needed for affinity, architecture, critical
    // state, and efficiency mode. The Toolhelp fallback passes nullptr and keeps the per-handle path.
    void FillProcessExtended(RedXeDataValue* values, HANDLE process, uint32_t parentPid, bool hasParent,
                             uint32_t sessionId, bool hasSession, std::int32_t basePriority, bool hasPriority,
                             uint64_t peakWorkingSet, bool hasPeak, uint64_t virtualBytes, bool hasVirtual,
                             uint64_t systemDelta, ProcessCpuSample& current,
                             const RedXeNativeProcessRow* native) noexcept
    {
        const ProcessCpuSample* previous =
            current.creationTime != 0 ? FindPreviousProcess(current.processId, current.creationTime) : nullptr;
        const bool hasTimes = native != nullptr || (process && current.creationTime != 0);
        const double seconds = static_cast<double>(systemDelta) / 10'000'000.0;
        values[7] = UInt64Value(parentPid, hasParent ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        values[8] = UInt64Value(sessionId, hasSession ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        values[9] = UInt64Value(current.userTime, hasTimes ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        values[10] = UInt64Value(current.kernelTime, hasTimes ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        RedXeDataQuality splitCpu = RedXeDataQualityUnavailable;
        double userPercent = 0.0;
        double kernelPercent = 0.0;
        if (previous && systemDelta != 0 && current.userTime >= previous->userTime &&
            current.kernelTime >= previous->kernelTime)
        {
            userPercent = BoundedPercent(current.userTime - previous->userTime, systemDelta);
            kernelPercent = BoundedPercent(current.kernelTime - previous->kernelTime, systemDelta);
            splitCpu = RedXeDataQualityGood;
        }
        else if (hasTimes)
        {
            splitCpu = RedXeDataQualityInitializing;
        }
        values[11] = Float64Value(userPercent, splitCpu);
        values[12] = Float64Value(kernelPercent, splitCpu);
        values[13] = UInt64Value(peakWorkingSet, hasPeak ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        values[14] = UInt64Value(virtualBytes, hasVirtual ? RedXeDataQualityGood : RedXeDataQualityUnavailable);

        bool hasIo = false;
        if (native)
        {
            current.ioReadBytes = native->ioReadBytes;
            current.ioWriteBytes = native->ioWriteBytes;
            current.ioOtherBytes = native->ioOtherBytes;
            hasIo = true;
        }
        else
        {
            IO_COUNTERS io{};
            if (process && GetProcessIoCounters(process, &io) != FALSE)
            {
                current.ioReadBytes = io.ReadTransferCount;
                current.ioWriteBytes = io.WriteTransferCount;
                current.ioOtherBytes = io.OtherTransferCount;
                hasIo = true;
            }
        }
        if (hasIo)
        {
            values[15] = UInt64Value(current.ioReadBytes, RedXeDataQualityGood);
            values[16] = UInt64Value(current.ioWriteBytes, RedXeDataQualityGood);
            values[17] = UInt64Value(current.ioOtherBytes, RedXeDataQualityGood);
            if (previous && systemDelta != 0 && current.ioReadBytes >= previous->ioReadBytes &&
                current.ioWriteBytes >= previous->ioWriteBytes)
            {
                values[18] = Float64Value(
                    seconds > 0.0 ? static_cast<double>(current.ioReadBytes - previous->ioReadBytes) / seconds : 0.0,
                    RedXeDataQualityGood);
                values[19] = Float64Value(
                    seconds > 0.0 ? static_cast<double>(current.ioWriteBytes - previous->ioWriteBytes) / seconds : 0.0,
                    RedXeDataQualityGood);
            }
            else
            {
                values[18] = Float64Value(0.0, RedXeDataQualityInitializing);
                values[19] = Float64Value(0.0, RedXeDataQualityInitializing);
            }
        }
        else
        {
            values[15] = UInt64Value(0, RedXeDataQualityUnavailable);
            values[16] = UInt64Value(0, RedXeDataQualityUnavailable);
            values[17] = UInt64Value(0, RedXeDataQualityUnavailable);
            values[18] = Float64Value(0.0, RedXeDataQualityUnavailable);
            values[19] = Float64Value(0.0, RedXeDataQualityUnavailable);
        }

        if (native || current.pageFaults != 0 || (process && current.creationTime != 0))
        {
            values[20] = UInt64Value(current.pageFaults, RedXeDataQualityGood);
            if (previous && systemDelta != 0 && current.pageFaults >= previous->pageFaults)
            {
                values[21] = Float64Value(
                    seconds > 0.0 ? static_cast<double>(current.pageFaults - previous->pageFaults) / seconds : 0.0,
                    RedXeDataQualityGood);
            }
            else
            {
                values[21] = Float64Value(0.0, RedXeDataQualityInitializing);
            }
        }
        else
        {
            values[20] = UInt64Value(0, RedXeDataQualityUnavailable);
            values[21] = Float64Value(0.0, RedXeDataQualityUnavailable);
        }

        values[22] = UInt64Value(current.creationTime,
                                 current.creationTime != 0 ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        if (native)
        {
            values[23] = UInt64Value(native->cycleTime,
                                     native->cycleTime != 0 ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
        }
        else
        {
            ULONG64 cycles = 0;
            values[23] = process && QueryProcessCycleTime(process, &cycles) != FALSE
                             ? UInt64Value(cycles, RedXeDataQualityGood)
                             : UInt64Value(0, RedXeDataQualityUnavailable);
        }
        values[24] = UInt64Value(static_cast<uint64_t>(basePriority),
                                 hasPriority ? RedXeDataQualityGood : RedXeDataQualityUnavailable);

        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask = 0;
        if (process && GetActiveProcessorGroupCount() == 1 &&
            GetProcessAffinityMask(process, &processMask, &systemMask) != FALSE)
        {
            values[25] = UInt64Value(0, RedXeDataQualityGood);
            values[26] = UInt64Value(static_cast<uint64_t>(processMask), RedXeDataQualityGood);
        }
        else
        {
            values[25] = UInt64Value(0, RedXeDataQualityUnavailable);
            values[26] = UInt64Value(0, RedXeDataQualityUnavailable);
        }

        USHORT processMachine = 0;
        USHORT nativeMachine = 0;
        if (process && IsWow64Process2(process, &processMachine, &nativeMachine) != FALSE)
        {
            values[27] = UInt64Value(processMachine != IMAGE_FILE_MACHINE_UNKNOWN ? 1 : 0, RedXeDataQualityGood);
            values[28] = UInt64Value(nativeMachine, RedXeDataQualityGood);
        }
        else
        {
            values[27] = UInt64Value(0, RedXeDataQualityUnavailable);
            values[28] = UInt64Value(0, RedXeDataQualityUnavailable);
        }
        values[29] = UInt64Value(0, RedXeDataQualityUnavailable);
        BOOL critical = FALSE;
        values[30] = process && IsProcessCritical(process, &critical) != FALSE
                         ? UInt64Value(critical ? 1 : 0, RedXeDataQualityGood)
                         : UInt64Value(0, RedXeDataQualityUnavailable);

        const RedXeDataQuality nativeQuality = native ? RedXeDataQualityGood : RedXeDataQualityUnavailable;
        values[31] = UInt64Value(native ? native->hardFaultCount : 0, nativeQuality);
        values[32] = UInt64Value(native ? native->workingSetPrivateBytes : 0, nativeQuality);
        values[33] = UInt64Value(native ? native->pagefileBytes : 0, nativeQuality);
        values[34] = UInt64Value(native ? native->ioReadOperations : 0, nativeQuality);
        values[35] = UInt64Value(native ? native->ioWriteOperations : 0, nativeQuality);
        values[36] = UInt64Value(native ? native->ioOtherOperations : 0, nativeQuality);

        // Efficiency mode (EcoQoS): the documented per-process power-throttling state behind Task Manager's leaf.
        PROCESS_POWER_THROTTLING_STATE throttling{};
        throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        if (process && GetProcessInformation(process, ProcessPowerThrottling, &throttling,
                                             static_cast<DWORD>(sizeof(throttling))) != FALSE)
        {
            const bool efficiency = (throttling.ControlMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0 &&
                                    (throttling.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0;
            values[37] = UInt64Value(efficiency ? 1 : 0, RedXeDataQualityGood);
            values[38] = UInt64Value(throttling.ControlMask, RedXeDataQualityGood);
        }
        else
        {
            values[37] = UInt64Value(0, RedXeDataQualityUnavailable);
            values[38] = UInt64Value(0, RedXeDataQualityUnavailable);
        }
    }

    [[nodiscard]] bool AppendProcessRow(const PROCESSENTRY32W& entry, uint64_t systemDelta, size_t& rowCount,
                                        size_t& nameCharacters) noexcept
    {
        size_t imageCharacters = 0;
        while (imageCharacters < std::size(entry.szExeFile) && entry.szExeFile[imageCharacters] != L'\0')
        {
            ++imageCharacters;
        }
        if (rowCount >= kMaximumProcesses ||
            nameCharacters + imageCharacters + 1 > Storage<SourceFamily::Process>()._processNames.size())
        {
            return false;
        }

        wchar_t* imageName = Storage<SourceFamily::Process>()._processNames.data() + nameCharacters;
        std::wmemcpy(imageName, entry.szExeFile, imageCharacters);
        imageName[imageCharacters] = L'\0';
        nameCharacters += imageCharacters + 1;

        const DWORD desiredAccess = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ;
        wil::unique_handle process = OpenProcessForCollection(desiredAccess, entry.th32ProcessID);
        const bool canReadMemory = static_cast<bool>(process);
        if (!process)
        {
            process = OpenProcessForCollection(PROCESS_QUERY_LIMITED_INFORMATION, entry.th32ProcessID);
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
                currentCpu.userTime = FileTimeValue(user);
                currentCpu.kernelTime = FileTimeValue(kernel);
                currentCpu.processorTime = currentCpu.userTime + currentCpu.kernelTime;
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

        uint64_t workingSet = 0;
        uint64_t privateBytes = 0;
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
                currentCpu.pageFaults = memory.PageFaultCount;
                memoryQuality = RedXeDataQualityGood;
            }
        }

        DWORD handleCount = 0;
        const RedXeDataQuality handleQuality = process && GetProcessHandleCount(process.get(), &handleCount)
                                                   ? RedXeDataQualityGood
                                                   : RedXeDataQualityUnavailable;

        RedXeDataValue* values = ProcessValues().data() + rowCount * _processValueStride;
        values[0] = UInt64Value(entry.th32ProcessID, RedXeDataQualityGood);
        values[1] = Utf16Value(imageName, static_cast<uint32_t>(imageCharacters), RedXeDataQualityGood);
        values[2] = Float64Value(cpuPercent, cpuQuality);
        values[3] = UInt64Value(workingSet, memoryQuality);
        values[4] = UInt64Value(privateBytes, memoryQuality);
        values[5] = UInt64Value(entry.cntThreads, RedXeDataQualityGood);
        values[6] = UInt64Value(handleCount, handleQuality);
        if (_needProcessDetails)
            FillProcessExtended(values, process.get(), entry.th32ParentProcessID, true, 0, false, 0, false, 0, false, 0,
                                false, systemDelta, currentCpu, nullptr);
        else
        {
            values[7] = UInt64Value(entry.th32ParentProcessID, RedXeDataQualityGood);
            // Keep the legacy I/O epoch continuous if a detail subscriber joins after an activity-only fallback.
            IO_COUNTERS io{};
            if (process && GetProcessIoCounters(process.get(), &io) != FALSE)
            {
                currentCpu.ioReadBytes = io.ReadTransferCount;
                currentCpu.ioWriteBytes = io.WriteTransferCount;
                currentCpu.ioOtherBytes = io.OtherTransferCount;
            }
        }
        Storage<SourceFamily::Process>()._currentCpu[rowCount] = currentCpu;
        Storage<SourceFamily::Process>()._processRows[rowCount] =
            RedXeDataRow{sizeof(RedXeDataRow), values, _processValueStride};
        ++rowCount;
        return true;
    }

    void FinishProcessCollection(size_t rowCount, bool truncated, bool hasSystemTimes, uint64_t systemTotal,
                                 uint64_t sequence, uint64_t timestamp, const RedXeDataSnapshot** output) noexcept
    {
        ++_processHistoryCommits;
        Storage<SourceFamily::Process>()._processHistory.fill({});
        for (size_t index = 0; index < rowCount; ++index)
        {
            InsertProcessHistory(Storage<SourceFamily::Process>()._currentCpu[index]);
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
            sequence,
            timestamp,
            Storage<SourceFamily::Process>()._processRows.data(),
            static_cast<uint32_t>(rowCount),
            kProcessColumnCount,
        };
        if (!_needProcessDetails)
            _processSnapshot = {};
        if (_needProcessActivity)
        {
            if (_needProcessDetails)
                for (size_t index = 0; index < rowCount; ++index)
                    Storage<SourceFamily::ActivityViews>()._processActivityRows[index] =
                        RedXeDataRow{sizeof(RedXeDataRow), Storage<SourceFamily::Process>()._processRows[index].values,
                                     kProcessActivityColumnCount};
            _processActivitySnapshot = RedXeDataSnapshot{
                sizeof(RedXeDataSnapshot),
                truncated ? RedXeDataSnapshotFlagTruncated : RedXeDataSnapshotFlagNone,
                kProcessActivityDataSetId,
                sequence,
                timestamp,
                _needProcessDetails ? Storage<SourceFamily::ActivityViews>()._processActivityRows.data()
                                    : Storage<SourceFamily::Process>()._processRows.data(),
                static_cast<uint32_t>(rowCount),
                kProcessActivityColumnCount};
        }
        _processReady = true;
        *output = _needProcessDetails ? &_processSnapshot : &_processActivitySnapshot;
    }

    [[nodiscard]] bool ReadProcessSystemTimes(uint64_t& idle, uint64_t& total) const noexcept
    {
        if (_processSystemTimesForTest && _walkReady)
        {
            idle = 0;
            total = _processTestSystemTotal;
            return total != 0;
        }
        return ReadSystemTimes(idle, total);
    }

    [[nodiscard]] wil::unique_handle OpenProcessForCollection(DWORD access, DWORD pid) noexcept
    {
        ++_processHandleOpens;
        return wil::unique_handle{OpenProcess(access, FALSE, pid)};
    }

    [[nodiscard]] HRESULT CollectProcessesFromWalk(uint64_t timestamp, uint64_t sequence,
                                                   const RedXeDataSnapshot** output) noexcept
    {
        uint64_t ignoredIdle = 0;
        uint64_t systemTotal = 0;
        const bool hasSystemTimes = ReadProcessSystemTimes(ignoredIdle, systemTotal);
        const uint64_t systemDelta = hasSystemTimes && _hasProcessSystemTotal && systemTotal > _processSystemTotal
                                         ? systemTotal - _processSystemTotal
                                         : 0;
        size_t rowCount = 0;
        size_t nameCharacters = 0;
        for (uint32_t index = 0; index < Storage<SourceFamily::Walk>()._walk.processCount; ++index)
        {
            const RedXeNativeProcessRow& native = Storage<SourceFamily::Walk>()._walk.processes[index];
            const size_t imageCharacters = native.imageCharacters;
            if (rowCount >= kMaximumProcesses ||
                nameCharacters + imageCharacters + 1 > Storage<SourceFamily::Process>()._processNames.size())
            {
                FinishProcessCollection(rowCount, true, hasSystemTimes, systemTotal, sequence, timestamp, output);
                return S_OK;
            }
            wchar_t* imageName = Storage<SourceFamily::Process>()._processNames.data() + nameCharacters;
            std::wmemcpy(imageName, Storage<SourceFamily::Walk>()._walk.imageArena.data() + native.imageOffset,
                         imageCharacters);
            imageName[imageCharacters] = L'\0';
            nameCharacters += imageCharacters + 1;

            // The activity prefix comes entirely from the walk. Only a legacy detail request opens each process
            // for affinity, architecture, critical state, and efficiency mode.
            wil::unique_handle process;
            if (_needProcessDetails)
                process = OpenProcessForCollection(PROCESS_QUERY_LIMITED_INFORMATION, native.processId);

            ProcessCpuSample currentCpu{};
            currentCpu.processId = native.processId;
            currentCpu.creationTime = native.createTime;
            currentCpu.userTime = native.userTime;
            currentCpu.kernelTime = native.kernelTime;
            currentCpu.processorTime = native.userTime + native.kernelTime;
            currentCpu.pageFaults = native.pageFaultCount;
            currentCpu.ioReadBytes = native.ioReadBytes;
            currentCpu.ioWriteBytes = native.ioWriteBytes;
            currentCpu.ioOtherBytes = native.ioOtherBytes;
            RedXeDataQuality cpuQuality = RedXeDataQualityInitializing;
            double cpuPercent = 0.0;
            const ProcessCpuSample* previousCpu =
                currentCpu.creationTime != 0 ? FindPreviousProcess(currentCpu.processId, currentCpu.creationTime)
                                             : nullptr;
            if (previousCpu && currentCpu.processorTime >= previousCpu->processorTime && systemDelta != 0)
            {
                cpuPercent = BoundedPercent(currentCpu.processorTime - previousCpu->processorTime, systemDelta);
                cpuQuality = RedXeDataQualityGood;
            }

            RedXeDataValue* values = ProcessValues().data() + rowCount * _processValueStride;
            values[0] = UInt64Value(native.processId, RedXeDataQualityGood);
            values[1] = Utf16Value(imageName, static_cast<uint32_t>(imageCharacters),
                                   imageCharacters != 0 ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            values[2] = Float64Value(cpuPercent, cpuQuality);
            values[3] = UInt64Value(native.workingSetBytes, RedXeDataQualityGood);
            values[4] = UInt64Value(native.privateBytes, RedXeDataQualityGood);
            values[5] = UInt64Value(native.threadCount, RedXeDataQualityGood);
            values[6] = UInt64Value(native.handleCount, RedXeDataQualityGood);
            if (_needProcessDetails)
                FillProcessExtended(values, process.get(), native.parentProcessId, native.hasParent, native.sessionId,
                                    true, native.basePriority, true, native.peakWorkingSetBytes, true,
                                    native.virtualBytes, true, systemDelta, currentCpu, &native);
            else
                values[7] = UInt64Value(native.parentProcessId,
                                        native.hasParent ? RedXeDataQualityGood : RedXeDataQualityUnavailable);
            Storage<SourceFamily::Process>()._currentCpu[rowCount] = currentCpu;
            Storage<SourceFamily::Process>()._processRows[rowCount] =
                RedXeDataRow{sizeof(RedXeDataRow), values, _processValueStride};
            ++rowCount;
        }
        FinishProcessCollection(rowCount, Storage<SourceFamily::Walk>()._walk.processesTruncated, hasSystemTimes,
                                systemTotal, sequence, timestamp, output);
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectProcesses(const char* dataSetId, uint64_t timestamp, uint64_t sequence,
                                           const RedXeDataSnapshot** output) noexcept
    {
        if (!_processReady)
        {
            const RedXeDataSnapshot* ignored = nullptr;
            const HRESULT result = CollectProcessRows(timestamp, sequence, &ignored);
            if (FAILED(result))
                return result;
        }
        *output =
            RedXeAsciiEqualsIgnoreCase(dataSetId, kProcessDataSetId) ? &_processSnapshot : &_processActivitySnapshot;
        return S_OK;
    }

    [[nodiscard]] HRESULT CollectProcessRows(uint64_t timestamp, uint64_t sequence,
                                             const RedXeDataSnapshot** output) noexcept
    {
        _processValueStride = _needProcessDetails ? kProcessColumnCount : kProcessActivityColumnCount;
        ++_processCollections;
        _processBackend = static_cast<uint32_t>(
            _walkReady && Storage<SourceFamily::Walk>()._walk.valid ? kBackendNative : kBackendWin32);
        if (_walkReady && Storage<SourceFamily::Walk>()._walk.valid)
        {
            return CollectProcessesFromWalk(timestamp, sequence, output);
        }
        HANDLE rawSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (rawSnapshot == INVALID_HANDLE_VALUE)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        wil::unique_handle processSnapshot{rawSnapshot};

        uint64_t ignoredIdle = 0;
        uint64_t systemTotal = 0;
        const bool hasSystemTimes = ReadProcessSystemTimes(ignoredIdle, systemTotal);
        const uint64_t systemDelta = hasSystemTimes && _hasProcessSystemTotal && systemTotal > _processSystemTotal
                                         ? systemTotal - _processSystemTotal
                                         : 0;

        size_t rowCount = 0;
        size_t nameCharacters = 0;
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

        FinishProcessCollection(rowCount, truncated, hasSystemTimes, systemTotal, sequence, timestamp, output);
        return S_OK;
    }

    uint64_t _arenaCommittedBytes;
    uint64_t _arenaCommittedFamilies = 0;
    uint32_t _arenaCommitAttempts = 1;
    uint32_t _arenaFailureAfter = UINT32_MAX;
    uint32_t _processValueCapacity = 0;
    RedXeDataValue* _processValuesBase = nullptr;
    std::atomic_flag _collecting = ATOMIC_FLAG_INIT;
    uint64_t _sequence = 0;
    uint64_t _summaryIdle = 0;
    uint64_t _summaryTotal = 0;
    uint64_t _processSystemTotal = 0;
    bool _hasSummaryTimes = false;
    bool _hasProcessSystemTotal = false;
    std::array<DatasetRuntimeStatus, kDataSets.size()> _status{};
    RedXeDataSnapshot _statusSnapshot{};
    std::array<const RedXeDataSnapshot*, RedXeDataCollectMaximumDataSets> _batchSnapshots{};
    RedXeDataCollectResult _batchResult{};
    bool _cheapReady = false;
    bool _walkReady = false;
    bool _gpuAdaptersReady = false;
    bool _gpuEnginesReady = false;
    bool _gpuProcessesReady = false;
    bool _disksReady = false;
    bool _diskTempsReady = false;
    bool _powerReady = false;
    bool _batteriesReady = false;
    bool _acpiReady = false;
    bool _fanPresenceReady = false;
    uint64_t _cpuSummaryIdle = 0;
    uint64_t _cpuSummaryKernel = 0;
    uint64_t _cpuSummaryUser = 0;
    uint64_t _cpuSummaryDpc = 0;
    uint64_t _cpuSummaryInterrupt = 0;
    uint64_t _cpuSummaryInterruptCount = 0;
    uint64_t _cpuSummaryTimestamp = 0;
    uint32_t _cpuSummaryContextSwitches = 0;
    bool _hasCpuSummaryTimes = false;
    bool _hasCpuSummaryTimestamp = false;
    bool _hasCpuSummaryContextSwitches = false;
    uint64_t _cpuLogicalTimestamp = 0;
    bool _hasCpuLogicalTimestamp = false;
    uint64_t _memoryTimestamp = 0;
    uint32_t _memoryPageFaults = 0;
    uint32_t _memoryPageReads = 0;
    uint32_t _memoryDirtyWrites = 0;
    uint32_t _memoryMappedWrites = 0;
    bool _hasMemoryTimestamp = false;
    bool _hasMemoryCounters = false;
    uint64_t _threadTimestamp = 0;
    uint64_t _threadSystemTotal = 0;
    bool _hasThreadTimestamp = false;
    bool _hasThreadSystemTotal = false;
    uint32_t _previousLogicalCount = 0;
    bool _hasPreviousLogical = false;
    RedXeDataSnapshot _cpuSummarySnapshot{};
    RedXeDataSnapshot _cpuLogicalSnapshot{};
    RedXeDataSnapshot _memorySnapshot{};
    RedXeDataSnapshot _threadSnapshot{};
    RedXeDataSnapshot _summarySnapshot{};
    RedXeDataSnapshot _processActivitySnapshot{};
    uint64_t _processCollections = 0;
    uint64_t _processHandleOpens = 0;
    uint64_t _processHistoryCommits = 0;
    uint64_t _processTestSystemTotal = 0;
    uint32_t _processValueStride = 0;
    uint32_t _processBackend = 0;
    bool _processReady = false;
    bool _needProcessDetails = false;
    bool _needProcessActivity = false;
    bool _walkTestPending = false;
    bool _processSystemTimesForTest = false;
    RedXeDataSnapshot _processSnapshot{};
    RedXeDataSnapshot _networkInterfaceSnapshot{};
    RedXeDataSnapshot _networkProtocolSnapshot{};
    RedXeDataSnapshot _storageDiskSnapshot{};
    RedXeDataSnapshot _storageVolumeSnapshot{};
    RedXeDataSnapshot _gpuAdapterSnapshot{};
    RedXeDataSnapshot _gpuEngineSnapshot{};
    RedXeDataSnapshot _gpuProcessSnapshot{};
    RedXeDataSnapshot _powerSummarySnapshot{};
    RedXeDataSnapshot _batterySnapshot{};
    RedXeDataSnapshot _thermalSnapshot{};
    RedXeDataSnapshot _fanSnapshot{};
    RedXeDataSnapshot _npuAdapterSnapshot{};
    RedXeDataSnapshot _npuEngineSnapshot{};
    RedXeDataSnapshot _npuProcessSnapshot{};
    RedXeDataSnapshot _securitySnapshot{};
};
#pragma warning(pop)

static_assert(sizeof(SystemDataSource) <= kSourceControlRegionBytes);

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
    return RedXeCreateFromFactoryEntries(kFactoryEntries.data(), static_cast<uint32_t>(kFactoryEntries.size()),
                                         interfaceId, options, host, pluginId, result);
}

extern "C" HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata, uint32_t* count) noexcept
{
    return RedXeEnumerateFactoryMetadata(kMetadata.data(), static_cast<uint32_t>(kMetadata.size()), metadata, count);
}

extern "C" HRESULT __stdcall RedXeGetPluginSettingsContract(const char* pluginId,
                                                            const RedXePluginSettingsContract** contract) noexcept
{
    return RedXeGetStaticPluginSettingsContract(kPluginId, pluginId, &kSettingsContract, contract);
}

extern "C" __declspec(dllexport) HRESULT WINAPI
RedXeSystemDataTestArenaOwnership(SystemDataTestArenaOwnership* ownership) noexcept
{
    if (!ownership)
        return E_POINTER;
    if (ownership->sizeBytes != sizeof(SystemDataTestArenaOwnership))
        return E_INVALIDARG;
    const uint32_t count = gSourceArenaCount.load(std::memory_order_relaxed);
    *ownership = SystemDataTestArenaOwnership{
        sizeof(SystemDataTestArenaOwnership),
        count,
        count * static_cast<uint64_t>(kSourceArenaBytes),
        gSourceCommittedBytes.load(std::memory_order_relaxed),
        gSourceGpuOwners.load(std::memory_order_relaxed),
        gSourceNetOwners.load(std::memory_order_relaxed),
        gSourcePowerOwners.load(std::memory_order_relaxed),
        0,
    };
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI RedXeSystemDataTestAllocationFault(uint32_t stage) noexcept
{
    if (stage > 2)
        return E_INVALIDARG;
    gSourceAllocationFault.store(stage, std::memory_order_relaxed);
    return S_OK;
}

extern "C" __declspec(dllexport) BOOL WINAPI RedXeSystemDataTestDecodeWalk(const void* buffer, uint32_t bytes,
                                                                           RedXeNativeWalkSample* sample) noexcept
{
    if (!buffer || !sample)
        return FALSE;
    return RedXeNativeDecodeWalk({static_cast<const std::byte*>(buffer), bytes}, bytes, *sample) ? TRUE : FALSE;
}
