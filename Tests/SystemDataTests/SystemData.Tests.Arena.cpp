#include "PlugInterfaces/Data.h"
#include "PlugInterfaces/Factory.h"
#include "Plugins/SystemData/NativeQuery.h"
#include "Plugins/SystemData/NtLayout.h"
#include "Plugins/SystemData/SystemData.Tests.Contract.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <memory>
#include <psapi.h>
#include <stdexcept>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct Region final
{
    void* base = nullptr;
    uint64_t reservedBytes = 0;
    uint64_t committedBytes = 0;
};

Region QueryRegion(const void* address)
{
    MEMORY_BASIC_INFORMATION memory{};
    Require(VirtualQuery(address, &memory, sizeof(memory)) == sizeof(memory), "source region query failed");
    Region region{memory.AllocationBase};
    uintptr_t next = reinterpret_cast<uintptr_t>(region.base);
    while (VirtualQuery(reinterpret_cast<const void*>(next), &memory, sizeof(memory)) == sizeof(memory) &&
           memory.AllocationBase == region.base)
    {
        Require(memory.Type == MEM_PRIVATE && memory.RegionSize != 0, "source region is not private storage");
        region.reservedBytes += memory.RegionSize;
        if (memory.State == MEM_COMMIT)
            region.committedBytes += memory.RegionSize;
        else
            Require(memory.State == MEM_RESERVE, "source region has unexpected page state");
        next += memory.RegionSize;
    }
    return region;
}

struct Source final
{
    wil::com_ptr_nothrow<IRedXeDataSource> source;
    wil::com_ptr_nothrow<IRedXeSystemDataTestSource> test;
};

Source Create(RedXeCreateFn create)
{
    void* object = nullptr;
    Require(create(__uuidof(IRedXeDataSource), nullptr, nullptr, "builtin.system-data", &object) == S_OK && object,
            "arena source creation failed");
    Source owner;
    owner.source.attach(static_cast<IRedXeDataSource*>(object));
    Require(owner.source->QueryInterface(__uuidof(IRedXeSystemDataTestSource), owner.test.put_void()) == S_OK,
            "arena test interface is unavailable");
    return owner;
}

SystemDataTestDiagnostics Diagnostics(const Source& owner)
{
    SystemDataTestDiagnostics diagnostics{sizeof(diagnostics)};
    Require(owner.test->GetTestDiagnostics(&diagnostics) == S_OK, "arena diagnostics failed");
    const auto region = QueryRegion(owner.source.get());
    Require(region.base == diagnostics.arenaBase && region.reservedBytes == diagnostics.arenaReservedBytes &&
                region.committedBytes == diagnostics.arenaCommittedBytes,
            "arena diagnostics disagree with actual page ownership");
    Require(diagnostics.sourceStorageBytes < 16U * 1024U * 1024U && diagnostics.controlStorageBytes <= 64U * 1024U &&
                diagnostics.arenaReservedBytes == 16U * 1024U * 1024U,
            "arena exceeds its bounded storage contract");
    return diagnostics;
}

HRESULT TryCollect(IRedXeDataSource& source, const char* const* ids, uint32_t count,
                   const RedXeDataCollectResult** result)
{
    const RedXeDataCollectRequest request{sizeof(request), ids, count};
    return source.CollectSnapshots(&request, result);
}

const RedXeDataCollectResult& Collect(const Source& owner, const char* const* ids, uint32_t count)
{
    const RedXeDataCollectResult* result = nullptr;
    Require(TryCollect(*owner.source, ids, count, &result) == S_OK && result && result->snapshotCount == count,
            "arena dataset collection failed");
    const auto diagnostics = Diagnostics(owner);
    const auto base = reinterpret_cast<uintptr_t>(diagnostics.arenaBase);
    for (uint32_t snapshot = 0; snapshot < result->snapshotCount; ++snapshot)
    {
        const auto& data = *result->snapshots[snapshot];
        for (uint32_t row = 0; row < data.rowCount; ++row)
        {
            const uintptr_t values = reinterpret_cast<uintptr_t>(data.rows[row].values);
            Require(values >= base + 64U * 1024U && values + data.rows[row].valueCount * sizeof(RedXeDataValue) <=
                                                        base + diagnostics.sourceStorageBytes,
                    "arena output values escape source-owned storage");
        }
    }
    return *result;
}

