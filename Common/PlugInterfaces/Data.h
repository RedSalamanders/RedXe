#pragma once

#include <cstddef>
#include <cstdint>
#include <unknwn.h>
#include <windows.h>

// Every sizeBytes field must equal the current record's sizeof value. Each record below is pinned with a size
// assertion, and every record carrying a pointer is pinned with offset assertions, so a layout change that preserves
// size cannot pass the runtime sizeBytes guard unnoticed.

// Storage type carried by a data value.
enum RedXeDataValueType : uint32_t
{
    RedXeDataValueTypeInvalid = 0,
    RedXeDataValueTypeUInt64 = 1,
    RedXeDataValueTypeFloat64 = 2,
    RedXeDataValueTypeUtf16 = 3,
};

// Availability of a collected data value.
enum RedXeDataQuality : uint32_t
{
    RedXeDataQualityGood = 0,
    RedXeDataQualityUnavailable = 1,
    RedXeDataQualityInitializing = 2,
};

// Shape and sensitivity declared by a dataset.
enum RedXeDataSetFlags : uint32_t
{
    RedXeDataSetFlagNone = 0,
    RedXeDataSetFlagTable = 1U << 0U,
    RedXeDataSetFlagLocalSensitive = 1U << 1U,
    RedXeDataSetFlagDeviceLane = 1U << 2U,
};

// Conditions affecting a collected snapshot.
enum RedXeDataSnapshotFlags : uint32_t
{
    RedXeDataSnapshotFlagNone = 0,
    RedXeDataSnapshotFlagTruncated = 1U << 0U,
};

// Maximum unique dataset IDs in one CollectSnapshots request.
constexpr uint32_t RedXeDataCollectMaximumDataSets = 32;

// Module-owned metadata for one dataset column.
struct RedXeDataColumnDescriptor final
{
    uint32_t sizeBytes;
    const char* columnId;
    const wchar_t* displayName;
    const wchar_t* unit;
    uint32_t valueType;
};

static_assert(sizeof(RedXeDataColumnDescriptor) == 40);
static_assert(offsetof(RedXeDataColumnDescriptor, columnId) == 8);
static_assert(offsetof(RedXeDataColumnDescriptor, displayName) == 16);
static_assert(offsetof(RedXeDataColumnDescriptor, unit) == 24);

// Module-owned metadata for one bounded dataset.
struct RedXeDataSetDescriptor final
{
    uint32_t sizeBytes;
    const char* dataSetId;
    const wchar_t* displayName;
    const wchar_t* description;
    const RedXeDataColumnDescriptor* columns;
    uint32_t columnCount;
    uint32_t maximumRows;
    uint32_t recommendedIntervalMilliseconds;
    uint32_t flags;
};

static_assert(sizeof(RedXeDataSetDescriptor) == 56);
static_assert(offsetof(RedXeDataSetDescriptor, dataSetId) == 8);
static_assert(offsetof(RedXeDataSetDescriptor, columns) == 32);
static_assert(offsetof(RedXeDataSetDescriptor, columnCount) == 40);

// Provider-owned value borrowed with its snapshot.
struct RedXeDataValue final
{
    uint32_t sizeBytes;
    uint32_t valueType;
    uint32_t quality;
    union
    {
        uint64_t uint64Value;
        double float64Value;
        const wchar_t* utf16Value;
    };
    uint32_t utf16Characters;
};

static_assert(sizeof(RedXeDataValue) == 32);
static_assert(offsetof(RedXeDataValue, uint64Value) == 16);
static_assert(offsetof(RedXeDataValue, utf16Characters) == 24);

// Provider-owned row borrowed with its snapshot.
struct RedXeDataRow final
{
    uint32_t sizeBytes;
    const RedXeDataValue* values;
    uint32_t valueCount;
};

static_assert(sizeof(RedXeDataRow) == 24);
static_assert(offsetof(RedXeDataRow, values) == 8);

// Provider-owned bounded table returned by one collection.
struct RedXeDataSnapshot final
{
    uint32_t sizeBytes;
    uint32_t flags;
    const char* dataSetId;
    uint64_t sequence;
    uint64_t timestampFileTime100ns;
    const RedXeDataRow* rows;
    uint32_t rowCount;
    uint32_t columnCount;
};

static_assert(sizeof(RedXeDataSnapshot) == 48);
static_assert(offsetof(RedXeDataSnapshot, dataSetId) == 8);
static_assert(offsetof(RedXeDataSnapshot, sequence) == 16);
static_assert(offsetof(RedXeDataSnapshot, rows) == 32);

