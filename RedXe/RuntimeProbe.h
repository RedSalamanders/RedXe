#pragma once

#include "../Common/ProcessResources.h"
#include "../Plugins/AVControl/AVControlDiagnostics.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <dxgi1_4.h>

struct RuntimeProbeGraphics final
{
    uint64_t generation = 0;
    HRESULT presentResult = E_UNEXPECTED, statisticsResult = E_UNEXPECTED;
    UINT presentCount = 0;
    DXGI_FRAME_STATISTICS statistics{};
    HRESULT localResult = E_NOINTERFACE, nonLocalResult = E_NOINTERFACE;
    DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonLocal{};
};

struct RuntimeProbeSample final
{
    uint64_t tick = 0, intervalMilliseconds = 0, sampleMicroseconds = 0;
    uint32_t page = 0;
    // Bits: continuous, visible, display on, suspended, occluded, dock hidden, navigation, overlay motion.
    uint32_t state = 0;
    uint64_t scheduledDeadline = 0;
    ProcessResourceSnapshot process;
    RuntimeProbeGraphics graphics;
    // Bits: successful-present delta, DXGI frame-statistics deltas. A reset/failure invalidates the next delta too.
    uint32_t deltaValid = 0;
    UINT presents = 0, presentDelta = 0, refreshDelta = 0, syncRefreshDelta = 0;
    uint64_t syncQpcDelta = 0, syncGpuDelta = 0;
    HRESULT avResult = S_FALSE;
    AVControl::ProbeStatistics av;
};

// Exactly one bounded line, including extreme numeric values. Array order is versioned in the performance contract.
inline constexpr size_t kRuntimeProbeLineCapacity = 1024;
[[nodiscard]] uint32_t FormatRuntimeProbeLine(const RuntimeProbeSample& sample, char* line, size_t capacity) noexcept;

// Cumulative UI-thread staging measurements, opt-in only. A clear phase/CPU validity bit means unavailable.
enum class RuntimePageStagePhase : uint32_t
{
    Retirement,
    SettingsCopy,
    Providers,
    Layout,
    GpuSetup,
    Visibility,
    Complete,
    Count,
};
struct RuntimePageStageSample final
{
    uint64_t tick = 0, cpu100ns = 0;
    uint32_t fromPage = 0, page = UINT32_MAX;
    int32_t direction = 0;
    HRESULT result = E_UNEXPECTED;
    uint32_t phaseMask = 0, cpuValid = 0;
    std::array<uint64_t, static_cast<size_t>(RuntimePageStagePhase::Count)> wallMicroseconds{};
};
[[nodiscard]] uint32_t FormatRuntimePageStageLine(const RuntimePageStageSample& sample, char* line,
                                                  size_t capacity) noexcept;
class RuntimePageStageProbe final
{
  public:
    RuntimePageStageProbe(bool enabled, uint32_t fromPage, int32_t direction) noexcept;
    void Select(uint32_t page) noexcept;
    void Mark(RuntimePageStagePhase phase) noexcept;
    [[nodiscard]] RuntimePageStageSample Complete(HRESULT result) noexcept;
    [[nodiscard]] bool Enabled() const noexcept
    {
        return _enabled;
    }

  private:
    [[nodiscard]] static bool QueryCpu(uint64_t& cpu) noexcept;
    RuntimePageStageSample _sample;
    std::chrono::steady_clock::time_point _started{};
    uint64_t _cpuStarted = 0;
    bool _enabled = false, _cpuStartedValid = false;
};

class RuntimeProbe final
{
  public:
    void Start(uint32_t seconds, uint64_t now) noexcept
    {
        _interval = static_cast<uint64_t>(seconds) * 1000;
        _lastTick = now;
        _deadline = seconds != 0 ? now + _interval : 0;
        _previous = {};
    }
    [[nodiscard]] bool Enabled() const noexcept
    {
        return _interval != 0;
    }
    [[nodiscard]] bool Due(uint64_t now) const noexcept
    {
        return Enabled() && now >= _deadline;
    }
    [[nodiscard]] uint64_t Deadline() const noexcept
    {
        return _deadline;
    }
    // Called once per actual sample, never catches up missed intervals or invalidates a frame.
    void Complete(RuntimeProbeSample& sample, uint64_t now) noexcept;

  private:
    uint64_t _interval = 0, _deadline = 0, _lastTick = 0;
    RuntimeProbeGraphics _previous;
};

[[nodiscard]] inline DWORD RuntimeProbeWaitTimeout(uint64_t now, uint64_t scheduled, uint64_t probe) noexcept
{
    const uint64_t deadline = scheduled == 0 ? probe : (probe == 0 ? scheduled : (std::min)(scheduled, probe));
    if (deadline == 0)
        return INFINITE;
    return deadline <= now ? 0 : static_cast<DWORD>((std::min)(deadline - now, static_cast<uint64_t>(INFINITE - 1)));
}
