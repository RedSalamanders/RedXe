#include "LogiconRawInput.h"

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>

namespace Logicon
{
namespace
{
constexpr wchar_t kClassName[] = L"RedXe.Logicon.RawInput";
constexpr USHORT kGenericDesktopPage = 0x01;
constexpr USHORT kMouseUsage = 0x02;

[[nodiscard]] bool MouseRegistration(HWND& target, bool& found) noexcept
{
    target = nullptr;
    found = false;
    UINT count = 0;
    if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) == UINT(-1) || count > 32)
    {
        return false;
    }
    std::array<RAWINPUTDEVICE, 32> devices{};
    if (count != 0 && GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE)) == UINT(-1))
    {
        return false;
    }
    for (UINT index = 0; index < count; ++index)
    {
        if (devices[index].usUsagePage == kGenericDesktopPage && devices[index].usUsage == kMouseUsage)
        {
            target = devices[index].hwndTarget;
            found = true;
            break;
        }
    }
    return true;
}

[[nodiscard]] bool ContainsIgnoreCase(const wchar_t* text, const wchar_t* pattern) noexcept
{
    const size_t textLength = std::wcslen(text);
    const size_t patternLength = std::wcslen(pattern);
    if (patternLength == 0 || patternLength > textLength)
    {
        return patternLength == 0;
    }
    for (size_t start = 0; start + patternLength <= textLength; ++start)
    {
        size_t index = 0;
        while (index < patternLength && std::towlower(static_cast<wint_t>(text[start + index])) ==
                                            std::towlower(static_cast<wint_t>(pattern[index])))
        {
            ++index;
        }
        if (index == patternLength)
        {
            return true;
        }
    }
    return false;
}
} // namespace

RawWheelListener::~RawWheelListener()
{
    Stop();
}

bool RawWheelListener::DeviceNameMatches(const wchar_t* name, uint16_t vendorId, uint16_t productId) noexcept
{
    if (!name || name[0] == L'\0')
    {
        return false;
    }
    std::array<wchar_t, 40> bluetooth{};
    std::array<wchar_t, 40> usb{};
    (void)swprintf_s(bluetooth.data(), bluetooth.size(), L"vid&02%04x_pid&%04x", vendorId, productId);
    (void)swprintf_s(usb.data(), usb.size(), L"vid_%04x&pid_%04x", vendorId, productId);
    return ContainsIgnoreCase(name, bluetooth.data()) || ContainsIgnoreCase(name, usb.data());
}

void RawWheelListener::FoldMouse(const RAWMOUSE& mouse, WheelState& state, WheelDeltas& pending) noexcept
{
    ++state.reports;
    const USHORT flags = mouse.usButtonFlags;
    if ((flags & RI_MOUSE_WHEEL) != 0)
    {
        const int32_t delta = static_cast<int16_t>(mouse.usButtonData);
        state.rollerRaw += delta;
        pending.roller += delta;
        ++state.rollerEvents;
    }
    if ((flags & RI_MOUSE_HWHEEL) != 0)
    {
        const int32_t delta = static_cast<int16_t>(mouse.usButtonData);
        state.dialRaw += delta;
        pending.dial += delta;
        ++state.dialEvents;
    }
    if (mouse.lLastX != 0 || mouse.lLastY != 0)
    {
        ++state.motionEvents;
    }
    for (uint32_t button = 0; button < 5; ++button)
    {
        const USHORT down = static_cast<USHORT>(1U << (2U * button));
        const USHORT up = static_cast<USHORT>(down << 1U);
        if ((flags & down) != 0)
        {
            state.buttonMask |= 1U << button;
        }
        if ((flags & up) != 0)
        {
            state.buttonMask &= ~(1U << button);
        }
    }
}

