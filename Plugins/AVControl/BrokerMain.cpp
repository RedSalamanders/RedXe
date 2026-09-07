#include "AVControlProtocol.h"
#include "AVControlProtocolValidation.h"
#include "WindowsAudioBackend.h"
#include "WindowsCameraBackend.h"
#include <bit>
#include <cerrno>
#include <cwchar>
#include <memory>
#include <objbase.h>
#include <wil/resource.h>

namespace AVControl
{
namespace
{
bool ReadHandle(PCWSTR value, wil::unique_handle& handle) noexcept
{
    if (!value || !value[0] || value[0] == L'-')
        return false;
    wchar_t* end = nullptr;
    errno = 0;
    const auto parsed = wcstoull(value, &end, 10);
    if (errno || !end || *end || parsed == 0 || parsed > UINTPTR_MAX)
        return false;
    HANDLE raw = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(parsed));
    DWORD flags = 0;
    if (!GetHandleInformation(raw, &flags))
        return false;
    handle.reset(raw);
    return SetHandleInformation(raw, HANDLE_FLAG_INHERIT, 0) != FALSE;
}
void InitializeFixture(Inventory& inventory) noexcept
{
    inventory.outputCount = inventory.inputCount = inventory.cameraCount = 2;
    inventory.capabilities = CapabilityAudioLevels | CapabilityAudioDefaults | CapabilityCameraRoute;
    for (size_t i = 0; i < 2; ++i)
    {
        auto& output = inventory.outputs[i];
        auto& input = inventory.inputs[i];
        auto& camera = inventory.cameras[i];
        (void)output.id.Assign(i ? "fixture-output-2" : "fixture-output-1");
        (void)input.id.Assign(i ? "fixture-input-2" : "fixture-input-1");
        (void)camera.id.Assign(i ? "fixture-camera-2" : "fixture-camera-1");
        wcscpy_s(output.name.data(), output.name.size(), i ? L"Fixture headset" : L"Fixture speakers");
        wcscpy_s(input.name.data(), input.name.size(), i ? L"Fixture headset mic" : L"Fixture desk mic");
        wcscpy_s(camera.name.data(), camera.name.size(), i ? L"Fixture camera 2" : L"Fixture camera 1");
        output.availability = input.availability = camera.availability = Availability::Ready;
        output.level = 65;
        input.level = 72;
        input.muted = i == 0;
        output.generation = input.generation = output.levelRevision = input.levelRevision = output.muteRevision =
            input.muteRevision = 1;
    }
    inventory.state.output = inventory.outputs[0];
    inventory.state.microphone = inventory.inputs[0];
    inventory.state.camera = {inventory.cameras[0].id, Availability::Ready, false, 1};
    for (size_t role = 0; role < 3; ++role)
    {
        inventory.state.outputDefaults[role] = inventory.outputs[0].id;
        inventory.state.inputDefaults[role] = inventory.inputs[0].id;
        inventory.outputRoleRevisions[role] = inventory.inputRoleRevisions[role] = 1;
    }
    inventory.state.revision = 1;
}
HRESULT FixtureCommand(const BrokerCommand& command, Inventory& inventory) noexcept
{
    if (command.operation == BrokerOperation::Observe)
        return S_OK;
    if (command.operation == BrokerOperation::SuspendObservation)
        return S_OK;
    if (command.operation == BrokerOperation::FixtureHang)
    {
        Sleep(INFINITE);
        return E_UNEXPECTED;
    }
    if (command.operation == BrokerOperation::FixtureExit)
        ExitProcess(42);
    if (command.operation == BrokerOperation::FixtureMalformedReply)
    {
        switch (command.value)
        {
        case 1:
            inventory.outputs[0].id.length = MaximumDeviceIdBytes + 1;
            break;
        case 2:
            inventory.outputs[0].id.bytes[0] = std::bit_cast<char>(uint8_t{0xc0});
            break;
        case 3:
            inventory.outputs[0].id.bytes[1] = 0;
            break;
        case 4:
            inventory.outputs[0].name.fill(L'x');
            break;
        case 5:
            inventory.outputs[0].level = 101;
            break;
        case 6:
            inventory.outputs[0].availability = static_cast<Availability>(255);
            break;
        case 7:
            *reinterpret_cast<unsigned char*>(&inventory.outputs[0].muted) = 2;
            break;
        case 8:
            inventory.capabilities |= 0x80000000;
            break;
        case 9:
            *reinterpret_cast<unsigned char*>(&inventory.cameraRequiresHelper) = 2;
            break;
        case 10:
            inventory.outputs[1].id = inventory.outputs[0].id;
            break;
        case 11:
            inventory.state.outputDefaults[0].length = MaximumDeviceIdBytes + 1;
            break;
        case 12:
            inventory.state.camera.sourceId.bytes[0] = 0;
            break;
        case 13:
            inventory.outputs[0].name[0] = 0xd800;
            inventory.outputs[0].name[1] = 0;
            break;
        default:
            inventory.outputCount = 999;
            break;
        }
        return S_OK;
    }
    if (command.device == DeviceKind::Camera)
    {
        if (command.expectedRevision != inventory.state.camera.revision)
            return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        if (command.operation == BrokerOperation::SetCameraSource)
        {
            bool found = false;
            for (uint32_t i = 0; i < inventory.cameraCount; ++i)
                found = found || inventory.cameras[i].id == command.id;
            if (!found)
                return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            if (inventory.state.camera.sourceId != command.id)
            {
                inventory.state.camera.sourceId = command.id;
                ++inventory.state.camera.revision;
            }
        }
        else if (command.operation == BrokerOperation::SetCameraEnabled && command.value <= 1)
        {
            if (inventory.state.camera.enabled != (command.value != 0))
            {
                inventory.state.camera.enabled = command.value != 0;
                ++inventory.state.camera.revision;
            }
        }
        else
            return E_INVALIDARG;
    }
    else
    {
        if (command.device != DeviceKind::Output && command.device != DeviceKind::Microphone)
            return E_INVALIDARG;
        auto& endpoints = command.device == DeviceKind::Output ? inventory.outputs : inventory.inputs;
        const auto count = command.device == DeviceKind::Output ? inventory.outputCount : inventory.inputCount;
        Endpoint* endpoint = nullptr;
        for (uint32_t i = 0; i < count; ++i)
            if (endpoints[i].id == command.id)
                endpoint = &endpoints[i];
        if (!endpoint)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        if (endpoint->generation != command.expectedGeneration)
            return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        if (command.operation == BrokerOperation::SetMute)
        {
            if (command.value > 1)
                return E_INVALIDARG;
            if (command.expectedRevision != endpoint->muteRevision)
                return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
            if (endpoint->muted != (command.value != 0))
            {
                endpoint->muted = command.value != 0;
                ++endpoint->muteRevision;
            }
        }
        else if (command.operation == BrokerOperation::SetLevel)
        {
            if (command.value > 100)
                return E_INVALIDARG;
            if (command.expectedRevision != endpoint->levelRevision)
                return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
            if (endpoint->level != command.value)
            {
                endpoint->level = command.value;
                ++endpoint->levelRevision;
            }
        }
        else if (command.operation == BrokerOperation::SetDefault)
        {
            if (command.role >= 3)
                return E_INVALIDARG;
            auto& defaults =
                command.device == DeviceKind::Output ? inventory.state.outputDefaults : inventory.state.inputDefaults;
            auto& revisions =
                command.device == DeviceKind::Output ? inventory.outputRoleRevisions : inventory.inputRoleRevisions;
            if (command.expectedRevision != revisions[command.role])
                return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
            if (defaults[command.role] != command.id)
            {
                defaults[command.role] = command.id;
                ++revisions[command.role];
            }
        }
        else
            return E_INVALIDARG;
        for (uint32_t i = 0; i < inventory.outputCount; ++i)
            if (inventory.outputs[i].id == inventory.state.outputDefaults[0])
                inventory.state.output = inventory.outputs[i];
        for (uint32_t i = 0; i < inventory.inputCount; ++i)
            if (inventory.inputs[i].id == inventory.state.inputDefaults[0])
                inventory.state.microphone = inventory.inputs[i];
    }
    ++inventory.state.revision;
    inventory.cameraRequiresHelper = inventory.state.camera.enabled;
    return S_OK;
}
} // namespace
int RunBroker(int argc, wchar_t** argv)
{
    if (argc != 6 || std::wstring_view(argv[1]) != L"--broker")
        return 2;
    wil::unique_handle mapping, request, reply, changed;
    if (!ReadHandle(argv[2], mapping) || !ReadHandle(argv[3], request) || !ReadHandle(argv[4], reply) ||
        !ReadHandle(argv[5], changed))
        return 3;
    wil::unique_mapview_ptr<BrokerShared> shared(
        static_cast<BrokerShared*>(MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BrokerShared))));
    if (!shared || shared->magic != BrokerMagic || shared->version != BrokerProtocolVersion ||
        shared->sizeBytes != sizeof(BrokerShared) || shared->synthetic > 1)
        return 4;
    const bool synthetic = shared->synthetic != 0;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized))
        return 5;
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    std::unique_ptr<WindowsAudioBackend> windows;
    std::unique_ptr<WindowsCameraBackend> cameras;
    HRESULT backendResult = S_OK;
    HRESULT cameraResult = S_OK;
    if (synthetic)
        InitializeFixture(shared->reply.inventory);
    else
    {
        windows = std::make_unique<WindowsAudioBackend>();
        backendResult = windows->Initialize(changed.get());
        cameras = std::make_unique<WindowsCameraBackend>();
        cameraResult = cameras->Initialize(changed.get());
        if (FAILED(cameraResult))
            cameras.reset();
    }
    for (;;)
    {
        HANDLE events[3]{request.get()};
        DWORD count = 1, progressIndex = UINT_MAX, notificationIndex = UINT_MAX;
        if (cameras && cameras->ProgressEvent())
        {
            progressIndex = count;
            events[count++] = cameras->ProgressEvent();
        }
        if (cameras && cameras->NotificationEvent())
        {
            notificationIndex = count;
            events[count++] = cameras->NotificationEvent();
        }
        const DWORD timeout = cameras ? cameras->WatchdogTimeout() : INFINITE;
        const DWORD wait = WaitForMultipleObjects(count, events, FALSE, timeout);
        if (progressIndex < count && wait == WAIT_OBJECT_0 + progressIndex)
            continue;
        if (notificationIndex < count && wait == WAIT_OBJECT_0 + notificationIndex)
        {
            // Maintain camera hotplug safety even while display observation is suspended. Async work updates
            // backend state only; mapped reply bytes are written exclusively inside the request/reply handshake.
            (void)cameras->ProcessNotifications();
            SetEvent(changed.get());
            continue;
        }
        if (wait == WAIT_TIMEOUT)
        {
            if (!cameras || cameras->WatchdogTimeout() != 0)
                continue;
            // Only this owned helper terminates. A consumer-side source immediately switches to neutral;
            // the watchdog has no UI, device handle, recording, or authority over another process.
            SetEvent(changed.get());
            (void)TerminateProcess(GetCurrentProcess(), ERROR_TIMEOUT);
            return 9;
        }
        if (wait != WAIT_OBJECT_0)
            break;
        MemoryBarrier();
        if (shared->magic != BrokerMagic || shared->version != BrokerProtocolVersion ||
            shared->sizeBytes != sizeof(BrokerShared))
            return 6;
        const BrokerCommand command = shared->command;
        const uint32_t sequence = shared->requestSequence;
        if (!ValidInventoryPreferences(shared->preferences))
            shared->reply.result = E_INVALIDARG;
        else if (synthetic)
        {
            shared->reply.result = FixtureCommand(command, shared->reply.inventory);
            shared->reply.inventory.truncated = shared->preferences.truncated != 0;
        }
        else if (command.operation == BrokerOperation::SuspendObservation)
        {
            // Detach display-only audio subscriptions while retaining an armed/consumed camera route.
            // A later observation reconstructs subscriptions and reads current Windows state before input.
            windows.reset();
            shared->reply.inventory.cameraRequiresHelper = cameras && cameras->RequiresHelper();
            shared->reply.result = S_OK;
        }
        else
        {
            if (!windows)
            {
                windows = std::make_unique<WindowsAudioBackend>();
                backendResult = windows->Initialize(changed.get());
            }
            const bool cameraCommand = command.operation == BrokerOperation::SetCameraSource ||
                                       command.operation == BrokerOperation::SetCameraEnabled;
            const HRESULT audio = FAILED(backendResult)
                                      ? backendResult
                                      : windows->Execute(cameraCommand ? BrokerCommand{} : command, shared->preferences,
                                                         shared->reply.inventory);
            if (cameras)
                cameras->SetPreferences(shared->preferences.cameras);
            const HRESULT camera = cameras ? cameraCommand ? cameras->Execute(command, shared->reply.inventory)
                                                           : cameras->Observe(shared->reply.inventory)
                                           : cameraResult;
            if (!cameras)
            {
                shared->reply.inventory.state.camera = {};
                shared->reply.inventory.state.camera.availability =
                    cameraResult == E_ACCESSDENIED ? Availability::AccessDenied : Availability::Unsupported;
            }
            shared->reply.result = cameraCommand ? camera : audio;
            // A camera failure cannot disable independent audio controls. Camera commands retain their own
            // result and state; their async acquisition watchdog also applies while the tile is hidden.
        }
        shared->replySequence = sequence;
        MemoryBarrier();
        if (!SetEvent(reply.get()))
            return 7;
    }
    return 0;
}
} // namespace AVControl

int wmain(int argc, wchar_t** argv)
{
    try
    {
        return AVControl::RunBroker(argc, argv);
    }
    catch (...)
    {
        return 8;
    }
}
