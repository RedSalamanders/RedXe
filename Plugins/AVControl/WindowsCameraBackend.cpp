#include "WindowsCameraBackend.h"
#include "Camera/CameraCapture.h"
#include "Camera/CameraController.h"
#include "Camera/CameraRegistration.h"
#include <algorithm>
#include <cfgmgr32.h>
#include <ks.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mferror.h>
// Instantiate the SDK virtual-camera device-property key locally, without a static Windows 11 DLL import.
#include <initguid.h>
#include <mfvirtualcamera.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

#pragma comment(lib, "cfgmgr32.lib")

namespace AVControl
{
namespace
{
bool EncodeCameraId(PCWSTR text, DeviceId& id) noexcept
{
    if (!text) return false;
    const auto length = wcsnlen_s(text, MaximumDeviceIdBytes + 1);
    if (!length || length > MaximumDeviceIdBytes) return false;
    std::array<char, MaximumDeviceIdBytes> bytes{};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, static_cast<int>(length), bytes.data(),
        static_cast<int>(bytes.size()), nullptr, nullptr);
    return count > 0 && id.Assign({bytes.data(), static_cast<size_t>(count)});
}
bool DecodeCameraId(const DeviceId& id, std::array<wchar_t, MaximumDeviceIdBytes + 1>& result) noexcept
{
    const auto value = id.View(); result = {};
    return !value.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), static_cast<int>(result.size() - 1)) > 0;
}
class PhysicalCapture final : public Camera::CaptureSession
{
  public:
    HRESULT Open(const DeviceId& id, HANDLE wake) noexcept override
    {
        std::array<wchar_t, MaximumDeviceIdBytes + 1> name{};
        return DecodeCameraId(id, name) ? _reader.OpenDevice(name.data(), wake) : E_INVALIDARG;
    }
    void Close() noexcept override { _reader.Close(); }
    HRESULT ReadFrame(std::span<BYTE> output, LONGLONG& timestamp) noexcept override { return _reader.ReadFrame(output, timestamp); }
  private:
    Camera::CaptureReader _reader;
};
}
struct WindowsCameraBackend::State final
{
    Camera::VirtualCameraRoute route;
    std::unique_ptr<Camera::CameraController> controller;
    std::array<CameraDescriptor, MaximumCameras> cameras{};
    DevicePreferences<MaximumCameras> preferences;
    uint32_t count = 0;
    bool startedMf = false, truncated = false;
    HRESULT routeResult = E_NOTIMPL;
    HANDLE changed = nullptr; // Borrowed until the callback registration is drained at teardown.
    wil::unique_event inventoryChanged;
    std::atomic<bool> dirty{true};
    std::atomic<uint64_t> selectedChanges{0};
    uint64_t observedSelectedChanges = 0;
    SRWLOCK selectedLock = SRWLOCK_INIT;
    std::array<wchar_t, MaximumDeviceIdBytes + 1> selectedName{};
    wil::unique_any<HCMNOTIFICATION, decltype(&CM_Unregister_Notification), CM_Unregister_Notification> notification;
    ~State()
    {
        notification.reset(); controller.reset(); route.Close();
        if (startedMf) (void)MFShutdown();
    }
    static DWORD CALLBACK Changed(HCMNOTIFICATION, void* context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA data, DWORD size) noexcept
    {
        auto& self = *static_cast<State*>(context);
        if (action != CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL && action != CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) return ERROR_SUCCESS;
        constexpr size_t offset = offsetof(CM_NOTIFY_EVENT_DATA, u.DeviceInterface.SymbolicLink);
        if (data && size > offset && data->FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE)
        {
            const auto maximum = (size - offset) / sizeof(wchar_t);
            const auto* name = data->u.DeviceInterface.SymbolicLink;
            const auto length = wcsnlen_s(name, maximum);
            if (length < maximum)
            {
                const auto lock = wil::AcquireSRWLockShared(&self.selectedLock);
                if (self.selectedName[0] && CompareStringOrdinal(name, static_cast<int>(length), self.selectedName.data(), -1, TRUE) == CSTR_EQUAL)
                    ++self.selectedChanges;
            }
        }
        self.dirty.store(true, std::memory_order_release);
        if (self.inventoryChanged) SetEvent(self.inventoryChanged.get());
        if (self.changed) SetEvent(self.changed);
        return ERROR_SUCCESS;
    }
    bool Present(const DeviceId& id) const noexcept
    {
        for (uint32_t i = 0; i < count; ++i) if (cameras[i].id == id) return true;
        return false;
    }
    void Selected(const DeviceId& id) noexcept
    {
        const auto lock = wil::AcquireSRWLockExclusive(&selectedLock);
        (void)DecodeCameraId(id, selectedName);
    }
    HRESULT Refresh() noexcept
    {
        if (!startedMf) return E_UNEXPECTED;
        if (!dirty.exchange(false, std::memory_order_acq_rel)) return S_OK;
        auto failed = wil::scope_exit([&] { dirty.store(true, std::memory_order_release); });
        wil::com_ptr_nothrow<IMFAttributes> attributes;
        RETURN_IF_FAILED(MFCreateAttributes(attributes.put(), 1));
        RETURN_IF_FAILED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
        IMFActivate** raw = nullptr; UINT32 available = 0;
        RETURN_IF_FAILED(MFEnumDeviceSources(attributes.get(), &raw, &available));
        wil::unique_cotaskmem_ptr<IMFActivate*> devices(raw);
        const auto release = wil::scope_exit([&] {
            for (UINT32 i = 0; i < available; ++i) { wil::com_ptr_nothrow<IMFActivate> owner; owner.attach(raw[i]); }
        });
        std::array<CameraDescriptor, MaximumCameras> next{}; uint32_t admitted = 0; bool overflow = false;
        RankedInventory selection(next, admitted);
        const DeviceId selected = controller ? controller->Snapshot().sourceId : DeviceId{};
        for (UINT32 i = 0; i < available; ++i)
        {
            wil::unique_cotaskmem_string symbolic, friendly; UINT32 length = 0;
            if (FAILED(raw[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, symbolic.put(), &length))) continue;
            if (!length || length > MaximumDeviceIdBytes) { overflow = true; continue; }
            if (!route.SymbolicLink().empty() && CompareStringOrdinal(symbolic.get(), static_cast<int>(length),
                route.SymbolicLink().data(), static_cast<int>(route.SymbolicLink().size()), TRUE) == CSTR_EQUAL) continue;
            // Never wrap this route or another identified software camera into a recursive capture pipeline.
            DEVPROP_BOOLEAN virtualDevice = DEVPROP_FALSE; DEVPROPTYPE type = 0; ULONG bytes = sizeof(virtualDevice);
            if (CM_Get_Device_Interface_PropertyW(symbolic.get(), &DEVPKEY_DeviceInterface_IsVirtualCamera, &type,
                reinterpret_cast<PBYTE>(&virtualDevice), &bytes, 0) == CR_SUCCESS && type == DEVPROP_TYPE_BOOLEAN && virtualDevice != DEVPROP_FALSE) continue;
            CameraDescriptor device;
            if (!EncodeCameraId(symbolic.get(), device.id)) { overflow = true; continue; }
            device.availability = Availability::Ready;
            if (SUCCEEDED(raw[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, friendly.put(), &length)))
            {
                size_t copy = (std::min)(static_cast<size_t>(length), device.name.size() - 1);
                if (copy && friendly.get()[copy - 1] >= 0xd800 && friendly.get()[copy - 1] <= 0xdbff) --copy;
                std::copy_n(friendly.get(), copy, device.name.data());
            }
            selection.Offer(device, InventoryRank(device.id, std::span{&selected, size_t{1}}, preferences));
        }
        cameras = next; count = admitted; truncated = overflow || selection.Truncated();
        if (controller)
        {
            auto state = controller->Snapshot();
            const auto notifications = selectedChanges.load();
            if (notifications != observedSelectedChanges || (!state.sourceId.View().empty() && !Present(state.sourceId)))
            {
                observedSelectedChanges = notifications;
                RETURN_IF_FAILED(controller->Enable(false, state.revision));
                controller->InvalidateSourceRevision(); state = controller->Snapshot();
                // A camera that was removed/replaced never resumes capture automatically, including a quick
                // removal/arrival with the same symbolic link between two inventory observations.
            }
            if (state.sourceId.View().empty() && count) RETURN_IF_FAILED(controller->Select(cameras[0].id, state.revision));
            Selected(controller->Snapshot().sourceId);
        }
        failed.release();
        return S_OK;
    }
    void Copy(Inventory& inventory) noexcept
    {
        inventory.cameras = cameras; inventory.cameraCount = count; inventory.truncated = inventory.truncated || truncated;
        inventory.capabilities &= ~CapabilityCameraRoute;
        inventory.cameraRequiresHelper = controller && controller->RequiresHelper();
        if (controller)
        {
            inventory.capabilities |= CapabilityCameraRoute;
            inventory.state.camera = controller->Snapshot();
            if (!Present(inventory.state.camera.sourceId)) inventory.state.camera.availability = Availability::Missing;
        }
        else
        {
            inventory.state.camera = {};
            inventory.state.camera.availability = routeResult == E_ACCESSDENIED ? Availability::AccessDenied : Availability::Unsupported;
        }
    }
};
WindowsCameraBackend::WindowsCameraBackend() : _state(std::make_unique<State>()) {}
WindowsCameraBackend::~WindowsCameraBackend() = default;
void WindowsCameraBackend::SetPreferences(const DevicePreferences<MaximumCameras>& preferences) noexcept
{
    if (_state->preferences == preferences) return;
    _state->preferences = preferences;
    _state->dirty.store(true, std::memory_order_release);
}
HRESULT WindowsCameraBackend::Initialize(HANDLE changed) noexcept
{
    if (!changed) return E_INVALIDARG;
    _state->changed = changed;
    _state->inventoryChanged.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!_state->inventoryChanged) return HRESULT_FROM_WIN32(GetLastError());
    RETURN_IF_FAILED(MFStartup(MF_VERSION)); _state->startedMf = true;
    _state->routeResult = _state->route.Open();
    if (SUCCEEDED(_state->routeResult))
    {
        Camera::BridgeIdentity identity;
        RETURN_IF_FAILED(Camera::MakeBridgeIdentity(_state->route.OwnerSid().data(), identity));
        try { _state->controller = std::make_unique<Camera::CameraController>(std::move(identity), std::make_unique<PhysicalCapture>()); }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        RETURN_IF_FAILED(_state->controller->Initialize(changed));
    }
    CM_NOTIFY_FILTER filter{}; filter.cbSize = sizeof(filter); filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = KSCATEGORY_VIDEO_CAMERA;
    const CONFIGRET registered = CM_Register_Notification(&filter, _state.get(), State::Changed, _state->notification.put());
    if (registered != CR_SUCCESS) return HRESULT_FROM_WIN32(CM_MapCrToWin32Err(registered, ERROR_GEN_FAILURE));
    return _state->Refresh();
}
HRESULT WindowsCameraBackend::Observe(Inventory& inventory) noexcept
{
    const HRESULT result = _state->Refresh(); _state->Copy(inventory);
    if (FAILED(result)) inventory.state.camera.availability = Availability::Unknown;
    return result;
}
HRESULT WindowsCameraBackend::Execute(const BrokerCommand& command, Inventory& inventory) noexcept
{
    HRESULT result = _state->Refresh();
    const bool observed = SUCCEEDED(result);
    if (SUCCEEDED(result))
    {
        if (!_state->controller) result = FAILED(_state->routeResult) ? _state->routeResult : E_NOTIMPL;
        else if (command.device != DeviceKind::Camera) result = E_INVALIDARG;
        else if (command.operation == BrokerOperation::SetCameraSource)
            result = _state->Present(command.id) ? _state->controller->Select(command.id, command.expectedRevision) : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        else if (command.operation == BrokerOperation::SetCameraEnabled && command.value <= 1)
            result = command.value && !_state->Present(_state->controller->Snapshot().sourceId) ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND) :
                _state->controller->Enable(command.value != 0, command.expectedRevision);
        else result = E_INVALIDARG;
    }
    if (_state->controller) _state->Selected(_state->controller->Snapshot().sourceId);
    _state->Copy(inventory);
    if (!observed) inventory.state.camera.availability = Availability::Unknown;
    return result;
}
bool WindowsCameraBackend::RequiresHelper() noexcept { return _state->controller && _state->controller->RequiresHelper(); }
HANDLE WindowsCameraBackend::ProgressEvent() const noexcept { return _state->controller ? _state->controller->ProgressEvent() : nullptr; }
HANDLE WindowsCameraBackend::NotificationEvent() const noexcept { return _state->inventoryChanged.get(); }
HRESULT WindowsCameraBackend::ProcessNotifications() noexcept { return _state->Refresh(); }
DWORD WindowsCameraBackend::WatchdogTimeout() const noexcept { return _state->controller ? _state->controller->WatchdogTimeout() : INFINITE; }
} // namespace AVControl