LRESULT CALLBACK RawWheelListener::Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create ? create->lpCreateParams : nullptr));
    }
    else if (message == WM_INPUT)
    {
        auto* listener = reinterpret_cast<RawWheelListener*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (listener)
        {
            listener->HandleInput(reinterpret_cast<HRAWINPUT>(lParam));
        }
        // WM_INPUT must reach DefWindowProc so the system releases the packet.
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HRESULT RawWheelListener::Start(uint16_t vendorId, uint16_t productId) noexcept
{
    if (_window)
    {
        return S_OK;
    }
    HWND priorTarget = nullptr;
    bool priorFound = false;
    if (!MouseRegistration(priorTarget, priorFound) || priorFound)
    {
        // RegisterRawInputDevices replaces the process's registration for this device class.
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
    _vendorId = vendorId;
    _productId = productId;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&RawWheelListener::Procedure), &_module))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &RawWheelListener::Procedure;
    windowClass.hInstance = _module;
    windowClass.lpszClassName = kClassName;
    if (RegisterClassExW(&windowClass) != 0)
    {
        _classRegistered = true;
    }
    else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    // A hidden top-level window rather than a message-only one: raw input is not delivered to HWND_MESSAGE children.
    _window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"Logicon raw input", WS_POPUP, 0, 0, 1,
                              1, nullptr, nullptr, _module, this);
    if (!_window)
    {
        const HRESULT result = HRESULT_FROM_WIN32(GetLastError());
        Stop();
        return result;
    }
    RAWINPUTDEVICE device{};
    device.usUsagePage = kGenericDesktopPage;
    device.usUsage = kMouseUsage;
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = _window;
    if (!RegisterRawInputDevices(&device, 1, sizeof(device)))
    {
        const HRESULT result = HRESULT_FROM_WIN32(GetLastError());
        Stop();
        return result;
    }
    _sinkRegistered = true;
    _state = WheelState{};
    _pending = WheelDeltas{};
    _arrived = false;
    _deviceCount = 0;
    return S_OK;
}

void RawWheelListener::Stop() noexcept
{
    if (_sinkRegistered)
    {
        HWND registeredTarget = nullptr;
        bool registeredFound = false;
        if (MouseRegistration(registeredTarget, registeredFound) && registeredFound && registeredTarget == _window)
        {
            RAWINPUTDEVICE device{};
            device.usUsagePage = kGenericDesktopPage;
            device.usUsage = kMouseUsage;
            device.dwFlags = RIDEV_REMOVE;
            device.hwndTarget = nullptr;
            (void)RegisterRawInputDevices(&device, 1, sizeof(device));
        }
        _sinkRegistered = false;
    }
    if (_window)
    {
        (void)DestroyWindow(_window);
        _window = nullptr;
    }
    if (_classRegistered)
    {
        (void)UnregisterClassW(kClassName, _module);
        _classRegistered = false;
    }
    _module = nullptr;
    _deviceCount = 0;
}

bool RawWheelListener::Running() const noexcept
{
    return _window != nullptr && _sinkRegistered;
}

bool RawWheelListener::Pump() noexcept
{
    if (!_window)
    {
        return false;
    }
    MSG message{};
    for (uint32_t drained = 0; drained < 256 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++drained)
    {
        if (message.message == WM_QUIT)
        {
            break;
        }
        (void)TranslateMessage(&message);
        (void)DispatchMessageW(&message);
    }
    const bool arrived = _arrived;
    _arrived = false;
    return arrived;
}

const WheelState& RawWheelListener::State() const noexcept
{
    return _state;
}

WheelDeltas RawWheelListener::TakeDeltas() noexcept
{
    const WheelDeltas taken = _pending;
    _pending = WheelDeltas{};
    return taken;
}

void RawWheelListener::ForgetDevices() noexcept
{
    _deviceCount = 0;
}

bool RawWheelListener::Matches(HANDLE device) noexcept
{
    if (!device)
    {
        return false;
    }
    for (uint32_t index = 0; index < _deviceCount; ++index)
    {
        if (_devices[index].device == device)
        {
            return _devices[index].matched;
        }
    }
    std::array<wchar_t, kMaximumRawDeviceNameCharacters> name{};
    UINT characters = static_cast<UINT>(name.size());
    const UINT copied = GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(), &characters);
    const bool matched = copied != static_cast<UINT>(-1) && copied != 0 && copied < name.size() &&
                         DeviceNameMatches(name.data(), _vendorId, _productId);
    if (_deviceCount < _devices.size())
    {
        _devices[_deviceCount++] = DeviceMatch{device, matched};
    }
    return matched;
}

void RawWheelListener::HandleInput(HRAWINPUT input) noexcept
{
    alignas(8) std::array<uint8_t, 128> buffer{};
    UINT bytes = static_cast<UINT>(buffer.size());
    const UINT copied = GetRawInputData(input, RID_INPUT, buffer.data(), &bytes, sizeof(RAWINPUTHEADER));
    if (copied == static_cast<UINT>(-1) || copied < sizeof(RAWINPUTHEADER))
    {
        return;
    }
    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
    if (raw->header.dwType != RIM_TYPEMOUSE)
    {
        return;
    }
    if (!Matches(raw->header.hDevice))
    {
        ++_state.otherReports;
        return;
    }
    FoldMouse(raw->data.mouse, _state, _pending);
    _arrived = true;
}
} // namespace Logicon
