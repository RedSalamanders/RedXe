#include "GpuQuery.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <d3dkmthk.h>
#include <pdh.h>
#include <string_view>

// dxcore.dll is bound at runtime rather than linked, so dxcore.lib is not available to define the adapter attribute
// GUIDs that dxcore_interface.h only declares. INITGUID must precede this include so those declarations emit
// definitions in this translation unit; it is placed here, out of alphabetical order, for exactly that reason.
#include <initguid.h>

#include <dxcore.h>

namespace
{
constexpr UINT kKmtAdapterType = 15;
constexpr UINT kKmtNodeMetadata = 25;
constexpr UINT kKmtPhysicalAdapterCount = 30;
constexpr UINT kKmtNodePerfData = 61;
constexpr UINT kKmtAdapterPerfData = 62;
constexpr UINT kKmtAdapterPerfDataCaps = 63;
static_assert(kKmtAdapterType == KMTQAITYPE_ADAPTERTYPE);
static_assert(kKmtNodeMetadata == KMTQAITYPE_NODEMETADATA);
static_assert(kKmtPhysicalAdapterCount == KMTQAITYPE_PHYSICALADAPTERCOUNT);
static_assert(kKmtAdapterPerfData == KMTQAITYPE_ADAPTERPERFDATA);
static_assert(kKmtAdapterPerfDataCaps == KMTQAITYPE_ADAPTERPERFDATA_CAPS);
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
    if (dxcoreFactory)
    {
        dxcoreFactory->Release();
        dxcoreFactory = nullptr;
    }
    if (dxcoreModule)
    {
        (void)FreeLibrary(dxcoreModule);
        dxcoreModule = nullptr;
    }
}

// Enriches an already-built row from the DXGI adapter with the same LUID, when one exists. A compute-only adapter
// has no DXGI entry at all, which is normal rather than an error, so a miss simply leaves the DXGI fields unset.
void EnrichFromDxgi(RedXeGpuState& state, RedXeGpuAdapterRow& row) noexcept
{
    if (!state.dxgiFactory)
    {
        return;
    }
    for (UINT index = 0; index < kRedXeMaximumGpuAdapters; ++index)
    {
        wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
        const HRESULT enumerated = state.dxgiFactory->EnumAdapters1(index, adapter.put());
        if (enumerated == DXGI_ERROR_NOT_FOUND || FAILED(enumerated) || !adapter)
        {
            return;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) || LuidValue(description.AdapterLuid) != row.adapterLuid)
        {
            continue;
        }
        CopyWide(row.displayName, std::size(row.displayName), description.Description);
        row.vendorId = description.VendorId;
        row.deviceId = description.DeviceId;
        row.software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ? 1 : 0;
        row.dedicatedBytes = description.DedicatedVideoMemory;
        row.sharedBytes = description.SharedSystemMemory;
        row.hasDxgi = true;
        return;
    }
}

