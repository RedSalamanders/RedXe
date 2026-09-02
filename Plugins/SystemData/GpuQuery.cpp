#include "GpuQuery.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <pdh.h>
#include <string_view>

namespace
{
constexpr UINT kKmtNodeMetadata = 25;
constexpr UINT kKmtPhysicalAdapterCount = 30;
constexpr UINT kKmtNodePerfData = 61;
constexpr UINT kKmtAdapterPerfData = 62;
constexpr UINT kKmtAdapterPerfDataCaps = 63;
constexpr uint32_t kMaxNodesPerAdapter = 64;
constexpr wchar_t kGpuEngineCounterPath[] = L"\\GPU Engine(*)\\Utilization Percentage";

using D3dkmtHandle = UINT;

struct D3dkmtAdapterInfo
{
    D3dkmtHandle hAdapter;
    LUID AdapterLuid;
    ULONG NumOfSources;
    BOOL PrecisePresentRegionsPreferred;
};

struct D3dkmtEnumAdapters2
{
    ULONG NumAdapters;
    D3dkmtAdapterInfo* Adapters;
};

struct D3dkmtCloseAdapter
{
    D3dkmtHandle hAdapter;
};

struct D3dkmtQueryAdapterInfo
{
    D3dkmtHandle hAdapter;
    UINT Type;
    void* PrivateDriverData;
    UINT PrivateDriverDataSize;
};

struct D3dkmtAdapterPerfData
{
    UINT32 PhysicalAdapterIndex;
    UINT64 MemoryFrequency;
    UINT64 MaxMemoryFrequency;
    UINT64 MaxMemoryFrequencyOc;
    UINT64 MemoryBandwidth;
    UINT64 PcieBandwidth;
    ULONG FanRpm;
    ULONG Power;
    ULONG Temperature;
    UCHAR PowerStateOverride;
};

struct D3dkmtAdapterPerfDataCaps
{
    UINT32 PhysicalAdapterIndex;
    UINT64 MaxMemoryBandwidth;
    UINT64 MaxPcieBandwidth;
    ULONG MaxFanRpm;
    ULONG TemperatureMax;
    ULONG TemperatureWarning;
};

struct D3dkmtNodePerfData
{
    UINT32 NodeOrdinal;
    UINT32 PhysicalAdapterIndex;
    UINT64 Frequency;
    UINT64 MaxFrequency;
    UINT64 MaxFrequencyOc;
    ULONG Voltage;
    ULONG VoltageMax;
    ULONG VoltageMaxOc;
    UINT64 MaxTransitionLatency;
};

using D3dkmtEnumAdapters2Fn = LONG(APIENTRY*)(D3dkmtEnumAdapters2*);
using D3dkmtCloseAdapterFn = LONG(APIENTRY*)(const D3dkmtCloseAdapter*);
using D3dkmtQueryAdapterInfoFn = LONG(APIENTRY*)(const D3dkmtQueryAdapterInfo*);

template <typename Function> [[nodiscard]] Function ResolveExport(HMODULE module, const char* name) noexcept
{
    Function function = nullptr;
    if (!module)
    {
        return function;
    }
    const FARPROC procedure = GetProcAddress(module, name);
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

[[nodiscard]] uint64_t LuidValue(const LUID& luid) noexcept
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(luid.HighPart)) << 32) | static_cast<uint64_t>(luid.LowPart);
}