void TestPreflight(RedXeCreateFn create)
{
    auto owner = Create(create);
    const auto cold = Diagnostics(owner);
    constexpr const char* summary[] = {"system.summary"};
    Require(owner.test->FailArenaCommitForTest(1) == S_OK, "partial commitment fault could not be armed");
    const RedXeDataCollectResult* result = reinterpret_cast<const RedXeDataCollectResult*>(1);
    Require(TryCollect(*owner.source, summary, 1, &result) == E_OUTOFMEMORY && !result,
            "partial first-use failure published a batch");
    const auto partial = Diagnostics(owner);
    Require(partial.arenaCommittedBytes > cold.arenaCommittedBytes && partial.arenaCommittedBytes < 512U * 1024U &&
                partial.processCollections == 0 && partial.processHistoryCommits == 0,
            "partial preflight sampled data or committed unrelated storage");
    Require(Collect(owner, summary, 1).sequence == 1, "failed preflight consumed a sequence");
    const auto ready = Diagnostics(owner);
    Require(owner.test->FailArenaCommitForTest(0) == S_OK, "reuse fault could not be armed");
    Require(Collect(owner, summary, 1).sequence == 2 &&
                Diagnostics(owner).arenaCommitAttempts == ready.arenaCommitAttempts,
            "steady collection recommits storage");
    constexpr const char* security[] = {"security.posture"};
    result = reinterpret_cast<const RedXeDataCollectResult*>(1);
    Require(TryCollect(*owner.source, security, 1, &result) == E_OUTOFMEMORY && !result,
            "steady reuse consumed the pending first-use fault");
    Require(Collect(owner, security, 1).sequence == 3, "commit failure advanced a successful sequence");
}

