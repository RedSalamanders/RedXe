#include "NetStorage.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwchar>

// clang-format off
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
// clang-format on

#include <setupapi.h>
#include <winioctl.h>

#include <initguid.h>
#include <ntddstor.h>

namespace
{
constexpr DWORD kDeviceIoctlTimeoutMs = 100;
constexpr DWORD kCancelDrainMs = 1000;
constexpr std::int64_t kDiskInventoryIntervalQpcSeconds = 5;
constexpr std::int64_t kDiskPerformanceRetrySeconds = 5;
constexpr uint32_t kPhysicalDriveFallbackCount = 16;
constexpr size_t kStorageDescriptorBytes = 1024;
constexpr size_t kVolumeExtentBytes = 512;

constexpr wchar_t kProtocolIpv4[] = L"ipv4";
constexpr wchar_t kProtocolIpv6[] = L"ipv6";
constexpr wchar_t kProtocolTcp4[] = L"tcp4";
constexpr wchar_t kProtocolTcp6[] = L"tcp6";
constexpr wchar_t kProtocolUdp4[] = L"udp4";
constexpr wchar_t kProtocolUdp6[] = L"udp6";

void CALLBACK OnIpInterfaceChange(PVOID context, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE)
{
    if (context)
    {
        static_cast<std::atomic<bool>*>(context)->store(true, std::memory_order_release);
    }
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

[[nodiscard]] uint32_t WideLength(const wchar_t* value) noexcept
{
    if (!value)
    {
        return 0;
    }
    uint32_t length = 0;
    while (value[length] != L'\0')
    {
        ++length;
    }
    return length;
}

void CopyAnsiOffset(wchar_t* destination, size_t destinationCount, const STORAGE_DEVICE_DESCRIPTOR* descriptor,
                    ULONG offset) noexcept
{
    if (!destination || destinationCount == 0)
    {
        return;
    }
    destination[0] = L'\0';
    if (!descriptor || offset == 0 || offset >= descriptor->Size)
    {
        return;
    }
    const char* source = reinterpret_cast<const char*>(descriptor) + offset;
    const char* const end = reinterpret_cast<const char*>(descriptor) + descriptor->Size;
    char ansi[kRedXeDiskNameCharacters]{};
    size_t count = 0;
    while (source < end && *source != '\0' && count + 1 < sizeof(ansi))
    {
        ansi[count] = *source;
        ++count;
        ++source;
    }
    if (count == 0)
    {
        return;
    }
    ansi[count] = '\0';
    (void)MultiByteToWideChar(CP_ACP, 0, ansi, -1, destination, static_cast<int>(destinationCount));
}

[[nodiscard]] bool QueryCounter(LARGE_INTEGER& value) noexcept
{
    return QueryPerformanceCounter(&value) != FALSE;
}

[[nodiscard]] bool QueryFrequency(LARGE_INTEGER& value) noexcept
{
    return QueryPerformanceFrequency(&value) != FALSE && value.QuadPart != 0;
}

[[nodiscard]] double SecondsBetween(std::int64_t start, std::int64_t finish, std::int64_t frequency) noexcept
{
    if (frequency <= 0 || finish <= start)
    {
        return 0.0;
    }
    return static_cast<double>(finish - start) / static_cast<double>(frequency);
}

[[nodiscard]] const RedXeNetworkInterfacePrevious* FindPreviousInterface(const RedXeNetStorageState& state,
                                                                         uint64_t luid) noexcept
{
    for (uint32_t index = 0; index < state.previousInterfaceCount; ++index)
    {
        const RedXeNetworkInterfacePrevious& previous = state.previousInterfaces[index];
        if (previous.valid && previous.luid == luid)
        {
            return &previous;
        }
    }
    return nullptr;
}

[[nodiscard]] bool DeviceIoctl(RedXeNetStorageState& state, HANDLE device, DWORD controlCode, void* input,
                               DWORD inputBytes, void* output, DWORD outputBytes, DWORD* returned) noexcept
{
    if (returned)
    {
        *returned = 0;
    }
    if (!state.ioctlEvent || !device || device == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    if (ResetEvent(state.ioctlEvent.get()) == FALSE)
    {
        return false;
    }
    OVERLAPPED overlapped{};
    overlapped.hEvent = state.ioctlEvent.get();
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
    const DWORD wait = WaitForSingleObject(state.ioctlEvent.get(), kDeviceIoctlTimeoutMs);
    if (wait == WAIT_TIMEOUT)
    {
        (void)CancelIoEx(device, &overlapped);
        (void)WaitForSingleObject(state.ioctlEvent.get(), kCancelDrainMs);
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

void QueryDiskIdentity(RedXeNetStorageState& state, RedXeStorageDiskCache& disk, uint32_t diskNumber) noexcept
{
    disk.hasIdentity = false;
    disk.hasCapacity = false;
    disk.busType = 0;
    disk.mediaKind = 0;
    disk.capacityBytes = 0;
    disk.displayName[0] = L'\0';
    if (!disk.handle)
    {
        return;
    }

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    std::array<std::byte, kStorageDescriptorBytes> buffer{};
    DWORD returned = 0;
    if (DeviceIoctl(state, disk.handle.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, static_cast<DWORD>(sizeof(query)),
                    buffer.data(), static_cast<DWORD>(buffer.size()), &returned) &&
        returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR))
    {
        const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
        if (descriptor->Size >= sizeof(STORAGE_DEVICE_DESCRIPTOR) && descriptor->Size <= returned)
        {
            disk.busType = descriptor->BusType;
            wchar_t vendor[kRedXeDiskNameCharacters]{};
            wchar_t product[kRedXeDiskNameCharacters]{};
            CopyAnsiOffset(vendor, std::size(vendor), descriptor, descriptor->VendorIdOffset);
            CopyAnsiOffset(product, std::size(product), descriptor, descriptor->ProductIdOffset);
            if (product[0] != L'\0')
            {
                if (vendor[0] != L'\0')
                {
                    (void)swprintf_s(disk.displayName, L"%s %s", vendor, product);
                }
                else
                {
                    CopyWide(disk.displayName, std::size(disk.displayName), product);
                }
            }
            disk.hasIdentity = true;
        }
    }

    query = {};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR seekPenalty{};
    returned = 0;
    if (DeviceIoctl(state, disk.handle.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, static_cast<DWORD>(sizeof(query)),
                    &seekPenalty, static_cast<DWORD>(sizeof(seekPenalty)), &returned) &&
        returned >= sizeof(seekPenalty))
    {
        disk.mediaKind = seekPenalty.IncursSeekPenalty ? 1 : 2;
        disk.hasIdentity = true;
    }

    GET_LENGTH_INFORMATION length{};
    returned = 0;
    if (DeviceIoctl(state, disk.handle.get(), IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &length,
                    static_cast<DWORD>(sizeof(length)), &returned) &&
        returned >= sizeof(length))
    {
        disk.capacityBytes = static_cast<uint64_t>(length.Length.QuadPart);
        disk.hasCapacity = true;
        disk.hasIdentity = true;
    }

    if (disk.displayName[0] == L'\0')
    {
        (void)swprintf_s(disk.displayName, L"PhysicalDrive%u", diskNumber);
    }
}

bool OpenPhysicalDrive(RedXeNetStorageState& state, uint32_t diskNumber) noexcept
{
    if (diskNumber >= kRedXeMaximumStorageDisks)
    {
        return false;
    }
    RedXeStorageDiskCache& disk = state.diskCache[diskNumber];
    if (disk.open && disk.handle)
    {
        return true;
    }
    wchar_t path[64]{};
    if (swprintf_s(path, L"\\\\.\\PhysicalDrive%u", diskNumber) <= 0)
    {
        return false;
    }
    disk.handle.reset(CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr));
    if (!disk.handle)
    {
        disk.open = false;
        return false;
    }
    disk.open = true;
    disk.skipPerformance = false;
    disk.hasPreviousPerformance = false;
    QueryDiskIdentity(state, disk, diskNumber);
    return true;
}

void CollectPresentDisksFromSetupApi(RedXeNetStorageState& state,
                                     std::array<bool, kRedXeMaximumStorageDisks>& present) noexcept
{
    const HDEVINFO deviceInfo =
        SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISK, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE)
    {
        return;
    }
    auto closeInfo = wil::scope_exit([&] { (void)SetupDiDestroyDeviceInfoList(deviceInfo); });

    for (DWORD index = 0; index < kRedXeMaximumStorageDisks; ++index)
    {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = static_cast<DWORD>(sizeof(interfaceData));
        if (SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, &GUID_DEVINTERFACE_DISK, index, &interfaceData) == FALSE)
        {
            break;
        }
        std::array<std::byte, kStorageDescriptorBytes> detailBuffer{};
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
        detail->cbSize = static_cast<DWORD>(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W));
        DWORD required = 0;
        if (SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, detail,
                                             static_cast<DWORD>(detailBuffer.size()), &required, nullptr) == FALSE)
        {
            continue;
        }
        wil::unique_hfile handle{CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                             OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
        if (!handle)
        {
            continue;
        }
        STORAGE_DEVICE_NUMBER number{};
        DWORD returned = 0;
        if (!DeviceIoctl(state, handle.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number,
                         static_cast<DWORD>(sizeof(number)), &returned) ||
            returned < sizeof(number) || number.DeviceNumber >= kRedXeMaximumStorageDisks)
        {
            continue;
        }
        present[number.DeviceNumber] = true;
        RedXeStorageDiskCache& disk = state.diskCache[number.DeviceNumber];
        if (!disk.open || !disk.handle)
        {
            disk.handle.reset(handle.release());
            disk.open = true;
            disk.skipPerformance = false;
            disk.hasPreviousPerformance = false;
            QueryDiskIdentity(state, disk, number.DeviceNumber);
        }
    }
}

