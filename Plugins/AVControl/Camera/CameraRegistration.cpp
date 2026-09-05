#include <sdkddkver.h>
#include "CameraRegistration.h"
#include "CameraActivation.h"
#include <array>
#include <cstring>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <sddl.h>
#include <shlobj.h>
#include <string>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

#pragma comment(lib, "shell32.lib")

namespace AVControl::Camera
{
namespace
{
constexpr wchar_t FriendlyName[] = L"RedXe Camera";
constexpr wchar_t SourceKey[] = L"SOFTWARE\\Classes\\CLSID\\{10F8F1A2-4A82-4D82-9C0E-E253B151BDCB}\\InprocServer32";
HRESULT CurrentOwner(std::wstring& result) noexcept
{
    wil::unique_handle token;
    RETURN_IF_WIN32_BOOL_FALSE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()));
    alignas(TOKEN_USER) std::array<BYTE, sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE> bytes{};
    DWORD needed = 0;
    RETURN_IF_WIN32_BOOL_FALSE(GetTokenInformation(token.get(), TokenUser, bytes.data(), static_cast<DWORD>(bytes.size()), &needed));
    wil::unique_hlocal_string sid;
    RETURN_IF_WIN32_BOOL_FALSE(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, sid.put()));
    try { result = sid.get(); return S_OK; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
}
}
struct VirtualCameraRoute::State final
{
    wil::unique_hmodule module;
    wil::com_ptr_nothrow<IMFVirtualCamera> camera;
    std::wstring owner, symbolicLink;
    HRESULT Create() noexcept
    {
        RETURN_IF_FAILED(CurrentOwner(owner));
        module.reset(LoadLibraryExW(L"mfsensorgroup.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
        if (!module) return E_NOTIMPL; // Windows 10 remains loadable; the Windows 11 export is optional.
        const auto address = GetProcAddress(module.get(), "MFCreateVirtualCamera");
        if (!address) return E_NOTIMPL;
        decltype(&MFCreateVirtualCamera) create{};
        static_assert(sizeof(create) == sizeof(address)); std::memcpy(&create, &address, sizeof(create));
        // These arguments are the Windows identity key. Keep them stable across helper restarts and versions.
        // Access remains CurrentUser; no global default-camera switch or physical-device disabling is attempted.
        RETURN_IF_FAILED(create(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_System,
            MFVirtualCameraAccess_CurrentUser, FriendlyName, CameraSourceClsidString, nullptr, 0, camera.put()));
        return S_OK;
    }
};
VirtualCameraRoute::VirtualCameraRoute() : _state(std::make_unique<State>()) {}
VirtualCameraRoute::~VirtualCameraRoute() { Close(); }
HRESULT VirtualCameraRoute::ProbeInstalledSource() noexcept
{
    // Only the machine installer owns this key. Mapped development-drive locations are not valid source
    // installations for Local Service in Session 0. Do not load the source into this helper to test registration.
    std::array<wchar_t, 32768> registered{};
    DWORD bytes = static_cast<DWORD>(sizeof(registered));
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE, SourceKey, nullptr, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
        nullptr, registered.data(), &bytes);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    wil::unique_cotaskmem_string programFiles;
    RETURN_IF_FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, programFiles.put()));
    try
    {
        const std::wstring expected = std::wstring(programFiles.get()) + L"\\RedSalamanders\\RedXe Camera\\AVControlCamera.dll";
        if (_wcsicmp(expected.c_str(), registered.data()) != 0) return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
        const DWORD attributes = GetFileAttributesW(expected.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        return S_OK;
    }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
}
HRESULT VirtualCameraRoute::Open() noexcept
{
    if (_state->camera && !_state->symbolicLink.empty()) return S_OK;
    Close();
    RETURN_IF_FAILED(ProbeInstalledSource());
    auto cleanup = wil::scope_exit([&] { Close(); });
    RETURN_IF_FAILED(_state->Create());
    RETURN_IF_FAILED(_state->camera->SetString(CameraOwnerSidAttribute, _state->owner.c_str()));
    RETURN_IF_FAILED(_state->camera->Start(nullptr));
    wil::unique_cotaskmem_string symbolic;
    UINT32 length = 0;
    RETURN_IF_FAILED(_state->camera->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, symbolic.put(), &length));
    if (!length || length > 1024) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    try { _state->symbolicLink.assign(symbolic.get(), length); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    cleanup.release(); return S_OK;
}
HRESULT VirtualCameraRoute::Remove() noexcept
{
    if (!_state->camera) RETURN_IF_FAILED(_state->Create());
    const HRESULT result = _state->camera->Remove();
    Close(); return result;
}
void VirtualCameraRoute::Close() noexcept
{
    if (_state->camera) (void)_state->camera->Shutdown();
    _state->camera.reset(); _state->module.reset(); _state->owner.clear(); _state->symbolicLink.clear();
    // System lifetime deliberately keeps the registered route enumerable. A consumer with no helper produces
    // neutral frames; an ordinary helper/process exit must not invalidate an application's selected camera.
}
std::wstring_view VirtualCameraRoute::SymbolicLink() const noexcept { return _state->symbolicLink; }
std::wstring_view VirtualCameraRoute::OwnerSid() const noexcept { return _state->owner; }
} // namespace AVControl::Camera
