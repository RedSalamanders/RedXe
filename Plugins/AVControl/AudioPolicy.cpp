#include <mmdeviceapi.h>
#include <wil/com.h>

namespace AVControl
{
namespace
{
// Isolated undocumented ABI, never exposed through the plugin or called in RedXe.exe. The published COM identity
// has ten methods between IUnknown and SetDefaultEndpoint; their slots are deliberately not callable here.
// Identity/slot reference: https://github.com/tartakynov/audioswitch/blob/master/IPolicyConfig.h
// Availability is a runtime capability, not a promise of support on future Windows versions. No registry fallback.
interface __declspec(uuid("f8679f50-850a-41cf-9c72-430f290290c8")) __declspec(novtable) AudioPolicy : IUnknown
{
    virtual void STDMETHODCALLTYPE Reserved00() = 0;
    virtual void STDMETHODCALLTYPE Reserved01() = 0;
    virtual void STDMETHODCALLTYPE Reserved02() = 0;
    virtual void STDMETHODCALLTYPE Reserved03() = 0;
    virtual void STDMETHODCALLTYPE Reserved04() = 0;
    virtual void STDMETHODCALLTYPE Reserved05() = 0;
    virtual void STDMETHODCALLTYPE Reserved06() = 0;
    virtual void STDMETHODCALLTYPE Reserved07() = 0;
    virtual void STDMETHODCALLTYPE Reserved08() = 0;
    virtual void STDMETHODCALLTYPE Reserved09() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR endpoint, ERole role) = 0;
};
constexpr CLSID Client{0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
}
bool AudioPolicyAvailable() noexcept
{
    wil::com_ptr_nothrow<AudioPolicy> policy;
    return SUCCEEDED(CoCreateInstance(Client, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(policy.put())));
}
HRESULT SetAudioDefault(PCWSTR endpoint, ERole role) noexcept
{
    if (!endpoint || !endpoint[0] || role < eConsole || role >= ERole_enum_count) return E_INVALIDARG;
    wil::com_ptr_nothrow<AudioPolicy> policy;
    const HRESULT created = CoCreateInstance(Client, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(policy.put()));
    return FAILED(created) ? created : policy->SetDefaultEndpoint(endpoint, role);
}
} // namespace AVControl
