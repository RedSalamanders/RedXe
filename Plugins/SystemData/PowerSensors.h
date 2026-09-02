#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

constexpr size_t kRedXeMaximumBatteries = 32;
constexpr size_t kRedXeMaximumAcpiThermals = 32;
constexpr size_t kRedXePowerNameCharacters = 128;
constexpr size_t kRedXeBatteryPathCharacters = 260;
constexpr size_t kRedXeChemistryCharacters = 8;

constexpr uint64_t kRedXeThermalKindGpu = 0;
constexpr uint64_t kRedXeThermalKindStorage = 1;
constexpr uint64_t kRedXeThermalKindBattery = 2;
constexpr uint64_t kRedXeThermalKindAcpi = 3;

struct RedXePowerSummaryRow final
{
    uint64_t acOnline = 0;
    uint64_t batteryPresent = 0;
    uint64_t charging = 0;
    uint64_t batterySaver = 0;
    double chargePercent = 0.0;
    uint64_t remainingSeconds = 0;
    uint64_t fullLifeSeconds = 0;
    uint64_t systemS3 = 0;
    uint64_t systemS4 = 0;
    uint64_t hiberFilePresent = 0;
    uint64_t thermalControl = 0;
    bool hasAc = false;
    bool hasBatteryPresent = false;
    bool hasCharging = false;
    bool hasSaver = false;
    bool hasChargePercent = false;
    bool hasRemaining = false;
    bool hasFullLife = false;
    bool hasCapabilities = false;
};

struct RedXeBatteryRow final
{
    wchar_t deviceId[kRedXeBatteryPathCharacters]{};
    wchar_t deviceName[kRedXePowerNameCharacters]{};
    wchar_t chemistry[kRedXeChemistryCharacters]{};
    uint64_t designedCapacity = 0;
    uint64_t fullChargedCapacity = 0;
    uint64_t currentCapacity = 0;
    double rateMw = 0.0;
    uint64_t voltageMv = 0;
    double temperatureC = 0.0;
    uint64_t relativeCapacity = 0;
    uint64_t cycleCount = 0;
    uint64_t estimatedTimeSeconds = 0;
    uint64_t powerState = 0;
    uint64_t tag = 0;
    bool hasDesigned = false;
    bool hasFullCharged = false;
    bool hasCurrent = false;
    bool hasRate = false;
    bool hasVoltage = false;
    bool hasTemperature = false;
    bool hasCycleCount = false;
    bool hasEstimatedTime = false;
    bool hasPowerState = false;
    bool hasRelative = false;
};

struct RedXeAcpiThermalRow final
{
    wchar_t sensorId[kRedXeBatteryPathCharacters]{};
    wchar_t displayName[kRedXePowerNameCharacters]{};
    double temperatureC = 0.0;
    double warningTemperatureC = 0.0;
    double criticalTemperatureC = 0.0;
    bool hasWarning = false;
    bool hasCritical = false;
};

struct RedXeBatteryCache final
{
    wil::unique_hfile handle;
    wchar_t devicePath[kRedXeBatteryPathCharacters]{};
    ULONG tag = 0;
    bool open = false;
    bool tagValid = false;
    bool used = false;
};

struct RedXePowerState final
{
    RedXePowerState() noexcept;
    ~RedXePowerState() noexcept;
    RedXePowerState(const RedXePowerState&) = delete;
    RedXePowerState& operator=(const RedXePowerState&) = delete;

    wil::unique_handle ioctlEvent;
    RedXePowerSummaryRow summary{};
    std::array<RedXeBatteryCache, kRedXeMaximumBatteries> batteryCache{};
    std::array<RedXeBatteryRow, kRedXeMaximumBatteries> batteries{};
    uint32_t batteryCount = 0;
    bool batteriesTruncated = false;
    std::array<RedXeAcpiThermalRow, kRedXeMaximumAcpiThermals> acpiThermals{};
    uint32_t acpiThermalCount = 0;
    bool acpiThermalsTruncated = false;
    uint32_t fanInterfaceCount = 0;
};

void RedXePowerSampleSummary(RedXePowerState& state) noexcept;
void RedXePowerSampleBatteries(RedXePowerState& state) noexcept;
void RedXePowerSampleAcpiThermals(RedXePowerState& state) noexcept;
void RedXePowerSampleFanPresence(RedXePowerState& state) noexcept;
