#pragma once

// System Data faces: one host data-sink subscribed to builtin.system-data (cpu.summary, memory.summary,
// gpu.adapter) at one second. Snapshots are reduced to three rounded percentages on the acquisition worker and
// handed to the service, which wakes its lane; nothing is rendered here. Subscriptions are created, activated, and
// released on the UI thread only (Start, ApplySettings, Stop of the service).

#include "PlugInterfaces/Data.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Host.h"

#include <array>
#include <cstdint>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace Logicon
{
inline constexpr char kSystemDataProviderId[] = "builtin.system-data";
inline constexpr uint32_t kSystemDataIntervalMilliseconds = 1000;

// Rounded percentages; -1 while unknown.
struct SystemValues final
{
    int32_t cpuPercent = -1;
    int32_t memoryPercent = -1;
    int32_t gpuPercent = -1;

    [[nodiscard]] bool operator==(const SystemValues& other) const noexcept
    {
        return cpuPercent == other.cpuPercent && memoryPercent == other.memoryPercent && gpuPercent == other.gpuPercent;
    }
};

// Receives the reduced values on the acquisition worker; must copy and return.
class SystemValuesListener
{
  public:
    virtual void OnSystemValues(const SystemValues& values) noexcept = 0;

  protected:
    ~SystemValuesListener() = default;
};

class SystemDataFeed final : public RedXeComObject<SystemDataFeed, IRedXeDataSink>
{
  public:
    explicit SystemDataFeed(SystemValuesListener& listener) noexcept;
    ~SystemDataFeed();

    // UI thread. Resolves the provider, the three data sets, and their columns, and subscribes inactive. Fails
    // when the provider or a data set is missing; Running() is then false and faces show "--".
    [[nodiscard]] HRESULT Start(IRedXeHost* host) noexcept;
    // UI thread. Activates or pauses collection; a paused feed keeps its last values.
    void SetActive(bool active) noexcept;
    // UI thread. Deactivates and releases every subscription, draining callbacks, then the provider.
    void Stop() noexcept;
    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] bool Active() const noexcept;

    // IRedXeDataSink (acquisition worker).
    HRESULT STDMETHODCALLTYPE OnDataSnapshot(const RedXeDataSnapshot* snapshot) noexcept override;

    // Reduces one snapshot into values; exposed for tests. Unknown data sets leave values untouched.
    static void Reduce(const RedXeDataSnapshot& snapshot, const std::array<int32_t, 8>& columns,
                       SystemValues& values) noexcept;

  private:
    enum Column : uint32_t
    {
        CpuTotal = 0,
        MemoryTotal,
        MemoryUsed,
        GpuUtilization,
        GpuSoftware,
        GpuIntegrated,
        ColumnCount,
    };

    SystemValuesListener& _listener;
    wil::com_ptr_nothrow<IRedXeDataProvider> _provider;
    std::array<wil::com_ptr_nothrow<IRedXeDataSubscription>, 3> _subscriptions{};
    // Column indexes resolved from the provider's descriptors; -1 = absent.
    std::array<int32_t, 8> _columns{};
    SRWLOCK _lock = SRWLOCK_INIT;
    SystemValues _values{};
    bool _active = false;
};
} // namespace Logicon
