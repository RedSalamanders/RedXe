#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <windows.h>

constexpr size_t kRedXeMaximumLogicalProcessors = 1024;
constexpr size_t kRedXeNtProcessBufferBytes = 4U * 1024U * 1024U;
constexpr size_t kRedXeNtBasicProcessBufferBytes = 256U * 1024U;
constexpr size_t kRedXeMaximumNativeProcesses = 2048;
constexpr size_t kRedXeMaximumNativeThreads = 8192;
constexpr size_t kRedXeNativeImageCharacters = 260;

struct RedXeNativeLogicalCpu final
{
    uint16_t group = 0;
    uint16_t index = 0;
    uint64_t idleTime = 0;
    uint64_t kernelTime = 0;
    uint64_t userTime = 0;
    uint32_t currentMhz = 0;
    uint32_t maxMhz = 0;
    uint32_t cpuSetId = 0;
    std::uint8_t efficiencyClass = 0;
    bool parked = false;
    bool allocated = false;
    bool hasTimes = false;
    bool hasFrequency = false;
    bool hasCpuSet = false;
};

struct RedXeNativeProcessRow final
{
    uint32_t processId = 0;
    uint32_t parentProcessId = 0;
    uint32_t threadCount = 0;
    uint32_t handleCount = 0;
    uint32_t sessionId = 0;
    std::int32_t basePriority = 0;
    uint64_t peakVirtualBytes = 0;
    uint64_t virtualBytes = 0;
    uint64_t peakWorkingSetBytes = 0;
    uint64_t workingSetBytes = 0;
    uint64_t privateBytes = 0;
    uint32_t imageCharacters = 0;
    bool hasParent = false;
    wchar_t imageName[kRedXeNativeImageCharacters]{};
};

struct RedXeNativeThreadRow final
{
    uint32_t processId = 0;
    uint32_t threadId = 0;
    std::int32_t priority = 0;
    std::int32_t basePriority = 0;
    uint32_t threadState = 0;
    uint32_t waitReason = 0;
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
    std::array<RedXeNativeLogicalCpu, kRedXeMaximumLogicalProcessors> logical{};
};

struct RedXeNativeWalkSample final
{
    bool valid = false;
    bool processesTruncated = false;
    bool threadsTruncated = false;
    uint32_t processCount = 0;
    uint32_t threadCount = 0;
    std::array<RedXeNativeProcessRow, kRedXeMaximumNativeProcesses> processes{};
    std::array<RedXeNativeThreadRow, kRedXeMaximumNativeThreads> threads{};
};

struct RedXeNativeState final
{
    std::array<std::byte, kRedXeNtProcessBufferBytes> processBuffer{};
    std::array<std::byte, kRedXeNtBasicProcessBufferBytes> basicProcessBuffer{};
    std::array<std::byte, 64U * 1024U> topologyBuffer{};
};

[[nodiscard]] bool RedXeNativeQueryCheap(RedXeNativeState& state, RedXeNativeCheapSample& sample) noexcept;
[[nodiscard]] bool RedXeNativeQueryWalk(RedXeNativeState& state, RedXeNativeWalkSample& sample) noexcept;