void CollectPresentDisksFromVolumes(RedXeNetStorageState& state,
                                    std::array<bool, kRedXeMaximumStorageDisks>& present) noexcept
{
    wchar_t volumeName[kRedXeVolumeGuidCharacters]{};
    const HANDLE find = FindFirstVolumeW(volumeName, static_cast<DWORD>(std::size(volumeName)));
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }
    auto closeFind = wil::scope_exit([&] { (void)FindVolumeClose(find); });
    BOOL more = TRUE;
    while (more != FALSE)
    {
        wchar_t openPath[kRedXeVolumeGuidCharacters]{};
        CopyWide(openPath, std::size(openPath), volumeName);
        const uint32_t length = WideLength(openPath);
        if (length > 1 && openPath[length - 1] == L'\\')
        {
            openPath[length - 1] = L'\0';
        }
        wil::unique_hfile volume{CreateFileW(openPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr)};
        if (volume)
        {
            std::array<std::byte, kVolumeExtentBytes> extentBuffer{};
            auto* extents = reinterpret_cast<VOLUME_DISK_EXTENTS*>(extentBuffer.data());
            DWORD returned = 0;
            if (DeviceIoctl(state, volume.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, extents,
                            static_cast<DWORD>(extentBuffer.size()), &returned) &&
                returned >= sizeof(DWORD) && extents->NumberOfDiskExtents > 0)
            {
                const DWORD extentCount =
                    (std::min)(extents->NumberOfDiskExtents,
                               static_cast<DWORD>((extentBuffer.size() - offsetof(VOLUME_DISK_EXTENTS, Extents)) /
                                                  sizeof(DISK_EXTENT)));
                for (DWORD index = 0; index < extentCount; ++index)
                {
                    const DWORD diskNumber = extents->Extents[index].DiskNumber;
                    if (diskNumber < kRedXeMaximumStorageDisks)
                    {
                        present[diskNumber] = true;
                    }
                }
            }
        }
        more = FindNextVolumeW(find, volumeName, static_cast<DWORD>(std::size(volumeName)));
    }
}

