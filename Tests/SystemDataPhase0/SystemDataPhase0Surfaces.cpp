#include "SystemDataPhase0.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <psapi.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

#include <initguid.h>

#include <devguid.h>
#include <dxcore.h>
#include <dxgi.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <perflib.h>
#include <poclass.h>
#include <setupapi.h>
#include <winioctl.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr uint32_t kWarmupCollects = 2;
constexpr uint32_t kTimedCollects = 16;
constexpr uint32_t kMaxAdapters = 32;
constexpr uint32_t kMaxPhysicalDrives = 32;
constexpr uint32_t kMaxPdhArrayBytes = 2 * 1024 * 1024;
constexpr uint32_t kMaxPerfDataBytes = 1024 * 1024;
constexpr DWORD kDeviceIoctlTimeoutMs = 100;
constexpr DWORD kCancelDrainMs = 1000;
constexpr UINT kKmtAdapterPerfData = 62;
constexpr UINT kKmtAdapterPerfDataCaps = 63;

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

using D3dkmtEnumAdapters2Fn = LONG(APIENTRY*)(D3dkmtEnumAdapters2*);
using D3dkmtCloseAdapterFn = LONG(APIENTRY*)(const D3dkmtCloseAdapter*);
using D3dkmtQueryAdapterInfoFn = LONG(APIENTRY*)(const D3dkmtQueryAdapterInfo*);
using DxcoreCreateAdapterFactoryFn = HRESULT(WINAPI*)(REFIID, void**);

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Function> [[nodiscard]] Function TryResolveExport(HMODULE module, const char* name)
{
    const FARPROC procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

[[nodiscard]] double Microseconds(const LARGE_INTEGER& start, const LARGE_INTEGER& finish,
                                  const LARGE_INTEGER& frequency)
{
    return static_cast<double>(finish.QuadPart - start.QuadPart) * 1'000'000.0 /
           static_cast<double>(frequency.QuadPart);
}

[[nodiscard]] LARGE_INTEGER QueryCounter()
{
    LARGE_INTEGER value{};
    Expect(QueryPerformanceCounter(&value) != FALSE, "QPC counter failed");
    return value;
}

[[nodiscard]] LARGE_INTEGER QueryFrequency()
{
    LARGE_INTEGER value{};
    Expect(QueryPerformanceFrequency(&value) != FALSE, "QPC frequency failed");
    return value;
}

[[nodiscard]] PROCESS_MEMORY_COUNTERS_EX ProcessMemory()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = static_cast<DWORD>(sizeof(counters));
    Expect(K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                   counters.cb) != FALSE,
           "process memory query failed");
    return counters;
}

[[nodiscard]] DWORD ProcessHandles()
{
    DWORD handles = 0;
    Expect(GetProcessHandleCount(GetCurrentProcess(), &handles) != FALSE, "handle count query failed");
    return handles;
}

void PrintU32(const wchar_t* name, uint32_t value)
{
    std::wcout << name << L'=' << value << L'\n';
}

void PrintU64(const wchar_t* name, uint64_t value)
{
    std::wcout << name << L'=' << value << L'\n';
}

void PrintHex(const wchar_t* name, unsigned long value)
{
    std::wcout << name << L"=0x" << std::hex << value << std::dec << L'\n';
}

void PrintDouble(const wchar_t* name, double value)
{
    std::wcout << name << L'=' << value << L'\n';
}

void PrintText(const wchar_t* name, const wchar_t* value)
{
    std::wcout << name << L'=' << (value ? value : L"") << L'\n';
}

[[nodiscard]] bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (size_t index = 0; index < left.size(); ++index)
    {
        wchar_t a = left[index];
        wchar_t b = right[index];
        if (a >= L'A' && a <= L'Z')
        {
            a = static_cast<wchar_t>(a - L'A' + L'a');
        }
        if (b >= L'A' && b <= L'Z')
        {
            b = static_cast<wchar_t>(b - L'A' + L'a');
        }
        if (a != b)
        {
            return false;
        }
    }
    return true;
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
        acc = acc * 10 + static_cast<unsigned>(rest.front() - L'0');
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
    if (!ConsumePrefix(rest, L"0x") && !ConsumePrefix(rest, L"0X"))
    {
        return false;
    }
    if (rest.empty())
    {
        return false;
    }
    uint64_t acc = 0;
    bool any = false;
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

struct GpuEngineInstance
{
    uint32_t pid = 0;
    LUID luid{};
    uint32_t physicalAdapter = 0;
    uint32_t engine = 0;
};

[[nodiscard]] bool ParseGpuEngineInstance(std::wstring_view name, GpuEngineInstance& parsed) noexcept
{
    parsed = {};
    std::wstring_view rest = name;
    uint32_t high = 0;
    uint32_t low = 0;
    if (!ConsumePrefix(rest, L"pid_") || !ConsumeUInt32(rest, parsed.pid) || !ConsumePrefix(rest, L"_luid_") ||
        !ConsumeHex32(rest, high) || !ConsumePrefix(rest, L"_") || !ConsumeHex32(rest, low) ||
        !ConsumePrefix(rest, L"_phys_") || !ConsumeUInt32(rest, parsed.physicalAdapter) ||
        !ConsumePrefix(rest, L"_eng_") || !ConsumeUInt32(rest, parsed.engine) || !ConsumePrefix(rest, L"_engtype_"))
    {
        return false;
    }
    parsed.luid.HighPart = static_cast<LONG>(high);
    parsed.luid.LowPart = low;
    return true;
}

[[nodiscard]] bool DeviceIoControlBounded(HANDLE device, DWORD controlCode, void* input, DWORD inputBytes, void* output,
                                          DWORD outputBytes, DWORD* returned)
{
    OVERLAPPED overlapped{};
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event)
    {
        return false;
    }
    overlapped.hEvent = event.get();
    const BOOL issued =
        DeviceIoControl(device, controlCode, input, inputBytes, output, outputBytes, nullptr, &overlapped);
    if (issued != FALSE)
    {
        DWORD bytes = 0;
        if (GetOverlappedResult(device, &overlapped, &bytes, FALSE) == FALSE)
        {
            return false;
        }
        if (returned)
        {
            *returned = bytes;
        }
        return true;
    }
    if (GetLastError() != ERROR_IO_PENDING)
    {
        return false;
    }
    const DWORD wait = WaitForSingleObject(event.get(), kDeviceIoctlTimeoutMs);
    if (wait == WAIT_TIMEOUT)
    {
        CancelIoEx(device, &overlapped);
        WaitForSingleObject(event.get(), kCancelDrainMs);
        return false;
    }
    if (wait != WAIT_OBJECT_0)
    {
        return false;
    }
    DWORD bytes = 0;
    if (GetOverlappedResult(device, &overlapped, &bytes, FALSE) == FALSE)
    {
        return false;
    }
    if (returned)
    {
        *returned = bytes;
    }
    return true;
}