void CopyWide(wchar_t* destination, size_t destinationCount, const wchar_t* source) noexcept
{
    if (!destination || destinationCount == 0)
    {
        return;
    }
    destination[0] = L'\0';
    if (!source)
    {
        return;
    }
    size_t index = 0;
    while (index + 1 < destinationCount && source[index] != L'\0')
    {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = L'\0';
}

[[nodiscard]] bool ConsumePrefix(std::wstring_view& rest, std::wstring_view prefix) noexcept
{
    if (rest.size() < prefix.size() || rest.substr(0, prefix.size()) != prefix)
    {
        return false;
    }
    rest.remove_prefix(prefix.size());
    return true;
}

[[nodiscard]] bool ConsumeUInt32(std::wstring_view& rest, uint32_t& value) noexcept
{
    if (rest.empty() || rest.front() < L'0' || rest.front() > L'9')
    {
        return false;
    }
    uint64_t acc = 0;
    while (!rest.empty() && rest.front() >= L'0' && rest.front() <= L'9')
    {
        acc = acc * 10 + static_cast<uint64_t>(rest.front() - L'0');
        if (acc > 0xFFFFFFFFull)
        {
            return false;
        }
        rest.remove_prefix(1);
    }
    value = static_cast<uint32_t>(acc);
    return true;
}

[[nodiscard]] bool ConsumeHex32(std::wstring_view& rest, uint32_t& value) noexcept
{
    if (rest.size() >= 2 && rest[0] == L'0' && (rest[1] == L'x' || rest[1] == L'X'))
    {
        rest.remove_prefix(2);
    }
    bool any = false;
    uint64_t acc = 0;
    while (!rest.empty())
    {
        const wchar_t character = rest.front();
        unsigned digit = 0;
        if (character >= L'0' && character <= L'9')
        {
            digit = static_cast<unsigned>(character - L'0');
        }
        else if (character >= L'a' && character <= L'f')
        {
            digit = static_cast<unsigned>(character - L'a' + 10);
        }
        else if (character >= L'A' && character <= L'F')
        {
            digit = static_cast<unsigned>(character - L'A' + 10);
        }
        else
        {
            break;
        }
        acc = (acc << 4) | digit;
        if (acc > 0xFFFFFFFFull)
        {
            return false;
        }
        rest.remove_prefix(1);
        any = true;
    }
    if (!any)
    {
        return false;
    }
    value = static_cast<uint32_t>(acc);
    return true;
}

[[nodiscard]] bool ParseGpuEngineInstance(std::wstring_view name, uint32_t& pid, uint64_t& luid,
                                          uint32_t& physicalAdapter, uint32_t& engine, wchar_t* engineType,
                                          size_t engineTypeCount) noexcept
{
    pid = 0;
    luid = 0;
    physicalAdapter = 0;
    engine = 0;
    if (engineType && engineTypeCount > 0)
    {
        engineType[0] = L'\0';
    }
    std::wstring_view rest = name;
    uint32_t high = 0;
    uint32_t low = 0;
    if (!ConsumePrefix(rest, L"pid_") || !ConsumeUInt32(rest, pid) || !ConsumePrefix(rest, L"_luid_") ||
        !ConsumeHex32(rest, high) || !ConsumePrefix(rest, L"_") || !ConsumeHex32(rest, low) ||
        !ConsumePrefix(rest, L"_phys_") || !ConsumeUInt32(rest, physicalAdapter) || !ConsumePrefix(rest, L"_eng_") ||
        !ConsumeUInt32(rest, engine) || !ConsumePrefix(rest, L"_engtype_"))
    {
        return false;
    }
    LUID parsed{};
    parsed.HighPart = static_cast<LONG>(high);
    parsed.LowPart = low;
    luid = LuidValue(parsed);
    if (engineType && engineTypeCount > 0 && !rest.empty())
    {
        CopyWide(engineType, engineTypeCount, rest.data());
    }
    return true;
}

void CloseKmtAdapters(RedXeGpuState& state) noexcept
{
    const auto closeAdapter = reinterpret_cast<D3dkmtCloseAdapterFn>(state.kmtCloseAdapter);
    if (!closeAdapter)
    {
        state.kmtCount = 0;
        return;
    }
    for (uint32_t index = 0; index < state.kmtCount; ++index)
    {
        if (state.kmtHandles[index] != 0)
        {
            D3dkmtCloseAdapter closing{state.kmtHandles[index]};
            (void)closeAdapter(&closing);
            state.kmtHandles[index] = 0;
        }
    }
    state.kmtCount = 0;
}

void ResolveKmt(RedXeGpuState& state) noexcept
{
    if (state.kmtResolved)
    {
        return;
    }
    state.kmtResolved = true;
    const HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
    state.kmtEnumAdapters2 =
        reinterpret_cast<void*>(ResolveExport<D3dkmtEnumAdapters2Fn>(gdi32, "D3DKMTEnumAdapters2"));
    state.kmtCloseAdapter = reinterpret_cast<void*>(ResolveExport<D3dkmtCloseAdapterFn>(gdi32, "D3DKMTCloseAdapter"));
    state.kmtQueryAdapterInfo =
        reinterpret_cast<void*>(ResolveExport<D3dkmtQueryAdapterInfoFn>(gdi32, "D3DKMTQueryAdapterInfo"));
}

void RefreshKmtAdapters(RedXeGpuState& state) noexcept
{
    ResolveKmt(state);
    CloseKmtAdapters(state);
    const auto enumAdapters2 = reinterpret_cast<D3dkmtEnumAdapters2Fn>(state.kmtEnumAdapters2);
    if (!enumAdapters2 || !state.kmtCloseAdapter || !state.kmtQueryAdapterInfo)
    {
        return;
    }
    std::array<D3dkmtAdapterInfo, kRedXeMaximumGpuAdapters> adapters{};
    D3dkmtEnumAdapters2 enumerate{};
    enumerate.NumAdapters = static_cast<ULONG>(kRedXeMaximumGpuAdapters);
    enumerate.Adapters = adapters.data();
    if (enumAdapters2(&enumerate) < 0)
    {
        return;
    }
    const uint32_t count = enumerate.NumAdapters > kRedXeMaximumGpuAdapters
                               ? static_cast<uint32_t>(kRedXeMaximumGpuAdapters)
                               : enumerate.NumAdapters;
    for (uint32_t index = 0; index < count; ++index)
    {
        state.kmtHandles[index] = adapters[index].hAdapter;
        state.kmtLuids[index] = LuidValue(adapters[index].AdapterLuid);
        adapters[index].hAdapter = 0;
    }
    state.kmtCount = count;
}

[[nodiscard]] D3dkmtHandle FindKmtHandle(const RedXeGpuState& state, uint64_t luid) noexcept
{
    for (uint32_t index = 0; index < state.kmtCount; ++index)
    {
        if (state.kmtLuids[index] == luid)
        {
            return state.kmtHandles[index];
        }
    }
    return 0;
}

void EnsurePdh(RedXeGpuState& state) noexcept
{
    if (state.pdhReady)
    {
        return;
    }
    PDH_HQUERY query = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS || !query)
    {
        return;
    }
    PDH_HCOUNTER counter = nullptr;
    if (PdhAddEnglishCounterW(query, kGpuEngineCounterPath, 0, &counter) != ERROR_SUCCESS || !counter)
    {
        (void)PdhCloseQuery(query);
        return;
    }
    state.pdhQuery = query;
    state.pdhGpuEngineCounter = counter;
    state.pdhReady = true;
}
} // namespace

