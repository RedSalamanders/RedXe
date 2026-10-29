#pragma once
#include <Windows.h>
#include <memory>
#include <string_view>

namespace AVControl::Camera
{
// Per-user persistent software-camera identity. This object only manages the official virtual-device API;
// physical capture remains in CaptureReader. Registration of the source DLL itself is an explicit installer step.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
class VirtualCameraRoute final
{
  public:
    VirtualCameraRoute();
    ~VirtualCameraRoute();
    static HRESULT ProbeInstalledSource() noexcept;
    HRESULT Open() noexcept;
    // Called only by an explicit Remove action, never by destructor/hidden state/process shutdown.
    HRESULT Remove() noexcept;
    void Close() noexcept;
    [[nodiscard]] std::wstring_view SymbolicLink() const noexcept;
    [[nodiscard]] std::wstring_view OwnerSid() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> _state;
};
#pragma warning(pop)

} // namespace AVControl::Camera