struct PdhProbe
{
    PDH_STATUS addStatus = static_cast<PDH_STATUS>(0xFFFFFFFF);
    PDH_STATUS collectStatus = static_cast<PDH_STATUS>(0xFFFFFFFF);
    PDH_STATUS arrayStatus = static_cast<PDH_STATUS>(0xFFFFFFFF);
    DWORD instanceCount = 0;
    double setupUs = 0;
    double collectUs = 0;
    SIZE_T privateBytesDelta = 0;
    DWORD handlesDelta = 0;
    bool ok = false;
    std::wstring firstInstance;
    uint32_t grammarOk = 0;
    uint32_t grammarSkip = 0;
};

[[nodiscard]] PdhProbe ProbePdh(const wchar_t* path, bool parseGpuGrammar)
{
    PdhProbe result{};
    const LARGE_INTEGER frequency = QueryFrequency();
    const PROCESS_MEMORY_COUNTERS_EX memoryBefore = ProcessMemory();
    const DWORD handlesBefore = ProcessHandles();

    PDH_HQUERY query = nullptr;
    const LARGE_INTEGER setupStart = QueryCounter();
    const PDH_STATUS opened = PdhOpenQueryW(nullptr, 0, &query);
    if (opened != ERROR_SUCCESS || query == nullptr)
    {
        PrintHex(L"pdh_open_status", static_cast<unsigned long>(opened));
        return result;
    }
    auto closeQuery = wil::scope_exit(
        [&]
        {
            if (query)
            {
                PdhCloseQuery(query);
                query = nullptr;
            }
        });

    PDH_HCOUNTER counter = nullptr;
    result.addStatus = PdhAddEnglishCounterW(query, path, 0, &counter);
    const LARGE_INTEGER setupFinish = QueryCounter();
    result.setupUs = Microseconds(setupStart, setupFinish, frequency);
    if (result.addStatus != ERROR_SUCCESS)
    {
        return result;
    }

    for (uint32_t warmup = 0; warmup < kWarmupCollects; ++warmup)
    {
        (void)PdhCollectQueryData(query);
    }

    const LARGE_INTEGER collectStart = QueryCounter();
    for (uint32_t iteration = 0; iteration < kTimedCollects; ++iteration)
    {
        result.collectStatus = PdhCollectQueryData(query);
    }
    const LARGE_INTEGER collectFinish = QueryCounter();
    result.collectUs = Microseconds(collectStart, collectFinish, frequency) / kTimedCollects;
    if (result.collectStatus != ERROR_SUCCESS)
    {
        return result;
    }

    DWORD bufferBytes = 0;
    DWORD itemCount = 0;
    result.arrayStatus =
        PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bufferBytes, &itemCount, nullptr);
    if (result.arrayStatus == PDH_MORE_DATA)
    {
        if (bufferBytes == 0 || bufferBytes > kMaxPdhArrayBytes)
        {
            PrintU32(L"pdh_array_bytes_rejected", bufferBytes);
            return result;
        }
        std::vector<std::byte> buffer(bufferBytes);
        result.arrayStatus =
            PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bufferBytes, &itemCount,
                                         reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data()));
        if (result.arrayStatus == ERROR_SUCCESS)
        {
            result.instanceCount = itemCount;
            const auto* items = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            for (DWORD index = 0; index < itemCount; ++index)
            {
                if (!items[index].szName)
                {
                    continue;
                }
                const std::wstring_view name{items[index].szName};
                if (result.firstInstance.empty() && !EqualsIgnoreCase(name, L"_Total"))
                {
                    result.firstInstance.assign(name);
                }
                if (!parseGpuGrammar)
                {
                    continue;
                }
                if (EqualsIgnoreCase(name, L"_Total"))
                {
                    ++result.grammarSkip;
                    continue;
                }
                GpuEngineInstance parsed{};
                if (ParseGpuEngineInstance(name, parsed))
                {
                    ++result.grammarOk;
                }
                else
                {
                    ++result.grammarSkip;
                }
            }
        }
    }

    const PROCESS_MEMORY_COUNTERS_EX memoryAfter = ProcessMemory();
    result.privateBytesDelta = memoryAfter.PrivateUsage >= memoryBefore.PrivateUsage
                                   ? memoryAfter.PrivateUsage - memoryBefore.PrivateUsage
                                   : 0;
    const DWORD handlesAfter = ProcessHandles();
    result.handlesDelta = handlesAfter >= handlesBefore ? handlesAfter - handlesBefore : 0;
    result.ok = result.addStatus == ERROR_SUCCESS && result.collectStatus == ERROR_SUCCESS &&
                result.arrayStatus == ERROR_SUCCESS;
    return result;
}

void PrintPdhProbe(const wchar_t* id, const PdhProbe& probe)
{
    std::wcout << id << L"_ok=" << (probe.ok ? 1 : 0) << L'\n';
    PrintHex((std::wstring(id) + L"_add").c_str(), static_cast<unsigned long>(probe.addStatus));
    PrintHex((std::wstring(id) + L"_collect").c_str(), static_cast<unsigned long>(probe.collectStatus));
    PrintHex((std::wstring(id) + L"_array").c_str(), static_cast<unsigned long>(probe.arrayStatus));
    PrintU32((std::wstring(id) + L"_instances").c_str(), probe.instanceCount);
    PrintDouble((std::wstring(id) + L"_setup_us").c_str(), probe.setupUs);
    PrintDouble((std::wstring(id) + L"_collect_us").c_str(), probe.collectUs);
    PrintU64((std::wstring(id) + L"_private_delta").c_str(), probe.privateBytesDelta);
    PrintU32((std::wstring(id) + L"_handles_delta").c_str(), probe.handlesDelta);
    if (!probe.firstInstance.empty())
    {
        std::wcout << id << L"_first_instance=" << probe.firstInstance << L'\n';
    }
    if (probe.grammarOk != 0 || probe.grammarSkip != 0)
    {
        PrintU32((std::wstring(id) + L"_grammar_ok").c_str(), probe.grammarOk);
        PrintU32((std::wstring(id) + L"_grammar_other").c_str(), probe.grammarSkip);
    }
}

