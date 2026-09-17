#define REDXE_PLUGIN_EXPORTS
#include "LogiconService.h"
#include "LogiconTestContract.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Service.h"

#include "LogiconMonitor.h"
#include "PlugInterfaces/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
constexpr RedXePluginSettingsContract kServiceContract{
    sizeof(RedXePluginSettingsContract),    Logicon::kSettingsSchema,
    sizeof(Logicon::kSettingsSchema) - 1,   Logicon::kSettingsDefaults,
    sizeof(Logicon::kSettingsDefaults) - 1,
};

constexpr RedXePluginSettingsContract kMonitorContract{
    sizeof(RedXePluginSettingsContract),           Logicon::kMonitorSettingsSchema,
    sizeof(Logicon::kMonitorSettingsSchema) - 1,   Logicon::kMonitorSettingsDefaults,
    sizeof(Logicon::kMonitorSettingsDefaults) - 1,
};

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        Logicon::kPluginId,
        L"Logicon",
        L"Drives the Logitech MX Creative Console: keypad faces and keys, the dialpad, and dashboard actions.",
        L"RedXe",
        L"1.0.0",
        RedXePluginCapabilityService | RedXePluginCapabilityActions,
    },
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        Logicon::kMonitorPluginId,
        L"Logicon Monitor",
        L"Developer tile showing the keypad's keys, faces, page buttons, dial state, and HID++ traffic (Debug builds).",
        L"RedXe",
        L"1.0.0",
        RedXePluginCapabilityWidgetProvider,
    },
};

constexpr std::array kSettingsContracts{
    RedXeSettingsContractEntry{Logicon::kPluginId, &kServiceContract},
    RedXeSettingsContractEntry{Logicon::kMonitorPluginId, &kMonitorContract},
};

// The published "logicon" namespace: key pages and keypad brightness, executed on the service lane.
constexpr std::array kLogiconActions{
    RedXeActionDescriptor{sizeof(RedXeActionDescriptor), RedXeActionFlagDeferred, "logicon.keyPage.next",
                          L"Next key page", L"", RedXeActionTargetNone, 0, 0, nullptr},
    RedXeActionDescriptor{sizeof(RedXeActionDescriptor), RedXeActionFlagDeferred, "logicon.keyPage.previous",
                          L"Previous key page", L"", RedXeActionTargetNone, 0, 0, nullptr},
    RedXeActionDescriptor{sizeof(RedXeActionDescriptor), RedXeActionFlagDeferred, "logicon.keyPage.goto",
                          L"Go to key page", L"0 through 3", RedXeActionTargetInteger, 0, 3, nullptr},
    RedXeActionDescriptor{sizeof(RedXeActionDescriptor), RedXeActionFlagDeferred, "logicon.brightness",
                          L"Keypad brightness", L"1 through 100, +n, or -n", RedXeActionTargetDelta, 1, 100, nullptr},
};

constexpr std::array kLogiconNamespaces{
    RedXeActionNamespace{sizeof(RedXeActionNamespace), static_cast<uint32_t>(kLogiconActions.size()),
                         Logicon::kActionNamespace, kLogiconActions.data()},
};

constexpr RedXeActionContract kLogiconActionContract{
    sizeof(RedXeActionContract), static_cast<uint32_t>(kLogiconNamespaces.size()), kLogiconNamespaces.data()};

HRESULT CreateLogiconService(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                             void** result) noexcept
{
    // The service object also carries IRedXeActionPack, but the host obtains that on the started object.
    if (interfaceId != __uuidof(IRedXeService))
    {
        return E_NOINTERFACE;
    }
    if (!host)
    {
        return E_POINTER;
    }
    if (options && options->sizeBytes != sizeof(RedXeFactoryOptions))
    {
        return E_INVALIDARG;
    }
    const char* configuration = options ? options->configurationJsonUtf8 : nullptr;
    const uint32_t configurationBytes = options ? options->configurationBytes : 0;
    if ((configuration == nullptr) != (configurationBytes == 0) ||
        configurationBytes > kRedXeMaximumFactoryConfigurationBytes)
    {
        return E_INVALIDARG;
    }
    const uint32_t backgroundRgb = options ? RedXeBackgroundRgb(options) : 0;
    wil::com_ptr_nothrow<Logicon::LogiconService> service;
    service.attach(new (std::nothrow) Logicon::LogiconService(host, backgroundRgb));
    if (!service)
    {
        return E_OUTOFMEMORY;
    }
    const HRESULT parsed = service->ParseConfiguration(configuration, configurationBytes);
    if (FAILED(parsed))
    {
        return parsed;
    }
    *result = static_cast<IRedXeService*>(service.detach());
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateLogiconService},
    RedXeFactoryEntry{&kMetadata[1], Logicon::CreateMonitorProvider},
};
} // namespace

extern "C" HRESULT __stdcall RedXeCreate(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                         const char* pluginId, void** result) noexcept
{
    return RedXeCreateFromFactoryEntries(kFactoryEntries.data(), static_cast<uint32_t>(kFactoryEntries.size()),
                                         interfaceId, options, host, pluginId, result);
}

extern "C" HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata, uint32_t* count) noexcept
{
    return RedXeEnumerateFactoryMetadata(kMetadata.data(), static_cast<uint32_t>(kMetadata.size()), metadata, count);
}

extern "C" HRESULT __stdcall RedXeGetPluginSettingsContract(const char* pluginId,
                                                            const RedXePluginSettingsContract** contract) noexcept
{
    return RedXeGetPluginSettingsContractFromEntries(
        kSettingsContracts.data(), static_cast<uint32_t>(kSettingsContracts.size()), pluginId, contract);
}

