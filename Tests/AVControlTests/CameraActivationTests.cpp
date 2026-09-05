#include "Camera/CameraActivation.h"
#include <cstring>
#include <filesystem>
#include <mfapi.h>
#include <mferror.h>
#include <stdexcept>
#include <wil/com.h>
#include <wil/resource.h>

namespace
{
uint32_t checks = 0;
void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class Function> Function Export(HMODULE module, PCSTR name)
{
    const auto address = GetProcAddress(module, name);
    Check(address != nullptr, "camera DLL exports the standard COM entrypoint");
    Function result{}; static_assert(sizeof(result) == sizeof(address)); std::memcpy(&result, &address, sizeof(result)); return result;
}
}
uint32_t RunCameraActivationTests()
{
    using namespace AVControl::Camera;
    checks = 0;
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "camera DLL fixture MTA");
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    Check(SUCCEEDED(MFStartup(MF_VERSION)), "camera DLL fixture MF");
    const auto mf = wil::scope_exit([] { (void)MFShutdown(); });
    wchar_t executable[MAX_PATH]{};
    Check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "camera DLL fixture executable path");
    const auto path = std::filesystem::path(executable).parent_path() / L"Plugins" / L"AVControlCamera.dll";
    wil::unique_hmodule module(LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32));
    Check(!!module, "camera DLL loads directly without installing a camera or COM registration");
    auto factoryEntry = Export<HRESULT (WINAPI*)(REFCLSID, REFIID, void**)>(module.get(), "DllGetClassObject");
    auto unloadEntry = Export<HRESULT (WINAPI*)()>(module.get(), "DllCanUnloadNow");
    Check(unloadEntry() == S_OK, "loading camera DLL creates no media object or background worker");
    void* invalid = reinterpret_cast<void*>(1);
    Check(factoryEntry(GUID_NULL, IID_IClassFactory, &invalid) == CLASS_E_CLASSNOTAVAILABLE && !invalid, "camera DLL rejects unrelated COM class IDs");
    wil::com_ptr_nothrow<IClassFactory> factory;
    Check(factoryEntry(CameraSourceClsid, IID_PPV_ARGS(factory.put())) == S_OK && unloadEntry() == S_FALSE, "class factory pins DLL lifetime");
    Check(factory->CreateInstance(factory.get(), __uuidof(IMFActivate), &invalid) == CLASS_E_NOAGGREGATION && !invalid,
        "camera COM activation rejects aggregation");
    Check(factory->LockServer(FALSE) == E_UNEXPECTED, "unbalanced server unlock cannot underflow lifetime");
    Check(factory->LockServer(TRUE) == S_OK, "camera COM server lock retained");
    wil::com_ptr_nothrow<IMFActivate> activation;
    Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(activation.put())) == S_OK, "factory exposes IMFActivate");
    wil::com_ptr_nothrow<IMFAttributes> attributes;
    wil::com_ptr_nothrow<IUnknown> identity, attributeIdentity;
    Check(SUCCEEDED(activation.query_to(attributes.put())) && SUCCEEDED(activation.query_to(identity.put())) &&
        SUCCEEDED(attributes.query_to(attributeIdentity.put())) && identity == attributeIdentity, "activation and attributes share controlling COM identity");
    identity.reset(); attributeIdentity.reset();
    wil::com_ptr_nothrow<IMFMediaSource> source;
    Check(FAILED(activation->ActivateObject(IID_PPV_ARGS(source.put()))) && !source, "missing owner does not activate an unbound route");
    Check(attributes->SetString(CameraOwnerSidAttribute, L"invalid sid") == S_OK && FAILED(activation->ActivateObject(IID_PPV_ARGS(source.put()))),
        "activation validates owner identity before source creation");
    // Source construction only: no Start, Global channel, device registration, helper or physical capture.
    Check(attributes->SetString(CameraOwnerSidAttribute, L"S-1-5-21-1-2-3-1001") == S_OK &&
        activation->ActivateObject(IID_PPV_ARGS(source.put())) == S_OK, "valid activation constructs metadata lazily");
    const auto cleanupSource = wil::scope_exit([&] { if (source) (void)source->Shutdown(); });
    wil::com_ptr_nothrow<IMFMediaSource> same;
    Check(activation->ActivateObject(IID_PPV_ARGS(same.put())) == S_OK && source == same, "repeated activation returns the same live object");
    same.reset();
    wil::com_ptr_nothrow<IMFPresentationDescriptor> presentation;
    Check(source->CreatePresentationDescriptor(presentation.put()) == S_OK, "activated DLL exposes real media metadata"); presentation.reset();
    Check(activation->DetachObject() == S_OK && source->CreatePresentationDescriptor(presentation.put()) == S_OK,
        "detach transfers shutdown responsibility without stopping the caller's source"); presentation.reset();
    Check(activation->ShutdownObject() == S_OK, "empty activation shutdown is idempotent");
    attributes.reset(); activation.reset();
    Check(factory->LockServer(FALSE) == S_OK, "camera COM server lock drains"); factory.reset();
    Check(unloadEntry() == S_FALSE, "detached source alone still pins camera DLL");
    Check(source->Shutdown() == S_OK && source->Shutdown() == S_OK, "detached source has idempotent explicit shutdown"); source.reset();
    Check(unloadEntry() == S_OK, "all detached source/stream references drain before DLL unload");

    Check(factoryEntry(CameraSourceClsid, IID_PPV_ARGS(factory.put())) == S_OK && factory->CreateInstance(nullptr, IID_PPV_ARGS(activation.put())) == S_OK,
        "camera activation is reusable");
    Check(activation->SetString(CameraOwnerSidAttribute, L"S-1-5-21-1-2-3-1001") == S_OK && activation->ActivateObject(IID_PPV_ARGS(source.put())) == S_OK,
        "second metadata activation");
    Check(activation->ShutdownObject() == S_OK && source->CreatePresentationDescriptor(presentation.put()) == MF_E_SHUTDOWN,
        "activation shutdown invalidates external source references");
    source.reset(); activation.reset(); factory.reset();
    Check(unloadEntry() == S_OK, "activation-owned shutdown drains source cycles");
    return checks;
}
