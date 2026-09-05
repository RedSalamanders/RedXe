#pragma once
#include "AVControlProtocol.h"
#include <memory>

struct IMMDeviceEnumerator;

namespace AVControl
{
// Constructed and called exclusively in AVControlBroker.exe's MTA. Device callbacks only signal the supplied event.
class WindowsAudioBackend final
{
  public:
    WindowsAudioBackend();
    ~WindowsAudioBackend();
    HRESULT Initialize(HANDLE changedEvent) noexcept;
    // Explicit injected enumerator for isolated component tests. This path never activates a Windows policy client.
    HRESULT InitializeWithEnumerator(HANDLE changedEvent, IMMDeviceEnumerator* enumerator) noexcept;
    HRESULT Execute(const BrokerCommand& command, const InventoryPreferences& preferences, Inventory& inventory) noexcept;
  private:
    struct State;
    std::unique_ptr<State> _state;
};
} // namespace AVControl
