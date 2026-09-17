#include "LogiconSystemData.h"

#include <cmath>
#include <cstring>
#include <new>

namespace Logicon
{
namespace
{
constexpr char kCpuDataSetId[] = "cpu.summary";
constexpr char kMemoryDataSetId[] = "memory.summary";
constexpr char kGpuDataSetId[] = "gpu.adapter";

struct ColumnLookup final
{
    const char* dataSetId;
    const char* columnId;
};

// One entry per SystemDataFeed::Column, in order.
constexpr ColumnLookup kLookups[] = {
    {kCpuDataSetId, "totalPercent"},
    {kMemoryDataSetId, "totalPhysicalBytes"},
    {kMemoryDataSetId, "usedPhysicalBytes"},
    {kGpuDataSetId, "utilizationPercent"},
    {kGpuDataSetId, "software"},
    {kGpuDataSetId, "integrated"},
};

[[nodiscard]] bool SameId(const char* left, const char* right) noexcept
{
    return left && right && std::strcmp(left, right) == 0;
}

[[nodiscard]] const RedXeDataValue* ValueAt(const RedXeDataRow& row, int32_t column) noexcept
{
    if (column < 0 || !row.values || static_cast<uint32_t>(column) >= row.valueCount)
    {
        return nullptr;
    }
    const RedXeDataValue& value = row.values[column];
    return value.sizeBytes == sizeof(RedXeDataValue) && value.quality == RedXeDataQualityGood ? &value : nullptr;
}

[[nodiscard]] bool ReadNumber(const RedXeDataValue* value, double& number) noexcept
{
    if (!value)
    {
        return false;
    }
    if (value->valueType == RedXeDataValueTypeFloat64)
    {
        number = value->float64Value;
        return std::isfinite(number);
    }
    if (value->valueType == RedXeDataValueTypeUInt64)
    {
        number = static_cast<double>(value->uint64Value);
        return true;
    }
    return false;
}

[[nodiscard]] int32_t RoundPercent(double value) noexcept
{
    if (!std::isfinite(value))
    {
        return -1;
    }
    const double clamped = value < 0.0 ? 0.0 : value > 100.0 ? 100.0 : value;
    return static_cast<int32_t>(clamped + 0.5);
}
} // namespace

SystemDataFeed::SystemDataFeed(SystemValuesListener& listener) noexcept : _listener(listener)
{
    _columns.fill(-1);
}

SystemDataFeed::~SystemDataFeed()
{
    Stop();
}

HRESULT SystemDataFeed::Start(IRedXeHost* host) noexcept
{
    if (!host)
    {
        return E_POINTER;
    }
    if (_provider)
    {
        return S_OK;
    }
    wil::com_ptr_nothrow<IRedXeDataProvider> provider;
    HRESULT result = host->GetDataProvider(kSystemDataProviderId, provider.put());
    if (FAILED(result))
    {
        return result;
    }
    if (!provider)
    {
        return E_UNEXPECTED;
    }
    const RedXeDataSetDescriptor* descriptors = nullptr;
    uint32_t count = 0;
    result = provider->GetDataSets(&descriptors, &count);
    if (FAILED(result))
    {
        return result;
    }
    _columns.fill(-1);
    for (uint32_t index = 0; index < count && descriptors; ++index)
    {
        const RedXeDataSetDescriptor& descriptor = descriptors[index];
        if (descriptor.sizeBytes != sizeof(RedXeDataSetDescriptor) || !descriptor.columns)
        {
            continue;
        }
        for (uint32_t lookup = 0; lookup < ColumnCount; ++lookup)
        {
            if (!SameId(descriptor.dataSetId, kLookups[lookup].dataSetId))
            {
                continue;
            }
            for (uint32_t column = 0; column < descriptor.columnCount; ++column)
            {
                if (SameId(descriptor.columns[column].columnId, kLookups[lookup].columnId))
                {
                    _columns[lookup] = static_cast<int32_t>(column);
                    break;
                }
            }
        }
    }
    if (_columns[CpuTotal] < 0 || _columns[MemoryTotal] < 0 || _columns[MemoryUsed] < 0 || _columns[GpuUtilization] < 0)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    const char* dataSetIds[] = {kCpuDataSetId, kMemoryDataSetId, kGpuDataSetId};
    for (uint32_t index = 0; index < _subscriptions.size(); ++index)
    {
        RedXeDataSubscriptionOptions options{};
        options.sizeBytes = sizeof(options);
        options.dataSetId = dataSetIds[index];
        options.requestedIntervalMilliseconds = kSystemDataIntervalMilliseconds;
        result = provider->Subscribe(&options, static_cast<IRedXeDataSink*>(this), _subscriptions[index].put());
        if (FAILED(result))
        {
            for (auto& subscription : _subscriptions)
            {
                subscription.reset();
            }
            return result;
        }
    }
    _provider = std::move(provider);
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        _values = SystemValues{};
    }
    return S_OK;
}

