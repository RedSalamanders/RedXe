#include "PowerSensors.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <cwchar>

// clang-format off
#include <initguid.h>
#include <poclass.h>
#include <powrprof.h>
#include <setupapi.h>
#include <winioctl.h>
// clang-format on

namespace
{
constexpr DWORD kDeviceIoctlTimeoutMs = 100;
constexpr DWORD kCancelDrainMs = 1000;
constexpr size_t kInterfaceDetailBytes = 1024;
constexpr double kAbsoluteZeroC = -273.15;
constexpr double kMinimumPlausibleC = -40.0;
constexpr double kMaximumPlausibleC = 150.0;

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

[[nodiscard]] bool WideEquals(const wchar_t* left, const wchar_t* right) noexcept
{
    if (!left || !right)
    {
        return left == right;
    }
    while (*left != L'\0' && *right != L'\0')
    {
        if (*left != *right)
        {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
}

[[nodiscard]] bool DeviceIoctl(RedXePowerState& state, HANDLE device, DWORD controlCode, void* input, DWORD inputBytes,
                               void* output, DWORD outputBytes, DWORD* returned) noexcept
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

[[nodiscard]] bool TenthsKelvinToCelsius(ULONG tenthsKelvin, double& celsius) noexcept
{
    if (tenthsKelvin == 0)
    {
        return false;
    }
    celsius = (static_cast<double>(tenthsKelvin) / 10.0) + kAbsoluteZeroC;
    return celsius >= kMinimumPlausibleC && celsius <= kMaximumPlausibleC;
}

[[nodiscard]] uint32_t CountDeviceInterfaces(const GUID& guid) noexcept
{
    const HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    auto destroy = wil::scope_exit([&] { (void)SetupDiDestroyDeviceInfoList(set); });
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

[[nodiscard]] bool GetInterfacePath(HDEVINFO set, SP_DEVICE_INTERFACE_DATA& data, wchar_t* path,
                                    size_t pathCount) noexcept
{
    if (!path || pathCount == 0)
    {
        return false;
    }
    path[0] = L'\0';
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailW(set, &data, nullptr, 0, &needed, nullptr);
    if (needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || needed > kInterfaceDetailBytes)
    {
        return false;
    }
    std::array<std::byte, kInterfaceDetailBytes> detailBytes{};
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBytes.data());
    detail->cbSize = static_cast<DWORD>(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W));
    if (SetupDiGetDeviceInterfaceDetailW(set, &data, detail, needed, nullptr, nullptr) == FALSE)
    {
        return false;
    }
    CopyWide(path, pathCount, detail->DevicePath);
    return path[0] != L'\0';
}

RedXeBatteryCache* FindBatteryCache(RedXePowerState& state, const wchar_t* path) noexcept
{
    for (RedXeBatteryCache& cache : state.batteryCache)
    {
        if (cache.open && WideEquals(cache.devicePath, path))
        {
            return &cache;
        }
    }
    return nullptr;
}

RedXeBatteryCache* AllocateBatteryCache(RedXePowerState& state) noexcept
{
    for (RedXeBatteryCache& cache : state.batteryCache)
    {
        if (!cache.open)
        {
            return &cache;
        }
    }
    return nullptr;
}

void CloseBatteryCache(RedXeBatteryCache& cache) noexcept
{
    cache.handle.reset();
    cache.devicePath[0] = L'\0';
    cache.tag = 0;
    cache.open = false;
    cache.tagValid = false;
    cache.used = false;
}

bool RefreshBatteryTag(RedXePowerState& state, RedXeBatteryCache& cache) noexcept
{
    ULONG timeout = 0;
    ULONG tag = 0;
    DWORD returned = 0;
    if (!DeviceIoctl(state, cache.handle.get(), IOCTL_BATTERY_QUERY_TAG, &timeout, static_cast<DWORD>(sizeof(timeout)),
                     &tag, static_cast<DWORD>(sizeof(tag)), &returned) ||
        returned < sizeof(tag) || tag == BATTERY_TAG_INVALID)
    {
        cache.tagValid = false;
        cache.tag = 0;
        return false;
    }
    cache.tag = tag;
    cache.tagValid = true;
    return true;
}

bool QueryBatteryInformation(RedXePowerState& state, RedXeBatteryCache& cache, BATTERY_QUERY_INFORMATION_LEVEL level,
                             void* output, DWORD outputBytes, DWORD* returned) noexcept
{
    BATTERY_QUERY_INFORMATION query{};
    query.BatteryTag = cache.tag;
    query.InformationLevel = level;
    query.AtRate = 0;
    return DeviceIoctl(state, cache.handle.get(), IOCTL_BATTERY_QUERY_INFORMATION, &query,
                       static_cast<DWORD>(sizeof(query)), output, outputBytes, returned);
}

void FillBatteryRow(RedXePowerState& state, RedXeBatteryCache& cache, RedXeBatteryRow& row) noexcept
{
    row = {};
    CopyWide(row.deviceId, std::size(row.deviceId), cache.devicePath);
    row.tag = cache.tag;

    BATTERY_INFORMATION information{};
    DWORD returned = 0;
    if (QueryBatteryInformation(state, cache, BatteryInformation, &information, static_cast<DWORD>(sizeof(information)),
                                &returned) &&
        returned >= sizeof(information))
    {
        row.relativeCapacity = (information.Capabilities & BATTERY_CAPACITY_RELATIVE) != 0 ? 1 : 0;
        row.hasRelative = true;
        if (information.DesignedCapacity != BATTERY_UNKNOWN_CAPACITY)
        {
            row.designedCapacity = information.DesignedCapacity;
            row.hasDesigned = true;
        }
        if (information.FullChargedCapacity != BATTERY_UNKNOWN_CAPACITY)
        {
            row.fullChargedCapacity = information.FullChargedCapacity;
            row.hasFullCharged = true;
        }
        if (information.CycleCount != 0)
        {
            row.cycleCount = information.CycleCount;
            row.hasCycleCount = true;
        }
        wchar_t chemistry[kRedXeChemistryCharacters]{};
        for (size_t index = 0; index < 4 && index + 1 < std::size(chemistry); ++index)
        {
            const char character = static_cast<char>(information.Chemistry[index]);
            if (character == '\0')
            {
                break;
            }
            chemistry[index] = static_cast<wchar_t>(character);
        }
        if (chemistry[0] != L'\0')
        {
            CopyWide(row.chemistry, std::size(row.chemistry), chemistry);
        }
    }

    wchar_t name[MAX_BATTERY_STRING_SIZE / sizeof(wchar_t)]{};
    if (QueryBatteryInformation(state, cache, BatteryDeviceName, name, static_cast<DWORD>(sizeof(name)), &returned) &&
        returned >= sizeof(wchar_t) && name[0] != L'\0')
    {
        CopyWide(row.deviceName, std::size(row.deviceName), name);
    }

    ULONG temperature = 0;
    if (QueryBatteryInformation(state, cache, BatteryTemperature, &temperature, static_cast<DWORD>(sizeof(temperature)),
                                &returned) &&
        returned >= sizeof(temperature))
    {
        double celsius = 0.0;
        if (TenthsKelvinToCelsius(temperature, celsius))
        {
            row.temperatureC = celsius;
            row.hasTemperature = true;
        }
    }

    ULONG estimated = 0;
    if (QueryBatteryInformation(state, cache, BatteryEstimatedTime, &estimated, static_cast<DWORD>(sizeof(estimated)),
                                &returned) &&
        returned >= sizeof(estimated) && estimated != BATTERY_UNKNOWN_TIME)
    {
        row.estimatedTimeSeconds = estimated;
        row.hasEstimatedTime = true;
    }

    BATTERY_WAIT_STATUS wait{};
    wait.BatteryTag = cache.tag;
    wait.Timeout = 0;
    wait.LowCapacity = 0;
    wait.HighCapacity = BATTERY_UNKNOWN_CAPACITY;
    BATTERY_STATUS status{};
    if (DeviceIoctl(state, cache.handle.get(), IOCTL_BATTERY_QUERY_STATUS, &wait, static_cast<DWORD>(sizeof(wait)),
                    &status, static_cast<DWORD>(sizeof(status)), &returned) &&
        returned >= sizeof(status))
    {
        row.powerState = status.PowerState;
        row.hasPowerState = true;
        if (status.Capacity != BATTERY_UNKNOWN_CAPACITY)
        {
            row.currentCapacity = status.Capacity;
            row.hasCurrent = true;
        }
        if (status.Voltage != BATTERY_UNKNOWN_VOLTAGE)
        {
            row.voltageMv = status.Voltage;
            row.hasVoltage = true;
        }
        if (status.Rate != static_cast<LONG>(BATTERY_UNKNOWN_RATE))
        {
            row.rateMw = static_cast<double>(status.Rate);
            row.hasRate = true;
        }
    }
}
} // namespace

RedXePowerState::RedXePowerState() noexcept
{
    ioctlEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
}

RedXePowerState::~RedXePowerState() noexcept = default;

void RedXePowerSampleSummary(RedXePowerState& state) noexcept
{
    state.summary = {};
    SYSTEM_POWER_STATUS power{};
    if (GetSystemPowerStatus(&power) != FALSE)
    {
        if (power.ACLineStatus == 0 || power.ACLineStatus == 1)
        {
            state.summary.acOnline = power.ACLineStatus;
            state.summary.hasAc = true;
        }
        if (power.BatteryFlag != 255)
        {
            state.summary.batteryPresent = (power.BatteryFlag & 128) == 0 ? 1 : 0;
            state.summary.hasBatteryPresent = true;
            state.summary.charging = (power.BatteryFlag & 8) != 0 ? 1 : 0;
            state.summary.hasCharging = true;
        }
        state.summary.batterySaver = (power.SystemStatusFlag & 1) != 0 ? 1 : 0;
        state.summary.hasSaver = true;
        if (power.BatteryLifePercent != 255)
        {
            state.summary.chargePercent = std::clamp(static_cast<double>(power.BatteryLifePercent), 0.0, 100.0);
            state.summary.hasChargePercent = true;
        }
        if (power.BatteryLifeTime != BATTERY_UNKNOWN_TIME)
        {
            state.summary.remainingSeconds = power.BatteryLifeTime;
            state.summary.hasRemaining = true;
        }
        if (power.BatteryFullLifeTime != BATTERY_UNKNOWN_TIME)
        {
            state.summary.fullLifeSeconds = power.BatteryFullLifeTime;
            state.summary.hasFullLife = true;
        }
    }

    SYSTEM_POWER_CAPABILITIES capabilities{};
    if (GetPwrCapabilities(&capabilities) != FALSE)
    {
        state.summary.systemS3 = capabilities.SystemS3 ? 1 : 0;
        state.summary.systemS4 = capabilities.SystemS4 ? 1 : 0;
        state.summary.hiberFilePresent = capabilities.HiberFilePresent ? 1 : 0;
        state.summary.thermalControl = capabilities.ThermalControl ? 1 : 0;
        // Modern standby (S0 low-power idle). Without it a machine that reports SystemS3 == 0 looks unable to
        // sleep at all, which is wrong for most current laptops.
        state.summary.modernStandby = capabilities.AoAc ? 1 : 0;
        state.summary.modernStandbyConnected = capabilities.AoAcConnectivitySupported ? 1 : 0;
        state.summary.hasCapabilities = true;
    }
}

void RedXePowerSampleBatteries(RedXePowerState& state) noexcept
{
    for (RedXeBatteryCache& cache : state.batteryCache)
    {
        cache.used = false;
    }
    state.batteryCount = 0;
    state.batteriesTruncated = false;

    const HDEVINFO set =
        SetupDiGetClassDevsW(&GUID_DEVICE_BATTERY, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
    {
        for (RedXeBatteryCache& cache : state.batteryCache)
        {
            if (cache.open && !cache.used)
            {
                CloseBatteryCache(cache);
            }
        }
        return;
    }
    auto destroy = wil::scope_exit([&] { (void)SetupDiDestroyDeviceInfoList(set); });

    for (DWORD index = 0; index < kRedXeMaximumBatteries; ++index)
    {
        SP_DEVICE_INTERFACE_DATA data{};
        data.cbSize = static_cast<DWORD>(sizeof(data));
        if (SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVICE_BATTERY, index, &data) == FALSE)
        {
            break;
        }
        wchar_t path[kRedXeBatteryPathCharacters]{};
        if (!GetInterfacePath(set, data, path, std::size(path)))
        {
            continue;
        }
        RedXeBatteryCache* cache = FindBatteryCache(state, path);
        if (!cache)
        {
            cache = AllocateBatteryCache(state);
            if (!cache)
            {
                state.batteriesTruncated = true;
                break;
            }
            cache->handle.reset(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
            if (!cache->handle)
            {
                CloseBatteryCache(*cache);
                continue;
            }
            CopyWide(cache->devicePath, std::size(cache->devicePath), path);
            cache->open = true;
            cache->tagValid = false;
        }
        cache->used = true;
        if (!cache->tagValid && !RefreshBatteryTag(state, *cache))
        {
            continue;
        }
        FillBatteryRow(state, *cache, state.batteries[state.batteryCount]);
        if (!state.batteries[state.batteryCount].hasPowerState && cache->tagValid)
        {
            cache->tagValid = false;
            if (RefreshBatteryTag(state, *cache))
            {
                FillBatteryRow(state, *cache, state.batteries[state.batteryCount]);
            }
        }
        ++state.batteryCount;
    }

    SP_DEVICE_INTERFACE_DATA extra{};
    extra.cbSize = static_cast<DWORD>(sizeof(extra));
    if (SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVICE_BATTERY, kRedXeMaximumBatteries, &extra) != FALSE)
    {
        state.batteriesTruncated = true;
    }

    for (RedXeBatteryCache& cache : state.batteryCache)
    {
        if (cache.open && !cache.used)
        {
            CloseBatteryCache(cache);
        }
    }
}

void RedXePowerSampleAcpiThermals(RedXePowerState& state) noexcept
{
    state.acpiThermalCount = 0;
    state.acpiThermalsTruncated = false;
    const HDEVINFO set =
        SetupDiGetClassDevsW(&GUID_DEVICE_THERMAL_ZONE, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
    {
        return;
    }
    auto destroy = wil::scope_exit([&] { (void)SetupDiDestroyDeviceInfoList(set); });
    for (DWORD index = 0;; ++index)
    {
        SP_DEVICE_INTERFACE_DATA data{};
        data.cbSize = static_cast<DWORD>(sizeof(data));
        if (SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVICE_THERMAL_ZONE, index, &data) == FALSE)
        {
            break;
        }
        if (state.acpiThermalCount >= kRedXeMaximumAcpiThermals)
        {
            state.acpiThermalsTruncated = true;
            break;
        }
        wchar_t path[kRedXeBatteryPathCharacters]{};
        if (!GetInterfacePath(set, data, path, std::size(path)))
        {
            continue;
        }
        wil::unique_hfile zone{CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
        if (!zone)
        {
            continue;
        }
        THERMAL_WAIT_READ wait{};
        wait.Timeout = THERMAL_WAIT_READ_TIMEOUT_IMMEDIATE;
        ULONG tenthsKelvin = 0;
        DWORD returned = 0;
        if (!DeviceIoctl(state, zone.get(), IOCTL_THERMAL_READ_TEMPERATURE, &wait, static_cast<DWORD>(sizeof(wait)),
                         &tenthsKelvin, static_cast<DWORD>(sizeof(tenthsKelvin)), &returned) ||
            returned < sizeof(tenthsKelvin))
        {
            continue;
        }
        double celsius = 0.0;
        if (!TenthsKelvinToCelsius(tenthsKelvin, celsius))
        {
            continue;
        }
        RedXeAcpiThermalRow& row = state.acpiThermals[state.acpiThermalCount];
        row = {};
        CopyWide(row.sensorId, std::size(row.sensorId), path);
        CopyWide(row.displayName, std::size(row.displayName), L"ACPI thermal zone");
        row.temperatureC = celsius;
        THERMAL_INFORMATION information{};
        if (DeviceIoctl(state, zone.get(), IOCTL_THERMAL_QUERY_INFORMATION, nullptr, 0, &information,
                        static_cast<DWORD>(sizeof(information)), &returned) &&
            returned >= sizeof(information))
        {
            double warning = 0.0;
            double critical = 0.0;
            if (TenthsKelvinToCelsius(information.PassiveTripPoint, warning))
            {
                row.warningTemperatureC = warning;
                row.hasWarning = true;
            }
            if (TenthsKelvinToCelsius(information.CriticalTripPoint, critical))
            {
                row.criticalTemperatureC = critical;
                row.hasCritical = true;
            }
        }
        ++state.acpiThermalCount;
    }
}

void RedXePowerSampleFanPresence(RedXePowerState& state) noexcept
{
    state.fanInterfaceCount = CountDeviceInterfaces(GUID_DEVICE_FAN);
}