void RefreshDiskInventory(RedXeNetStorageState& state) noexcept
{
    LARGE_INTEGER now{};
    LARGE_INTEGER frequency{};
    if (!QueryCounter(now) || !QueryFrequency(frequency))
    {
        return;
    }
    if (state.lastDiskInventoryQpc != 0)
    {
        const double elapsed = SecondsBetween(state.lastDiskInventoryQpc, now.QuadPart, frequency.QuadPart);
        if (elapsed < static_cast<double>(kDiskInventoryIntervalQpcSeconds))
        {
            return;
        }
    }
    state.lastDiskInventoryQpc = now.QuadPart;

    std::array<bool, kRedXeMaximumStorageDisks> present{};
    CollectPresentDisksFromSetupApi(state, present);
    CollectPresentDisksFromVolumes(state, present);

    bool any = false;
    for (bool bit : present)
    {
        if (bit)
        {
            any = true;
            break;
        }
    }
    if (!any)
    {
        for (uint32_t index = 0; index < kPhysicalDriveFallbackCount; ++index)
        {
            present[index] = true;
        }
    }

    for (uint32_t index = 0; index < kRedXeMaximumStorageDisks; ++index)
    {
        if (!present[index])
        {
            state.diskCache[index].handle.reset();
            state.diskCache[index].open = false;
            state.diskCache[index].hasPreviousPerformance = false;
            continue;
        }
        (void)OpenPhysicalDrive(state, index);
    }
}

[[nodiscard]] uint64_t Widen32(uint32_t current, uint64_t previous) noexcept
{
    const uint32_t previousLow = static_cast<uint32_t>(previous);
    uint64_t high = previous >> 32;
    if (current < previousLow)
    {
        ++high;
    }
    return (high << 32) | current;
}

void FillProtocolRow(RedXeNetworkProtocolRow& row, RedXeNetworkProtocolPrevious& previous, const wchar_t* protocolId,
                     uint64_t family, bool hasCounts, uint64_t inCount, uint64_t outCount,
                     uint64_t errorCount, uint64_t discardCount, bool hasRetransmit,
                     uint64_t retransmitCount, bool hasEstablished, uint64_t established, bool hasReset,
                     uint64_t resetCount, std::int64_t qpc, std::int64_t frequency) noexcept
{
    row = {};
    row.protocolId = protocolId;
    row.addressFamily = family;
    row.hasCounts = hasCounts;
    row.inCount = inCount;
    row.outCount = outCount;
    row.errorCount = errorCount;
    row.discardCount = discardCount;
    row.hasRetransmit = hasRetransmit;
    row.retransmitCount = retransmitCount;
    row.hasEstablished = hasEstablished;
    row.currentEstablished = established;
    row.hasReset = hasReset;
    row.resetCount = resetCount;
    if (!hasCounts || !previous.valid)
    {
        row.ratesReady = false;
        previous.inCount = inCount;
        previous.outCount = outCount;
        previous.qpc = qpc;
        previous.valid = hasCounts;
        return;
    }
    const double seconds = SecondsBetween(previous.qpc, qpc, frequency);
    if (seconds <= 0.0 || inCount < previous.inCount || outCount < previous.outCount)
    {
        row.ratesReady = false;
        previous.inCount = inCount;
        previous.outCount = outCount;
        previous.qpc = qpc;
        previous.valid = true;
        return;
    }
    row.inRate = static_cast<double>(inCount - previous.inCount) / seconds;
    row.outRate = static_cast<double>(outCount - previous.outCount) / seconds;
    row.ratesReady = true;
    previous.inCount = inCount;
    previous.outCount = outCount;
    previous.qpc = qpc;
    previous.valid = true;
}
} // namespace

