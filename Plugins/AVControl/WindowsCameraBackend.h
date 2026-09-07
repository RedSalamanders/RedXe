#pragma once
#include "AVControlProtocol.h"
#include <memory>

namespace AVControl
{
// Helper-only MTA backend. The camera's persistent virtual-device lifetime and its optional acquisition lane
// are independent from display observation. Initialize/Observe never capture a physical camera while off.
class WindowsCameraBackend final
{
  public:
    WindowsCameraBackend();
    ~WindowsCameraBackend();
    HRESULT Initialize(HANDLE changed) noexcept;
    void SetPreferences(const DevicePreferences<MaximumCameras>& preferences) noexcept;
    HRESULT Observe(Inventory& inventory) noexcept;
    HRESULT Execute(const BrokerCommand& command, Inventory& inventory) noexcept;
    [[nodiscard]] bool RequiresHelper() noexcept;
    [[nodiscard]] HANDLE ProgressEvent() const noexcept;
    [[nodiscard]] HANDLE NotificationEvent() const noexcept;
    HRESULT ProcessNotifications() noexcept;
    [[nodiscard]] DWORD WatchdogTimeout() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> _state;
};
} // namespace AVControl
