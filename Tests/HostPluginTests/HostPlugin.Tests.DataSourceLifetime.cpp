#include "PlugInterfaces/FactoryImpl.h"
#include "PluginHost.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

struct PluginHostDataSourceTestAccess final
{
    [[nodiscard]] static size_t RecordsBytes(PluginHost& host) noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&host._subscriptionLock);
        size_t bytes = 0;
        for (const auto& runtime : host._providers)
            if (runtime.dataSets)
                bytes += runtime.descriptorCount * sizeof(PluginHost::DataSetRuntime);
        return bytes;
    }
    [[nodiscard]] static size_t RecordOwners(PluginHost& host) noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&host._subscriptionLock);
        size_t count = 0;
        for (const auto& runtime : host._providers)
            count += runtime.dataSets ? 1U : 0U;
        return count;
    }
    [[nodiscard]] static size_t Providers(PluginHost& host) noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&host._subscriptionLock);
        return host._providerCount;
    }
    static void FailNextAllocation(PluginHost& host) noexcept
    {
        host._failDataSetAllocationForTesting.store(true, std::memory_order_relaxed);
    }
    [[nodiscard]] static HRESULT Validate(PluginHost& host, const RedXeDataSetDescriptor* descriptors,
                                          uint32_t count) noexcept
    {
        PluginHost::ProviderRuntime candidate;
        candidate.descriptors = descriptors;
        candidate.descriptorCount = count;
        return host.ValidateDataSets(candidate);
    }
    [[nodiscard]] static constexpr size_t FixedBytes() noexcept
    {
        return sizeof(PluginHost::_providers);
    }
    [[nodiscard]] static size_t Sources(PluginHost& host) noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&host._subscriptionLock);
        size_t count = 0;
        for (const auto& runtime : host._providers)
            count += runtime.source ? 1U : 0U;
        return count;
    }

    [[nodiscard]] static HRESULT ReplaceSource(PluginHost& host, wil::com_ptr_nothrow<IRedXeDataSource> source) noexcept
    {
        // Replace only before the first subscription, retaining the production facade and scheduling machinery.
        wil::com_ptr_nothrow<IRedXeDataSource> retired;
        HRESULT result = E_UNEXPECTED;
        {
            const auto guard = wil::AcquireSRWLockExclusive(&host._subscriptionLock);
            if (host._providerCount == 1 && !host._worker.joinable())
            {
                auto& runtime = host._providers[0];
                retired = std::move(runtime.source);
                runtime.source = std::move(source);
                runtime.dataSets.reset();
                result = runtime.source->GetDataSets(&runtime.descriptors, &runtime.descriptorCount);
                if (SUCCEEDED(result))
                    result = host.ValidateDataSets(runtime);
            }
        }
        retired.reset();
        return result;
    }
};

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct Gate final
{
    wil::unique_event_nothrow entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_event_nothrow resume{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> timedOut{false};

    void Wait() noexcept
    {
        SetEvent(entered.get());
        if (WaitForSingleObject(resume.get(), 5000) != WAIT_OBJECT_0)
            timedOut.store(true);
    }
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
struct SourceProbe final
{
    Gate collection;
    wil::unique_event_nothrow destroyed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<uint32_t> collections{0};
    std::atomic<uint32_t> destructions{0};
    bool pauseCollection = false;
};
#pragma warning(pop)

constexpr RedXeDataColumnDescriptor kColumns[] = {
    {sizeof(RedXeDataColumnDescriptor), "value", L"Value", L"", RedXeDataValueTypeUInt64},
};
constexpr RedXeDataSetDescriptor kDataSets[] = {
    {sizeof(RedXeDataSetDescriptor), "cpu.summary", L"CPU", L"Controlled source", kColumns, 1, 1, 1000, 0},
};

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class ControlledSource final : public RedXeComObject<ControlledSource, IRedXeDataSource>
{
  public:
    explicit ControlledSource(SourceProbe& probe, const RedXeDataSetDescriptor* descriptors = kDataSets,
                              uint32_t count = 1) noexcept
        : _probe(probe), _descriptors(descriptors), _count(count)
    {
    }
    ~ControlledSource()
    {
        _probe.destructions.fetch_add(1);
        SetEvent(_probe.destroyed.get());
    }

    HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors, uint32_t* count) noexcept override
    {
        if (!descriptors || !count)
            return E_POINTER;
        *descriptors = _descriptors;
        *count = _count;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CollectSnapshots(const RedXeDataCollectRequest* request,
                                               const RedXeDataCollectResult** result) noexcept override
    {
        if (!request || !result || request->sizeBytes != sizeof(*request) || request->dataSetCount != 1 ||
            !request->dataSetIds || !request->dataSetIds[0])
            return E_INVALIDARG;
        *result = nullptr;
        bool known = false;
        for (uint32_t index = 0; index < _count; ++index)
            known |= RedXeAsciiEqualsIgnoreCase(_descriptors[index].dataSetId, request->dataSetIds[0]);
        if (!known)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        _probe.collections.fetch_add(1);
        if (_probe.pauseCollection)
            _probe.collection.Wait();
        FILETIME time{};
        GetSystemTimePreciseAsFileTime(&time);
        const uint64_t timestamp = (static_cast<uint64_t>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
        _snapshot = {sizeof(_snapshot), 0, request->dataSetIds[0], ++_sequence, timestamp, &_row, 1, 1};
        _result = {sizeof(_result), 1, _sequence, timestamp, _snapshots.data()};
        *result = &_result;
        return S_OK;
    }

  private:
    SourceProbe& _probe;
    const RedXeDataSetDescriptor* _descriptors;
    uint32_t _count;
    uint64_t _sequence = 0;
    RedXeDataValue _value{sizeof(RedXeDataValue), RedXeDataValueTypeUInt64, RedXeDataQualityGood, {42}, 0};
    RedXeDataRow _row{sizeof(RedXeDataRow), &_value, 1};
    RedXeDataSnapshot _snapshot{};
    std::array<const RedXeDataSnapshot*, 1> _snapshots{&_snapshot};
    RedXeDataCollectResult _result{};
};
#pragma warning(pop)

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class ControlledSink final : public RedXeComObject<ControlledSink, IRedXeDataSink>
{
  public:
    explicit ControlledSink(Gate* callbackGate = nullptr, const char* dataSetId = "cpu.summary") noexcept
        : _callbackGate(callbackGate), _dataSetId(dataSetId)
    {
    }
    wil::unique_event_nothrow received{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<bool> valid{true};

    HRESULT STDMETHODCALLTYPE OnDataSnapshot(const RedXeDataSnapshot* snapshot) noexcept override
    {
        if (!snapshot || !RedXeAsciiEqualsIgnoreCase(snapshot->dataSetId, _dataSetId) || snapshot->rowCount != 1 ||
            !snapshot->rows || snapshot->rows[0].values[0].uint64Value != 42)
            valid.store(false);
        callbacks.fetch_add(1);
        if (_callbackGate)
            _callbackGate->Wait();
        SetEvent(received.get());
        return S_OK;
    }

  private:
    Gate* _callbackGate;
    const char* _dataSetId;
};
#pragma warning(pop)

wil::com_ptr_nothrow<IRedXeDataProvider> Provider(PluginHost& host)
{
    wil::com_ptr_nothrow<IRedXeDataProvider> provider;
    Require(SUCCEEDED(host.GetDataProvider("builtin.system-data", provider.put())), "provider lookup failed");
    return provider;
}

void InstallControlledSource(PluginHost& host, SourceProbe& probe,
                             const RedXeDataSetDescriptor* descriptors = kDataSets, uint32_t count = 1)
{
    Require(probe.collection.entered && probe.collection.resume && probe.destroyed, "source barriers failed");
    wil::com_ptr_nothrow<IRedXeDataSource> source;
    source.attach(new ControlledSource(probe, descriptors, count));
    Require(SUCCEEDED(PluginHostDataSourceTestAccess::ReplaceSource(host, std::move(source))),
            "source replacement failed");
}

wil::com_ptr_nothrow<IRedXeDataSubscription> Subscribe(IRedXeDataProvider* provider, IRedXeDataSink* sink)
{
    wil::com_ptr_nothrow<IRedXeDataSubscription> subscription;
    const RedXeDataSubscriptionOptions options{sizeof(options), "cpu.summary", 1000};
    Require(SUCCEEDED(provider->Subscribe(&options, sink, subscription.put())), "subscription creation failed");
    return subscription;
}

void ExpectRetired(PluginHost& host, SourceProbe& probe)
{
    Require(WaitForSingleObject(probe.destroyed.get(), 5000) == WAIT_OBJECT_0, "unleased source was not reclaimed");
    Require(probe.destructions.load() == 1 && PluginHostDataSourceTestAccess::Sources(host) == 0 &&
                PluginHostDataSourceTestAccess::RecordOwners(host) == 0,
            "source cleanup was incomplete or repeated");
}

struct DescriptorFixture final
{
    std::array<std::array<char, 32>, 257> ids{};
    std::array<RedXeDataSetDescriptor, 257> descriptors{};
    DescriptorFixture() noexcept
    {
        for (uint32_t index = 0; index < descriptors.size(); ++index)
        {
            (void)sprintf_s(ids[index].data(), ids[index].size(), "fixture.data.%u", index);
            descriptors[index] = kDataSets[0];
            descriptors[index].dataSetId = ids[index].data();
        }
    }
};

void TestDescriptorBoundsAndValidationOrder()
{
    auto fixture = std::make_unique<DescriptorFixture>();
    PluginHost host;
    const HRESULT invalid = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    Require(PluginHostDataSourceTestAccess::Validate(host, nullptr, 1) == invalid &&
                PluginHostDataSourceTestAccess::Validate(host, fixture->descriptors.data(), 0) == invalid &&
                PluginHostDataSourceTestAccess::Validate(host, fixture->descriptors.data(), 257) == invalid,
            "descriptor bounds did not reject before allocation");
    const auto final = fixture->descriptors[255];
    fixture->descriptors[255].dataSetId = fixture->descriptors[0].dataSetId;
    Require(PluginHostDataSourceTestAccess::Validate(host, fixture->descriptors.data(), 256) ==
                HRESULT_FROM_WIN32(ERROR_DUP_NAME),
            "duplicate final descriptor was accepted");
    fixture->descriptors[255] = final;
    PluginHostDataSourceTestAccess::FailNextAllocation(host);
    fixture->descriptors[255].columnCount = 0;
    Require(PluginHostDataSourceTestAccess::Validate(host, fixture->descriptors.data(), 256) == invalid,
            "invalid final descriptor consumed the allocation failure");
    fixture->descriptors[255] = final;
    Require(PluginHostDataSourceTestAccess::Validate(host, fixture->descriptors.data(), 256) == E_OUTOFMEMORY &&
                PluginHostDataSourceTestAccess::Validate(host, fixture->descriptors.data(), 256) == S_OK &&
                PluginHostDataSourceTestAccess::RecordOwners(host) == 0,
            "validated descriptor allocation did not fail transactionally and retry");
}

void TestAllocationFailureAndFacadeRevisit()
{
    PluginHost host;
    Require(PluginHostDataSourceTestAccess::RecordOwners(host) == 0 &&
                PluginHostDataSourceTestAccess::RecordsBytes(host) == 0,
            "cold host retained dataset records");
    PluginHostDataSourceTestAccess::FailNextAllocation(host);
    IRedXeDataProvider* rejected = reinterpret_cast<IRedXeDataProvider*>(1);
    Require(host.GetDataProvider("builtin.system-data", &rejected) == E_OUTOFMEMORY && !rejected &&
                PluginHostDataSourceTestAccess::Sources(host) == 0 &&
                PluginHostDataSourceTestAccess::Providers(host) == 0 &&
                PluginHostDataSourceTestAccess::RecordOwners(host) == 0,
            "failed first publication left a source, facade or record owner");
    auto provider = Provider(host);
    const auto* identity = provider.get();
    Require(PluginHostDataSourceTestAccess::RecordsBytes(host) == 23 * 24,
            "production source did not own count-sized scheduling records");
    std::wcout << L"provider_storage fixed_bytes=" << PluginHostDataSourceTestAccess::FixedBytes()
               << L" system_records_bytes=" << PluginHostDataSourceTestAccess::RecordsBytes(host) << L'\n';
    provider.reset();
    PluginHostDataSourceTestAccess::FailNextAllocation(host);
    rejected = reinterpret_cast<IRedXeDataProvider*>(1);
    Require(host.GetDataProvider("builtin.system-data", &rejected) == E_OUTOFMEMORY && !rejected &&
                PluginHostDataSourceTestAccess::Providers(host) == 1 &&
                PluginHostDataSourceTestAccess::Sources(host) == 0 &&
                PluginHostDataSourceTestAccess::RecordOwners(host) == 0,
            "failed rehydration changed or populated the retained facade");
    provider = Provider(host);
    Require(provider.get() == identity, "allocation retry changed provider identity");
    provider.reset();
    Require(PluginHostDataSourceTestAccess::RecordsBytes(host) == 0,
            "quiescent source retirement retained scheduling storage");
}

void TestLastDatasetDeliveryAtCapacity()
{
    auto fixture = std::make_unique<DescriptorFixture>();
    SourceProbe probe;
    PluginHost host;
    auto provider = Provider(host);
    InstallControlledSource(host, probe, fixture->descriptors.data(), 256);
    const RedXeDataSetDescriptor* descriptors = nullptr;
    uint32_t count = 0;
    Require(provider->GetDataSets(&descriptors, &count) == S_OK && count == 256 &&
                descriptors == fixture->descriptors.data() &&
                PluginHostDataSourceTestAccess::RecordsBytes(host) == 256 * 24,
            "full-capacity metadata or scheduling owner was truncated");
    wil::com_ptr_nothrow<ControlledSink> sink;
    sink.attach(new ControlledSink(nullptr, descriptors[255].dataSetId));
    const RedXeDataSubscriptionOptions options{sizeof(options), descriptors[255].dataSetId, 1000};
    wil::com_ptr_nothrow<IRedXeDataSubscription> subscription;
    Require(provider->Subscribe(&options, sink.get(), subscription.put()) == S_OK &&
                subscription->SetActive(TRUE) == S_OK &&
                WaitForSingleObject(sink->received.get(), 5000) == WAIT_OBJECT_0 && sink->valid.load(),
            "final descriptor could not collect and deliver its snapshot");
    Require(subscription->SetActive(FALSE) == S_OK, "final dataset deactivation failed");
    provider.reset();
    Require(PluginHostDataSourceTestAccess::RecordOwners(host) == 1,
            "inactive full-capacity subscription lost its scheduling owner");
    subscription.reset();
    ExpectRetired(host, probe);
}

void TestFacadeLeasesAndRevisit()
{
    PluginHost host;
    auto provider = Provider(host);
    wil::com_ptr_nothrow<IUnknown> identity;
    Require(SUCCEEDED(provider.query_to(identity.put())), "provider identity lookup failed");
    const auto* original = identity.get();
    provider.reset();
    Require(PluginHostDataSourceTestAccess::Sources(host) == 1, "external identity lease did not retain source");
    identity.reset();
    Require(PluginHostDataSourceTestAccess::Sources(host) == 0, "cold unleased production source was not retired");
    for (uint32_t index = 0; index < 8; ++index)
    {
        provider = Provider(host);
        Require(SUCCEEDED(provider.query_to(identity.put())) && identity.get() == original,
                "revisit changed controlling provider identity");
        const RedXeDataSetDescriptor* descriptors = nullptr;
        uint32_t count = 0;
        Require(provider->GetDataSets(&descriptors, &count) == S_OK && descriptors && count == 23,
                "revisit did not recreate the full production source");
        identity.reset();
        provider.reset();
        Require(PluginHostDataSourceTestAccess::Sources(host) == 0, "repeated cold revisit retained source storage");
    }
}

void TestAllocatedAndSharedSubscriptions()
{
    SourceProbe probe;
    PluginHost host;
    auto provider = Provider(host);
    InstallControlledSource(host, probe);
    wil::com_ptr_nothrow<ControlledSink> sink;
    sink.attach(new ControlledSink());
    auto first = Subscribe(provider.get(), sink.get());
    auto staged = Subscribe(provider.get(), sink.get());
    Require(first->SetActive(TRUE) == S_OK && WaitForSingleObject(sink->received.get(), 5000) == WAIT_OBJECT_0,
            "controlled subscription did not deliver");
    Require(first->SetActive(FALSE) == S_OK, "controlled subscription did not deactivate");
    provider.reset();
    Require(PluginHostDataSourceTestAccess::Sources(host) == 1 && probe.destructions.load() == 0,
            "inactive subscriptions did not retain source");
    first.reset();
    Require(staged->SetActive(TRUE) == S_OK, "staged consumer could not activate after sibling release");
    Require(staged->SetActive(FALSE) == S_OK && PluginHostDataSourceTestAccess::Sources(host) == 1,
            "sibling release reclaimed the staged consumer's source");
    staged.reset();
    ExpectRetired(host, probe);
    Require(sink->valid.load(), "borrowed controlled snapshot was corrupted");
    // Rehydrate while the event-blocked acquisition worker exists. Collection plans must be published together.
    for (uint32_t index = 0; index < 8; ++index)
    {
        provider = Provider(host);
        const RedXeDataSetDescriptor* descriptors = nullptr;
        uint32_t count = 0;
        Require(provider->GetDataSets(&descriptors, &count) == S_OK && count == 23, "worker revisit metadata failed");
        auto subscription = Subscribe(provider.get(), sink.get());
        Require(subscription->SetActive(TRUE) == S_OK && subscription->SetActive(FALSE) == S_OK,
                "worker revisit plan activation failed");
        subscription.reset();
        provider.reset();
    }
}

void TestBlockedCollection()
{
    SourceProbe probe;
    probe.pauseCollection = true;
    PluginHost host;
    auto provider = Provider(host);
    InstallControlledSource(host, probe);
    wil::com_ptr_nothrow<ControlledSink> sink;
    sink.attach(new ControlledSink());
    auto subscription = Subscribe(provider.get(), sink.get());
    // Declared after subscription so exception cleanup opens the gate before any draining COM release.
    const auto unblock = wil::scope_exit([&]() noexcept { SetEvent(probe.collection.resume.get()); });
    Require(subscription->SetActive(TRUE) == S_OK &&
                WaitForSingleObject(probe.collection.entered.get(), 5000) == WAIT_OBJECT_0,
            "collection barrier was not reached");
    provider.reset();
    subscription.reset();
    Require(PluginHostDataSourceTestAccess::Sources(host) == 1 && probe.destructions.load() == 0 &&
                PluginHostDataSourceTestAccess::RecordOwners(host) == 1,
            "source was reclaimed while collection was borrowing its storage");
    SetEvent(probe.collection.resume.get());
    ExpectRetired(host, probe);
    Require(!probe.collection.timedOut.load() && sink->callbacks.load() == 0,
            "removed subscription received a later collection callback");
}

void TestReservedCallbacks()
{
    SourceProbe probe;
    probe.pauseCollection = true;
    Gate firstCallback;
    PluginHost host;
    auto provider = Provider(host);
    InstallControlledSource(host, probe);
    wil::com_ptr_nothrow<ControlledSink> firstSink;
    wil::com_ptr_nothrow<ControlledSink> reservedSink;
    firstSink.attach(new ControlledSink(&firstCallback));
    reservedSink.attach(new ControlledSink());
    auto first = Subscribe(provider.get(), firstSink.get());
    auto reserved = Subscribe(provider.get(), reservedSink.get());
    const auto unblock = wil::scope_exit(
        [&]() noexcept
        {
            SetEvent(probe.collection.resume.get());
            SetEvent(firstCallback.resume.get());
        });
    Require(first->SetActive(TRUE) == S_OK &&
                WaitForSingleObject(probe.collection.entered.get(), 5000) == WAIT_OBJECT_0 &&
                reserved->SetActive(TRUE) == S_OK,
            "reservation setup failed");
    SetEvent(probe.collection.resume.get());
    Require(WaitForSingleObject(firstCallback.entered.get(), 5000) == WAIT_OBJECT_0, "first callback did not enter");
    provider.reset();
    wil::unique_event_nothrow started{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    wil::unique_event_nothrow returned{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Require(started && returned, "reservation barriers failed");
    std::jthread removal(
        [&]() noexcept
        {
            SetEvent(started.get());
            reserved.reset();
            first.reset();
            SetEvent(returned.get());
        });
    const bool waited = WaitForSingleObject(started.get(), 2000) == WAIT_OBJECT_0 &&
                        WaitForSingleObject(returned.get(), 100) == WAIT_TIMEOUT &&
                        reservedSink->callbacks.load() == 0 && probe.destructions.load() == 0;
    // Always unblock before an assertion can unwind into the thread's joining destructor.
    SetEvent(firstCallback.resume.get());
    removal.join();
    Require(waited, "reserved callback or source lifetime did not drain");
    ExpectRetired(host, probe);
    std::wcout << L"source_callback_drain active_callbacks=" << firstSink->callbacks.load() << L" reserved_callbacks="
               << reservedSink->callbacks.load() << L" destroyed=" << probe.destructions.load() << L'\n';
    // The first sibling stays active until the reserved sibling's release returns. A pending plan change may sample
    // it again in that interval; only the removing reserved sibling must finish exactly its one copied callback.
    Require(firstSink->callbacks.load() >= 1 && reservedSink->callbacks.load() == 1 && !firstCallback.timedOut.load() &&
                firstSink->valid.load() && reservedSink->valid.load(),
            "reserved delivery did not complete before reclamation");
}

void TestShutdownWithExternalLeases()
{
    SourceProbe probe;
    PluginHost host;
    auto provider = Provider(host);
    InstallControlledSource(host, probe);
    wil::com_ptr_nothrow<ControlledSink> sink;
    sink.attach(new ControlledSink());
    auto subscription = Subscribe(provider.get(), sink.get());
    Require(subscription->SetActive(TRUE) == S_OK && WaitForSingleObject(sink->received.get(), 5000) == WAIT_OBJECT_0,
            "shutdown setup collection failed");
    host.Shutdown();
    ExpectRetired(host, probe);
    const RedXeDataSetDescriptor* descriptors = reinterpret_cast<const RedXeDataSetDescriptor*>(1);
    uint32_t count = 1;
    Require(provider->GetDataSets(&descriptors, &count) == E_UNEXPECTED && !descriptors && count == 0,
            "shutdown facade retained stale source metadata");
    const RedXeDataSubscriptionOptions options{sizeof(options), "cpu.summary", 1000};
    IRedXeDataSubscription* rejected = reinterpret_cast<IRedXeDataSubscription*>(1);
    Require(provider->Subscribe(&options, sink.get(), &rejected) == E_UNEXPECTED && !rejected &&
                subscription->SetActive(TRUE) == E_UNEXPECTED,
            "shutdown facade restarted subscription work");
    IRedXeDataProvider* revived = reinterpret_cast<IRedXeDataProvider*>(1);
    Require(host.GetDataProvider("builtin.system-data", &revived) == E_UNEXPECTED && !revived,
            "shutdown lookup resurrected source storage");
    subscription.reset();
    provider.reset();
    host.Shutdown();
    Require(probe.destructions.load() == 1, "late releases repeated source cleanup");
}
} // namespace

void TestDataSourceLifetime(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] source reclamation, provider identity, collection and reserved callback leases\n";
    try
    {
        TestDescriptorBoundsAndValidationOrder();
        TestAllocationFailureAndFacadeRevisit();
        TestLastDatasetDeliveryAtCapacity();
        TestFacadeLeasesAndRevisit();
        TestAllocatedAndSharedSubscriptions();
        TestBlockedCollection();
        TestReservedCallbacks();
        TestShutdownWithExternalLeases();
    }
    catch (const std::exception& error)
    {
        success = false;
        std::cerr << "FAIL source lifetime: " << error.what() << '\n';
    }
    catch (...)
    {
        success = false;
        std::cerr << "FAIL source lifetime: unexpected exception\n";
    }
}