RedXeNetStorageState::RedXeNetStorageState() noexcept
{
    ioctlEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
}

RedXeNetStorageState::~RedXeNetStorageState() noexcept
{
    if (ipNotify)
    {
        (void)CancelMibChangeNotify2(ipNotify);
        ipNotify = nullptr;
    }
}

void RedXeNetStorageEnsureNotify(RedXeNetStorageState& state) noexcept
{
    if (state.ipNotify)
    {
        return;
    }
    HANDLE handle = nullptr;
    const DWORD status =
        NotifyIpInterfaceChange(AF_UNSPEC, OnIpInterfaceChange, &state.networkTopologyDirty, FALSE, &handle);
    if (status == NO_ERROR)
    {
        state.ipNotify = handle;
    }
}

void RedXeNetStorageSampleInterfaces(RedXeNetStorageState& state) noexcept
{
    RedXeNetStorageEnsureNotify(state);
    LARGE_INTEGER qpc{};
    LARGE_INTEGER frequency{};
    if (!QueryCounter(qpc) || !QueryFrequency(frequency))
    {
        state.interfaceCount = 0;
        return;
    }

    const bool refreshTable =
        state.networkTopologyDirty.exchange(false, std::memory_order_acq_rel) || state.interfaceCount == 0;
    if (refreshTable)
    {
        MIB_IF_TABLE2* table = nullptr;
        const DWORD status = GetIfTable2Ex(MibIfTableNormal, &table);
        auto freeTable = wil::scope_exit(
            [&]
            {
                if (table)
                {
                    FreeMibTable(table);
                    table = nullptr;
                }
            });
        state.interfaceCount = 0;
        state.interfacesTruncated = false;
        if (status != NO_ERROR || !table)
        {
            state.networkTopologyDirty.store(true, std::memory_order_release);
            return;
        }
        const uint32_t available = table->NumEntries;
        const uint32_t copyCount = available > kRedXeMaximumNetworkInterfaces
                                            ? static_cast<uint32_t>(kRedXeMaximumNetworkInterfaces)
                                            : available;
        state.interfacesTruncated = available > kRedXeMaximumNetworkInterfaces;
        for (uint32_t index = 0; index < copyCount; ++index)
        {
            const MIB_IF_ROW2& source = table->Table[index];
            RedXeNetworkInterfaceRow& row = state.interfaces[index];
            row = {};
            row.luid = source.InterfaceLuid.Value;
            CopyWide(row.alias, std::size(row.alias), source.Alias);
            CopyWide(row.description, std::size(row.description), source.Description);
            row.type = source.Type;
            row.operationalStatus = source.OperStatus;
            row.mediaConnectState = source.MediaConnectState;
            row.mtu = source.Mtu;
            row.receiveLinkSpeed = source.ReceiveLinkSpeed;
            row.transmitLinkSpeed = source.TransmitLinkSpeed;
            row.inOctets = source.InOctets;
            row.outOctets = source.OutOctets;
            row.inUcastPkts = source.InUcastPkts;
            row.outUcastPkts = source.OutUcastPkts;
            row.inNUcastPkts = source.InNUcastPkts;
            row.outNUcastPkts = source.OutNUcastPkts;
            row.inErrors = source.InErrors;
            row.outErrors = source.OutErrors;
            row.inDiscards = source.InDiscards;
            row.outDiscards = source.OutDiscards;
            row.hasCounters = true;
        }
        state.interfaceCount = copyCount;
        std::sort(state.interfaces.begin(), state.interfaces.begin() + state.interfaceCount,
                  [](const RedXeNetworkInterfaceRow& left, const RedXeNetworkInterfaceRow& right)
                  { return left.luid < right.luid; });
    }
    else
    {
        for (uint32_t index = 0; index < state.interfaceCount; ++index)
        {
            RedXeNetworkInterfaceRow& row = state.interfaces[index];
            MIB_IF_ROW2 entry{};
            entry.InterfaceLuid.Value = row.luid;
            const DWORD status = GetIfEntry2(&entry);
            if (status != NO_ERROR)
            {
                row.hasCounters = false;
                row.ratesReady = false;
                row.utilizationReady = false;
                state.networkTopologyDirty.store(true, std::memory_order_release);
                continue;
            }
            CopyWide(row.alias, std::size(row.alias), entry.Alias);
            CopyWide(row.description, std::size(row.description), entry.Description);
            row.type = entry.Type;
            row.operationalStatus = entry.OperStatus;
            row.mediaConnectState = entry.MediaConnectState;
            row.mtu = entry.Mtu;
            row.receiveLinkSpeed = entry.ReceiveLinkSpeed;
            row.transmitLinkSpeed = entry.TransmitLinkSpeed;
            row.inOctets = entry.InOctets;
            row.outOctets = entry.OutOctets;
            row.inUcastPkts = entry.InUcastPkts;
            row.outUcastPkts = entry.OutUcastPkts;
            row.inNUcastPkts = entry.InNUcastPkts;
            row.outNUcastPkts = entry.OutNUcastPkts;
            row.inErrors = entry.InErrors;
            row.outErrors = entry.OutErrors;
            row.inDiscards = entry.InDiscards;
            row.outDiscards = entry.OutDiscards;
            row.hasCounters = true;
        }
    }

    for (uint32_t index = 0; index < state.interfaceCount; ++index)
    {
        RedXeNetworkInterfaceRow& row = state.interfaces[index];
        row.ratesReady = false;
        row.utilizationReady = false;
        row.inOctetRate = 0.0;
        row.outOctetRate = 0.0;
        row.utilizationPercent = 0.0;
        const RedXeNetworkInterfacePrevious* previous = FindPreviousInterface(state, row.luid);
        if (!row.hasCounters || !previous || !previous->valid)
        {
            continue;
        }
        const double seconds = SecondsBetween(previous->qpc, qpc.QuadPart, frequency.QuadPart);
        if (seconds <= 0.0 || row.inOctets < previous->inOctets || row.outOctets < previous->outOctets)
        {
            continue;
        }
        row.inOctetRate = static_cast<double>(row.inOctets - previous->inOctets) / seconds;
        row.outOctetRate = static_cast<double>(row.outOctets - previous->outOctets) / seconds;
        row.ratesReady = true;
        const uint64_t linkSpeed = row.transmitLinkSpeed != 0 ? row.transmitLinkSpeed : row.receiveLinkSpeed;
        if (linkSpeed != 0)
        {
            const double maxBytesPerSecond = (std::max)(row.inOctetRate, row.outOctetRate);
            row.utilizationPercent =
                std::clamp((8.0 * maxBytesPerSecond * 100.0) / static_cast<double>(linkSpeed), 0.0, 100.0);
            row.utilizationReady = true;
        }
    }

    state.previousInterfaceCount = state.interfaceCount;
    for (uint32_t index = 0; index < state.interfaceCount; ++index)
    {
        const RedXeNetworkInterfaceRow& row = state.interfaces[index];
        RedXeNetworkInterfacePrevious& previous = state.previousInterfaces[index];
        previous.luid = row.luid;
        previous.inOctets = row.inOctets;
        previous.outOctets = row.outOctets;
        previous.qpc = qpc.QuadPart;
        previous.valid = row.hasCounters;
    }
}