void ResolveDxcore(RedXeGpuState& state) noexcept
{
    if (state.dxcoreResolved)
    {
        return;
    }
    state.dxcoreResolved = true;
    state.dxcoreModule = LoadLibraryExW(L"dxcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!state.dxcoreModule)
    {
        return;
    }
    using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    const auto create = ResolveExport<CreateFactoryFn>(state.dxcoreModule, "DXCoreCreateAdapterFactory");
    if (!create)
    {
        return;
    }
    IDXCoreAdapterFactory* factory = nullptr;
    if (SUCCEEDED(create(__uuidof(IDXCoreAdapterFactory), reinterpret_cast<void**>(&factory))) && factory)
    {
        state.dxcoreFactory = factory;
    }
}

void ReleaseDxcoreAdapters(RedXeGpuState& state) noexcept
{
    for (uint32_t index = 0; index < state.dxcoreCount; ++index)
    {
        if (state.dxcoreAdapters[index])
        {
            state.dxcoreAdapters[index]->Release();
            state.dxcoreAdapters[index] = nullptr;
        }
    }
    state.dxcoreCount = 0;
}

// Builds the DXCore side of the join: one IDXCoreAdapter1 per enumerated device, tagged with the runtime-agnostic
// hardware type. Microsoft documents that exactly one hardware-type attribute is reported by the driver or inferred
// by DXCore, which is the labelling Task Manager itself relies on. Adapter lists are per-attribute, so this walks
// the four types and records the first that claims each LUID.
void RefreshDxcoreAdapters(RedXeGpuState& state) noexcept
{
    ResolveDxcore(state);
    ReleaseDxcoreAdapters(state);
    if (!state.dxcoreFactory)
    {
        return;
    }
    auto* factory = static_cast<IDXCoreAdapterFactory*>(state.dxcoreFactory);
    struct ClassAttribute final
    {
        const GUID* attribute;
        uint64_t deviceClass;
    };
    const ClassAttribute attributes[]{
        {&DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU, kRedXeDeviceClassNpu},
        {&DXCORE_HARDWARE_TYPE_ATTRIBUTE_GPU, kRedXeDeviceClassGpu},
        {&DXCORE_HARDWARE_TYPE_ATTRIBUTE_COMPUTE_ACCELERATOR, kRedXeDeviceClassComputeAccelerator},
        {&DXCORE_HARDWARE_TYPE_ATTRIBUTE_MEDIA_ACCELERATOR, kRedXeDeviceClassMediaAccelerator},
    };
    for (const ClassAttribute& entry : attributes)
    {
        wil::com_ptr_nothrow<IDXCoreAdapterList> list;
        if (FAILED(factory->CreateAdapterList(1, entry.attribute, __uuidof(IDXCoreAdapterList), list.put_void())) ||
            !list)
        {
            continue;
        }
        const uint32_t count = list->GetAdapterCount();
        for (uint32_t index = 0; index < count && state.dxcoreCount < kRedXeMaximumGpuAdapters; ++index)
        {
            wil::com_ptr_nothrow<IDXCoreAdapter> adapter;
            if (FAILED(list->GetAdapter(index, __uuidof(IDXCoreAdapter), adapter.put_void())) || !adapter)
            {
                continue;
            }
            LUID instance{};
            if (FAILED(adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, &instance)))
            {
                continue;
            }
            const uint64_t luid = LuidValue(instance);
            bool alreadyKnown = false;
            for (uint32_t known = 0; known < state.dxcoreCount; ++known)
            {
                alreadyKnown = alreadyKnown || state.dxcoreLuids[known] == luid;
            }
            if (alreadyKnown)
            {
                continue;
            }
            // IDXCoreAdapter1 carries QueryState/GetPropertyWithInput; an older runtime exposes only the base
            // interface, in which case the row keeps its class but loses the state queries.
            IUnknown* held = nullptr;
            wil::com_ptr_nothrow<IDXCoreAdapter1> adapter1;
            if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXCoreAdapter1), adapter1.put_void())) && adapter1)
            {
                held = adapter1.detach();
            }
            state.dxcoreAdapters[state.dxcoreCount] = held;
            state.dxcoreLuids[state.dxcoreCount] = luid;
            state.dxcoreClasses[state.dxcoreCount] = entry.deviceClass;
            ++state.dxcoreCount;
        }
    }
}

[[nodiscard]] uint32_t FindDxcoreIndex(const RedXeGpuState& state, uint64_t luid) noexcept
{
    for (uint32_t index = 0; index < state.dxcoreCount; ++index)
    {
        if (state.dxcoreLuids[index] == luid)
        {
            return index;
        }
    }
    return state.dxcoreCount;
}

// Machine-wide adapter memory use and temperature. These DXCoreAdapterState items are documented but flagged
// prerelease, so every one is probed per adapter and a failure leaves the field unavailable rather than zero. This
// is the documented answer to IDXGIAdapter3::QueryVideoMemoryInfo reporting only the calling process's budget.
void FillDxcoreAdapterState(RedXeGpuState& state, RedXeGpuAdapterRow& row) noexcept
{
    const uint32_t index = FindDxcoreIndex(state, row.adapterLuid);
    if (index >= state.dxcoreCount || !state.dxcoreAdapters[index])
    {
        return;
    }
    auto* adapter = static_cast<IDXCoreAdapter1*>(state.dxcoreAdapters[index]);
    uint32_t engineCount = 0;
    if (SUCCEEDED(adapter->GetProperty(DXCoreAdapterProperty::AdapterEngineCount, sizeof(engineCount), &engineCount)))
    {
        row.engineCount = engineCount;
        row.hasEngineCount = true;
    }
    DXCoreMemoryQueryInput dedicatedInput{0, DXCoreMemoryType::Dedicated};
    DXCoreMemoryUsage dedicated{};
    DXCoreMemoryQueryInput sharedInput{0, DXCoreMemoryType::Shared};
    DXCoreMemoryUsage shared{};
    if (SUCCEEDED(adapter->QueryState(DXCoreAdapterState::AdapterMemoryUsageBytes, &dedicatedInput, &dedicated)) &&
        SUCCEEDED(adapter->QueryState(DXCoreAdapterState::AdapterMemoryUsageBytes, &sharedInput, &shared)))
    {
        row.usedDedicatedBytes = dedicated.resident;
        row.usedSharedBytes = shared.resident;
        row.hasMemoryUsage = true;
    }
    if (!row.hasTemperature)
    {
        float temperature = 0.0f;
        uint32_t physicalIndex = 0;
        if (SUCCEEDED(adapter->QueryState(DXCoreAdapterState::AdapterTemperatureCelsius, &physicalIndex,
                                          &temperature)) &&
            temperature > 0.0f)
        {
            row.temperatureC = static_cast<double>(temperature);
            row.hasTemperature = true;
        }
    }
}