// Host-owned batch of unique due dataset IDs for one source.
struct RedXeDataCollectRequest final
{
    uint32_t sizeBytes;
    const char* const* dataSetIds;
    uint32_t dataSetCount;
};

// Source-owned batch result. Snapshots and strings are invalid at the next
// CollectSnapshots call on this source, or when the source is released.
struct RedXeDataCollectResult final
{
    uint32_t sizeBytes;
    uint32_t snapshotCount;
    uint64_t sequence;
    uint64_t timestampFileTime100ns;
    const RedXeDataSnapshot* const* snapshots;
};

static_assert(sizeof(RedXeDataCollectRequest) == 24);
static_assert(offsetof(RedXeDataCollectRequest, dataSetIds) == 8);
static_assert(sizeof(RedXeDataCollectResult) == 32);
static_assert(offsetof(RedXeDataCollectResult, sequence) == 8);
static_assert(offsetof(RedXeDataCollectResult, snapshots) == 24);

// Dataset and cadence requested from one host data provider.
struct RedXeDataSubscriptionOptions final
{
    uint32_t sizeBytes;
    const char* dataSetId;
    uint32_t requestedIntervalMilliseconds;
};

static_assert(sizeof(RedXeDataSubscriptionOptions) == 24);
static_assert(offsetof(RedXeDataSubscriptionOptions, dataSetId) == 8);

// Plugin-side pull source, called only by the host data service and only from its single acquisition worker.
// A source MUST NOT create its own acquisition thread, timer, or wake-up: the host owns all scheduling.
interface __declspec(uuid("C3A81F6E-2D47-4B90-A1E5-6F8C9D0B3E21")) __declspec(novtable) IRedXeDataSource : IUnknown
{
    // Returns a module-owned immutable descriptor array valid while the module is mapped. Both outputs are cleared
    // before validation.
    virtual HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors,
                                                  uint32_t* count) noexcept = 0;
    // Collects one batch of due datasets. The result and everything it points at are source-owned and stay valid
    // only until the next CollectSnapshots call on this source or until the source is released.
    virtual HRESULT STDMETHODCALLTYPE CollectSnapshots(const RedXeDataCollectRequest* request,
                                                       const RedXeDataCollectResult** result) noexcept = 0;
};

// Receives one borrowed snapshot synchronously on the host acquisition worker.
interface __declspec(uuid("F9834987-EBC6-411E-9F28-A49E4DBB49D9")) __declspec(novtable) IRedXeDataSink : IUnknown
{
    // Runs on the host acquisition worker, never on the UI thread, while the source's storage is borrowed. Copy only
    // the bounded values needed and return.
    //
    // Inside this call the sink MUST NOT block, and MUST NOT re-enter the host except through
    // IRedXeHost::RequestFrame and IRedXeHost::Log. In particular it MUST NOT activate, deactivate, or release a
    // subscription, and MUST NOT call IRedXeHost::GetDataProvider. The host releases its lock before calling sinks,
    // but subscription deactivation/release drains reserved callbacks and would wait for this call itself. A failing
    // sink is isolated and does not stop later sinks or future
    // acquisition cycles.
    virtual HRESULT STDMETHODCALLTYPE OnDataSnapshot(const RedXeDataSnapshot* snapshot) noexcept = 0;
};

// Controls one inactive-by-default host subscription.
interface __declspec(uuid("B8912B7D-89AD-4830-9CFB-73F4E72027FB")) __declspec(novtable) IRedXeDataSubscription
    : IUnknown
{
    // Called outside OnDataSnapshot, SetActive(FALSE) and releasing the subscription both drain running and reserved
    // sink callbacks (including a copied sink not yet invoked) before returning. No later callback may start while
    // inactive. Releasing subscriptions before dropping a sink's owner therefore ends all callback access to it.
    virtual HRESULT STDMETHODCALLTYPE SetActive(BOOL active) noexcept = 0;
};

// Shared host-managed access to one data source. Repeated lookup of the same provider ID returns the same
// controlling identity, and multiple widgets may hold it and subscribe independently. Subscriptions for the same
// dataset share one collection at the shortest requested interval, clamped to the source recommendation.
interface __declspec(uuid("9EAE20F1-36A8-48A8-B451-F60401A898CD")) __declspec(novtable) IRedXeDataProvider : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors,
                                                  uint32_t* count) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Subscribe(const RedXeDataSubscriptionOptions* options, IRedXeDataSink* sink,
                                                IRedXeDataSubscription** subscription) noexcept = 0;
};
