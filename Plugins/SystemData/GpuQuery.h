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
constexpr size_t kRedXeGpuEngineTypeCharacters = 32;
constexpr size_t kRedXePdhGpuArrayBytes = 1024U * 1024U;

// Device class for one dxgkrnl adapter. The graphics datasets publish every class except NPU; the accelerator
// datasets publish only NPU. Precedence when the sources disagree is DXCore hardware type, then the D3DKMT adapter
// type bits, then DXGI presence — never an inference drawn from missing evidence.
constexpr uint64_t kRedXeDeviceClassUnknown = 0;
constexpr uint64_t kRedXeDeviceClassGpu = 1;
constexpr uint64_t kRedXeDeviceClassNpu = 2;
constexpr uint64_t kRedXeDeviceClassComputeAccelerator = 3;
constexpr uint64_t kRedXeDeviceClassMediaAccelerator = 4;
constexpr uint64_t kRedXeDeviceClassSoftware = 5;

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
    uint64_t deviceClass = kRedXeDeviceClassUnknown;
    uint64_t computeOnly = 0;
    uint64_t physicalAdapterCount = 0;
    uint64_t engineCount = 0;
    uint64_t usedDedicatedBytes = 0;
    uint64_t usedSharedBytes = 0;
    double utilizationPercent = 0.0;
    bool hasDxgi = false;
    bool hasIntegrated = false;
    bool hasPerf = false;
    bool hasFan = false;
    bool hasTemperature = false;
    bool hasAdapterType = false;
    bool hasDeviceClass = false;
    bool hasEngineCount = false;
    bool hasMemoryUsage = false;
    bool hasUtilization = false;
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
    double utilizationPercent = 0.0;
    bool hasClass = false;
    bool hasFrequency = false;
    bool hasUtilization = false;
};

// One previous DXCore engine running-time reading, used to turn a cumulative microsecond counter into a busy
// percentage. Keyed by adapter LUID plus physical-adapter and engine index, so a changed adapter set is simply a
// miss rather than a wrong rate.
struct RedXeAcceleratorEngineSample final
{
    uint64_t adapterLuid = 0;
    uint32_t physicalAdapterIndex = 0;
    uint32_t engineIndex = 0;
    uint64_t runningTimeMicroseconds = 0;
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
    // DXCore is the only enumerator that sees a compute-only MCDM adapter as a first-class device and reports its
    // hardware type. Resolved lazily through the module so an older host simply loses the labelling instead of
    // failing to load the plugin.
    HMODULE dxcoreModule = nullptr;
    IUnknown* dxcoreFactory = nullptr;
    bool dxcoreResolved = false;
    // One IDXCoreAdapter1 per enumerated adapter, held across samples so the per-second engine and memory queries do
    // not re-enumerate. Rebuilt when the adapter set changes.
    std::array<IUnknown*, kRedXeMaximumGpuAdapters> dxcoreAdapters{};
    std::array<uint64_t, kRedXeMaximumGpuAdapters> dxcoreLuids{};
    std::array<uint64_t, kRedXeMaximumGpuAdapters> dxcoreClasses{};
    uint32_t dxcoreCount = 0;
    std::array<RedXeAcceleratorEngineSample, kRedXeMaximumGpuEngines> previousEngines{};
    uint32_t previousEngineCount = 0;
    uint64_t previousEngineTimestamp100ns = 0;
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

// Device class of the adapter owning `luid`, for splitting the sampled rows between the graphics and accelerator
// datasets. Returns kRedXeDeviceClassUnknown when the LUID is not in the current sample.
[[nodiscard]] uint64_t RedXeGpuDeviceClassForLuid(const RedXeGpuState& state, uint64_t luid) noexcept;
