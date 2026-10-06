#include "AVControlProtocolValidation.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "WindowsAudioBackend.h"
#include <algorithm>
#include <cstring>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <stdexcept>
#include <string>
#include <wil/com.h>
#include <wil/resource.h>

namespace
{
uint32_t checks = 0;
void Check(bool value, const char* message)
{
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
struct SubscriptionCounts final
{
    uint32_t active = 0, peak = 0, registrations = 0;
};

// Real backend code consumes these explicit COM fixtures. No MMDeviceEnumerator or policy object is activated.
class Volume final : public RedXeComObject<Volume, IAudioEndpointVolume>
{
  public:
    explicit Volume(SubscriptionCounts& counts) : _counts(counts) {}
    wil::com_ptr_nothrow<IAudioEndpointVolumeCallback> callback;
    float scalar = .5f;
    BOOL muted = TRUE;
    HRESULT readResult = S_OK;
    uint32_t writes = 0;
    void Notify() noexcept
    {
        if (!callback)
            return;
        AUDIO_VOLUME_NOTIFICATION_DATA data{};
        data.fMasterVolume = scalar;
        data.bMuted = muted;
        (void)callback->OnNotify(&data);
    }
    HRESULT STDMETHODCALLTYPE RegisterControlChangeNotify(IAudioEndpointVolumeCallback* value) noexcept override
    {
        if (!value)
            return E_POINTER;
        if (callback)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        callback = value;
        ++_counts.active;
        ++_counts.registrations;
        _counts.peak = (std::max)(_counts.peak, _counts.active);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnregisterControlChangeNotify(IAudioEndpointVolumeCallback* value) noexcept override
    {
        if (callback.get() != value)
            return E_INVALIDARG;
        callback.reset();
        --_counts.active;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetChannelCount(UINT* count) noexcept override
    {
        if (!count)
            return E_POINTER;
        *count = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetMasterVolumeLevel(float, LPCGUID) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetMasterVolumeLevelScalar(float value, LPCGUID) noexcept override
    {
        scalar = value;
        ++writes;
        Notify();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMasterVolumeLevel(float*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetMasterVolumeLevelScalar(float* value) noexcept override
    {
        if (!value)
            return E_POINTER;
        *value = scalar;
        return readResult;
    }
    HRESULT STDMETHODCALLTYPE SetChannelVolumeLevel(UINT, float, LPCGUID) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetChannelVolumeLevelScalar(UINT, float, LPCGUID) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetChannelVolumeLevel(UINT, float*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetChannelVolumeLevelScalar(UINT, float*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetMute(BOOL value, LPCGUID) noexcept override
    {
        muted = value;
        ++writes;
        Notify();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMute(BOOL* value) noexcept override
    {
        if (!value)
            return E_POINTER;
        *value = muted;
        return readResult;
    }
    HRESULT STDMETHODCALLTYPE GetVolumeStepInfo(UINT*, UINT*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE VolumeStepUp(LPCGUID) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE VolumeStepDown(LPCGUID) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD*) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetVolumeRange(float*, float*, float*) noexcept override
    {
        return E_NOTIMPL;
    }

  private:
    SubscriptionCounts& _counts;
};
class Device final : public RedXeComObject<Device, IMMDevice>
{
  public:
    Device(SubscriptionCounts& counts, uint32_t flow, uint32_t index)
        : id(L"injected-" + std::to_wstring(flow) + L"-" + std::to_wstring(index))
    {
        volume.attach(new Volume(counts));
    }
    std::wstring id;
    wil::com_ptr_nothrow<Volume> volume;
    DWORD state = DEVICE_STATE_ACTIVE;
    HRESULT STDMETHODCALLTYPE Activate(REFIID iid, DWORD, PROPVARIANT*, void** object) noexcept override
    {
        return volume->QueryInterface(iid, object);
    }
    HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD, IPropertyStore** result) noexcept override
    {
        if (result)
            *result = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetId(LPWSTR* result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        wil::unique_cotaskmem_string copy(static_cast<PWSTR>(CoTaskMemAlloc((id.size() + 1) * sizeof(wchar_t))));
        if (!copy)
            return E_OUTOFMEMORY;
        std::copy_n(id.c_str(), id.size() + 1, copy.get());
        *result = copy.release();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetState(DWORD* value) noexcept override
    {
        if (!value)
            return E_POINTER;
        *value = state;
        return S_OK;
    }
};
class Collection final : public RedXeComObject<Collection, IMMDeviceCollection>
{
  public:
    std::array<wil::com_ptr_nothrow<Device>, 40> devices;
    uint32_t count = 0;
    HRESULT STDMETHODCALLTYPE GetCount(UINT* value) noexcept override
    {
        if (!value)
            return E_POINTER;
        *value = count;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Item(UINT index, IMMDevice** value) noexcept override
    {
        if (!value)
            return E_POINTER;
        *value = nullptr;
        return index < count ? devices[index]->QueryInterface(IID_PPV_ARGS(value)) : E_INVALIDARG;
    }
};
class Enumerator final : public RedXeComObject<Enumerator, IMMDeviceEnumerator>
{
  public:
    SubscriptionCounts subscriptions;
    std::array<std::array<wil::com_ptr_nothrow<Device>, 40>, 2> devices;
    std::array<std::array<uint32_t, 3>, 2> defaults{{{39, 38, 37}, {39, 38, 37}}};
    wil::com_ptr_nothrow<IMMNotificationClient> callback;
    HRESULT enumerationResult = S_OK;
    Enumerator()
    {
        for (uint32_t flow = 0; flow < 2; ++flow)
            for (uint32_t i = 0; i < 40; ++i)
                devices[flow][i].attach(new Device(subscriptions, flow, i));
    }
    HRESULT STDMETHODCALLTYPE EnumAudioEndpoints(EDataFlow flow, DWORD state,
                                                 IMMDeviceCollection** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (FAILED(enumerationResult))
            return enumerationResult;
        if ((flow != eRender && flow != eCapture) || state != DEVICE_STATE_ACTIVE)
            return E_INVALIDARG;
        wil::com_ptr_nothrow<Collection> collection;
        collection.attach(new (std::nothrow) Collection);
        if (!collection)
            return E_OUTOFMEMORY;
        for (auto& device : devices[flow == eCapture ? 1 : 0])
            if (device->state == DEVICE_STATE_ACTIVE)
                collection->devices[collection->count++] = device;
        *result = collection.detach();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDefaultAudioEndpoint(EDataFlow flow, ERole role, IMMDevice** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if ((flow != eRender && flow != eCapture) || role < eConsole || role > eCommunications)
            return E_INVALIDARG;
        const uint32_t lane = flow == eCapture ? 1U : 0U;
        return devices[lane][defaults[lane][role]]->QueryInterface(IID_PPV_ARGS(result));
    }
    HRESULT STDMETHODCALLTYPE GetDevice(LPCWSTR id, IMMDevice** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (!id)
            return E_INVALIDARG;
        for (auto& flow : devices)
            for (auto& device : flow)
                if (device->id == id)
                    return device->QueryInterface(IID_PPV_ARGS(result));
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    HRESULT STDMETHODCALLTYPE RegisterEndpointNotificationCallback(IMMNotificationClient* value) noexcept override
    {
        if (!value)
            return E_POINTER;
        if (callback)
            return E_UNEXPECTED;
        callback = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnregisterEndpointNotificationCallback(IMMNotificationClient* value) noexcept override
    {
        if (callback.get() != value)
            return E_INVALIDARG;
        callback.reset();
        return S_OK;
    }
    void Default(uint32_t flow, uint32_t role, uint32_t index) noexcept
    {
        defaults[flow][role] = index;
        if (callback)
            (void)callback->OnDefaultDeviceChanged(flow ? eCapture : eRender, static_cast<ERole>(role),
                                                   devices[flow][index]->id.c_str());
    }
    AVControl::DeviceId Id(uint32_t flow, uint32_t index) const
    {
        AVControl::DeviceId result;
        Check(result.Assign("injected-" + std::to_string(flow) + "-" + std::to_string(index)), "injected device ID");
        return result;
    }
};
const AVControl::Endpoint* Find(const AVControl::Inventory& inventory, uint32_t flow, const AVControl::DeviceId& id)
{
    const auto& rows = flow ? inventory.inputs : inventory.outputs;
    const uint32_t count = flow ? inventory.inputCount : inventory.outputCount;
    for (uint32_t i = 0; i < count; ++i)
        if (rows[i].id == id)
            return &rows[i];
    return nullptr;
}
} // namespace

uint32_t RunAudioBackendTests()
{
    using namespace AVControl;
    checks = 0;
    auto backend = std::make_unique<WindowsAudioBackend>();
    wil::com_ptr_nothrow<Enumerator> catalog;
    catalog.attach(new Enumerator);
    wil::unique_event changed(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    Check(changed && backend->InitializeWithEnumerator(changed.get(), catalog.get()) == S_OK,
          "audio backend initializes with injected interfaces only");
    Check(backend->InitializeWithEnumerator(changed.get(), catalog.get()) ==
              HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED),
          "duplicate initialization cannot leak notifications");
    auto preferences = std::make_unique<InventoryPreferences>();
    (void)preferences->outputs.Add(catalog->Id(0, 35));
    (void)preferences->inputs.Add(catalog->Id(1, 36));
    auto inventory = std::make_unique<Inventory>();
    Check(backend->Execute({}, *preferences, *inventory) == S_OK && ValidInventoryPayload(*inventory),
          "real backend publishes valid injected inventory");
    Check(inventory->truncated && inventory->outputCount == 32 && inventory->inputCount == 32,
          "real backend enforces both inventory limits");
    Check(catalog->subscriptions.active == 64 && catalog->subscriptions.peak == 64,
          "all admitted endpoints receive exactly one bounded subscription");
    for (uint32_t flow = 0; flow < 2; ++flow)
        for (uint32_t role = 0; role < 3; ++role)
            Check(Find(*inventory, flow, catalog->Id(flow, 39 - role)) != nullptr,
                  "each late default survives actual backend enumeration");
    Check(Find(*inventory, 0, catalog->Id(0, 35)) && Find(*inventory, 1, catalog->Id(1, 36)),
          "saved targets survive actual enumeration");
    const auto registered = catalog->subscriptions.registrations;
    Check(backend->Execute({}, *preferences, *inventory) == S_OK && catalog->subscriptions.registrations == registered,
          "unchanged inventory does not re-register endpoint callbacks");
    const auto oldDefault = inventory->state.output;
    catalog->Default(0, 0, 34);
    Check(backend->Execute({}, *preferences, *inventory) == S_OK && inventory->state.output.id == catalog->Id(0, 34) &&
              inventory->state.output.availability == Availability::Ready,
          "late new default replaces a still-active old default without quota failure");
    Check(!catalog->devices[0][39]->volume->callback && catalog->devices[0][34]->volume->callback &&
              catalog->subscriptions.peak == 64,
          "backend retires evicted subscriptions before opening replacements");
    catalog->Default(0, 0, 39);
    Check(backend->Execute({}, *preferences, *inventory) == S_OK &&
              inventory->state.output.generation != oldDefault.generation,
          "re-admitted endpoint has a new generation so old gestures cannot follow it");
    auto& volume = *catalog->devices[0][39]->volume;
    const auto endpoint = inventory->state.output;
    BrokerCommand level{BrokerOperation::SetLevel, DeviceKind::Output,    endpoint.id, 61, 0,
                        endpoint.generation,       endpoint.levelRevision};
    volume.scalar = .503f;
    volume.Notify();
    volume.scalar = .5f;
    volume.Notify();
    Check(backend->Execute(level, *preferences, *inventory) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH) &&
              volume.writes == 0,
          "change-back callbacks invalidate old field revisions in the production backend");
    level.expectedRevision = inventory->state.output.levelRevision;
    Check(backend->Execute(level, *preferences, *inventory) == S_OK && inventory->state.output.level == 61 &&
              inventory->state.output.muted,
          "successful volume readback preserves the existing mute");
    const auto current = inventory->state.output;
    BrokerCommand policy{BrokerOperation::SetDefault,      DeviceKind::Output, current.id, 0, 0, current.generation,
                         inventory->outputRoleRevisions[0]};
    Check(backend->Execute(policy, *preferences, *inventory) == E_NOTIMPL,
          "injected backend never reaches the machine's default-policy setter");
    catalog->enumerationResult = E_FAIL;
    Check(backend->Execute({}, *preferences, *inventory) == E_FAIL &&
              inventory->state.output.availability == Availability::Unknown &&
              inventory->state.microphone.availability == Availability::Unknown,
          "failed refresh never leaves stale Ready live controls");
    for (uint32_t i = 0; i < inventory->outputCount; ++i)
        Check(inventory->outputs[i].availability == Availability::Unknown,
              "failed inventory disables stale selector entries");
    catalog->enumerationResult = S_OK;
    Check(backend->Execute({}, *preferences, *inventory) == S_OK &&
              inventory->state.output.availability == Availability::Ready,
          "fresh successful observation restores confirmed availability");
    volume.readResult = E_ACCESSDENIED;
    Check(backend->Execute({}, *preferences, *inventory) == S_OK &&
              inventory->state.output.availability == Availability::AccessDenied,
          "per-endpoint read errors remain local and distinct from enumeration failure");
    volume.readResult = S_OK;
    const auto priorRoleRevision = inventory->outputRoleRevisions[0];
    catalog->Default(0, 0, 38);
    catalog->Default(0, 0, 39);
    catalog->enumerationResult = E_FAIL;
    Check(backend->Execute({}, *preferences, *inventory) == E_FAIL,
          "role change-back followed by a failed observation");
    catalog->enumerationResult = S_OK;
    Check(backend->Execute({}, *preferences, *inventory) == S_OK &&
              inventory->outputRoleRevisions[0] == priorRoleRevision + 2,
          "failed refresh cannot consume and lose default-role callback revisions");
    const auto beforeReplacement = inventory->state.output;
    auto oldVolume = catalog->devices[0][39]->volume;
    catalog->devices[0][39]->volume.attach(new Volume(catalog->subscriptions));
    (void)catalog->callback->OnDeviceRemoved(catalog->devices[0][39]->id.c_str());
    (void)catalog->callback->OnDeviceAdded(catalog->devices[0][39]->id.c_str());
    level.expectedGeneration = beforeReplacement.generation;
    level.expectedRevision = beforeReplacement.levelRevision;
    Check(backend->Execute(level, *preferences, *inventory) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH) &&
              inventory->state.output.generation != beforeReplacement.generation &&
              catalog->devices[0][39]->volume->writes == 0,
          "rapid remove/re-add with the same ID rejects intents for the old device incarnation");
    Check(!oldVolume->callback && catalog->devices[0][39]->volume->callback && catalog->subscriptions.peak == 64,
          "replacement retains the subscription bound and stops observing the dead volume interface");
    const auto selectedGeneration = inventory->state.output.generation;
    const auto priorRegistrations = catalog->subscriptions.registrations;
    (void)catalog->callback->OnDeviceAdded(L"unrelated-untracked-endpoint");
    Check(backend->Execute({}, *preferences, *inventory) == S_OK &&
              inventory->state.output.generation == selectedGeneration &&
              catalog->subscriptions.registrations == priorRegistrations,
          "unrelated hotplug does not rebuild unchanged endpoint subscriptions");
    backend.reset();
    Check(catalog->subscriptions.active == 0 && !catalog->callback,
          "backend teardown unregisters every retained device callback");
    return checks;
}