void TestActivityCommitHistory(RedXeCreateFn create)
{
    auto owner = Create(create);
    auto walk = std::make_unique<RedXeNativeWalkSample>();
    walk->valid = true;
    walk->processCount = 1;
    walk->imageArenaUsed = 2;
    walk->imageArena[0] = L'x';
    auto& process = walk->processes[0];
    process.processId = 0x0FFFFFFDU;
    process.createTime = 100;
    process.userTime = 1000;
    process.imageCharacters = 1;
    constexpr const char* activity[] = {"process.activity"};
    constexpr const char* activityAndNetwork[] = {"process.activity", "network.interface"};
    constexpr const char* combined[] = {"process.activity", "process.list"};
    Require(owner.test->SetNextProcessWalkForTest(walk.get(), 10000) == S_OK, "arena walk seed failed");
    Require(Collect(owner, activity, 1).sequence == 1, "arena activity seed failed");
    const auto initial = Diagnostics(owner);
    Require(initial.arenaCommittedBytes < 6U * 1024U * 1024U && initial.processValueStride == 8,
            "activity commits the legacy or thread capacity");
    process.userTime += 1000;
    Require(owner.test->SetNextProcessWalkForTest(walk.get(), 20000) == S_OK &&
                owner.test->FailArenaCommitForTest(0) == S_OK,
            "arena history failure could not be armed");
    const RedXeDataCollectResult* failed = reinterpret_cast<const RedXeDataCollectResult*>(1);
    Require(TryCollect(*owner.source, activityAndNetwork, 2, &failed) == E_OUTOFMEMORY && !failed,
            "mixed preflight failure publishes activity");
    const auto afterFailure = Diagnostics(owner);
    Require(afterFailure.processCollections == initial.processCollections &&
                afterFailure.processHistoryCommits == initial.processHistoryCommits &&
                afterFailure.arenaCommittedBytes == initial.arenaCommittedBytes,
            "failed first use changed activity history or existing commitment");
    const auto& resumed = Collect(owner, activity, 1);
    Require(resumed.sequence == 2 && resumed.snapshots[0]->rows[0].values[2].quality == RedXeDataQualityGood &&
                std::abs(resumed.snapshots[0]->rows[0].values[2].float64Value - 10.0) < 0.0001,
            "first-use failure consumed the pending walk or activity rate history");
    process.userTime += 1000;
    Require(owner.test->SetNextProcessWalkForTest(walk.get(), 30000) == S_OK &&
                owner.test->FailArenaCommitForTest(0) == S_OK,
            "detail expansion failure could not be armed");
    failed = reinterpret_cast<const RedXeDataCollectResult*>(1);
    const auto beforeExpansion = Diagnostics(owner);
    Require(TryCollect(*owner.source, combined, 2, &failed) == E_OUTOFMEMORY && !failed &&
                Diagnostics(owner).arenaCommittedBytes == beforeExpansion.arenaCommittedBytes,
            "failed detail expansion partially publishes or touches its suffix");
    const auto& expanded = Collect(owner, combined, 2);
    Require(expanded.sequence == 3 && expanded.snapshots[0]->rows[0].values == expanded.snapshots[1]->rows[0].values &&
                expanded.snapshots[0]->columnCount == 8 && expanded.snapshots[1]->columnCount == 39 &&
                std::abs(expanded.snapshots[0]->rows[0].values[2].float64Value - 10.0) < 0.0001,
            "detail commitment moved leading values or reset rate history");
    const auto detailed = Diagnostics(owner);
    Require(detailed.arenaCommittedBytes - beforeExpansion.arenaCommittedBytes ==
                2048U * 31U * sizeof(RedXeDataValue) + 2048U * sizeof(RedXeDataRow),
            "detail expansion commits unrelated storage");
    Require(owner.test->FailArenaCommitForTest(0) == S_OK, "activity reuse fault failed");
    (void)Collect(owner, activity, 1);
    Require(Diagnostics(owner).arenaCommitAttempts == detailed.arenaCommitAttempts &&
                Diagnostics(owner).processValueStride == 8,
            "activity after detail reinitializes or recommits legacy storage");
}

constexpr std::array kDefaultDataSets{
    "cpu.summary",      "cpu.logical",   "memory.summary", "process.activity", "network.interface",
    "network.protocol", "storage.disk",  "storage.volume", "gpu.adapter",      "system.summary",
    "gpu.process",      "power.summary", "battery.list",   "thermal.sensor",   "fan.sensor",
};

template <typename Function> Function Resolve(HMODULE module, const char* name)
{
    const FARPROC address = GetProcAddress(module, name);
    Require(address != nullptr, "arena test export is unavailable");
    return std::bit_cast<Function>(address);
}

SystemDataTestArenaOwnership Ownership(SystemDataTestArenaOwnershipFn get)
{
    SystemDataTestArenaOwnership ownership{sizeof(ownership)};
    Require(get(&ownership) == S_OK, "arena module ownership query failed");
    return ownership;
}

bool SameOwnership(const SystemDataTestArenaOwnership& left, const SystemDataTestArenaOwnership& right)
{
    return left.sourceCount == right.sourceCount && left.reservedBytes == right.reservedBytes &&
           left.committedBytes == right.committedBytes && left.gpuOwners == right.gpuOwners &&
           left.netOwners == right.netOwners && left.powerOwners == right.powerOwners;
}

