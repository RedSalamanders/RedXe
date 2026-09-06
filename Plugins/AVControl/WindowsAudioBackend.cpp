#include "WindowsAudioBackend.h"
#include "AudioRevisionState.h"
#include "PlugInterfaces/FactoryImpl.h"
#include <algorithm>
#include <cmath>
#include <audioclient.h>
#include <endpointvolume.h>
// clang-format off
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
// clang-format on
#include <mmdeviceapi.h>
#include <propsys.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

namespace AVControl
{
bool AudioPolicyAvailable() noexcept;
HRESULT SetAudioDefault(PCWSTR endpoint, ERole role) noexcept;
namespace
{
constexpr GUID OwnVolumeContext{0x45b87693, 0xa0c8, 0x4403, {0x95, 0xf4, 0x8d, 0x28, 0xef, 0xc1, 0xee, 0x2a}};
Availability Unavailable(HRESULT hr) noexcept
{
    if (hr == E_ACCESSDENIED) return Availability::AccessDenied;
    if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || hr == AUDCLNT_E_DEVICE_INVALIDATED) return Availability::Missing;
    if (hr == E_NOINTERFACE || hr == E_NOTIMPL) return Availability::Unsupported;
    return Availability::Failed;
}
bool EncodeId(PCWSTR id, DeviceId& destination) noexcept
{
    if (!id) return false;
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, id, -1, destination.bytes.data(),
        static_cast<int>(destination.bytes.size()), nullptr, nullptr);
    if (count <= 1) return false;
    destination.length = static_cast<uint32_t>(count - 1); return true;
}
bool DecodeId(const DeviceId& id, std::array<wchar_t, MaximumDeviceIdBytes + 1>& destination) noexcept
{
    if (id.length == 0 || id.length > MaximumDeviceIdBytes || id.View().find('\0') != std::string_view::npos) return false;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, id.bytes.data(), static_cast<int>(id.length),
        destination.data(), static_cast<int>(destination.size() - 1));
    if (count <= 0) return false;
    destination[static_cast<size_t>(count)] = 0; return true;
}
const Endpoint* Find(const Inventory& inventory, DeviceKind kind, const DeviceId& id) noexcept
{
    const auto& endpoints = kind == DeviceKind::Output ? inventory.outputs : inventory.inputs;
    const uint32_t count = kind == DeviceKind::Output ? inventory.outputCount : inventory.inputCount;
    for (uint32_t i = 0; i < count; ++i) if (endpoints[i].id == id) return &endpoints[i];
    return nullptr;
}
class Notifications final : public RedXeComObject<Notifications, IMMNotificationClient>
{
  public:
    explicit Notifications(HANDLE event) noexcept : _event(event) {}
    std::array<std::atomic<uint64_t>, 6> roleChanges{};
    static constexpr uint32_t NoDevice = UINT32_MAX;
    uint32_t Track(const DeviceId& id, uint64_t& revision) noexcept
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_deviceLock);
        for (uint32_t i = 0; i < _devices.size(); ++i)
        {
            if (_devices[i].id.length) continue;
            _devices[i].id = id;
            revision = ++_devices[i].revision;
            return i;
        }
        return NoDevice;
    }
    void Untrack(uint32_t index) noexcept
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_deviceLock);
        if (index < _devices.size()) _devices[index].id = {};
    }
    uint64_t DeviceRevision(uint32_t index) noexcept
    {
        const auto lock = wil::AcquireSRWLockShared(&_deviceLock);
        return index < _devices.size() ? _devices[index].revision.load() : UINT64_MAX;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD) noexcept override { return DeviceChanged(id); }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) noexcept override { return DeviceChanged(id); }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) noexcept override { return DeviceChanged(id); }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) noexcept override
    {
        if ((flow == eRender || flow == eCapture) && role >= eConsole && role <= eCommunications)
            ++roleChanges[(flow == eCapture ? 3U : 0U) + static_cast<uint32_t>(role)];
        return Changed();
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) noexcept override { return Changed(); }
  private:
    struct Binding final { DeviceId id; std::atomic<uint64_t> revision{0}; };
    std::array<Binding, MaximumOutputs + MaximumInputs> _devices;
    SRWLOCK _deviceLock = SRWLOCK_INIT;
    HANDLE _event;
    HRESULT DeviceChanged(LPCWSTR value) noexcept
    {
        DeviceId id;
        if (EncodeId(value, id))
        {
            const auto lock = wil::AcquireSRWLockShared(&_deviceLock);
            for (auto& device : _devices) if (device.id == id) ++device.revision;
        }
        return Changed();
    }
    HRESULT Changed() noexcept { (void)SetEvent(_event); return S_OK; }
};
class VolumeNotifications final : public RedXeComObject<VolumeNotifications, IAudioEndpointVolumeCallback>
{
  public:
    explicit VolumeNotifications(HANDLE event) noexcept : _event(event) {}
    AudioRevisionState revisions;
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA notification) noexcept override
    {
        if (!notification) return E_POINTER;
        (void)revisions.Observe(notification->fMasterVolume, notification->bMuted != FALSE);
        (void)SetEvent(_event);
        return S_OK;
    }
  private:
    HANDLE _event;
};
}
struct WindowsAudioBackend::State final
{
    wil::com_ptr_nothrow<IMMDeviceEnumerator> enumerator;
    wil::com_ptr_nothrow<Notifications> callbacks;
    struct Watch final
    {
        DeviceId id;
        wil::com_ptr_nothrow<IAudioEndpointVolume> volume;
        wil::com_ptr_nothrow<VolumeNotifications> callback;
        wil::com_ptr_nothrow<Notifications> notifications;
        uint32_t deviceToken = Notifications::NoDevice;
        uint64_t deviceRevision = 0;
        bool seen = false;
        uint64_t generation = 0;
        void Reset() noexcept
        {
            if (volume && callback) (void)volume->UnregisterControlChangeNotify(callback.get());
            if (notifications) notifications->Untrack(deviceToken);
            notifications.reset(); deviceToken = Notifications::NoDevice;
            volume.reset(); callback.reset(); id = {}; seen = false;
        }
        ~Watch() { Reset(); }
    };
    // Watch every admitted endpoint: profile targets and communications defaults need the same external-change
    // protection as console endpoints. Storage is fixed; unchanged membership never re-registers callbacks.
    std::array<Watch, MaximumOutputs + MaximumInputs> watched;
    HANDLE changedEvent = nullptr;
    std::array<uint64_t, 6> observedRoleChanges{};
    Inventory current;
    Inventory scratch;
    uint64_t generation = 0;
    bool notificationsRegistered = false;
    ~State()
    {
        for (auto& watch : watched) watch.Reset();
        if (enumerator && notificationsRegistered) (void)enumerator->UnregisterEndpointNotificationCallback(callbacks.get());
    }
    HRESULT WatchEndpoint(const DeviceId& id, IAudioEndpointVolume* volume, Watch*& output) noexcept
    {
        output = nullptr;
        for (auto& watch : watched)
            if (watch.id == id && watch.volume) { watch.seen = true; output = &watch; return S_OK; }
        for (auto& watch : watched)
        {
            if (watch.volume) continue;
            watch.deviceToken = callbacks->Track(id, watch.deviceRevision);
            if (watch.deviceToken == Notifications::NoDevice) return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
            watch.notifications = callbacks;
            watch.callback.attach(new (std::nothrow) VolumeNotifications(changedEvent));
            if (!watch.callback) { watch.Reset(); return E_OUTOFMEMORY; }
            const HRESULT result = volume->RegisterControlChangeNotify(watch.callback.get());
            if (FAILED(result)) { watch.Reset(); return result; }
            watch.volume = volume; watch.id = id; watch.seen = true; watch.generation = ++generation; output = &watch;
            return S_OK;
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    }
    HRESULT Resolve(const DeviceId& id, wil::com_ptr_nothrow<IMMDevice>& device,
                    wil::com_ptr_nothrow<IAudioEndpointVolume>& volume) noexcept
    {
        std::array<wchar_t, MaximumDeviceIdBytes + 1> wide{};
        if (!DecodeId(id, wide)) return E_INVALIDARG;
        RETURN_IF_FAILED(enumerator->GetDevice(wide.data(), device.put()));
        DWORD state = 0; RETURN_IF_FAILED(device->GetState(&state));
        if (state != DEVICE_STATE_ACTIVE) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        return device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, volume.put_void());
    }
    HRESULT Enumerate(EDataFlow flow, const InventoryPreferences& preferences) noexcept
    {
        auto& destination = flow == eRender ? scratch.outputs : scratch.inputs;
        auto& count = flow == eRender ? scratch.outputCount : scratch.inputCount;
        const auto& defaults = flow == eRender ? scratch.state.outputDefaults : scratch.state.inputDefaults;
        const auto& references = flow == eRender ? preferences.outputs : preferences.inputs;
        RankedInventory selection(destination, count);
        wil::com_ptr_nothrow<IMMDeviceCollection> collection;
        RETURN_IF_FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.put()));
        UINT available = 0; RETURN_IF_FAILED(collection->GetCount(&available));
        for (UINT i = 0; i < available; ++i)
        {
            wil::com_ptr_nothrow<IMMDevice> device;
            if (FAILED(collection->Item(i, device.put()))) continue;
            wil::unique_cotaskmem_string id;
            if (FAILED(device->GetId(id.put()))) continue;
            Endpoint endpoint;
            if (!EncodeId(id.get(), endpoint.id)) { scratch.truncated = true; continue; }
            selection.Offer(endpoint, InventoryRank(endpoint.id, defaults, references));
        }
        scratch.truncated = scratch.truncated || selection.Truncated();
        return S_OK;
    }
    void ReadEndpoint(Endpoint& endpoint) noexcept
    {
        wil::com_ptr_nothrow<IMMDevice> device;
        wil::com_ptr_nothrow<IAudioEndpointVolume> volume;
        HRESULT hr = Resolve(endpoint.id, device, volume);
        if (device)
        {
            wil::com_ptr_nothrow<IPropertyStore> properties;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.put())))
            {
                wil::unique_prop_variant name;
                if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR && name.pwszVal)
                {
                    size_t length = wcsnlen_s(name.pwszVal, endpoint.name.size() - 1);
                    if (length && name.pwszVal[length - 1] >= 0xd800 && name.pwszVal[length - 1] <= 0xdbff) --length;
                    std::copy_n(name.pwszVal, length, endpoint.name.data());
                }
            }
        }
        Watch* watch = nullptr;
        if (SUCCEEDED(hr)) hr = WatchEndpoint(endpoint.id, volume.get(), watch);
        if (watch) endpoint.generation = watch->generation;
        float scalar = 0; BOOL muted = FALSE;
        if (SUCCEEDED(hr)) hr = volume->GetMasterVolumeLevelScalar(&scalar);
        if (SUCCEEDED(hr)) hr = volume->GetMute(&muted);
        if (SUCCEEDED(hr) && (!std::isfinite(scalar) || scalar < 0 || scalar > 1)) hr = E_UNEXPECTED;
        endpoint.availability = SUCCEEDED(hr) ? Availability::Ready : Unavailable(hr);
        if (SUCCEEDED(hr))
        {
            endpoint.level = static_cast<uint32_t>(std::lround(scalar * 100)); endpoint.muted = muted != FALSE;
            const auto revisions = watch->callback->revisions.Observe(scalar, muted != FALSE);
            endpoint.levelRevision = revisions.level;
            endpoint.muteRevision = revisions.mute;
        }
    }
    HRESULT Refresh(const InventoryPreferences& preferences) noexcept
    {
        scratch = current;
        scratch.truncated = preferences.truncated != 0;
        std::array<uint64_t, 6> nextRoleChanges{};
        // Read every role before bounded admission; a late-enumerated communications endpoint remains usable.
        for (uint32_t flow = 0; flow < 2; ++flow)
        {
            auto& defaults = flow ? scratch.state.inputDefaults : scratch.state.outputDefaults;
            const auto& previous = flow ? current.state.inputDefaults : current.state.outputDefaults;
            auto& revisions = flow ? scratch.inputRoleRevisions : scratch.outputRoleRevisions;
            for (uint32_t role = 0; role < 3; ++role)
            {
                DeviceId id;
                wil::com_ptr_nothrow<IMMDevice> device;
                wil::unique_cotaskmem_string wide;
                if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow ? eCapture : eRender, static_cast<ERole>(role), device.put())) &&
                    SUCCEEDED(device->GetId(wide.put()))) (void)EncodeId(wide.get(), id);
                const size_t index = flow * 3U + role;
                const uint64_t notifications = callbacks->roleChanges[index].load();
                revisions[role] += notifications - observedRoleChanges[index];
                nextRoleChanges[index] = notifications;
                if (id != previous[role] || revisions[role] == 0) ++revisions[role];
                defaults[role] = id;
            }
        }
        RETURN_IF_FAILED(Enumerate(eRender, preferences)); RETURN_IF_FAILED(Enumerate(eCapture, preferences));
        // Membership can change while an old device remains active. Retire all evicted watches before admitting
        // replacements, so a full array cannot fail the new default/profile with a transient quota error.
        for (auto& watch : watched)
        {
            watch.seen = false;
            if (watch.volume && ((!Find(scratch, DeviceKind::Output, watch.id) && !Find(scratch, DeviceKind::Microphone, watch.id)) ||
                watch.notifications->DeviceRevision(watch.deviceToken) != watch.deviceRevision)) watch.Reset();
        }
        for (uint32_t i = 0; i < scratch.outputCount; ++i) ReadEndpoint(scratch.outputs[i]);
        for (uint32_t i = 0; i < scratch.inputCount; ++i) ReadEndpoint(scratch.inputs[i]);
        for (auto& watch : watched) if (!watch.seen) watch.Reset();
        for (uint32_t flow = 0; flow < 2; ++flow)
        {
            const auto& defaults = flow ? scratch.state.inputDefaults : scratch.state.outputDefaults;
            auto& selected = flow ? scratch.state.microphone : scratch.state.output;
            const auto* endpoint = Find(scratch, flow ? DeviceKind::Microphone : DeviceKind::Output, defaults[0]);
            if (endpoint) selected = *endpoint;
            else { selected = {}; selected.availability = Availability::Missing; }
        }
        ++scratch.state.revision;
        current = scratch;
        observedRoleChanges = nextRoleChanges;
        return S_OK;
    }
    HRESULT Mutate(const BrokerCommand& command) noexcept
    {
        if (command.operation == BrokerOperation::Observe) return S_OK;
        if (command.device != DeviceKind::Output && command.device != DeviceKind::Microphone) return E_NOTIMPL;
        const auto* endpoint = Find(current, command.device, command.id);
        if (!endpoint || endpoint->availability != Availability::Ready) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        if (endpoint->generation != command.expectedGeneration) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        wil::com_ptr_nothrow<IMMDevice> device;
        wil::com_ptr_nothrow<IAudioEndpointVolume> volume;
        if (command.operation == BrokerOperation::SetMute || command.operation == BrokerOperation::SetLevel)
        {
            const bool mute = command.operation == BrokerOperation::SetMute;
            if (command.value > (mute ? 1U : 100U)) return E_INVALIDARG;
            if (command.expectedRevision != (mute ? endpoint->muteRevision : endpoint->levelRevision))
                return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
            RETURN_IF_FAILED(Resolve(command.id, device, volume));
            for (auto& watch : watched)
            {
                if (watch.id != command.id || !watch.callback) continue;
                if (watch.notifications->DeviceRevision(watch.deviceToken) != watch.deviceRevision)
                    return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
                const auto latest = watch.callback->revisions.Current();
                if (command.expectedRevision != (mute ? latest.mute : latest.level))
                    return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
                break;
            }
            return mute ? volume->SetMute(command.value != 0, &OwnVolumeContext) :
                volume->SetMasterVolumeLevelScalar(command.value / 100.0f, &OwnVolumeContext);
        }
        if (command.operation == BrokerOperation::SetDefault)
        {
            if (!(current.capabilities & CapabilityAudioDefaults)) return E_NOTIMPL;
            if (command.role >= 3) return E_INVALIDARG;
            const auto& revisions = command.device == DeviceKind::Output ? current.outputRoleRevisions : current.inputRoleRevisions;
            if (command.expectedRevision != revisions[command.role]) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
            const size_t index = (command.device == DeviceKind::Microphone ? 3U : 0U) + command.role;
            if (callbacks->roleChanges[index].load() != observedRoleChanges[index]) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
            for (const auto& watch : watched)
                if (watch.id == command.id && watch.notifications && watch.notifications->DeviceRevision(watch.deviceToken) != watch.deviceRevision)
                    return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
            std::array<wchar_t, MaximumDeviceIdBytes + 1> wide{};
            if (!DecodeId(command.id, wide)) return E_INVALIDARG;
            return SetAudioDefault(wide.data(), static_cast<ERole>(command.role));
        }
        return E_NOTIMPL;
    }
};
WindowsAudioBackend::WindowsAudioBackend() : _state(std::make_unique<State>()) {}
WindowsAudioBackend::~WindowsAudioBackend() = default;
HRESULT WindowsAudioBackend::Initialize(HANDLE changedEvent) noexcept
{
    wil::com_ptr_nothrow<IMMDeviceEnumerator> enumerator;
    RETURN_IF_FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(enumerator.put())));
    RETURN_IF_FAILED(InitializeWithEnumerator(changedEvent, enumerator.get()));
    if (AudioPolicyAvailable()) _state->current.capabilities |= CapabilityAudioDefaults;
    return S_OK;
}
HRESULT WindowsAudioBackend::InitializeWithEnumerator(HANDLE changedEvent, IMMDeviceEnumerator* enumerator) noexcept
{
    if (!changedEvent || !enumerator) return E_INVALIDARG;
    if (_state->enumerator) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    _state->changedEvent = changedEvent;
    _state->enumerator = enumerator;
    _state->callbacks.attach(new (std::nothrow) Notifications(changedEvent));
    if (!_state->callbacks) return E_OUTOFMEMORY;
    RETURN_IF_FAILED(_state->enumerator->RegisterEndpointNotificationCallback(_state->callbacks.get()));
    _state->notificationsRegistered = true;
    _state->current.capabilities = CapabilityAudioLevels;
    _state->current.state.camera.availability = Availability::Unsupported;
    return S_OK;
}
HRESULT WindowsAudioBackend::Execute(const BrokerCommand& command, const InventoryPreferences& preferences, Inventory& inventory) noexcept
{
    if (!_state->enumerator) return E_UNEXPECTED;
    HRESULT result = _state->Refresh(preferences);
    bool observed = SUCCEEDED(result);
    if (SUCCEEDED(result)) result = _state->Mutate(command);
    if (command.operation != BrokerOperation::Observe)
    {
        const HRESULT refreshed = _state->Refresh(preferences);
        observed = SUCCEEDED(refreshed);
        if (SUCCEEDED(result) && FAILED(refreshed)) result = refreshed;
        if (SUCCEEDED(result) && command.operation == BrokerOperation::SetDefault)
        {
            const auto& defaults = command.device == DeviceKind::Output ? _state->current.state.outputDefaults : _state->current.state.inputDefaults;
            if (defaults[command.role] != command.id) result = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        }
        if (SUCCEEDED(result) && command.operation == BrokerOperation::SetMute)
        {
            const auto* endpoint = Find(_state->current, command.device, command.id);
            if (!endpoint || endpoint->muted != (command.value != 0)) result = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        }
    }
    inventory = _state->current;
    if (!observed)
    {
        inventory.state.output.availability = Availability::Unknown;
        inventory.state.microphone.availability = Availability::Unknown;
        for (uint32_t i = 0; i < inventory.outputCount; ++i) inventory.outputs[i].availability = Availability::Unknown;
        for (uint32_t i = 0; i < inventory.inputCount; ++i) inventory.inputs[i].availability = Availability::Unknown;
    }
    return result;
}
} // namespace AVControl
