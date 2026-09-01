#pragma once

#include <cstdint>
#include <unknwn.h>
#include <windows.h>

// Every sizeBytes field must equal the current record's sizeof value.

// Storage type carried by a data value.
enum RedXeDataValueType : std::uint32_t
{
    RedXeDataValueTypeInvalid = 0,
    RedXeDataValueTypeUInt64 = 1,
    RedXeDataValueTypeFloat64 = 2,
    RedXeDataValueTypeUtf16 = 3,
};

// Availability of a collected data value.
enum RedXeDataQuality : std::uint32_t
{
    RedXeDataQualityGood = 0,
    RedXeDataQualityUnavailable = 1,
    RedXeDataQualityInitializing = 2,
};

// Shape and sensitivity declared by a dataset.
enum RedXeDataSetFlags : std::uint32_t
{
    RedXeDataSetFlagNone = 0,
    RedXeDataSetFlagTable = 1U << 0U,
    RedXeDataSetFlagLocalSensitive = 1U << 1U,
};

// Conditions affecting a collected snapshot.
enum RedXeDataSnapshotFlags : std::uint32_t
{
    RedXeDataSnapshotFlagNone = 0,
    RedXeDataSnapshotFlagTruncated = 1U << 0U,
};

// Module-owned metadata for one dataset column.
struct RedXeDataColumnDescriptor final
{
    std::uint32_t sizeBytes;
    const char* columnId;
    const wchar_t* displayName;
    const wchar_t* unit;
    std::uint32_t valueType;
};

// Module-owned metadata for one bounded dataset.
struct RedXeDataSetDescriptor final
{
    std::uint32_t sizeBytes;
    const char* dataSetId;
    const wchar_t* displayName;
    const wchar_t* description;
    const RedXeDataColumnDescriptor* columns;
    std::uint32_t columnCount;
    std::uint32_t maximumRows;
    std::uint32_t recommendedIntervalMilliseconds;
    std::uint32_t flags;
};

// Provider-owned value borrowed with its snapshot.
struct RedXeDataValue final
{
    std::uint32_t sizeBytes;
    std::uint32_t valueType;
    std::uint32_t quality;
    union
    {
        std::uint64_t uint64Value;
        double float64Value;
        const wchar_t* utf16Value;
    };
    std::uint32_t utf16Characters;
};

// Provider-owned row borrowed with its snapshot.
struct RedXeDataRow final
{
    std::uint32_t sizeBytes;
    const RedXeDataValue* values;
    std::uint32_t valueCount;
};

// Provider-owned bounded table returned by one collection.
struct RedXeDataSnapshot final
{
    std::uint32_t sizeBytes;
    std::uint32_t flags;
    const char* dataSetId;
    std::uint64_t sequence;
    std::uint64_t timestampFileTime100ns;
    const RedXeDataRow* rows;
    std::uint32_t rowCount;
    std::uint32_t columnCount;
};

// Dataset and cadence requested from one host data provider.
struct RedXeDataSubscriptionOptions final
{
    std::uint32_t sizeBytes;
    const char* dataSetId;
    std::uint32_t requestedIntervalMilliseconds;
};

// Plugin-side pull source called only by the host data service.
interface __declspec(uuid("4E52264A-89C4-4DF6-AB47-B4D31B6247D2")) __declspec(novtable) IRedXeDataSource : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors,
                                                  std::uint32_t* count) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE CollectSnapshot(const char* dataSetId,
                                                      const RedXeDataSnapshot** snapshot) noexcept = 0;
};

// Receives one borrowed snapshot synchronously on the host acquisition worker.
interface __declspec(uuid("F9834987-EBC6-411E-9F28-A49E4DBB49D9")) __declspec(novtable) IRedXeDataSink : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE OnDataSnapshot(const RedXeDataSnapshot* snapshot) noexcept = 0;
};

// Controls one inactive-by-default host subscription.
interface __declspec(uuid("B8912B7D-89AD-4830-9CFB-73F4E72027FB")) __declspec(novtable) IRedXeDataSubscription
    : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetActive(BOOL active) noexcept = 0;
};

// Shared host-managed access to one data source.
interface __declspec(uuid("9EAE20F1-36A8-48A8-B451-F60401A898CD")) __declspec(novtable) IRedXeDataProvider : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors,
                                                  std::uint32_t* count) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Subscribe(const RedXeDataSubscriptionOptions* options, IRedXeDataSink* sink,
                                                IRedXeDataSubscription** subscription) noexcept = 0;
};
