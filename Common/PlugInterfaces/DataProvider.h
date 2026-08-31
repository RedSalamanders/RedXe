#pragma once

#include <cstdint>
#include <unknwn.h>

enum RedXeDataValueType : std::uint32_t
{
    RedXeDataValueTypeInvalid = 0,
    RedXeDataValueTypeUInt64 = 1,
    RedXeDataValueTypeFloat64 = 2,
    RedXeDataValueTypeUtf16 = 3,
};

enum RedXeDataQuality : std::uint32_t
{
    RedXeDataQualityGood = 0,
    RedXeDataQualityUnavailable = 1,
    RedXeDataQualityInitializing = 2,
};

enum RedXeDataSetFlags : std::uint32_t
{
    RedXeDataSetFlagNone = 0,
    RedXeDataSetFlagTable = 1U << 0U,
    RedXeDataSetFlagLocalSensitive = 1U << 1U,
};

enum RedXeDataSnapshotFlags : std::uint32_t
{
    RedXeDataSnapshotFlagNone = 0,
    RedXeDataSnapshotFlagTruncated = 1U << 0U,
};

struct RedXeDataColumnDescriptor final
{
    std::uint32_t sizeBytes;
    const char* columnId;
    const wchar_t* displayName;
    const wchar_t* unit;
    std::uint32_t valueType;
};

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

struct RedXeDataValue final
{
    std::uint32_t sizeBytes;
    std::uint32_t valueType;
    std::uint32_t quality;
    std::uint64_t uint64Value;
    double float64Value;
    const wchar_t* utf16Value;
    std::uint32_t utf16Characters;
};

struct RedXeDataRow final
{
    std::uint32_t sizeBytes;
    const RedXeDataValue* values;
    std::uint32_t valueCount;
};

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

// Descriptors are immutable and module-owned. Snapshot storage is provider-owned and remains valid only until the
// next CollectSnapshot call on this provider, or provider release. Calls are synchronous and non-reentrant.
interface __declspec(uuid("4E52264A-89C4-4DF6-AB47-B4D31B6247D2")) __declspec(novtable) IRedXeDataProvider : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors,
                                                  std::uint32_t* count) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE CollectSnapshot(const char* dataSetId,
                                                      const RedXeDataSnapshot** snapshot) noexcept = 0;
};