void TestFamilyLifetimes(RedXeCreateFn create, SystemDataTestArenaOwnershipFn get)
{
    const auto baseline = Ownership(get);
    struct Case final
    {
        const char* id;
        uint64_t maximumBytes;
        uint32_t gpu;
        uint32_t net;
        uint32_t power;
    };
    constexpr std::array cases{
        Case{"source.status", 64U * 1024U, 0, 0, 0},      Case{"security.posture", 64U * 1024U, 0, 0, 0},
        Case{"system.summary", 512U * 1024U, 0, 0, 0},    Case{"gpu.adapter", 2U * 1024U * 1024U, 1, 0, 0},
        Case{"network.protocol", 1024U * 1024U, 0, 1, 0}, Case{"power.summary", 128U * 1024U, 0, 0, 1},
    };
    for (const auto& test : cases)
    {
        {
            auto owner = Create(create);
            const auto cold = Diagnostics(owner);
            const auto before = Ownership(get);
            Require(cold.arenaCommittedBytes <= 64U * 1024U && before.gpuOwners == baseline.gpuOwners &&
                        before.netOwners == baseline.netOwners && before.powerOwners == baseline.powerOwners,
                    "cold source constructs unused native owners");
            const char* ids[] = {test.id};
            (void)Collect(owner, ids, 1);
            const auto first = Diagnostics(owner);
            const auto active = Ownership(get);
            Require(first.arenaCommittedBytes <= test.maximumBytes &&
                        active.gpuOwners == baseline.gpuOwners + test.gpu &&
                        active.netOwners == baseline.netOwners + test.net &&
                        active.powerOwners == baseline.powerOwners + test.power,
                    "single dataset commits or constructs an unrelated family");
            (void)Collect(owner, ids, 1);
            Require(Diagnostics(owner).arenaCommitAttempts == first.arenaCommitAttempts,
                    "single-dataset steady use recommits storage");
        }
        Require(SameOwnership(Ownership(get), baseline), "single-family teardown retains an arena or native owner");
    }
    void* arena = nullptr;
    {
        auto owner = Create(create);
        arena = const_cast<void*>(Diagnostics(owner).arenaBase);
        (void)Collect(owner, kDefaultDataSets.data(), static_cast<uint32_t>(kDefaultDataSets.size()));
        const auto settled = Diagnostics(owner);
        Require(settled.arenaCommittedBytes <= 10U * 1024U * 1024U,
                "default System datasets exceed ten MiB of committed arena storage");
        (void)Collect(owner, kDefaultDataSets.data(), static_cast<uint32_t>(kDefaultDataSets.size()));
        Require(Diagnostics(owner).arenaCommitAttempts == settled.arenaCommitAttempts,
                "default settled collection commits additional pages");
        constexpr const char* threads[] = {"thread.list"};
        const auto& threadBatch = Collect(owner, threads, 1);
        const auto threaded = Diagnostics(owner);
        Require(threadBatch.snapshots[0]->rowCount != 0 &&
                    threaded.arenaCommittedBytes - settled.arenaCommittedBytes >= 4U * 1024U * 1024U - 65536U,
                "default System eagerly commits or loses optional thread storage");
        const RedXeDataSetDescriptor* descriptors = nullptr;
        uint32_t count = 0;
        Require(owner.source->GetDataSets(&descriptors, &count) == S_OK && count <= RedXeDataCollectMaximumDataSets,
                "arena catalog query failed");
        std::array<const char*, RedXeDataCollectMaximumDataSets> ids{};
        for (uint32_t index = 0; index < count; ++index)
            ids[index] = descriptors[index].dataSetId;
        (void)Collect(owner, ids.data(), count);
        const auto full = Diagnostics(owner);
        Require(full.arenaCommittedBytes < 16U * 1024U * 1024U &&
                    full.arenaCommittedBytes - settled.arenaCommittedBytes > 6U * 1024U * 1024U,
                "fully-used source exceeds its ceiling or default pays for unused families");
        (void)Collect(owner, ids.data(), count);
        Require(Diagnostics(owner).arenaCommitAttempts == full.arenaCommitAttempts,
                "full-catalog steady collection recommits storage");
        wil::com_ptr_nothrow<IUnknown> identity;
        wil::com_ptr_nothrow<IUnknown> testIdentity;
        Require(owner.source->QueryInterface(IID_IUnknown, identity.put_void()) == S_OK &&
                    owner.test->QueryInterface(IID_IUnknown, testIdentity.put_void()) == S_OK &&
                    identity.get() == testIdentity.get(),
                "arena allocation changed controlling COM identity");
        std::wcout << L"Arena storage: control=" << settled.controlStorageBytes << L" default_commit="
                   << settled.arenaCommittedBytes << L" full_commit=" << full.arenaCommittedBytes
                   << L" storage_ceiling=" << full.sourceStorageBytes << L" bytes.\n";
    }
    MEMORY_BASIC_INFORMATION freed{};
    Require(VirtualQuery(arena, &freed, sizeof(freed)) == sizeof(freed) && freed.State == MEM_FREE &&
                SameOwnership(Ownership(get), baseline),
            "final COM release retains committed/reserved pages or native owners");
}