[[nodiscard]] bool FindCounterSetGuid(std::wstring_view englishName, GUID& guid)
{
    guid = {};
    DWORD needed = 0;
    ULONG status = PerfEnumerateCounterSet(nullptr, nullptr, 0, &needed);
    if (status != ERROR_NOT_ENOUGH_MEMORY && status != ERROR_SUCCESS)
    {
        PrintHex(L"perflib_enum_status", status);
        return false;
    }
    if (needed == 0)
    {
        return false;
    }
    std::vector<GUID> ids(needed);
    DWORD actual = 0;
    status = PerfEnumerateCounterSet(nullptr, ids.data(), needed, &actual);
    if (status != ERROR_SUCCESS)
    {
        PrintHex(L"perflib_enum_status", status);
        return false;
    }
    for (DWORD index = 0; index < actual; ++index)
    {
        DWORD nameBytes = 0;
        (void)PerfQueryCounterSetRegistrationInfo(nullptr, &ids[index], PERF_REG_COUNTERSET_ENGLISH_NAME, 0, nullptr, 0,
                                                  &nameBytes);
        if (nameBytes == 0 || nameBytes > 1024)
        {
            continue;
        }
        std::vector<wchar_t> name(nameBytes / sizeof(wchar_t) + 1);
        DWORD written = 0;
        if (PerfQueryCounterSetRegistrationInfo(nullptr, &ids[index], PERF_REG_COUNTERSET_ENGLISH_NAME, 0,
                                                reinterpret_cast<LPBYTE>(name.data()), nameBytes,
                                                &written) != ERROR_SUCCESS)
        {
            continue;
        }
        if (EqualsIgnoreCase(name.data(), englishName))
        {
            guid = ids[index];
            std::wcout << L"perflib_matched=" << name.data() << L" guid={" << std::hex << guid.Data1 << L'-'
                       << guid.Data2 << L'-' << guid.Data3 << std::dec << L"}\n";
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ContainsIgnoreCase(std::wstring_view haystack, std::wstring_view needle) noexcept
{
    if (needle.empty() || haystack.size() < needle.size())
    {
        return false;
    }
    for (size_t start = 0; start + needle.size() <= haystack.size(); ++start)
    {
        if (EqualsIgnoreCase(haystack.substr(start, needle.size()), needle))
        {
            return true;
        }
    }
    return false;
}

void DumpInterestingCounterSets()
{
    DWORD needed = 0;
    ULONG status = PerfEnumerateCounterSet(nullptr, nullptr, 0, &needed);
    if ((status != ERROR_NOT_ENOUGH_MEMORY && status != ERROR_SUCCESS) || needed == 0)
    {
        PrintHex(L"perflib_dump_enum", status);
        return;
    }
    std::vector<GUID> ids(needed);
    DWORD actual = 0;
    status = PerfEnumerateCounterSet(nullptr, ids.data(), needed, &actual);
    if (status != ERROR_SUCCESS)
    {
        PrintHex(L"perflib_dump_enum", status);
        return;
    }
    PrintU32(L"perflib_counter_sets", actual);
    uint32_t printed = 0;
    for (DWORD index = 0; index < actual && printed < 32; ++index)
    {
        DWORD nameBytes = 0;
        (void)PerfQueryCounterSetRegistrationInfo(nullptr, &ids[index], PERF_REG_COUNTERSET_ENGLISH_NAME, 0, nullptr, 0,
                                                  &nameBytes);
        if (nameBytes == 0 || nameBytes > 1024)
        {
            continue;
        }
        std::vector<wchar_t> name(nameBytes / sizeof(wchar_t) + 1);
        DWORD written = 0;
        if (PerfQueryCounterSetRegistrationInfo(nullptr, &ids[index], PERF_REG_COUNTERSET_ENGLISH_NAME, 0,
                                                reinterpret_cast<LPBYTE>(name.data()), nameBytes,
                                                &written) != ERROR_SUCCESS)
        {
            continue;
        }
        const std::wstring_view view{name.data()};
        if (!ContainsIgnoreCase(view, L"processor") && !ContainsIgnoreCase(view, L"disk") &&
            !ContainsIgnoreCase(view, L"gpu") && !ContainsIgnoreCase(view, L"physical"))
        {
            continue;
        }
        std::wcout << L"perflib_set=" << name.data() << L'\n';
        ++printed;
    }
}

struct PerfProbe
{
    ULONG addStatus = 0xFFFFFFFF;
    ULONG queryStatus = 0xFFFFFFFF;
    DWORD identifierStatus = 0xFFFFFFFF;
    ULONG dataBytes = 0;
    ULONG counterBlocks = 0;
    double setupUs = 0;
    double collectUs = 0;
    SIZE_T privateBytesDelta = 0;
    DWORD handlesDelta = 0;
    bool ok = false;
};

[[nodiscard]] PerfProbe ProbePerfLib(std::wstring_view englishName)
{
    PerfProbe result{};
    GUID setId{};
    if (!FindCounterSetGuid(englishName, setId))
    {
        PrintText(L"perflib_missing_set", std::wstring(englishName).c_str());
        return result;
    }

    const LARGE_INTEGER frequency = QueryFrequency();
    const PROCESS_MEMORY_COUNTERS_EX memoryBefore = ProcessMemory();
    const DWORD handlesBefore = ProcessHandles();
    HANDLE query = nullptr;
    const LARGE_INTEGER setupStart = QueryCounter();
    if (PerfOpenQueryHandle(nullptr, &query) != ERROR_SUCCESS || query == nullptr)
    {
        PrintText(L"perflib_open", L"failed");
        return result;
    }
    auto closeQuery = wil::scope_exit(
        [&]
        {
            if (query)
            {
                PerfCloseQueryHandle(query);
                query = nullptr;
            }
        });

    alignas(8) PERF_COUNTER_IDENTIFIER identifier{};
    identifier.CounterSetGuid = setId;
    identifier.Size = static_cast<ULONG>(sizeof(identifier));
    identifier.CounterId = PERF_WILDCARD_COUNTER;
    identifier.InstanceId = PERF_WILDCARD_COUNTER;
    result.addStatus = PerfAddCounters(query, &identifier, static_cast<DWORD>(sizeof(identifier)));
    result.identifierStatus = identifier.Status;
    if (result.addStatus != ERROR_SUCCESS || identifier.Status != ERROR_SUCCESS)
    {
        alignas(8) std::array<std::byte, 64> named{};
        auto* namedId = reinterpret_cast<PERF_COUNTER_IDENTIFIER*>(named.data());
        namedId->CounterSetGuid = setId;
        namedId->CounterId = PERF_WILDCARD_COUNTER;
        namedId->InstanceId = PERF_WILDCARD_COUNTER;
        auto* instanceName = reinterpret_cast<wchar_t*>(named.data() + sizeof(PERF_COUNTER_IDENTIFIER));
        instanceName[0] = L'*';
        instanceName[1] = L'\0';
        namedId->Size = 48;
        result.addStatus = PerfAddCounters(query, namedId, namedId->Size);
        result.identifierStatus = namedId->Status;
    }
    const LARGE_INTEGER setupFinish = QueryCounter();
    result.setupUs = Microseconds(setupStart, setupFinish, frequency);
    if (result.addStatus != ERROR_SUCCESS || result.identifierStatus != ERROR_SUCCESS)
    {
        return result;
    }

    std::vector<std::byte> data(65536);
    for (uint32_t warmup = 0; warmup < kWarmupCollects; ++warmup)
    {
        DWORD actual = 0;
        ULONG queryStatus = PerfQueryCounterData(query, reinterpret_cast<PERF_DATA_HEADER*>(data.data()),
                                                 static_cast<DWORD>(data.size()), &actual);
        if (queryStatus == ERROR_NOT_ENOUGH_MEMORY)
        {
            if (actual == 0 || actual > kMaxPerfDataBytes)
            {
                return result;
            }
            data.resize(actual);
            queryStatus = PerfQueryCounterData(query, reinterpret_cast<PERF_DATA_HEADER*>(data.data()),
                                               static_cast<DWORD>(data.size()), &actual);
        }
        result.queryStatus = queryStatus;
        result.dataBytes = actual;
    }
    if (result.queryStatus != ERROR_SUCCESS)
    {
        return result;
    }

    const LARGE_INTEGER collectStart = QueryCounter();
    for (uint32_t iteration = 0; iteration < kTimedCollects; ++iteration)
    {
        DWORD actual = 0;
        result.queryStatus = PerfQueryCounterData(query, reinterpret_cast<PERF_DATA_HEADER*>(data.data()),
                                                  static_cast<DWORD>(data.size()), &actual);
        result.dataBytes = actual;
        if (result.queryStatus != ERROR_SUCCESS)
        {
            break;
        }
    }
    const LARGE_INTEGER collectFinish = QueryCounter();
    result.collectUs = Microseconds(collectStart, collectFinish, frequency) / kTimedCollects;
    if (result.queryStatus == ERROR_SUCCESS && result.dataBytes >= sizeof(PERF_DATA_HEADER))
    {
        const auto* header = reinterpret_cast<const PERF_DATA_HEADER*>(data.data());
        result.counterBlocks = header->dwNumCounters;
        result.ok = true;
    }
    const PROCESS_MEMORY_COUNTERS_EX memoryAfter = ProcessMemory();
    result.privateBytesDelta = memoryAfter.PrivateUsage >= memoryBefore.PrivateUsage
                                   ? memoryAfter.PrivateUsage - memoryBefore.PrivateUsage
                                   : 0;
    const DWORD handlesAfter = ProcessHandles();
    result.handlesDelta = handlesAfter >= handlesBefore ? handlesAfter - handlesBefore : 0;
    return result;
}

void PrintPerfProbe(const wchar_t* id, const PerfProbe& probe)
{
    std::wcout << id << L"_ok=" << (probe.ok ? 1 : 0) << L'\n';
    PrintHex((std::wstring(id) + L"_add").c_str(), probe.addStatus);
    PrintHex((std::wstring(id) + L"_id_status").c_str(), probe.identifierStatus);
    PrintHex((std::wstring(id) + L"_query").c_str(), probe.queryStatus);
    PrintU32((std::wstring(id) + L"_bytes").c_str(), probe.dataBytes);
    PrintU32((std::wstring(id) + L"_blocks").c_str(), probe.counterBlocks);
    PrintDouble((std::wstring(id) + L"_setup_us").c_str(), probe.setupUs);
    PrintDouble((std::wstring(id) + L"_collect_us").c_str(), probe.collectUs);
    PrintU64((std::wstring(id) + L"_private_delta").c_str(), probe.privateBytesDelta);
    PrintU32((std::wstring(id) + L"_handles_delta").c_str(), probe.handlesDelta);
}

[[nodiscard]] const wchar_t* ChooseCounterBackend(const PdhProbe& pdh, const PerfProbe& perf)
{
    if (!pdh.ok && !perf.ok)
    {
        return L"unsupported";
    }
    if (!perf.ok)
    {
        return L"pdh";
    }
    if (!pdh.ok)
    {
        return L"perflib";
    }
    // PerfLib V2 wins only when it is materially cheaper on the warm collect.
    if (perf.collectUs * 1.25 < pdh.collectUs && pdh.collectUs > 10.0)
    {
        return L"perflib";
    }
    return L"pdh";
}

void SpikeCounters()
{
    std::wcout << L"ui_language=" << GetUserDefaultUILanguage() << L'\n';
    PrintText(L"pdh_collect_ex", L"not_called");
    DumpInterestingCounterSets();

    const PdhProbe cpuPdh = ProbePdh(L"\\Processor Information(*)\\% Processor Time", false);
    PrintPdhProbe(L"pdh_cpu", cpuPdh);
    const PerfProbe cpuPerf = ProbePerfLib(L"Processor Information");
    PrintPerfProbe(L"perflib_cpu", cpuPerf);
    PrintText(L"gate_ctr_cpu", ChooseCounterBackend(cpuPdh, cpuPerf));

    const PdhProbe gpuEngine = ProbePdh(L"\\GPU Engine(*)\\Utilization Percentage", true);
    PrintPdhProbe(L"pdh_gpu_engine", gpuEngine);
    const PdhProbe gpuAdapterMemory = ProbePdh(L"\\GPU Adapter Memory(*)\\Dedicated Usage", false);
    PrintPdhProbe(L"pdh_gpu_adapter_memory", gpuAdapterMemory);
    const PdhProbe gpuProcessMemory = ProbePdh(L"\\GPU Process Memory(*)\\Dedicated Usage", false);
    PrintPdhProbe(L"pdh_gpu_process_memory", gpuProcessMemory);
    const PerfProbe gpuPerf = ProbePerfLib(L"GPU Engine");
    PrintPerfProbe(L"perflib_gpu_engine", gpuPerf);
    PrintText(L"gate_ctr_gpu", ChooseCounterBackend(gpuEngine, gpuPerf));
}

struct DiskIoctlResult
{
    uint32_t opened = 0;
    uint32_t ioctlOk = 0;
    uint32_t ioctlFail = 0;
    uint32_t temperatureOk = 0;
    uint32_t temperatureFail = 0;
    double ioctlUs = 0;
};

[[nodiscard]] DiskIoctlResult SpikeDiskIoctl()
{
    DiskIoctlResult result{};
    const LARGE_INTEGER frequency = QueryFrequency();
    LARGE_INTEGER ioctlTime{};
    for (uint32_t index = 0; index < kMaxPhysicalDrives; ++index)
    {
        wchar_t path[64]{};
        const int written = swprintf_s(path, L"\\\\.\\PhysicalDrive%u", index);
        Expect(written > 0, "physical drive path format failed");
        wil::unique_hfile disk{CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                           FILE_FLAG_OVERLAPPED, nullptr)};
        if (!disk)
        {
            continue;
        }
        ++result.opened;
        DISK_PERFORMANCE performance{};
        DWORD returned = 0;
        const LARGE_INTEGER start = QueryCounter();
        const bool ok = DeviceIoControlBounded(disk.get(), IOCTL_DISK_PERFORMANCE, nullptr, 0, &performance,
                                               static_cast<DWORD>(sizeof(performance)), &returned);
        const LARGE_INTEGER finish = QueryCounter();
        ioctlTime.QuadPart += finish.QuadPart - start.QuadPart;
        if (ok)
        {
            ++result.ioctlOk;
        }
        else
        {
            ++result.ioctlFail;
        }

        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceTemperatureProperty;
        query.QueryType = PropertyStandardQuery;
        std::array<std::byte, 512> buffer{};
        DWORD temperatureBytes = 0;
        if (DeviceIoControlBounded(disk.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, static_cast<DWORD>(sizeof(query)),
                                   buffer.data(), static_cast<DWORD>(buffer.size()), &temperatureBytes))
        {
            ++result.temperatureOk;
        }
        else
        {
            ++result.temperatureFail;
        }
    }
    if (result.ioctlOk + result.ioctlFail > 0)
    {
        result.ioctlUs = static_cast<double>(ioctlTime.QuadPart) * 1'000'000.0 /
                         static_cast<double>(frequency.QuadPart) / (result.ioctlOk + result.ioctlFail);
    }
    PrintU32(L"disk_opened", result.opened);
    PrintU32(L"disk_ioctl_ok", result.ioctlOk);
    PrintU32(L"disk_ioctl_fail", result.ioctlFail);
    PrintDouble(L"disk_ioctl_us", result.ioctlUs);
    PrintU32(L"storage_temp_ok", result.temperatureOk);
    PrintU32(L"storage_temp_fail", result.temperatureFail);
    PrintText(L"disk_performance_off", L"not_called");
    return result;
}

void SpikeDiskSideEffect(const PdhProbe& before, const DiskIoctlResult& ioctl)
{
    const PdhProbe after = ProbePdh(L"\\PhysicalDisk(*)\\Disk Reads/sec", false);
    PrintPdhProbe(L"pdh_disk_after", after);
    const wchar_t* sideEffect = L"none";
    if (!before.ok && after.ok)
    {
        sideEffect = L"enabled_by_ioctl";
    }
    else if (before.ok && after.ok && before.instanceCount == 0 && after.instanceCount > 0)
    {
        sideEffect = L"enabled_by_ioctl";
    }
    else if (before.ok && after.ok)
    {
        sideEffect = L"already_on";
    }
    else if (!before.ok && !after.ok)
    {
        sideEffect = L"counters_unavailable";
    }
    PrintText(L"disk_side_effect", sideEffect);
    const wchar_t* gate = L"fallback";
    if (ioctl.ioctlOk > 0 && EqualsIgnoreCase(sideEffect, L"already_on"))
    {
        gate = L"keep";
    }
    else if (ioctl.ioctlOk == 0 && (before.ok || after.ok))
    {
        gate = L"fallback";
    }
    else if (EqualsIgnoreCase(sideEffect, L"enabled_by_ioctl"))
    {
        gate = L"fallback";
    }
    else if (ioctl.ioctlOk == 0 && !before.ok && !after.ok)
    {
        gate = L"unsupported";
    }
    PrintText(L"gate_disk_ioctl", gate);
}

void SpikeGpu()
{
    wil::com_ptr_nothrow<IDXGIFactory1> factory;
    const HRESULT created = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.put()));
    PrintHex(L"dxgi_factory", static_cast<unsigned long>(created));
    uint32_t dxgiCount = 0;
    uint32_t dxgiSoftware = 0;
    std::array<LUID, kMaxAdapters> dxgiLuids{};
    if (SUCCEEDED(created) && factory)
    {
        for (UINT index = 0; index < kMaxAdapters; ++index)
        {
            wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
            const HRESULT enumerated = factory->EnumAdapters1(index, adapter.put());
            if (enumerated == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            if (FAILED(enumerated) || !adapter)
            {
                PrintHex(L"dxgi_enum", static_cast<unsigned long>(enumerated));
                break;
            }
            DXGI_ADAPTER_DESC1 description{};
            if (FAILED(adapter->GetDesc1(&description)))
            {
                continue;
            }
            dxgiLuids[dxgiCount] = description.AdapterLuid;
            ++dxgiCount;
            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            {
                ++dxgiSoftware;
            }
            std::wcout << L"dxgi_adapter_" << index << L"_luid=" << std::hex << description.AdapterLuid.HighPart << L':'
                       << description.AdapterLuid.LowPart << std::dec << L" vendor=0x" << std::hex
                       << description.VendorId << L" device=0x" << description.DeviceId << std::dec << L" dedicated="
                       << description.DedicatedVideoMemory << L" software="
                       << (((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) ? 1 : 0) << L'\n';
        }
    }
    PrintU32(L"dxgi_adapters", dxgiCount);
    PrintU32(L"dxgi_software", dxgiSoftware);
    PrintText(L"dxgi_d3d_device", L"not_created");

    uint32_t dxcoreCount = 0;
    uint32_t dxcoreJoined = 0;
    wil::unique_hmodule dxcoreModule{LoadLibraryExW(L"dxcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    if (!dxcoreModule)
    {
        PrintText(L"dxcore", L"absent");
    }
    else
    {
        const auto createFactory =
            TryResolveExport<DxcoreCreateAdapterFactoryFn>(dxcoreModule.get(), "DXCoreCreateAdapterFactory");
        if (!createFactory)
        {
            PrintText(L"dxcore", L"missing_export");
        }
        else
        {
            wil::com_ptr_nothrow<IDXCoreAdapterFactory> dxcoreFactory;
            const HRESULT dxcoreCreated =
                createFactory(__uuidof(IDXCoreAdapterFactory), reinterpret_cast<void**>(dxcoreFactory.put()));
            PrintHex(L"dxcore_factory", static_cast<unsigned long>(dxcoreCreated));
            if (SUCCEEDED(dxcoreCreated) && dxcoreFactory)
            {
                const GUID attributes[] = {DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS,
                                           DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS,
                                           DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE};
                std::array<LUID, kMaxAdapters> seen{};
                uint32_t seenCount = 0;
                for (const GUID& attribute : attributes)
                {
                    wil::com_ptr_nothrow<IDXCoreAdapterList> list;
                    if (FAILED(dxcoreFactory->CreateAdapterList(1, &attribute, __uuidof(IDXCoreAdapterList),
                                                                reinterpret_cast<void**>(list.put()))) ||
                        !list)
                    {
                        continue;
                    }
                    const uint32_t count = list->GetAdapterCount();
                    for (uint32_t index = 0; index < count && dxcoreCount < kMaxAdapters; ++index)
                    {
                        wil::com_ptr_nothrow<IDXCoreAdapter> adapter;
                        if (FAILED(list->GetAdapter(index, __uuidof(IDXCoreAdapter),
                                                    reinterpret_cast<void**>(adapter.put()))) ||
                            !adapter)
                        {
                            continue;
                        }
                        if (!adapter->IsPropertySupported(DXCoreAdapterProperty::InstanceLuid))
                        {
                            continue;
                        }
                        size_t luidSize = 0;
                        if (FAILED(adapter->GetPropertySize(DXCoreAdapterProperty::InstanceLuid, &luidSize)) ||
                            luidSize != sizeof(LUID))
                        {
                            continue;
                        }
                        LUID luid{};
                        if (FAILED(adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, sizeof(luid), &luid)))
                        {
                            continue;
                        }
                        bool duplicate = false;
                        for (uint32_t seenIndex = 0; seenIndex < seenCount; ++seenIndex)
                        {
                            if (seen[seenIndex].LowPart == luid.LowPart && seen[seenIndex].HighPart == luid.HighPart)
                            {
                                duplicate = true;
                                break;
                            }
                        }
                        if (duplicate)
                        {
                            continue;
                        }
                        if (seenCount < seen.size())
                        {
                            seen[seenCount] = luid;
                            ++seenCount;
                        }
                        bool hardware = false;
                        bool integrated = false;
                        if (adapter->IsPropertySupported(DXCoreAdapterProperty::IsHardware))
                        {
                            size_t hardwareSize = 0;
                            if (SUCCEEDED(adapter->GetPropertySize(DXCoreAdapterProperty::IsHardware, &hardwareSize)) &&
                                hardwareSize == sizeof(hardware))
                            {
                                (void)adapter->GetProperty(DXCoreAdapterProperty::IsHardware, sizeof(hardware),
                                                           &hardware);
                            }
                        }
                        if (adapter->IsPropertySupported(DXCoreAdapterProperty::IsIntegrated))
                        {
                            size_t integratedSize = 0;
                            if (SUCCEEDED(
                                    adapter->GetPropertySize(DXCoreAdapterProperty::IsIntegrated, &integratedSize)) &&
                                integratedSize == sizeof(integrated))
                            {
                                (void)adapter->GetProperty(DXCoreAdapterProperty::IsIntegrated, sizeof(integrated),
                                                           &integrated);
                            }
                        }
                        ++dxcoreCount;
                        for (uint32_t dxgiIndex = 0; dxgiIndex < dxgiCount; ++dxgiIndex)
                        {
                            if (dxgiLuids[dxgiIndex].LowPart == luid.LowPart &&
                                dxgiLuids[dxgiIndex].HighPart == luid.HighPart)
                            {
                                ++dxcoreJoined;
                                break;
                            }
                        }
                        std::wcout << L"dxcore_luid=" << std::hex << luid.HighPart << L':' << luid.LowPart << std::dec
                                   << L" hardware=" << (hardware ? 1 : 0) << L" integrated=" << (integrated ? 1 : 0)
                                   << L'\n';
                    }
                }
            }
        }
    }
    PrintU32(L"dxcore_adapters", dxcoreCount);
    PrintU32(L"dxcore_joined_luid", dxcoreJoined);

    uint32_t kmtCount = 0;
    uint32_t kmtPerfOk = 0;
    uint32_t kmtJoined = 0;
    wil::unique_hmodule gdi32{LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    Expect(static_cast<bool>(gdi32), "gdi32.dll could not be loaded");
    const auto enumAdapters2 = TryResolveExport<D3dkmtEnumAdapters2Fn>(gdi32.get(), "D3DKMTEnumAdapters2");
    const auto closeAdapter = TryResolveExport<D3dkmtCloseAdapterFn>(gdi32.get(), "D3DKMTCloseAdapter");
    const auto queryAdapterInfo = TryResolveExport<D3dkmtQueryAdapterInfoFn>(gdi32.get(), "D3DKMTQueryAdapterInfo");
    if (!enumAdapters2 || !closeAdapter || !queryAdapterInfo)
    {
        PrintText(L"d3dkmt", L"missing_export");
    }
    else
    {
        std::array<D3dkmtAdapterInfo, kMaxAdapters> adapters{};
        D3dkmtEnumAdapters2 enumerate{};
        enumerate.NumAdapters = kMaxAdapters;
        enumerate.Adapters = adapters.data();
        const LONG enumerated = enumAdapters2(&enumerate);
        PrintHex(L"d3dkmt_enum", static_cast<unsigned long>(enumerated));
        if (enumerated >= 0)
        {
            kmtCount = enumerate.NumAdapters;
            for (ULONG index = 0; index < enumerate.NumAdapters; ++index)
            {
                D3dkmtCloseAdapter closing{};
                closing.hAdapter = adapters[index].hAdapter;
                auto close = wil::scope_exit([&] { (void)closeAdapter(&closing); });
                for (uint32_t dxgiIndex = 0; dxgiIndex < dxgiCount; ++dxgiIndex)
                {
                    if (dxgiLuids[dxgiIndex].LowPart == adapters[index].AdapterLuid.LowPart &&
                        dxgiLuids[dxgiIndex].HighPart == adapters[index].AdapterLuid.HighPart)
                    {
                        ++kmtJoined;
                        break;
                    }
                }
                D3dkmtAdapterPerfData perf{};
                D3dkmtQueryAdapterInfo query{};
                query.hAdapter = adapters[index].hAdapter;
                query.Type = kKmtAdapterPerfData;
                query.PrivateDriverData = &perf;
                query.PrivateDriverDataSize = static_cast<UINT>(sizeof(perf));
                const LONG queried = queryAdapterInfo(&query);
                D3dkmtAdapterPerfDataCaps caps{};
                D3dkmtQueryAdapterInfo capsQuery{};
                capsQuery.hAdapter = adapters[index].hAdapter;
                capsQuery.Type = kKmtAdapterPerfDataCaps;
                capsQuery.PrivateDriverData = &caps;
                capsQuery.PrivateDriverDataSize = static_cast<UINT>(sizeof(caps));
                const LONG capsStatus = queryAdapterInfo(&capsQuery);
                std::wcout << L"d3dkmt_adapter_" << index << L"_luid=" << std::hex
                           << adapters[index].AdapterLuid.HighPart << L':' << adapters[index].AdapterLuid.LowPart
                           << std::dec << L" perf=0x" << std::hex << static_cast<unsigned long>(queried) << L" caps=0x"
                           << static_cast<unsigned long>(capsStatus) << std::dec;
                if (queried >= 0)
                {
                    ++kmtPerfOk;
                    std::wcout << L" fan_rpm=" << perf.FanRpm << L" power_tenth_percent=" << perf.Power
                               << L" temp_deci_c=" << perf.Temperature;
                }
                if (capsStatus >= 0)
                {
                    std::wcout << L" max_fan_rpm=" << caps.MaxFanRpm << L" temp_warn_deci_c=" << caps.TemperatureWarning
                               << L" temp_max_deci_c=" << caps.TemperatureMax;
                }
                std::wcout << L'\n';
            }
        }
    }
    PrintU32(L"d3dkmt_adapters", kmtCount);
    PrintU32(L"d3dkmt_perf_ok", kmtPerfOk);
    PrintU32(L"d3dkmt_joined_luid", kmtJoined);
    PrintU32(L"d3dkmt_enum_cap_used", kMaxAdapters);
    PrintU32(L"d3dkmt_sdk_max_enum_adapters", 16);
    const wchar_t* gate = L"fallback";
    if (dxgiCount > 0 && kmtCount > 0)
    {
        gate = kmtPerfOk > 0 ? L"keep" : L"keep_identity_perf_fallback";
    }
    else if (dxgiCount > 0)
    {
        gate = L"dxgi_only";
    }
    else
    {
        gate = L"unsupported";
    }
    PrintText(L"gate_gpu_kmt", gate);
}

void CALLBACK OnIpInterfaceChange(PVOID, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) {}

void SpikeNetwork()
{
    MIB_IF_TABLE2* table = nullptr;
    const LARGE_INTEGER frequency = QueryFrequency();
    const LARGE_INTEGER start = QueryCounter();
    const DWORD status = GetIfTable2Ex(MibIfTableNormal, &table);
    const LARGE_INTEGER finish = QueryCounter();
    auto freeTable = wil::scope_exit(
        [&]
        {
            if (table)
            {
                FreeMibTable(table);
                table = nullptr;
            }
        });
    PrintHex(L"iftable2_status", status);
    PrintDouble(L"iftable2_us", Microseconds(start, finish, frequency));
    uint32_t up = 0;
    uint32_t hardware = 0;
    if (status == NO_ERROR && table)
    {
        PrintU32(L"iftable2_rows", table->NumEntries);
        for (ULONG index = 0; index < table->NumEntries; ++index)
        {
            const MIB_IF_ROW2& row = table->Table[index];
            if (row.OperStatus == IfOperStatusUp)
            {
                ++up;
            }
            if (row.InterfaceAndOperStatusFlags.HardwareInterface)
            {
                ++hardware;
            }
        }
        PrintU32(L"iftable2_up", up);
        PrintU32(L"iftable2_hardware", hardware);
        if (table->NumEntries > 0)
        {
            MIB_IF_ROW2 hot = table->Table[0];
            const LARGE_INTEGER hotStart = QueryCounter();
            const DWORD hotStatus = GetIfEntry2(&hot);
            const LARGE_INTEGER hotFinish = QueryCounter();
            PrintHex(L"ifentry2_status", hotStatus);
            PrintDouble(L"ifentry2_us", Microseconds(hotStart, hotFinish, frequency));
        }
    }
    HANDLE notification = nullptr;
    const DWORD notifyStatus = NotifyIpInterfaceChange(AF_UNSPEC, OnIpInterfaceChange, nullptr, FALSE, &notification);
    PrintHex(L"notify_ip_status", notifyStatus);
    if (notification)
    {
        const DWORD cancelStatus = CancelMibChangeNotify2(notification);
        PrintHex(L"notify_ip_cancel", cancelStatus);
    }
    PrintText(L"gate_net", status == NO_ERROR ? L"keep" : L"unsupported");
}

[[nodiscard]] uint32_t CountDeviceInterfaces(const GUID& guid)
{
    const HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    auto destroy = wil::scope_exit([&] { SetupDiDestroyDeviceInfoList(set); });
    uint32_t count = 0;
    for (;;)
    {
        SP_DEVICE_INTERFACE_DATA data{};
        data.cbSize = static_cast<DWORD>(sizeof(data));
        if (SetupDiEnumDeviceInterfaces(set, nullptr, &guid, count, &data) == FALSE)
        {
            break;
        }
        ++count;
        if (count >= 128)
        {
            break;
        }
    }
    return count;
}

[[nodiscard]] uint32_t CountClassDevices(const GUID& guid)
{
    const HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    auto destroy = wil::scope_exit([&] { SetupDiDestroyDeviceInfoList(set); });
    uint32_t count = 0;
    for (;;)
    {
        SP_DEVINFO_DATA data{};
        data.cbSize = static_cast<DWORD>(sizeof(data));
        if (SetupDiEnumDeviceInfo(set, count, &data) == FALSE)
        {
            break;
        }
        ++count;
        if (count >= 128)
        {
            break;
        }
    }
    return count;
}

void SpikeSensors()
{
    SYSTEM_POWER_STATUS power{};
    const BOOL powerOk = GetSystemPowerStatus(&power);
    PrintU32(L"power_status_ok", powerOk != FALSE ? 1 : 0);
    PrintU32(L"power_ac", power.ACLineStatus);
    PrintU32(L"power_battery_flag", power.BatteryFlag);
    PrintU32(L"power_battery_percent", power.BatteryLifePercent);

    const uint32_t batteryClass = CountClassDevices(GUID_DEVCLASS_BATTERY);
    const uint32_t batteryInterface = CountDeviceInterfaces(GUID_DEVICE_BATTERY);
    const uint32_t thermal = CountDeviceInterfaces(GUID_DEVICE_THERMAL_ZONE);
    const uint32_t fan = CountDeviceInterfaces(GUID_DEVICE_FAN);
    PrintU32(L"battery_class_devices", batteryClass);
    PrintU32(L"battery_interfaces", batteryInterface);
    PrintU32(L"thermal_zone_interfaces", thermal);
    PrintU32(L"fan_interfaces", fan);

    if (thermal > 0)
    {
        const HDEVINFO set =
            SetupDiGetClassDevsW(&GUID_DEVICE_THERMAL_ZONE, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set != INVALID_HANDLE_VALUE)
        {
            auto destroy = wil::scope_exit([&] { SetupDiDestroyDeviceInfoList(set); });
            SP_DEVICE_INTERFACE_DATA data{};
            data.cbSize = static_cast<DWORD>(sizeof(data));
            if (SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVICE_THERMAL_ZONE, 0, &data) != FALSE)
            {
                DWORD needed = 0;
                SetupDiGetDeviceInterfaceDetailW(set, &data, nullptr, 0, &needed, nullptr);
                if (needed > sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) && needed < 4096)
                {
                    std::vector<std::byte> detailBytes(needed);
                    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBytes.data());
                    detail->cbSize = static_cast<DWORD>(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W));
                    if (SetupDiGetDeviceInterfaceDetailW(set, &data, detail, needed, nullptr, nullptr) != FALSE)
                    {
                        wil::unique_hfile zone{CreateFileW(detail->DevicePath, GENERIC_READ,
                                                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                                           FILE_FLAG_OVERLAPPED, nullptr)};
                        PrintU32(L"thermal_zone_open", static_cast<uint32_t>(static_cast<bool>(zone)));
                        if (zone)
                        {
                            THERMAL_WAIT_READ wait{};
                            wait.Timeout = THERMAL_WAIT_READ_TIMEOUT_IMMEDIATE;
                            ULONG temperature = 0;
                            DWORD returned = 0;
                            const bool readOk = DeviceIoControlBounded(
                                zone.get(), IOCTL_THERMAL_READ_TEMPERATURE, &wait, static_cast<DWORD>(sizeof(wait)),
                                &temperature, static_cast<DWORD>(sizeof(temperature)), &returned);
                            PrintU32(L"thermal_read_ok", readOk ? 1 : 0);
                            if (readOk)
                            {
                                PrintU32(L"thermal_tenths_k", temperature);
                            }
                        }
                    }
                }
            }
        }
    }

    PrintText(L"gate_bat", batteryInterface > 0 ? L"keep_probe" : L"empty_on_primary");
    PrintText(L"gate_thrm", thermal > 0 ? L"keep_probe" : L"empty_on_primary");
    PrintText(L"gate_fan", L"presence_only_no_rpm");
    PrintText(L"fan_rpm_from_presence", L"not_invented");
}
} // namespace

void SpikePhase0Surfaces()
{
    std::wcout << L"surfaces_begin\n";
    SpikeCounters();
    const PdhProbe diskBefore = ProbePdh(L"\\PhysicalDisk(*)\\Disk Reads/sec", false);
    PrintPdhProbe(L"pdh_disk_pre_ioctl", diskBefore);
    const PerfProbe diskPerf = ProbePerfLib(L"PhysicalDisk");
    PrintPerfProbe(L"perflib_disk", diskPerf);
    PrintText(L"gate_ctr_disk_counters", ChooseCounterBackend(diskBefore, diskPerf));
    const DiskIoctlResult ioctl = SpikeDiskIoctl();
    SpikeDiskSideEffect(diskBefore, ioctl);
    SpikeGpu();
    SpikeNetwork();
    SpikeSensors();
    std::wcout << L"surfaces_end\n";
}