RedXeGpuState::~RedXeGpuState() noexcept
{
    CloseKmtAdapters(*this);
    if (pdhQuery)
    {
        (void)PdhCloseQuery(pdhQuery);
        pdhQuery = nullptr;
        pdhGpuEngineCounter = nullptr;
        pdhReady = false;
    }
}

void RedXeGpuSampleAdapters(RedXeGpuState& state) noexcept
{
    if (!state.dxgiFactory)
    {
        (void)CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(state.dxgiFactory.put()));
    }
    RefreshKmtAdapters(state);
    state.adapterCount = 0;
    if (!state.dxgiFactory)
    {
        return;
    }
    for (UINT index = 0; index < kRedXeMaximumGpuAdapters; ++index)
    {
        wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
        const HRESULT enumerated = state.dxgiFactory->EnumAdapters1(index, adapter.put());
        if (enumerated == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        if (FAILED(enumerated) || !adapter)
        {
            break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)))
        {
            continue;
        }
        RedXeGpuAdapterRow& row = state.adapters[state.adapterCount];
        row = {};
        row.adapterLuid = LuidValue(description.AdapterLuid);
        CopyWide(row.displayName, std::size(row.displayName), description.Description);
        row.vendorId = description.VendorId;
        row.deviceId = description.DeviceId;
        row.software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ? 1 : 0;
        row.dedicatedBytes = description.DedicatedVideoMemory;
        row.sharedBytes = description.SharedSystemMemory;
        row.hasDxgi = true;

        const D3dkmtHandle handle = FindKmtHandle(state, row.adapterLuid);
        const auto queryAdapterInfo = reinterpret_cast<D3dkmtQueryAdapterInfoFn>(state.kmtQueryAdapterInfo);
        if (handle != 0 && queryAdapterInfo)
        {
            D3dkmtAdapterPerfData perf{};
            D3dkmtQueryAdapterInfo query{};
            query.hAdapter = handle;
            query.Type = kKmtAdapterPerfData;
            query.PrivateDriverData = &perf;
            query.PrivateDriverDataSize = static_cast<UINT>(sizeof(perf));
            if (queryAdapterInfo(&query) >= 0)
            {
                row.hasPerf = true;
                row.memoryClockHz = perf.MemoryFrequency;
                row.maxMemoryClockHz = perf.MaxMemoryFrequency;
                row.powerPercent = static_cast<double>(perf.Power) / 10.0;
                row.temperatureC = static_cast<double>(perf.Temperature) / 10.0;
                row.hasTemperature = true;
                row.fanRpm = perf.FanRpm;
            }
            D3dkmtAdapterPerfDataCaps caps{};
            query.Type = kKmtAdapterPerfDataCaps;
            query.PrivateDriverData = &caps;
            query.PrivateDriverDataSize = static_cast<UINT>(sizeof(caps));
            if (queryAdapterInfo(&query) >= 0)
            {
                row.maxFanRpm = caps.MaxFanRpm;
                row.warningTemperatureC = static_cast<double>(caps.TemperatureWarning) / 10.0;
                row.maxTemperatureC = static_cast<double>(caps.TemperatureMax) / 10.0;
                row.hasFan = caps.MaxFanRpm != 0;
                if (!row.hasTemperature && caps.TemperatureMax != 0)
                {
                    row.hasTemperature = true;
                }
            }
        }
        ++state.adapterCount;
    }
    std::sort(state.adapters.begin(), state.adapters.begin() + state.adapterCount,
              [](const RedXeGpuAdapterRow& left, const RedXeGpuAdapterRow& right)
              { return left.adapterLuid < right.adapterLuid; });
}