void TestOptionalThreadWalk(SystemDataTestDecodeWalkFn decode, RedXeCreateFn create)
{
    constexpr wchar_t name[] = L"walk.exe";
    constexpr size_t nameOffset = sizeof(RedXeNtProcessRecord) + 2 * sizeof(RedXeNtThreadRecord);
    constexpr size_t bytes = nameOffset + sizeof(name);
    std::array<uint64_t, (bytes + 7) / 8> storage{};
    auto* buffer = reinterpret_cast<std::byte*>(storage.data());
    auto* record = ::new (buffer) RedXeNtProcessRecord{};
    record->numberOfThreads = 2;
    record->uniqueProcessId = reinterpret_cast<HANDLE>(12345);
    record->inheritedFromUniqueProcessId = reinterpret_cast<HANDLE>(123);
    record->createTime = 100;
    record->userTime = 200;
    record->imageName.Length = static_cast<USHORT>(sizeof(name) - sizeof(wchar_t));
    record->imageName.MaximumLength = static_cast<USHORT>(sizeof(name));
    record->imageName.Buffer = reinterpret_cast<wchar_t*>(buffer + nameOffset);
    std::memcpy(record->imageName.Buffer, name, sizeof(name));
    auto* threads = ::new (buffer + sizeof(RedXeNtProcessRecord)) RedXeNtThreadRecord[2]{};
    for (uint32_t index = 0; index < 2; ++index)
    {
        threads[index].clientId.UniqueProcess = record->uniqueProcessId;
        threads[index].clientId.UniqueThread = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(23456 + index));
        threads[index].createTime = 1000 + index;
        threads[index].userTime = 2000 + index;
        threads[index].contextSwitches = 9 + index;
    }
    auto sample = std::make_unique<RedXeNativeWalkSample>();
    Require(decode(buffer, static_cast<uint32_t>(bytes), sample.get()) != FALSE && sample->valid &&
                sample->processCount == 1 && sample->processes[0].processId == 12345 &&
                sample->processes[0].threadCount == 2 && sample->threadCount == 0 && !sample->threadsTruncated &&
                std::wcscmp(sample->imageArena.data(), name) == 0,
            "process-only native decode loses fields or copies optional thread rows");
    Require(decode(buffer, static_cast<uint32_t>(nameOffset - 1), sample.get()) == FALSE && !sample->valid,
            "process-only decode accepts truncated thread bytes");
    Require(decode(buffer, 0, sample.get()) == FALSE && decode(nullptr, 0, sample.get()) == FALSE &&
                decode(buffer, static_cast<uint32_t>(bytes), nullptr) == FALSE,
            "native decode accepts an empty or null input");
    record->nextEntryOffset = static_cast<ULONG>(sizeof(RedXeNtProcessRecord));
    Require(decode(buffer, static_cast<uint32_t>(bytes), sample.get()) == FALSE && !sample->valid,
            "native decode accepts overlapping process/thread records");
    record->nextEntryOffset = 0;
    Require(decode(buffer + 1, static_cast<uint32_t>(bytes - 1), sample.get()) == FALSE,
            "native decode accepts an unaligned record");
    auto* image = record->imageName.Buffer;
    record->imageName.Buffer = reinterpret_cast<wchar_t*>(buffer + bytes + 2);
    Require(decode(buffer, static_cast<uint32_t>(bytes), sample.get()) == FALSE,
            "native decode accepts an image outside ReturnLength");
    record->imageName.Buffer = image;
    std::array<RedXeNativeThreadRow, 2> scratch{};
    sample->threads = std::span<RedXeNativeThreadRow>(scratch.data(), 1);
    Require(decode(buffer, static_cast<uint32_t>(bytes), sample.get()) != FALSE && sample->threadCount == 1 &&
                sample->threadsTruncated && sample->threads[0].threadId == 23456,
            "optional thread decode does not preserve its cap");
    sample->threads = scratch;
    Require(decode(buffer, static_cast<uint32_t>(bytes), sample.get()) != FALSE && sample->threadCount == 2 &&
                !sample->threadsTruncated && sample->threads[1].threadId == 23457 &&
                sample->threads[1].createTime == 1001 && sample->threads[1].contextSwitches == 10,
            "optional native thread copy loses identity/history fields");
    auto owner = Create(create);
    Require(owner.test->SetNextProcessWalkForTest(sample.get(), 10000) == S_OK,
            "borrowed-thread fixture was not copied");
    scratch[1].threadId = 0;
    constexpr const char* ids[] = {"thread.list"};
    const auto& batch = Collect(owner, ids, 1);
    Require(batch.snapshots[0]->rowCount == 2 && batch.snapshots[0]->rows[1].values[2].uint64Value == 23457,
            "test walk retained the caller's thread scratch pointer");
}
} // namespace

