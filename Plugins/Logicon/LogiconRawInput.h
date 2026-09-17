#pragma once

// Raw Input reader for the dialpad's wheels. The MX Creative Dialpad reports its dial and roller as the wheels of a
// mouse collection that Windows opens exclusively, so the lane owns a hidden top-level window, registers it as a
// mouse raw-input sink, and pumps its queue between waits. The name match and the packet fold are pure and testable.

#include <array>
#include <cstdint>
#include <windows.h>

namespace Logicon
{
inline constexpr uint32_t kMaximumRawDevices = 8;
inline constexpr uint32_t kMaximumRawDeviceNameCharacters = 512;

// What the dialpad's wheels reported since the listener started. Raw units are the HID deltas (120 per detent on
// a classic wheel). Which wheel is the dial follows the first observation on hardware; the labels stay visible in
// the monitor so a wrong guess is obvious rather than hidden.
struct WheelState final
{
    // Horizontal wheel (AC Pan).
    int32_t dialRaw = 0;
    // Vertical wheel.
    int32_t rollerRaw = 0;
    uint32_t dialEvents = 0;
    uint32_t rollerEvents = 0;
    // X/Y motion packets; none are expected from the dialpad.
    uint32_t motionEvents = 0;
    // Raw mouse buttons currently held (bit n = button n+1); populated only while the buttons are not diverted.
    uint32_t buttonMask = 0;
    // Every matched raw packet, and packets from other mice (a diagnostic that the sink is alive).
    uint32_t reports = 0;
    uint32_t otherReports = 0;
};

// Wheel movement since the previous take.
struct WheelDeltas final
{
    int32_t dial = 0;
    int32_t roller = 0;
};

class RawWheelListener final
{
  public:
    RawWheelListener() = default;
    ~RawWheelListener();
    RawWheelListener(const RawWheelListener&) = delete;
    RawWheelListener& operator=(const RawWheelListener&) = delete;

    // Creates the hidden window on the calling thread and registers it as a mouse input sink. Pump and Stop must
    // run on that same thread.
    [[nodiscard]] HRESULT Start(uint16_t vendorId, uint16_t productId) noexcept;
    void Stop() noexcept;
    [[nodiscard]] bool Running() const noexcept;
    // Dispatches every queued message of the thread. Returns true when a matched packet arrived since the last pump.
    bool Pump() noexcept;
    [[nodiscard]] const WheelState& State() const noexcept;
    [[nodiscard]] WheelDeltas TakeDeltas() noexcept;
    // Drops the cached device matches; call after a hotplug change so a re-enumerated handle is looked up again.
    void ForgetDevices() noexcept;

    // Whether a raw-input device name belongs to vendorId:productId in the Bluetooth (`VID&02046d_PID&bc00`) or
    // the USB (`VID_046D&PID_BC00`) spelling, case-insensitively.
    [[nodiscard]] static bool DeviceNameMatches(const wchar_t* name, uint16_t vendorId, uint16_t productId) noexcept;
    // Folds one raw mouse packet into the state and the pending deltas.
    static void FoldMouse(const RAWMOUSE& mouse, WheelState& state, WheelDeltas& pending) noexcept;

  private:
    struct DeviceMatch final
    {
        HANDLE device = nullptr;
        bool matched = false;
    };

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    void HandleInput(HRAWINPUT input) noexcept;
    [[nodiscard]] bool Matches(HANDLE device) noexcept;

    HWND _window = nullptr;
    HMODULE _module = nullptr;
    bool _classRegistered = false;
    bool _sinkRegistered = false;
    uint16_t _vendorId = 0;
    uint16_t _productId = 0;
    WheelState _state{};
    WheelDeltas _pending{};
    bool _arrived = false;
    std::array<DeviceMatch, kMaximumRawDevices> _devices{};
    uint32_t _deviceCount = 0;
};
} // namespace Logicon