void RedXeGpuSampleEngines(RedXeGpuState& state) noexcept
{
    if (state.kmtCount == 0)
    {
        RefreshKmtAdapters(state);
    }
    const auto queryAdapterInfo = reinterpret_cast<D3dkmtQueryAdapterInfoFn>(state.kmtQueryAdapterInfo);
    state.engineCount = 0;
    state.enginesTruncated = false;
    if (!queryAdapterInfo)
    {
        return;
    }
    for (uint32_t adapterIndex = 0; adapterIndex < state.kmtCount; ++adapterIndex)
    {
        const D3dkmtHandle handle = state.kmtHandles[adapterIndex];
        const uint64_t luid = state.kmtLuids[adapterIndex];
        UINT32 physicalCount = 1;
        D3dkmtQueryAdapterInfo query{};
        query.hAdapter = handle;
        query.Type = kKmtPhysicalAdapterCount;
        query.PrivateDriverData = &physicalCount;
        query.PrivateDriverDataSize = static_cast<UINT>(sizeof(physicalCount));
        if (queryAdapterInfo(&query) < 0 || physicalCount == 0)
        {
            physicalCount = 1;
        }
        physicalCount = (std::min)(physicalCount, 8u);
        for (UINT32 physical = 0; physical < physicalCount; ++physical)
        {
            for (UINT32 node = 0; node < kMaxNodesPerAdapter; ++node)
            {
                if (state.engineCount >= kRedXeMaximumGpuEngines)
                {
                    state.enginesTruncated = true;
                    return;
                }
                std::array<std::byte, 256> metadataBuffer{};
                auto* ordinal = reinterpret_cast<UINT*>(metadataBuffer.data());
                *ordinal = (physical << 16) | node;
                query.Type = kKmtNodeMetadata;
                query.PrivateDriverData = metadataBuffer.data();
                query.PrivateDriverDataSize = static_cast<UINT>(metadataBuffer.size());
                const bool hasMetadata = queryAdapterInfo(&query) >= 0;
                D3dkmtNodePerfData perf{};
                perf.NodeOrdinal = node;
                perf.PhysicalAdapterIndex = physical;
                query.Type = kKmtNodePerfData;
                query.PrivateDriverData = &perf;
                query.PrivateDriverDataSize = static_cast<UINT>(sizeof(perf));
                const bool hasPerf = queryAdapterInfo(&query) >= 0;
                if (!hasMetadata && !hasPerf)
                {
                    break;
                }
                RedXeGpuEngineRow& row = state.engines[state.engineCount];
                row = {};
                row.adapterLuid = luid;
                row.physicalAdapterIndex = physical;
                row.nodeOrdinal = node;
                if (hasMetadata)
                {
                    row.engineClass = *reinterpret_cast<UINT*>(metadataBuffer.data() + sizeof(UINT));
                    CopyWide(row.friendlyName, std::size(row.friendlyName),
                             reinterpret_cast<const wchar_t*>(metadataBuffer.data() + sizeof(UINT) * 2));
                    row.hasClass = true;
                }
                if (hasPerf)
                {
                    row.currentFrequencyHz = perf.Frequency;
                    row.maxFrequencyHz = perf.MaxFrequency;
                    row.voltageMv = perf.Voltage;
                    row.hasFrequency = perf.Frequency != 0 || perf.MaxFrequency != 0;
                }
                ++state.engineCount;
            }
        }
    }
}

