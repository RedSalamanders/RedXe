#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

constexpr size_t kRedXeMaximumNetworkInterfaces = 256;
constexpr size_t kRedXeMaximumNetworkProtocols = 16;
constexpr size_t kRedXeMaximumStorageDisks = 128;
constexpr size_t kRedXeMaximumStorageTemperatures = 128;
constexpr size_t kRedXeMaximumStorageVolumes = 256;
constexpr size_t kRedXeNetworkNameCharacters = 256;
constexpr size_t kRedXeDiskNameCharacters = 64;
constexpr size_t kRedXeVolumeGuidCharacters = 64;
constexpr size_t kRedXeVolumeMountCharacters = 260;
constexpr size_t kRedXeFileSystemCharacters = 32;
constexpr uint32_t kRedXeNetworkProtocolCount = 6;

struct RedXeNetworkInterfaceRow final
{
    uint64_t luid = 0;
    wchar_t alias[kRedXeNetworkNameCharacters]{};
    wchar_t description[kRedXeNetworkNameCharacters]{};
    uint64_t type = 0;
    uint64_t operationalStatus = 0;
    uint64_t mediaConnectState = 0;
    uint64_t mtu = 0;
    uint64_t receiveLinkSpeed = 0;
    uint64_t transmitLinkSpeed = 0;
    uint64_t inOctets = 0;
    uint64_t outOctets = 0;
    uint64_t inUcastPkts = 0;
    uint64_t outUcastPkts = 0;
    uint64_t inNUcastPkts = 0;
    uint64_t outNUcastPkts = 0;
    uint64_t inErrors = 0;
    uint64_t outErrors = 0;
    uint64_t inDiscards = 0;
    uint64_t outDiscards = 0;
    double inOctetRate = 0.0;
    double outOctetRate = 0.0;
    double utilizationPercent = 0.0;
    bool hasCounters = false;
    bool ratesReady = false;
    bool utilizationReady = false;
};

struct RedXeNetworkProtocolRow final
{
    const wchar_t* protocolId = nullptr;
    uint64_t addressFamily = 0;
    uint64_t inCount = 0;
    uint64_t outCount = 0;
    uint64_t errorCount = 0;
    uint64_t discardCount = 0;
    uint64_t retransmitCount = 0;
    uint64_t currentEstablished = 0;
    uint64_t resetCount = 0;
    double inRate = 0.0;
    double outRate = 0.0;
    bool hasCounts = false;
    bool hasRetransmit = false;
    bool hasEstablished = false;
    bool hasReset = false;
    bool ratesReady = false;
};

struct RedXeStorageDiskRow final
{
    uint32_t diskNumber = 0;
    wchar_t displayName[kRedXeDiskNameCharacters]{};
    uint64_t busType = 0;
    uint64_t mediaKind = 0;
    uint64_t capacityBytes = 0;
    uint64_t queueDepth = 0;
    uint64_t splitCount = 0;
    double activePercent = 0.0;
    double idlePercent = 0.0;
    double readBytesPerSecond = 0.0;
    double writeBytesPerSecond = 0.0;
    double readOpsPerSecond = 0.0;
    double writeOpsPerSecond = 0.0;
    double avgReadLatencyMs = 0.0;
    double avgWriteLatencyMs = 0.0;
    bool hasIdentity = false;
    bool hasCapacity = false;
    bool hasPerformance = false;
    bool ratesReady = false;
    bool hasLatency = false;
};

struct RedXeStorageTemperatureRow final
{
    uint32_t diskNumber = 0;
    uint32_t sensorIndex = 0;
    wchar_t displayName[kRedXeDiskNameCharacters]{};
    double temperatureC = 0.0;
    double warningTemperatureC = 0.0;
    double criticalTemperatureC = 0.0;
    bool hasWarning = false;
    bool hasCritical = false;
};

