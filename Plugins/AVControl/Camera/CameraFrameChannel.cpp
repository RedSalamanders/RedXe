#include "CameraFrameChannel.h"
#include <cstring>
#include <sddl.h>
#include <string>
#include <type_traits>
#include <wil/result.h>

#pragma comment(lib, "advapi32.lib")

namespace AVControl::Camera
{
namespace
{
constexpr uint32_t Magic = 0x43465852;
constexpr uint32_t Protocol = 1;
constexpr size_t ImageBytes = static_cast<size_t>(FrameWidth) * FrameHeight * 3 / 2;
constexpr LONGLONG MaximumFrameAge = 2'500'000; // 250 ms; a dead producer can never repeat a stale physical frame forever.
constexpr DWORD LockDeadlineMs = 100;
bool ValidName(std::wstring_view value, bool mutex, bool allowLocal) noexcept
{
    const std::wstring_view globalPrefix = mutex ? L"Global\\RedXe.Camera.Lock." : L"Global\\RedXe.Camera.Frame.";
    const std::wstring_view localPrefix = mutex ? L"Local\\RedXe.Camera.Lock." : L"Local\\RedXe.Camera.Frame.";
    if (value.starts_with(globalPrefix)) value.remove_prefix(globalPrefix.size());
    else if (allowLocal && value.starts_with(localPrefix)) value.remove_prefix(localPrefix.size());
    else return false;
    if (value.size() != 38 || value.front() != L'{' || value.back() != L'}') return false;
    for (size_t i = 1; i < 37; ++i)
    {
        const wchar_t c = value[i];
        if (i == 9 || i == 14 || i == 19 || i == 24) { if (c != L'-') return false; }
        else if (!((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F') || (c >= L'a' && c <= L'f'))) return false;
    }
    return true;
}
}
struct FrameChannel::Shared final
{
    uint32_t magic = Magic, version = Protocol, size = sizeof(Shared), width = FrameWidth, height = FrameHeight;
    uint32_t imageBytes = static_cast<uint32_t>(ImageBytes);
    uint64_t revision = 1;
    LONGLONG timestamp = 0;
    uint32_t enabled = 0, ready = 0;
    std::array<BYTE, ImageBytes> image{};
};
FrameChannel::~FrameChannel() { Close(); }
void FrameChannel::Close() noexcept
{
    _readLease.reset(); _view.reset(); _mutex.reset(); _mapping.reset(); _names = {};
}
bool FrameChannel::Valid() const noexcept
{
    return _view && _view->magic == Magic && _view->version == Protocol && _view->size == sizeof(Shared) &&
        _view->width == FrameWidth && _view->height == FrameHeight && _view->imageBytes == ImageBytes &&
        _view->enabled <= 1 && _view->ready <= 1 && _view->revision != 0;
}
HRESULT FrameChannel::Lock(wil::mutex_release_scope_exit& lease) noexcept
{
    if (!_mutex || !_view || _readLease) return E_UNEXPECTED;
    const DWORD wait = WaitForSingleObject(_mutex.get(), LockDeadlineMs);
    if (wait == WAIT_TIMEOUT) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return HRESULT_FROM_WIN32(GetLastError());
    lease = wil::ReleaseMutex_scope_exit(_mutex.get());
    if (!Valid()) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (wait == WAIT_ABANDONED)
    {
        _view->enabled = 0; _view->ready = 0; _view->timestamp = 0;
        return HRESULT_FROM_WIN32(ERROR_ABANDONED_WAIT_0);
    }
    return S_OK;
}
HRESULT FrameChannel::Create(PCWSTR ownerSid, bool localTestNamespace) noexcept
{
    Close();
    if (!ownerSid || wcsnlen_s(ownerSid, 192) >= 192) return E_INVALIDARG;
    wil::unique_hlocal sid;
    RETURN_IF_WIN32_BOOL_FALSE(ConvertStringSidToSidW(ownerSid, sid.put()));
    wil::unique_hlocal_string canonical;
    RETURN_IF_WIN32_BOOL_FALSE(ConvertSidToStringSidW(sid.get(), canonical.put()));
    try
    {
        const std::wstring sddl = std::wstring(L"D:P(A;;GA;;;SY)(A;;GA;;;LS)(A;;GA;;;") + canonical.get() + L")";
        wil::unique_hlocal security;
        RETURN_IF_WIN32_BOOL_FALSE(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
            security.put(), nullptr));
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), security.get(), FALSE};
        GUID guid{}; RETURN_IF_FAILED(CoCreateGuid(&guid));
        wchar_t suffix[40]{};
        if (!StringFromGUID2(guid, suffix, static_cast<int>(std::size(suffix)))) return E_UNEXPECTED;
        const wchar_t* scope = localTestNamespace ? L"Local" : L"Global";
        swprintf_s(_names.mapping.data(), _names.mapping.size(), L"%s\\RedXe.Camera.Frame.%s", scope, suffix);
        swprintf_s(_names.mutex.data(), _names.mutex.size(), L"%s\\RedXe.Camera.Lock.%s", scope, suffix);
        _mapping.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, sizeof(Shared), _names.mapping.data()));
        if (!_mapping) return HRESULT_FROM_WIN32(GetLastError());
        if (GetLastError() == ERROR_ALREADY_EXISTS) { Close(); return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS); }
        _mutex.reset(CreateMutexW(&attributes, FALSE, _names.mutex.data()));
        if (!_mutex) { const HRESULT result = HRESULT_FROM_WIN32(GetLastError()); Close(); return result; }
        if (GetLastError() == ERROR_ALREADY_EXISTS) { Close(); return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS); }
        _view.reset(static_cast<Shared*>(MapViewOfFile(_mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared))));
        if (!_view) { const HRESULT result = HRESULT_FROM_WIN32(GetLastError()); Close(); return result; }
        static_assert(std::is_trivially_copyable_v<Shared>);
        new (_view.get()) Shared();
        return S_OK;
    }
    catch (const std::bad_alloc&) { Close(); return E_OUTOFMEMORY; }
}
HRESULT FrameChannel::Open(const FrameChannelNames& names, bool allowLocalTestNamespace) noexcept
{
    Close();
    const size_t mappingLength = wcsnlen_s(names.mapping.data(), names.mapping.size());
    const size_t mutexLength = wcsnlen_s(names.mutex.data(), names.mutex.size());
    if (mappingLength == names.mapping.size() || mutexLength == names.mutex.size() ||
        !ValidName({names.mapping.data(), mappingLength}, false, allowLocalTestNamespace) ||
        !ValidName({names.mutex.data(), mutexLength}, true, allowLocalTestNamespace)) return E_INVALIDARG;
    // Both names must use the same random suffix and namespace, never unrelated locks around shared bytes.
    if (std::wstring_view(names.mapping.data()).substr(mappingLength - 38) != std::wstring_view(names.mutex.data()).substr(mutexLength - 38) ||
        names.mapping[0] != names.mutex[0]) return E_INVALIDARG;
    _mapping.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, names.mapping.data()));
    if (!_mapping) return HRESULT_FROM_WIN32(GetLastError());
    _mutex.reset(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, names.mutex.data()));
    if (!_mutex) { const HRESULT result = HRESULT_FROM_WIN32(GetLastError()); Close(); return result; }
    _view.reset(static_cast<Shared*>(MapViewOfFile(_mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared))));
    if (!_view) { const HRESULT result = HRESULT_FROM_WIN32(GetLastError()); Close(); return result; }
    wil::mutex_release_scope_exit lease;
    const HRESULT result = Lock(lease);
    if (FAILED(result)) { lease.reset(); Close(); return result; }
    _names = names; return S_OK;
}
HRESULT FrameChannel::State(ChannelState& result) noexcept
{
    wil::mutex_release_scope_exit lease; RETURN_IF_FAILED(Lock(lease));
    result = {_view->revision, _view->enabled != 0}; return S_OK;
}
HRESULT FrameChannel::SetGate(ChannelState state) noexcept
{
    if (!state.revision) return E_INVALIDARG;
    wil::mutex_release_scope_exit lease; RETURN_IF_FAILED(Lock(lease));
    if (state.revision < _view->revision || (state.revision == _view->revision && state.enabled != (_view->enabled != 0)))
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    if (state.revision != _view->revision || !state.enabled)
    {
        _view->ready = 0; _view->timestamp = 0;
        std::memset(_view->image.data(), 0, _view->image.size());
    }
    _view->revision = state.revision; _view->enabled = state.enabled ? 1U : 0U;
    return S_OK;
}
HRESULT FrameChannel::Write(uint64_t revision, std::span<const BYTE> nv12, LONGLONG timestamp) noexcept
{
    if (nv12.size() != ImageBytes || timestamp <= 0) return E_INVALIDARG;
    wil::mutex_release_scope_exit lease; RETURN_IF_FAILED(Lock(lease));
    if (!_view->enabled || revision != _view->revision) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    if (timestamp <= _view->timestamp) return S_FALSE;
    std::memcpy(_view->image.data(), nv12.data(), nv12.size());
    _view->timestamp = timestamp; _view->ready = 1; return S_OK;
}
HRESULT FrameChannel::Read(BYTE* bytes, DWORD capacity, LONG pitch, LONGLONG now) noexcept
{
    if (!bytes || pitch < static_cast<LONG>(FrameWidth) || static_cast<uint64_t>(pitch) * FrameHeight * 3 / 2 > capacity || now <= 0)
        return E_INVALIDARG;
    wil::mutex_release_scope_exit lease;
    RETURN_IF_FAILED(Lock(lease));
    _readLease = std::move(lease);
    const bool live = _view->enabled && _view->ready && _view->timestamp <= now && now - _view->timestamp <= MaximumFrameAge;
    for (UINT row = 0; row < FrameHeight * 3 / 2; ++row)
    {
        BYTE* target = bytes + static_cast<size_t>(row) * pitch;
        std::memset(target, row < FrameHeight ? 16 : 128, static_cast<size_t>(pitch));
        if (live) std::memcpy(target, _view->image.data() + static_cast<size_t>(row) * FrameWidth, FrameWidth);
    }
    return S_OK;
}
void FrameChannel::EndRead() noexcept { _readLease.reset(); }
} // namespace AVControl::Camera