// Builds one adapter row per dxgkrnl adapter. The D3DKMT adapter list is the spine because it is the only
// enumeration that returns compute-only MCDM devices; DXGI and DXCore enrich rows joined by LUID. Driving the loop
// from DXGI, as this did before, silently dropped every adapter without a Direct3D user-mode driver — every NPU.
void RedXeGpuSampleAdapters(RedXeGpuState& state) noexcept
{
    if (!state.dxgiFactory)
    {
        (void)CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(state.dxgiFactory.put()));
    }
    RefreshKmtAdapters(state);
    RefreshDxcoreAdapters(state);
    state.adapterCount = 0;
    const auto queryAdapterInfo = reinterpret_cast<D3dkmtQueryAdapterInfoFn>(state.kmtQueryAdapterInfo);
    for (uint32_t kmtIndex = 0; kmtIndex < state.kmtCount && state.adapterCount < kRedXeMaximumGpuAdapters; ++kmtIndex)
    {
        RedXeGpuAdapterRow& row = state.adapters[state.adapterCount];
        row = {};
        row.adapterLuid = state.kmtLuids[kmtIndex];
        EnrichFromDxgi(state, row);

        const uint32_t dxcoreIndex = FindDxcoreIndex(state, row.adapterLuid);
        const bool dxcoreResolved = dxcoreIndex < state.dxcoreCount;
        const uint64_t dxcoreClass = dxcoreResolved ? state.dxcoreClasses[dxcoreIndex] : kRedXeDeviceClassUnknown;

        const D3dkmtHandle handle = state.kmtHandles[kmtIndex];
        if (handle != 0 && queryAdapterInfo)
        {
            D3DKMT_ADAPTERTYPE adapterType{};
            D3dkmtQueryAdapterInfo typeQuery{};
            typeQuery.hAdapter = handle;
            typeQuery.Type = kKmtAdapterType;
            typeQuery.PrivateDriverData = &adapterType;
            typeQuery.PrivateDriverDataSize = static_cast<UINT>(sizeof(adapterType));
            if (queryAdapterInfo(&typeQuery) >= 0)
            {
                row.hasAdapterType = true;
                row.computeOnly = adapterType.ComputeOnly != 0 ? 1 : 0;
                if (adapterType.SoftwareDevice != 0)
                {
                    row.software = 1;
                }
                if (adapterType.HybridIntegrated != 0)
                {
                    row.integrated = 1;
                    row.hasIntegrated = true;
                }
            }
            UINT32 physicalCount = 0;
            D3dkmtQueryAdapterInfo countQuery{};
            countQuery.hAdapter = handle;
            countQuery.Type = kKmtPhysicalAdapterCount;
            countQuery.PrivateDriverData = &physicalCount;
            countQuery.PrivateDriverDataSize = static_cast<UINT>(sizeof(physicalCount));
            if (queryAdapterInfo(&countQuery) >= 0 && physicalCount != 0)
            {
                row.physicalAdapterCount = physicalCount;
            }
        }

        // Precedence: DXCore hardware type, then the D3DKMT adapter-type bits, then DXGI presence. A row with no
        // evidence at all stays Unknown rather than being assumed to be a GPU.
        if (dxcoreResolved)
        {
            row.deviceClass = dxcoreClass;
            row.hasDeviceClass = true;
        }
        else if (row.hasAdapterType)
        {
            row.deviceClass = row.software != 0      ? kRedXeDeviceClassSoftware
                              : row.computeOnly != 0 ? kRedXeDeviceClassComputeAccelerator
                                                     : kRedXeDeviceClassGpu;
            row.hasDeviceClass = true;
        }
        else if (row.hasDxgi)
        {
            row.deviceClass = row.software != 0 ? kRedXeDeviceClassSoftware : kRedXeDeviceClassGpu;
            row.hasDeviceClass = true;
        }

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
        FillDxcoreAdapterState(state, row);
        ++state.adapterCount;
    }
    std::sort(state.adapters.begin(), state.adapters.begin() + state.adapterCount,
              [](const RedXeGpuAdapterRow& left, const RedXeGpuAdapterRow& right)
              { return left.adapterLuid < right.adapterLuid; });
}