void SystemDataFeed::SetActive(bool active) noexcept
{
    if (!_provider || _active == active)
    {
        return;
    }
    for (auto& subscription : _subscriptions)
    {
        if (subscription)
        {
            (void)subscription->SetActive(active ? TRUE : FALSE);
        }
    }
    _active = active;
}

void SystemDataFeed::Stop() noexcept
{
    SetActive(false);
    for (auto& subscription : _subscriptions)
    {
        subscription.reset();
    }
    _provider.reset();
    _columns.fill(-1);
}

bool SystemDataFeed::Running() const noexcept
{
    return _provider != nullptr;
}

bool SystemDataFeed::Active() const noexcept
{
    return _active;
}

void SystemDataFeed::Reduce(const RedXeDataSnapshot& snapshot, const std::array<int32_t, 8>& columns,
                            SystemValues& values) noexcept
{
    if (snapshot.sizeBytes != sizeof(RedXeDataSnapshot) || !snapshot.dataSetId || !snapshot.rows ||
        snapshot.rowCount == 0)
    {
        return;
    }
    if (SameId(snapshot.dataSetId, kCpuDataSetId))
    {
        double total = 0.0;
        values.cpuPercent = ReadNumber(ValueAt(snapshot.rows[0], columns[CpuTotal]), total) ? RoundPercent(total) : -1;
        return;
    }
    if (SameId(snapshot.dataSetId, kMemoryDataSetId))
    {
        double total = 0.0;
        double used = 0.0;
        values.memoryPercent = ReadNumber(ValueAt(snapshot.rows[0], columns[MemoryTotal]), total) &&
                                       ReadNumber(ValueAt(snapshot.rows[0], columns[MemoryUsed]), used) && total > 0.0
                                   ? RoundPercent(used * 100.0 / total)
                                   : -1;
        return;
    }
    if (SameId(snapshot.dataSetId, kGpuDataSetId))
    {
        // Prefer a discrete hardware adapter, then any hardware adapter, then the first row.
        const RedXeDataRow* chosen = nullptr;
        int32_t chosenRank = 0;
        for (uint32_t index = 0; index < snapshot.rowCount; ++index)
        {
            const RedXeDataRow& row = snapshot.rows[index];
            double software = 0.0;
            double integrated = 0.0;
            const bool hardware = ReadNumber(ValueAt(row, columns[GpuSoftware]), software) && software == 0.0;
            const bool discrete =
                hardware && ReadNumber(ValueAt(row, columns[GpuIntegrated]), integrated) && integrated == 0.0;
            const int32_t rank = discrete ? 3 : hardware ? 2 : 1;
            if (!chosen || rank > chosenRank)
            {
                chosen = &row;
                chosenRank = rank;
            }
        }
        double utilization = 0.0;
        values.gpuPercent = chosen && ReadNumber(ValueAt(*chosen, columns[GpuUtilization]), utilization)
                                ? RoundPercent(utilization)
                                : -1;
    }
}

HRESULT SystemDataFeed::OnDataSnapshot(const RedXeDataSnapshot* snapshot) noexcept
{
    if (!snapshot)
    {
        return E_POINTER;
    }
    SystemValues updated{};
    {
        const auto guard = wil::AcquireSRWLockExclusive(&_lock);
        updated = _values;
        Reduce(*snapshot, _columns, updated);
        if (updated == _values)
        {
            return S_OK;
        }
        _values = updated;
    }
    _listener.OnSystemValues(updated);
    return S_OK;
}
} // namespace Logicon