void TestSystemDataArena(RedXeCreateFn create, HMODULE module)
{
    const auto get = Resolve<SystemDataTestArenaOwnershipFn>(module, kSystemDataTestArenaOwnershipExport);
    const auto fault = Resolve<SystemDataTestAllocationFaultFn>(module, kSystemDataTestAllocationFaultExport);
    const auto baseline = Ownership(get);
    Require(get(nullptr) == E_POINTER && fault(3) == E_INVALIDARG, "arena test controls accept invalid inputs");
    for (uint32_t stage : {1U, 2U})
    {
        Require(fault(stage) == S_OK, "arena allocation fault could not be armed");
        void* failed = reinterpret_cast<void*>(1);
        Require(create(__uuidof(IRedXeDataSource), nullptr, nullptr, "builtin.system-data", &failed) == E_OUTOFMEMORY &&
                    !failed && SameOwnership(Ownership(get), baseline),
                "failed reservation/control commitment publishes or retains a source");
    }
    void* object = nullptr;
    Require(create(__uuidof(IRedXeDataSource), nullptr, nullptr, "builtin.system-data", &object) == S_OK && object,
            "arena source creation failed");
    wil::com_ptr_nothrow<IRedXeDataSource> source;
    source.attach(static_cast<IRedXeDataSource*>(object));
    const auto cold = QueryRegion(source.get());
    Require(cold.committedBytes <= 64U * 1024U, "cold source eagerly commits unused tables");
    Require(cold.reservedBytes == 16U * 1024U * 1024U, "source does not own one bounded arena reservation");
    source.reset();
    TestPreflight(create);
    TestActivityCommitHistory(create);
    TestFamilyLifetimes(create, get);
    TestOptionalThreadWalk(Resolve<SystemDataTestDecodeWalkFn>(module, kSystemDataTestDecodeWalkExport), create);
    Require(SameOwnership(Ownership(get), baseline), "arena scenarios retain source ownership");
}
