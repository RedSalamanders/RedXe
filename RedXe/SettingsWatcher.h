#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <thread>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

class SettingsWatcher final
{
  public:
    static constexpr UINT kSettingsChangedMessage = WM_APP + 2;

    SettingsWatcher() = default;
    ~SettingsWatcher();

    SettingsWatcher(const SettingsWatcher&) = delete;
    SettingsWatcher& operator=(const SettingsWatcher&) = delete;
    SettingsWatcher(SettingsWatcher&&) = delete;
    SettingsWatcher& operator=(SettingsWatcher&&) = delete;

    [[nodiscard]] HRESULT Start(HWND targetWindow, std::wstring_view settingsDirectory) noexcept;
    void AcknowledgeNotification() noexcept;
    void Stop() noexcept;

  private:
    void WatchThread() noexcept;
    void PostSettingsChanged() noexcept;

    HWND _targetWindow = nullptr;
    std::wstring _settingsDirectory;
    wil::unique_event_nothrow _stopEvent;
    std::jthread _thread;
    std::atomic_bool _notificationPending = false;
};