struct RedXeStorageVolumeRow final
{
    wchar_t volumeGuid[kRedXeVolumeGuidCharacters]{};
    wchar_t displayName[kRedXeVolumeMountCharacters]{};
    wchar_t fileSystem[kRedXeFileSystemCharacters]{};
    uint64_t totalBytes = 0;
    uint64_t freeBytes = 0;
    uint64_t extentCount = 0;
    uint64_t firstDiskNumber = 0;
    bool hasCapacity = false;
    bool hasFirstDisk = false;
};

struct RedXeNetworkInterfacePrevious final
{
    uint64_t luid = 0;
    uint64_t inOctets = 0;
    uint64_t outOctets = 0;
    std::int64_t qpc = 0;
    bool valid = false;
};

struct RedXeNetworkProtocolPrevious final
{
    uint64_t inCount = 0;
    uint64_t outCount = 0;
    std::int64_t qpc = 0;
    bool valid = false;
};

struct RedXeStorageDiskCache final
{
    wil::unique_hfile handle;
    wchar_t displayName[kRedXeDiskNameCharacters]{};
    uint64_t busType = 0;
    uint64_t mediaKind = 0;
    uint64_t capacityBytes = 0;
    uint64_t bytesRead = 0;
    uint64_t bytesWritten = 0;
    uint64_t readCount = 0;
    uint64_t writeCount = 0;
    uint64_t readTime100ns = 0;
    uint64_t writeTime100ns = 0;
    uint64_t idleTime100ns = 0;
    uint64_t splitCount = 0;
    std::int64_t previousQpc = 0;
    std::int64_t skipPerformanceUntilQpc = 0;
    bool open = false;
    bool hasIdentity = false;
    bool hasCapacity = false;
    bool skipPerformance = false;
    bool hasPreviousPerformance = false;
};

struct RedXeNetStorageState final
{
    RedXeNetStorageState() noexcept;
    ~RedXeNetStorageState() noexcept;
    RedXeNetStorageState(const RedXeNetStorageState&) = delete;
    RedXeNetStorageState& operator=(const RedXeNetStorageState&) = delete;

    std::atomic<bool> networkTopologyDirty{true};
    HANDLE ipNotify = nullptr;
    wil::unique_handle ioctlEvent;
    std::int64_t lastDiskInventoryQpc = 0;

    std::array<RedXeNetworkInterfaceRow, kRedXeMaximumNetworkInterfaces> interfaces{};
    std::array<RedXeNetworkInterfacePrevious, kRedXeMaximumNetworkInterfaces> previousInterfaces{};
    uint32_t interfaceCount = 0;
    uint32_t previousInterfaceCount = 0;
    bool interfacesTruncated = false;

    std::array<RedXeNetworkProtocolRow, kRedXeNetworkProtocolCount> protocols{};
    std::array<RedXeNetworkProtocolPrevious, kRedXeNetworkProtocolCount> previousProtocols{};

    std::array<RedXeStorageDiskCache, kRedXeMaximumStorageDisks> diskCache{};
    std::array<RedXeStorageDiskRow, kRedXeMaximumStorageDisks> disks{};
    uint32_t diskCount = 0;

    std::array<RedXeStorageVolumeRow, kRedXeMaximumStorageVolumes> volumes{};
    uint32_t volumeCount = 0;
    bool volumesTruncated = false;

    std::array<RedXeStorageTemperatureRow, kRedXeMaximumStorageTemperatures> diskTemperatures{};
    uint32_t diskTemperatureCount = 0;
    bool diskTemperaturesTruncated = false;
};

void RedXeNetStorageEnsureNotify(RedXeNetStorageState& state) noexcept;
void RedXeNetStorageSampleInterfaces(RedXeNetStorageState& state) noexcept;
void RedXeNetStorageSampleProtocols(RedXeNetStorageState& state) noexcept;
void RedXeNetStorageSampleDisks(RedXeNetStorageState& state) noexcept;
void RedXeNetStorageSampleDiskTemperatures(RedXeNetStorageState& state) noexcept;
void RedXeNetStorageSampleVolumes(RedXeNetStorageState& state) noexcept;