void RedXeGpuSampleProcesses(RedXeGpuState& state) noexcept
{
    EnsurePdh(state);
    state.processCount = 0;
    state.processesTruncated = false;
    if (!state.pdhReady || !state.pdhQuery || !state.pdhGpuEngineCounter)
    {
        return;
    }
    if (PdhCollectQueryData(state.pdhQuery) != ERROR_SUCCESS)
    {
        return;
    }
    if (!state.pdhHasPriorCollect)
    {
        state.pdhHasPriorCollect = true;
        return;
    }
    DWORD bufferBytes = static_cast<DWORD>(state.pdhArray.size());
    DWORD itemCount = 0;
    const PDH_STATUS status =
        PdhGetFormattedCounterArrayW(state.pdhGpuEngineCounter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bufferBytes,
                                     &itemCount, reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(state.pdhArray.data()));
    if (status != ERROR_SUCCESS)
    {
        return;
    }
    const auto* items = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(state.pdhArray.data());
    for (DWORD index = 0; index < itemCount; ++index)
    {
        if (!items[index].szName)
        {
            continue;
        }
        const std::wstring_view name{items[index].szName};
        if (name.empty())
        {
            continue;
        }
        constexpr wchar_t kTotal[] = L"_total";
        bool isTotal = name.size() == 6;
        for (size_t charIndex = 0; isTotal && charIndex < 6; ++charIndex)
        {
            wchar_t character = name[charIndex];
            if (character >= L'A' && character <= L'Z')
            {
                character = static_cast<wchar_t>(character - L'A' + L'a');
            }
            if (character != kTotal[charIndex])
            {
                isTotal = false;
            }
        }
        if (isTotal)
        {
            continue;
        }
        if (state.processCount >= kRedXeMaximumGpuProcesses)
        {
            state.processesTruncated = true;
            break;
        }
        RedXeGpuProcessRow& row = state.processes[state.processCount];
        row = {};
        uint32_t physical = 0;
        uint32_t engine = 0;
        if (!ParseGpuEngineInstance(name, row.processId, row.adapterLuid, physical, engine, row.engineType,
                                    std::size(row.engineType)))
        {
            continue;
        }
        row.physicalAdapterIndex = physical;
        row.engineOrdinal = engine;
        if (items[index].FmtValue.CStatus == ERROR_SUCCESS)
        {
            row.utilizationPercent = std::clamp(items[index].FmtValue.doubleValue, 0.0, 100.0);
            row.hasUtilization = true;
        }
        ++state.processCount;
    }
}
