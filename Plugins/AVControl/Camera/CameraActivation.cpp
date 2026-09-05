#include "CameraActivation.h"
#include "CameraBridge.h"
#include "../../../Common/PlugInterfaces/FactoryImpl.h"
#include <atomic>
#include <mfapi.h>
#include <wil/com.h>
#include <wil/result.h>

namespace AVControl::Camera
{
namespace
{
std::atomic<uint32_t> activationObjects{0}, serverLocks{0};
struct Counted
{
    Counted() noexcept { ++activationObjects; }
    ~Counted() { --activationObjects; }
};
class Activation final : public RedXeComObject<Activation, IMFActivate>, private Counted
{
  public:
    HRESULT Initialize() noexcept { return MFCreateAttributes(_attributes.put(), 1); }
    ~Activation() { (void)ShutdownObject(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) noexcept override
    { return RedXeComObject::QueryInterface(iid == __uuidof(IMFAttributes) ? __uuidof(IMFActivate) : iid, result); }
    HRESULT STDMETHODCALLTYPE ActivateObject(REFIID iid, void** result) noexcept override
    {
        if (!result) return E_POINTER; *result = nullptr;
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        if (!_source)
        {
            std::array<wchar_t, 192> owner{};
            RETURN_IF_FAILED(_attributes->GetString(CameraOwnerSidAttribute, owner.data(), static_cast<UINT32>(owner.size()), nullptr));
            BridgeIdentity identity;
            RETURN_IF_FAILED(MakeBridgeIdentity(owner.data(), identity));
            try
            {
                auto provider = std::make_shared<BridgeFrameProvider>(std::move(identity));
                RETURN_IF_FAILED(CreateMediaSource(std::move(provider), _source.put()));
            }
            catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        }
        return _source->QueryInterface(iid, result);
    }
    HRESULT STDMETHODCALLTYPE ShutdownObject() noexcept override
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        const HRESULT result = _source ? _source->Shutdown() : S_OK;
        _source.reset(); return result;
    }
    HRESULT STDMETHODCALLTYPE DetachObject() noexcept override
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        // A detached source belongs to its consumer, which must call IMFMediaSource::Shutdown.
        _source.reset(); return S_OK;
    }
    // IMFAttributes owns its standard thread-safe store and allocation conventions. Keep that identity behind
    // the activation object's controlling IUnknown, rather than returning the aggregated store from QI.
    HRESULT STDMETHODCALLTYPE GetItem(REFGUID key, PROPVARIANT* value) noexcept override
    { return _attributes->GetItem(key, value); }
    HRESULT STDMETHODCALLTYPE GetItemType(REFGUID key, MF_ATTRIBUTE_TYPE* type) noexcept override
    { return _attributes->GetItemType(key, type); }
    HRESULT STDMETHODCALLTYPE CompareItem(REFGUID key, REFPROPVARIANT value, BOOL* equal) noexcept override
    { return _attributes->CompareItem(key, value, equal); }
    HRESULT STDMETHODCALLTYPE Compare(IMFAttributes* other, MF_ATTRIBUTES_MATCH_TYPE match, BOOL* equal) noexcept override
    { return _attributes->Compare(other, match, equal); }
    HRESULT STDMETHODCALLTYPE GetUINT32(REFGUID key, UINT32* value) noexcept override
    { return _attributes->GetUINT32(key, value); }
    HRESULT STDMETHODCALLTYPE GetUINT64(REFGUID key, UINT64* value) noexcept override
    { return _attributes->GetUINT64(key, value); }
    HRESULT STDMETHODCALLTYPE GetDouble(REFGUID key, double* value) noexcept override
    { return _attributes->GetDouble(key, value); }
    HRESULT STDMETHODCALLTYPE GetGUID(REFGUID key, GUID* value) noexcept override
    { return _attributes->GetGUID(key, value); }
    HRESULT STDMETHODCALLTYPE GetStringLength(REFGUID key, UINT32* length) noexcept override
    { return _attributes->GetStringLength(key, length); }
    HRESULT STDMETHODCALLTYPE GetString(REFGUID key, LPWSTR value, UINT32 capacity, UINT32* length) noexcept override
    { return _attributes->GetString(key, value, capacity, length); }
    HRESULT STDMETHODCALLTYPE GetAllocatedString(REFGUID key, LPWSTR* value, UINT32* length) noexcept override
    { return _attributes->GetAllocatedString(key, value, length); }
    HRESULT STDMETHODCALLTYPE GetBlobSize(REFGUID key, UINT32* length) noexcept override
    { return _attributes->GetBlobSize(key, length); }
    HRESULT STDMETHODCALLTYPE GetBlob(REFGUID key, UINT8* value, UINT32 capacity, UINT32* length) noexcept override
    { return _attributes->GetBlob(key, value, capacity, length); }
    HRESULT STDMETHODCALLTYPE GetAllocatedBlob(REFGUID key, UINT8** value, UINT32* length) noexcept override
    { return _attributes->GetAllocatedBlob(key, value, length); }
    HRESULT STDMETHODCALLTYPE GetUnknown(REFGUID key, REFIID iid, void** value) noexcept override
    { return _attributes->GetUnknown(key, iid, value); }
    HRESULT STDMETHODCALLTYPE SetItem(REFGUID key, REFPROPVARIANT value) noexcept override
    { return _attributes->SetItem(key, value); }
    HRESULT STDMETHODCALLTYPE DeleteItem(REFGUID key) noexcept override
    { return _attributes->DeleteItem(key); }
    HRESULT STDMETHODCALLTYPE DeleteAllItems() noexcept override
    { return _attributes->DeleteAllItems(); }
    HRESULT STDMETHODCALLTYPE SetUINT32(REFGUID key, UINT32 value) noexcept override
    { return _attributes->SetUINT32(key, value); }
    HRESULT STDMETHODCALLTYPE SetUINT64(REFGUID key, UINT64 value) noexcept override
    { return _attributes->SetUINT64(key, value); }
    HRESULT STDMETHODCALLTYPE SetDouble(REFGUID key, double value) noexcept override
    { return _attributes->SetDouble(key, value); }
    HRESULT STDMETHODCALLTYPE SetGUID(REFGUID key, REFGUID value) noexcept override
    { return _attributes->SetGUID(key, value); }
    HRESULT STDMETHODCALLTYPE SetString(REFGUID key, LPCWSTR value) noexcept override
    { return _attributes->SetString(key, value); }
    HRESULT STDMETHODCALLTYPE SetBlob(REFGUID key, const UINT8* value, UINT32 length) noexcept override
    { return _attributes->SetBlob(key, value, length); }
    HRESULT STDMETHODCALLTYPE SetUnknown(REFGUID key, IUnknown* value) noexcept override
    { return _attributes->SetUnknown(key, value); }
    HRESULT STDMETHODCALLTYPE LockStore() noexcept override
    { return _attributes->LockStore(); }
    HRESULT STDMETHODCALLTYPE UnlockStore() noexcept override
    { return _attributes->UnlockStore(); }
    HRESULT STDMETHODCALLTYPE GetCount(UINT32* count) noexcept override
    { return _attributes->GetCount(count); }
    HRESULT STDMETHODCALLTYPE GetItemByIndex(UINT32 index, GUID* key, PROPVARIANT* value) noexcept override
    { return _attributes->GetItemByIndex(index, key, value); }
    HRESULT STDMETHODCALLTYPE CopyAllItems(IMFAttributes* destination) noexcept override
    { return _attributes->CopyAllItems(destination); }
  private:
    SRWLOCK _lock = SRWLOCK_INIT;
    wil::com_ptr_nothrow<IMFAttributes> _attributes;
    wil::com_ptr_nothrow<IMFMediaSource> _source;
};
class Factory final : public RedXeComObject<Factory, IClassFactory>, private Counted
{
  public:
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** result) noexcept override
    {
        if (!result) return E_POINTER; *result = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        wil::com_ptr_nothrow<Activation> activation;
        activation.attach(new (std::nothrow) Activation());
        if (!activation) return E_OUTOFMEMORY;
        RETURN_IF_FAILED(activation->Initialize());
        return activation->QueryInterface(iid, result);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL locked) noexcept override
    {
        if (locked) { ++serverLocks; return S_OK; }
        auto count = serverLocks.load();
        while (count) if (serverLocks.compare_exchange_weak(count, count - 1)) return S_OK;
        return E_UNEXPECTED;
    }
};
}
HRESULT CreateCameraClassFactory(REFIID iid, void** result) noexcept
{
    if (!result) return E_POINTER; *result = nullptr;
    wil::com_ptr_nothrow<Factory> factory;
    factory.attach(new (std::nothrow) Factory());
    return factory ? factory->QueryInterface(iid, result) : E_OUTOFMEMORY;
}
HRESULT CanUnloadCameraDll() noexcept
{ return activationObjects == 0 && serverLocks == 0 && ActiveMediaObjects() == 0 ? S_OK : S_FALSE; }
} // namespace AVControl::Camera
