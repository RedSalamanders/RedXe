#pragma once
#include <Windows.h>
#include <memory>
#include <string_view>

namespace AVControl::Camera
{
// Per-user persistent software-camera identity. This object only manages the official virtual-device API;
// physical capture remains in CaptureReader. Registration of the source DLL itself is an explicit installer step.
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
} // namespace AVControl::Camera
