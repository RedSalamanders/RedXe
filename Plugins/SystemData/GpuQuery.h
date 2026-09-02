#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

#include <dxgi.h>

constexpr size_t kRedXeMaximumGpuAdapters = 32;
constexpr size_t kRedXeMaximumGpuEngines = 512;
constexpr size_t kRedXeMaximumGpuProcesses = 2048;
constexpr size_t kRedXeGpuNameCharacters = 128;
constexpr size_t kRedXeGpuEngineTypeCharacters = 64;
constexpr size_t kRedXePdhGpuArrayBytes = 1024U * 1024U;

struct RedXeGpuAdapterRow final
{
    uint64_t adapterLuid = 0;
    wchar_t displayName[kRedXeGpuNameCharacters]{};
    uint64_t vendorId = 0;
    uint64_t deviceId = 0;
    uint64_t software = 0;
    uint64_t integrated = 0;
    uint64_t dedicatedBytes = 0;
    uint64_t sharedBytes = 0;
    uint64_t memoryClockHz = 0;
    uint64_t maxMemoryClockHz = 0;
    double powerPercent = 0.0;
    double temperatureC = 0.0;
    double warningTemperatureC = 0.0;
    double maxTemperatureC = 0.0;
    uint64_t fanRpm = 0;
    uint64_t maxFanRpm = 0;
    bool hasDxgi = false;
    bool hasIntegrated = false;
    bool hasPerf = false;
    bool hasFan = false;
    bool hasTemperature = false;
};

struct RedXeGpuEngineRow final
{
    uint64_t adapterLuid = 0;
    uint64_t physicalAdapterIndex = 0;
    uint64_t nodeOrdinal = 0;
    uint64_t engineClass = 0;
    wchar_t friendlyName[kRedXeGpuEngineTypeCharacters]{};
    uint64_t currentFrequencyHz = 0;
    uint64_t maxFrequencyHz = 0;
    uint64_t voltageMv = 0;
    bool hasClass = false;
    bool hasFrequency = false;
};

struct RedXeGpuProcessRow final
{
    uint32_t processId = 0;
    uint64_t adapterLuid = 0;
    uint64_t physicalAdapterIndex = 0;
    uint64_t engineOrdinal = 0;
    wchar_t engineType[kRedXeGpuEngineTypeCharacters]{};
    double utilizationPercent = 0.0;
    bool hasUtilization = false;
};

struct RedXeGpuState final
{
    RedXeGpuState() noexcept = default;
    ~RedXeGpuState() noexcept;
    RedXeGpuState(const RedXeGpuState&) = delete;
    RedXeGpuState& operator=(const RedXeGpuState&) = delete;

    wil::com_ptr_nothrow<IDXGIFactory1> dxgiFactory;
    HANDLE pdhQuery = nullptr;
    HANDLE pdhGpuEngineCounter = nullptr;
    bool pdhReady = false;
    bool pdhHasPriorCollect = false;
    bool kmtResolved = false;
    void* kmtEnumAdapters2 = nullptr;
    void* kmtCloseAdapter = nullptr;
    void* kmtQueryAdapterInfo = nullptr;
    std::array<UINT, kRedXeMaximumGpuAdapters> kmtHandles{};
    std::array<uint64_t, kRedXeMaximumGpuAdapters> kmtLuids{};
    uint32_t kmtCount = 0;
    std::array<std::byte, kRedXePdhGpuArrayBytes> pdhArray{};

    std::array<RedXeGpuAdapterRow, kRedXeMaximumGpuAdapters> adapters{};
    uint32_t adapterCount = 0;
    std::array<RedXeGpuEngineRow, kRedXeMaximumGpuEngines> engines{};
    uint32_t engineCount = 0;
    bool enginesTruncated = false;
    std::array<RedXeGpuProcessRow, kRedXeMaximumGpuProcesses> processes{};
    uint32_t processCount = 0;
    bool processesTruncated = false;
};

void RedXeGpuSampleAdapters(RedXeGpuState& state) noexcept;
void RedXeGpuSampleEngines(RedXeGpuState& state) noexcept;
void RedXeGpuSampleProcesses(RedXeGpuState& state) noexcept;