void RedXeNetStorageSampleProtocols(RedXeNetStorageState& state) noexcept
{
    LARGE_INTEGER qpc{};
    LARGE_INTEGER frequency{};
    if (!QueryCounter(qpc) || !QueryFrequency(frequency))
    {
        return;
    }

    MIB_IPSTATS ip4{};
    MIB_IPSTATS ip6{};
    const bool hasIp4 = GetIpStatisticsEx(&ip4, AF_INET) == NO_ERROR;
    const bool hasIp6 = GetIpStatisticsEx(&ip6, AF_INET6) == NO_ERROR;

    MIB_TCPSTATS tcp4{};
    MIB_TCPSTATS tcp6{};
    const bool hasTcp4 = GetTcpStatisticsEx(&tcp4, AF_INET) == NO_ERROR;
    const bool hasTcp6 = GetTcpStatisticsEx(&tcp6, AF_INET6) == NO_ERROR;

    MIB_UDPSTATS udp4{};
    MIB_UDPSTATS udp6{};
    const bool hasUdp4 = GetUdpStatisticsEx(&udp4, AF_INET) == NO_ERROR;
    const bool hasUdp6 = GetUdpStatisticsEx(&udp6, AF_INET6) == NO_ERROR;

    const uint64_t ip4In = hasIp4 ? Widen32(ip4.dwInReceives, state.previousProtocols[0].inCount) : 0;
    const uint64_t ip4Out = hasIp4 ? Widen32(ip4.dwOutRequests, state.previousProtocols[0].outCount) : 0;
    const uint64_t ip6In = hasIp6 ? Widen32(ip6.dwInReceives, state.previousProtocols[1].inCount) : 0;
    const uint64_t ip6Out = hasIp6 ? Widen32(ip6.dwOutRequests, state.previousProtocols[1].outCount) : 0;
    const uint64_t tcp4In = hasTcp4 ? Widen32(tcp4.dwInSegs, state.previousProtocols[2].inCount) : 0;
    const uint64_t tcp4Out = hasTcp4 ? Widen32(tcp4.dwOutSegs, state.previousProtocols[2].outCount) : 0;
    const uint64_t tcp6In = hasTcp6 ? Widen32(tcp6.dwInSegs, state.previousProtocols[3].inCount) : 0;
    const uint64_t tcp6Out = hasTcp6 ? Widen32(tcp6.dwOutSegs, state.previousProtocols[3].outCount) : 0;
    const uint64_t udp4In = hasUdp4 ? Widen32(udp4.dwInDatagrams, state.previousProtocols[4].inCount) : 0;
    const uint64_t udp4Out = hasUdp4 ? Widen32(udp4.dwOutDatagrams, state.previousProtocols[4].outCount) : 0;
    const uint64_t udp6In = hasUdp6 ? Widen32(udp6.dwInDatagrams, state.previousProtocols[5].inCount) : 0;
    const uint64_t udp6Out = hasUdp6 ? Widen32(udp6.dwOutDatagrams, state.previousProtocols[5].outCount) : 0;

    FillProtocolRow(state.protocols[0], state.previousProtocols[0], kProtocolIpv4, 4, hasIp4, ip4In, ip4Out,
                    hasIp4 ? static_cast<uint64_t>(ip4.dwInHdrErrors) + ip4.dwInAddrErrors : 0,
                    hasIp4 ? static_cast<uint64_t>(ip4.dwInDiscards) + ip4.dwOutDiscards : 0, false, 0, false, 0,
                    false, 0, qpc.QuadPart, frequency.QuadPart);
    FillProtocolRow(state.protocols[1], state.previousProtocols[1], kProtocolIpv6, 6, hasIp6, ip6In, ip6Out,
                    hasIp6 ? static_cast<uint64_t>(ip6.dwInHdrErrors) + ip6.dwInAddrErrors : 0,
                    hasIp6 ? static_cast<uint64_t>(ip6.dwInDiscards) + ip6.dwOutDiscards : 0, false, 0, false, 0,
                    false, 0, qpc.QuadPart, frequency.QuadPart);
    FillProtocolRow(state.protocols[2], state.previousProtocols[2], kProtocolTcp4, 4, hasTcp4, tcp4In, tcp4Out,
                    hasTcp4 ? tcp4.dwInErrs : 0, 0, hasTcp4, tcp4.dwRetransSegs, hasTcp4, tcp4.dwCurrEstab, hasTcp4,
                    tcp4.dwEstabResets, qpc.QuadPart, frequency.QuadPart);
    FillProtocolRow(state.protocols[3], state.previousProtocols[3], kProtocolTcp6, 6, hasTcp6, tcp6In, tcp6Out,
                    hasTcp6 ? tcp6.dwInErrs : 0, 0, hasTcp6, tcp6.dwRetransSegs, hasTcp6, tcp6.dwCurrEstab, hasTcp6,
                    tcp6.dwEstabResets, qpc.QuadPart, frequency.QuadPart);
    FillProtocolRow(state.protocols[4], state.previousProtocols[4], kProtocolUdp4, 4, hasUdp4, udp4In, udp4Out,
                    hasUdp4 ? udp4.dwInErrors : 0, hasUdp4 ? udp4.dwNoPorts : 0, false, 0, false, 0, false, 0,
                    qpc.QuadPart, frequency.QuadPart);
    FillProtocolRow(state.protocols[5], state.previousProtocols[5], kProtocolUdp6, 6, hasUdp6, udp6In, udp6Out,
                    hasUdp6 ? udp6.dwInErrors : 0, hasUdp6 ? udp6.dwNoPorts : 0, false, 0, false, 0, false, 0,
                    qpc.QuadPart, frequency.QuadPart);
}