uint64_t RedXeGpuDeviceClassForLuid(const RedXeGpuState& state, uint64_t luid) noexcept
{
    for (uint32_t index = 0; index < state.adapterCount; ++index)
    {
        if (state.adapters[index].adapterLuid == luid)
        {
            return state.adapters[index].deviceClass;
        }
    }
    return kRedXeDeviceClassUnknown;
}

// Per-engine busy percentage from DXCore's cumulative running-time counter. This is the utilization path that works
// for any adapter DXCore enumerates, NPUs included, without depending on a performance-counter set name — Microsoft
// has not published the one NPU engines appear in, and it is not "GPU Engine".
void FillDxcoreEngineUtilization(RedXeGpuState& state) noexcept
{
    if (state.dxcoreCount == 0 || state.engineCount == 0)
    {
        return;
    }
    FILETIME nowFileTime{};
    GetSystemTimeAsFileTime(&nowFileTime);
    const uint64_t now = (static_cast<uint64_t>(nowFileTime.dwHighDateTime) << 32) | nowFileTime.dwLowDateTime;
    const uint64_t elapsed100ns =
        state.previousEngineTimestamp100ns != 0 && now > state.previousEngineTimestamp100ns
            ? now - state.previousEngineTimestamp100ns
            : 0;

    uint32_t written = 0;
    for (uint32_t index = 0; index < state.engineCount; ++index)
    {
        RedXeGpuEngineRow& row = state.engines[index];
        const uint32_t dxcoreIndex = FindDxcoreIndex(state, row.adapterLuid);
        if (dxcoreIndex >= state.dxcoreCount || !state.dxcoreAdapters[dxcoreIndex])
        {
            continue;
        }
        auto* adapter = static_cast<IDXCoreAdapter1*>(state.dxcoreAdapters[dxcoreIndex]);
        DXCoreAdapterEngineIndex engineIndex{static_cast<uint32_t>(row.physicalAdapterIndex),
                                             static_cast<uint32_t>(row.nodeOrdinal)};
        DXCoreEngineQueryOutput output{};
        if (FAILED(adapter->QueryState(DXCoreAdapterState::AdapterEngineRunningTimeMicroseconds, &engineIndex,
                                       &output)))
        {
            continue;
        }
        for (uint32_t previous = 0; previous < state.previousEngineCount; ++previous)
        {
            const RedXeAcceleratorEngineSample& sample = state.previousEngines[previous];
            if (sample.adapterLuid != row.adapterLuid || sample.physicalAdapterIndex != row.physicalAdapterIndex ||
                sample.engineIndex != row.nodeOrdinal)
            {
                continue;
            }
            if (elapsed100ns != 0 && output.runningTime >= sample.runningTimeMicroseconds)
            {
                // Running time is microseconds busy; the interval is 100 ns units, so ten microseconds of interval
                // per unit of elapsed time makes a percentage.
                const double busy = static_cast<double>(output.runningTime - sample.runningTimeMicroseconds);
                const double interval = static_cast<double>(elapsed100ns) / 10.0;
                row.utilizationPercent = interval > 0.0 ? std::clamp((busy / interval) * 100.0, 0.0, 100.0) : 0.0;
                row.hasUtilization = true;
            }
            break;
        }
        if (written < state.previousEngines.size())
        {
            state.previousEngines[written] = RedXeAcceleratorEngineSample{
                row.adapterLuid, static_cast<uint32_t>(row.physicalAdapterIndex),
                static_cast<uint32_t>(row.nodeOrdinal), output.runningTime};
            ++written;
        }
    }
    state.previousEngineCount = written;
    state.previousEngineTimestamp100ns = now;

    // Roll the per-engine readings up into one adapter utilization, taking the busiest engine rather than an average
    // so a single saturated engine is not hidden by idle siblings.
    for (uint32_t index = 0; index < state.adapterCount; ++index)
    {
        RedXeGpuAdapterRow& adapterRow = state.adapters[index];
        double best = 0.0;
        bool any = false;
        for (uint32_t engine = 0; engine < state.engineCount; ++engine)
        {
            const RedXeGpuEngineRow& engineRow = state.engines[engine];
            if (engineRow.adapterLuid == adapterRow.adapterLuid && engineRow.hasUtilization)
            {
                best = (std::max)(best, engineRow.utilizationPercent);
                any = true;
            }
        }
        if (any)
        {
            adapterRow.utilizationPercent = best;
            adapterRow.hasUtilization = true;
        }
    }
}

void RedXeGpuSampleEngines(RedXeGpuState& state) noexcept
{
    if (state.kmtCount == 0)
    {
        RefreshKmtAdapters(state);
    }
    if (state.adapterCount == 0)
    {
        RedXeGpuSampleAdapters(state);
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
    FillDxcoreEngineUtilization(state);
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