extern "C" HRESULT __stdcall RedXeGetActionContract(const char* pluginId, const RedXeActionContract** contract) noexcept
{
    if (!contract)
    {
        return E_POINTER;
    }
    *contract = nullptr;
    if (!RedXeAsciiEqualsIgnoreCase(pluginId, Logicon::kPluginId))
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    *contract = &kLogiconActionContract;
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeLogiconGetTestDiagnostics(RedXeLogiconTestDiagnostics* diagnostics) noexcept
{
    if (!diagnostics)
    {
        return E_POINTER;
    }
    if (diagnostics->sizeBytes != sizeof(RedXeLogiconTestDiagnostics))
    {
        return E_INVALIDARG;
    }
    Logicon::LogiconService* service = Logicon::LogiconService::Current();
    if (!service)
    {
        *diagnostics = RedXeLogiconTestDiagnostics{};
        diagnostics->sizeBytes = sizeof(RedXeLogiconTestDiagnostics);
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    }
    Logicon::MonitorSnapshot snapshot{};
    service->CopyMonitorSnapshot(snapshot);
    RedXeLogiconTestDiagnostics& out = *diagnostics;
    out = RedXeLogiconTestDiagnostics{};
    out.sizeBytes = sizeof(RedXeLogiconTestDiagnostics);
    out.serviceStarted = service->Started() ? 1U : 0U;
    out.laneRunning = snapshot.laneRunning ? 1U : 0U;
    out.deviceAccess = snapshot.deviceAccess ? 1U : 0U;
    out.connected = snapshot.connected ? 1U : 0U;
    out.synthetic = snapshot.synthetic ? 1U : 0U;
    out.rendererReady = snapshot.rendererReady ? 1U : 0U;
    out.keys = snapshot.keys;
    out.pageButtons = snapshot.pageButtons;
    out.keyPage = snapshot.keyPage;
    out.keyPageCount = snapshot.keyPageCount;
    out.brightness = snapshot.brightness;
    out.faceGeneration = snapshot.faceGeneration;
    out.facesWritten = snapshot.facesWritten;
    out.actionsRequested = snapshot.actionsRequested;
    out.localExecuted = snapshot.localExecuted;
    strncpy_s(out.lastAction, std::size(out.lastAction), snapshot.lastAction.data(), _TRUNCATE);
    out.hostPageIndex = snapshot.host.pageIndex;
    out.hostPageCount = snapshot.host.pageCount;
    out.hostFlags = snapshot.host.flags;
    out.reportsIn = snapshot.counters.reportsIn;
    out.reportsOut = snapshot.counters.reportsOut;
    out.lastFailure = static_cast<int32_t>(snapshot.lastFailure);
    out.dialpadConnected = snapshot.dialpad.connected ? 1U : 0U;
    out.dialButtons = snapshot.dialpad.buttons;
    out.dialButtonPresses = snapshot.dialpad.buttonPresses;
    out.wheelsListening = snapshot.dialpad.wheelsListening ? 1U : 0U;
    out.dialRaw = snapshot.dialpad.wheels.dialRaw;
    out.rollerRaw = snapshot.dialpad.wheels.rollerRaw;
    out.wheelSteps = snapshot.dialpad.wheelSteps;
    out.systemFeed = snapshot.systemFeed ? 1U : 0U;
    out.cpuPercent = snapshot.system.cpuPercent;
    out.memoryPercent = snapshot.system.memoryPercent;
    out.gpuPercent = snapshot.system.gpuPercent;
    Logicon::SyntheticKeypad* synthetic = service->Synthetic();
    if (synthetic)
    {
        out.syntheticImages = synthetic->ImagesWritten();
        out.syntheticCommands = synthetic->CommandsReceived();
        out.syntheticBrightness = synthetic->BrightnessPercent();
        out.syntheticPageButtonsDiverted = synthetic->PageButtonsDiverted() ? 1U : 0U;
        out.syntheticResetSeen = synthetic->ResetToLogoSeen() ? 1U : 0U;
    }
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeLogiconUseSyntheticDevice(BOOL enabled) noexcept
{
    Logicon::LogiconService* service = Logicon::LogiconService::Current();
    return service ? service->SetSynthetic(enabled != FALSE) : HRESULT_FROM_WIN32(ERROR_NOT_READY);
}

extern "C" HRESULT __stdcall RedXeLogiconInjectControl(uint32_t kind, uint32_t index, BOOL down) noexcept
{
    Logicon::LogiconService* service = Logicon::LogiconService::Current();
    return service ? service->InjectControl(kind, index, down != FALSE) : HRESULT_FROM_WIN32(ERROR_NOT_READY);
}

extern "C" HRESULT __stdcall RedXeLogiconInjectSyntheticReport(const uint8_t* report, uint32_t bytes) noexcept
{
    Logicon::LogiconService* service = Logicon::LogiconService::Current();
    return service ? service->InjectSyntheticReport(report, bytes) : HRESULT_FROM_WIN32(ERROR_NOT_READY);
}

extern "C" HRESULT __stdcall RedXeLogiconSetFaceOverride(uint32_t slot, uint32_t kind, uint32_t colorRgb) noexcept
{
    Logicon::LogiconService* service = Logicon::LogiconService::Current();
    if (!service)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    }
    if (kind > 2)
    {
        return E_INVALIDARG;
    }
    return service->SetFaceOverride(slot, static_cast<Logicon::OverrideKind>(kind), colorRgb);
}

extern "C" HRESULT __stdcall RedXeLogiconSetBrightness(uint32_t percent) noexcept
{
    Logicon::LogiconService* service = Logicon::LogiconService::Current();
    return service ? service->SetBrightness(percent) : HRESULT_FROM_WIN32(ERROR_NOT_READY);
}