void RedXeNetStorageSampleDisks(RedXeNetStorageState& state) noexcept
{
    RefreshDiskInventory(state);
    LARGE_INTEGER qpc{};
    LARGE_INTEGER frequency{};
    if (!QueryCounter(qpc) || !QueryFrequency(frequency))
    {
        state.diskCount = 0;
        return;
    }

    state.diskCount = 0;
    for (uint32_t diskNumber = 0; diskNumber < kRedXeMaximumStorageDisks; ++diskNumber)
    {
        RedXeStorageDiskCache& cache = state.diskCache[diskNumber];
        if (!cache.open || !cache.handle)
        {
            continue;
        }
        RedXeStorageDiskRow& row = state.disks[state.diskCount];
        row = {};
        row.diskNumber = diskNumber;
        CopyWide(row.displayName, std::size(row.displayName), cache.displayName);
        row.busType = cache.busType;
        row.mediaKind = cache.mediaKind;
        row.capacityBytes = cache.capacityBytes;
        row.hasIdentity = cache.hasIdentity || cache.displayName[0] != L'\0';
        row.hasCapacity = cache.hasCapacity;

        const bool retryDue = !cache.skipPerformance || qpc.QuadPart >= cache.skipPerformanceUntilQpc;
        DISK_PERFORMANCE performance{};
        DWORD returned = 0;
        bool ioctlOk = false;
        if (retryDue)
        {
            ioctlOk = DeviceIoctl(state, cache.handle.get(), IOCTL_DISK_PERFORMANCE, nullptr, 0, &performance,
                                  static_cast<DWORD>(sizeof(performance)), &returned) &&
                      returned >= sizeof(performance);
            if (!ioctlOk)
            {
                cache.skipPerformance = true;
                cache.hasPreviousPerformance = false;
                cache.skipPerformanceUntilQpc = qpc.QuadPart + frequency.QuadPart * kDiskPerformanceRetrySeconds;
            }
            else
            {
                cache.skipPerformance = false;
            }
        }
        if (!ioctlOk)
        {
            ++state.diskCount;
            continue;
        }

        row.hasPerformance = true;
        row.queueDepth = static_cast<uint64_t>(performance.QueueDepth);
        const uint64_t bytesRead = static_cast<uint64_t>(performance.BytesRead.QuadPart);
        const uint64_t bytesWritten = static_cast<uint64_t>(performance.BytesWritten.QuadPart);
        const uint64_t readCount = static_cast<uint64_t>(performance.ReadCount);
        const uint64_t writeCount = static_cast<uint64_t>(performance.WriteCount);
        const uint64_t readTime = static_cast<uint64_t>(performance.ReadTime.QuadPart);
        const uint64_t writeTime = static_cast<uint64_t>(performance.WriteTime.QuadPart);
        const uint64_t idleTime = static_cast<uint64_t>(performance.IdleTime.QuadPart);
        const uint64_t splitCount = static_cast<uint64_t>(performance.SplitCount);
        if (cache.hasPreviousPerformance)
        {
            const double seconds = SecondsBetween(cache.previousQpc, qpc.QuadPart, frequency.QuadPart);
            const bool reset = bytesRead < cache.bytesRead || bytesWritten < cache.bytesWritten ||
                               readCount < cache.readCount || writeCount < cache.writeCount;
            if (seconds > 0.0 && !reset)
            {
                row.readBytesPerSecond = static_cast<double>(bytesRead - cache.bytesRead) / seconds;
                row.writeBytesPerSecond = static_cast<double>(bytesWritten - cache.bytesWritten) / seconds;
                row.readOpsPerSecond = static_cast<double>(readCount - cache.readCount) / seconds;
                row.writeOpsPerSecond = static_cast<double>(writeCount - cache.writeCount) / seconds;
                row.splitCount = splitCount >= cache.splitCount ? splitCount - cache.splitCount : 0;
                row.ratesReady = true;
                const uint64_t elapsed100ns = static_cast<uint64_t>(seconds * 10'000'000.0);
                if (elapsed100ns > 0 && idleTime >= cache.idleTime100ns)
                {
                    const uint64_t idleDelta = idleTime - cache.idleTime100ns;
                    const uint64_t idleClamped = (std::min)(idleDelta, elapsed100ns);
                    row.idlePercent = std::clamp(
                        (static_cast<double>(idleClamped) * 100.0) / static_cast<double>(elapsed100ns), 0.0, 100.0);
                    row.activePercent = std::clamp(100.0 - row.idlePercent, 0.0, 100.0);
                }
                const uint64_t readDelta = readCount - cache.readCount;
                const uint64_t writeDelta = writeCount - cache.writeCount;
                if (readDelta > 0 && readTime >= cache.readTime100ns)
                {
                    row.avgReadLatencyMs = (static_cast<double>(readTime - cache.readTime100ns) / 10'000.0) /
                                           static_cast<double>(readDelta);
                    row.hasLatency = true;
                }
                if (writeDelta > 0 && writeTime >= cache.writeTime100ns)
                {
                    row.avgWriteLatencyMs = (static_cast<double>(writeTime - cache.writeTime100ns) / 10'000.0) /
                                            static_cast<double>(writeDelta);
                    row.hasLatency = true;
                }
            }
        }
        cache.bytesRead = bytesRead;
        cache.bytesWritten = bytesWritten;
        cache.readCount = readCount;
        cache.writeCount = writeCount;
        cache.readTime100ns = readTime;
        cache.writeTime100ns = writeTime;
        cache.idleTime100ns = idleTime;
        cache.splitCount = splitCount;
        cache.previousQpc = qpc.QuadPart;
        cache.hasPreviousPerformance = true;
        ++state.diskCount;
    }
}

void RedXeNetStorageSampleVolumes(RedXeNetStorageState& state) noexcept
{
    state.volumeCount = 0;
    state.volumesTruncated = false;
    wchar_t volumeName[kRedXeVolumeGuidCharacters]{};
    const HANDLE find = FindFirstVolumeW(volumeName, static_cast<DWORD>(std::size(volumeName)));
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }
    auto closeFind = wil::scope_exit([&] { (void)FindVolumeClose(find); });
    BOOL more = TRUE;
    while (more != FALSE)
    {
        if (state.volumeCount >= kRedXeMaximumStorageVolumes)
        {
            state.volumesTruncated = true;
            break;
        }
        RedXeStorageVolumeRow& row = state.volumes[state.volumeCount];
        row = {};
        CopyWide(row.volumeGuid, std::size(row.volumeGuid), volumeName);
        CopyWide(row.displayName, std::size(row.displayName), volumeName);

        wchar_t paths[kRedXeVolumeMountCharacters]{};
        DWORD pathCharacters = 0;
        if (GetVolumePathNamesForVolumeNameW(volumeName, paths, static_cast<DWORD>(std::size(paths)),
                                             &pathCharacters) != FALSE &&
            paths[0] != L'\0')
        {
            CopyWide(row.displayName, std::size(row.displayName), paths);
        }

        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER totalBytes{};
        ULARGE_INTEGER totalFree{};
        const wchar_t* spacePath = paths[0] != L'\0' ? paths : volumeName;
        if (GetDiskFreeSpaceExW(spacePath, &freeBytes, &totalBytes, &totalFree) != FALSE)
        {
            row.totalBytes = totalBytes.QuadPart;
            row.freeBytes = freeBytes.QuadPart;
            row.hasCapacity = true;
        }

        wchar_t fileSystem[kRedXeFileSystemCharacters]{};
        if (GetVolumeInformationW(volumeName, nullptr, 0, nullptr, nullptr, nullptr, fileSystem,
                                  static_cast<DWORD>(std::size(fileSystem))) != FALSE)
        {
            CopyWide(row.fileSystem, std::size(row.fileSystem), fileSystem);
        }

        wchar_t openPath[kRedXeVolumeGuidCharacters]{};
        CopyWide(openPath, std::size(openPath), volumeName);
        const uint32_t length = WideLength(openPath);
        if (length > 1 && openPath[length - 1] == L'\\')
        {
            openPath[length - 1] = L'\0';
        }
        wil::unique_hfile volume{CreateFileW(openPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr)};
        if (volume)
        {
            std::array<std::byte, kVolumeExtentBytes> extentBuffer{};
            auto* extents = reinterpret_cast<VOLUME_DISK_EXTENTS*>(extentBuffer.data());
            DWORD returned = 0;
            if (DeviceIoctl(state, volume.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, extents,
                            static_cast<DWORD>(extentBuffer.size()), &returned) &&
                returned >= sizeof(DWORD))
            {
                row.extentCount = extents->NumberOfDiskExtents;
                if (extents->NumberOfDiskExtents > 0)
                {
                    row.firstDiskNumber = extents->Extents[0].DiskNumber;
                    row.hasFirstDisk = true;
                }
            }
        }
        ++state.volumeCount;
        more = FindNextVolumeW(find, volumeName, static_cast<DWORD>(std::size(volumeName)));
    }
}

void RedXeNetStorageSampleDiskTemperatures(RedXeNetStorageState& state) noexcept
{
    state.diskTemperatureCount = 0;
    state.diskTemperaturesTruncated = false;
    constexpr size_t kTemperatureBufferBytes = 512;
    constexpr uint32_t kMaximumSensorsPerDisk = 8;
    for (uint32_t diskNumber = 0; diskNumber < kRedXeMaximumStorageDisks; ++diskNumber)
    {
        RedXeStorageDiskCache& cache = state.diskCache[diskNumber];
        if (!cache.open || !cache.handle)
        {
            continue;
        }
        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceTemperatureProperty;
        query.QueryType = PropertyStandardQuery;
        std::array<std::byte, kTemperatureBufferBytes> buffer{};
        DWORD returned = 0;
        if (!DeviceIoctl(state, cache.handle.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query,
                         static_cast<DWORD>(sizeof(query)), buffer.data(), static_cast<DWORD>(buffer.size()),
                         &returned))
        {
            continue;
        }
        const auto* descriptor = reinterpret_cast<const STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(buffer.data());
        const size_t headerBytes = offsetof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR, TemperatureInfo);
        if (returned < headerBytes || descriptor->Size > returned || descriptor->InfoCount == 0)
        {
            continue;
        }
        const uint32_t infoCount =
            (std::min)(static_cast<uint32_t>(descriptor->InfoCount), kMaximumSensorsPerDisk);
        const size_t needed = headerBytes + static_cast<size_t>(infoCount) * sizeof(STORAGE_TEMPERATURE_INFO);
        if (needed > returned || needed > descriptor->Size)
        {
            continue;
        }
        for (uint32_t sensor = 0; sensor < infoCount; ++sensor)
        {
            const STORAGE_TEMPERATURE_INFO& info = descriptor->TemperatureInfo[sensor];
            if (info.Temperature == static_cast<SHORT>(STORAGE_TEMPERATURE_VALUE_NOT_REPORTED))
            {
                continue;
            }
            if (state.diskTemperatureCount >= kRedXeMaximumStorageTemperatures)
            {
                state.diskTemperaturesTruncated = true;
                return;
            }
            RedXeStorageTemperatureRow& row = state.diskTemperatures[state.diskTemperatureCount];
            row = {};
            row.diskNumber = diskNumber;
            row.sensorIndex = info.Index;
            CopyWide(row.displayName, std::size(row.displayName), cache.displayName);
            row.temperatureC = static_cast<double>(info.Temperature);
            if (descriptor->WarningTemperature != static_cast<SHORT>(STORAGE_TEMPERATURE_VALUE_NOT_REPORTED))
            {
                row.warningTemperatureC = static_cast<double>(descriptor->WarningTemperature);
                row.hasWarning = true;
            }
            if (descriptor->CriticalTemperature != static_cast<SHORT>(STORAGE_TEMPERATURE_VALUE_NOT_REPORTED))
            {
                row.criticalTemperatureC = static_cast<double>(descriptor->CriticalTemperature);
                row.hasCritical = true;
            }
            ++state.diskTemperatureCount;
        }
    }
}
