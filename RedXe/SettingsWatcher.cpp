#include "SettingsWatcher.h"

#include <new>

namespace
{
using unique_change_notification =
    wil::unique_any<HANDLE, decltype(&::FindCloseChangeNotification), ::FindCloseChangeNotification>;

constexpr DWORD kNotificationFilter =
    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
constexpr DWORD kRetryMilliseconds = 1000;
constexpr DWORD kDebounceMilliseconds = 100;
} // namespace

SettingsWatcher::~SettingsWatcher()
{
    Stop();
}

HRESULT SettingsWatcher::Start(HWND targetWindow, std::wstring_view settingsDirectory) noexcept
{
    Stop();
    if (!targetWindow || !IsWindow(targetWindow) || settingsDirectory.empty())
    {
        return E_INVALIDARG;
    }

    try
    {
        _targetWindow = targetWindow;
        _settingsDirectory.assign(settingsDirectory);
        _stopEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_stopEvent)
        {
            const DWORD error = GetLastError();
            Stop();
            return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
        }

        _notificationPending.store(false, std::memory_order_release);
        _thread = std::jthread([this](std::stop_token) noexcept { WatchThread(); });
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        Stop();
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        Stop();
        return E_FAIL;
    }
}

void SettingsWatcher::AcknowledgeNotification() noexcept
{
    _notificationPending.store(false, std::memory_order_release);
}

void SettingsWatcher::Stop() noexcept
{
    if (_stopEvent)
    {
        SetEvent(_stopEvent.get());
    }
    if (_thread.joinable())
    {
        _thread.join();
    }

    _thread = std::jthread{};
    _stopEvent.reset();
    _notificationPending.store(false, std::memory_order_release);
    _settingsDirectory.clear();
    _targetWindow = nullptr;
}

void SettingsWatcher::WatchThread() noexcept
{
    unique_change_notification notification;
    for (;;)
    {
        if (!notification)
        {
            HANDLE raw = FindFirstChangeNotificationW(_settingsDirectory.c_str(), FALSE, kNotificationFilter);
            if (raw == nullptr || raw == INVALID_HANDLE_VALUE)
            {
                if (WaitForSingleObject(_stopEvent.get(), kRetryMilliseconds) == WAIT_OBJECT_0)
                {
                    return;
                }
                continue;
            }

            notification.reset(raw);
            // Close the startup/re-arm race. Stamp deduplication makes this catch-up notification cheap.
            PostSettingsChanged();
        }

        HANDLE handles[] = {_stopEvent.get(), notification.get()};
        const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0)
        {
            return;
        }
        if (waitResult != WAIT_OBJECT_0 + 1)
        {
            return;
        }

        if (!FindNextChangeNotification(notification.get()))
        {
            notification.reset();
        }

        if (WaitForSingleObject(_stopEvent.get(), kDebounceMilliseconds) == WAIT_OBJECT_0)
        {
            return;
        }
        PostSettingsChanged();
    }
}

void SettingsWatcher::PostSettingsChanged() noexcept
{
    if (_notificationPending.exchange(true, std::memory_order_acq_rel))
    {
        return;
    }
    if (!PostMessageW(_targetWindow, kSettingsChangedMessage, 0, 0))
    {
        _notificationPending.store(false, std::memory_order_release);
    }
}
