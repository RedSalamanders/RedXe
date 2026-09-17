#pragma once

// One connected HID++ device (the keypad, or the dialpad's vendor collection): the HID ports of its vendor
// collections, the HID++ feature indexes resolved at connect, the saved diverted-control flags, the current control
// state, and a bounded frame trace for the Debug monitor. Every method runs on the device lane; nothing here
// allocates after the ports are attached.

#include "LogiconHid.h"
#include "LogiconProtocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <windows.h>

namespace Logicon
{
inline constexpr uint32_t kCommandTimeoutMilliseconds = 1000;
inline constexpr uint32_t kWriteTimeoutMilliseconds = 1000;
inline constexpr uint32_t kTraceSlots = 32;
inline constexpr uint32_t kTraceBytes = 24;

struct DeviceFeatures final
{
    uint8_t display = 0;
    uint8_t reprogControls = 0;
    uint8_t brightness = 0;
    // The display index came from the reference observation because neither lookup named 0x19A1.
    bool displayAssumed = false;
};

// Contextual-display feature index observed on shipping keypad firmware by both reference implementations.
inline constexpr uint8_t kObservedDisplayFeatureIndex = 0x02;

enum class DeviceRole : uint8_t
{
    // Contextual display, page buttons, brightness.
    Keypad = 0,
    // Four diverted buttons; the dial and roller arrive through Raw Input, not through this session.
    Dialpad,
};

// Bit n of keys = LCD key n+1 held; bit 0 / bit 1 of pageButtons = previous / next page button held; bit n of
// dialButtons = dialpad button n (kDialpadControls order) held.
struct ControlState final
{
    uint32_t keys = 0;
    uint32_t pageButtons = 0;
    uint32_t dialButtons = 0;
};

// Controls that went down since the last pump, accumulated per report so a tap whose press and release arrive in
// one drain still counts as a press.
struct ControlEdges final
{
    uint32_t keysDown = 0;
    uint32_t pageButtonsDown = 0;
    uint32_t dialButtonsDown = 0;
};

inline constexpr uint32_t kMaximumSavedControls = 4;

struct TraceEntry final
{
    uint32_t tick = 0;
    uint32_t length = 0;
    bool outbound = false;
    std::array<uint8_t, kTraceBytes> bytes{};
};

// What the last root feature lookup asked and answered; kept for the connect-failed diagnostic.
struct LookupRecord final
{
    uint16_t featureId = 0;
    HRESULT result = S_OK;
    uint32_t paramBytes = 0;
    std::array<uint8_t, 8> params{};
};

struct DeviceCounters final
{
    uint32_t reportsIn = 0;
    uint32_t reportsOut = 0;
    uint32_t imagePackets = 0;
    uint32_t imagesWritten = 0;
    uint32_t commandErrors = 0;
    uint32_t lastHidppError = 0;
    HRESULT lastFailure = S_OK;
};

class DeviceSession final
{
  public:
    DeviceSession() = default;
    DeviceSession(const DeviceSession&) = delete;
    DeviceSession& operator=(const DeviceSession&) = delete;

    // Takes ownership of one opened port. Ports are routed by the report ids their collection describes.
    [[nodiscard]] HRESULT AttachPort(std::unique_ptr<HidPort> port) noexcept;
    void Detach() noexcept;
    [[nodiscard]] bool HasPorts() const noexcept;
    [[nodiscard]] bool Connected() const noexcept;
    [[nodiscard]] bool Disconnected() const noexcept;

    // Keypad: resolves the feature indexes, saves the page buttons' reporting flags, and diverts them. Requires a
    // port that carries 0x11 output; the display feature is required, brightness is optional.
    [[nodiscard]] HRESULT Connect(HANDLE stopEvent) noexcept;
    // Dialpad: resolves 0x1B04, then saves and diverts the buttons in buttonMask (bit n = kDialpadControls[n]);
    // the others keep their native behavior (mouse Back/Forward, keyboard usages).
    [[nodiscard]] HRESULT ConnectDialpad(HANDLE stopEvent, uint32_t buttonMask) noexcept;
    // Restores the saved reporting flags and optionally resets the panel to the Logi splash. Idempotent.
    [[nodiscard]] HRESULT Restore(HANDLE stopEvent, bool resetToLogo) noexcept;
    [[nodiscard]] DeviceRole Role() const noexcept
    {
        return _role;
    }
    [[nodiscard]] uint32_t DivertedDialButtons() const noexcept
    {
        return _divertedDialButtons;
    }

    [[nodiscard]] HRESULT SetBrightness(uint32_t percent, HANDLE stopEvent) noexcept;
    // Streams one JPEG to a panel region. Checks stopEvent between packets.
    [[nodiscard]] HRESULT WriteImage(const ImageRegion& region, const uint8_t* jpeg, uint32_t bytes, bool deferUpdate,
                                     HANDLE stopEvent) noexcept;

    // Read events of every attached port, for the lane's wait set.
    [[nodiscard]] uint32_t ReadEvents(HANDLE* handles, uint32_t capacity) const noexcept;
    // Drains every pending input report and updates the control state. changed reports a key or page-button
    // transition; edges lists every control that went down since the previous pump, including presses answered
    // while a command was waiting. A device-gone failure is returned and marks the session disconnected.
    [[nodiscard]] HRESULT Pump(bool& changed, ControlEdges& edges) noexcept;

    [[nodiscard]] const ControlState& Controls() const noexcept;
    [[nodiscard]] const DeviceFeatures& Features() const noexcept;
    [[nodiscard]] const DeviceCounters& Counters() const noexcept;
    [[nodiscard]] const LookupRecord& LastLookup() const noexcept
    {
        return _lastLookup;
    }
    [[nodiscard]] uint32_t PortCount() const noexcept;
    [[nodiscard]] const HidCollectionInfo* PortInfo(uint32_t index) const noexcept;
    // Copies up to capacity trace entries, oldest first. Returns the count.
    [[nodiscard]] uint32_t CopyTrace(TraceEntry* entries, uint32_t capacity) const noexcept;

  private:
    [[nodiscard]] HidPort* PortForOutput(uint8_t reportId) const noexcept;
    [[nodiscard]] HidPort* PortForFeature(uint8_t reportId) const noexcept;
    [[nodiscard]] HRESULT WriteReport(const uint8_t* report, uint32_t bytes, HANDLE stopEvent) noexcept;
    // Sends one long report and waits for the matching response, dispatching unrelated input meanwhile.
    [[nodiscard]] HRESULT SendCommand(uint8_t featureIndex, uint8_t function, const uint8_t* params,
                                      uint32_t paramBytes, HANDLE stopEvent, HidppFrame& response,
                                      uint8_t* responseBuffer, uint32_t responseCapacity) noexcept;
    [[nodiscard]] HRESULT ResolveFeature(uint16_t featureId, HANDLE stopEvent, uint8_t& index) noexcept;
    // Walks IFeatureSet (0x0001) for a feature the root hides. At most 64 entries are examined.
    [[nodiscard]] HRESULT ResolveFeatureThroughSet(uint16_t featureId, HANDLE stopEvent, uint8_t& index) noexcept;
    // Reads each control's 0x1B04 reporting, remembers it for Restore, and diverts the control.
    [[nodiscard]] HRESULT DivertControls(const uint16_t* controls, uint32_t count, HANDLE stopEvent) noexcept;
    void HandleInput(const uint8_t* report, uint32_t bytes, bool& changed) noexcept;
    void Trace(const uint8_t* report, uint32_t bytes, bool outbound) noexcept;

    std::array<std::unique_ptr<HidPort>, kMaximumHidCollections> _ports{};
    uint32_t _portCount = 0;
    DeviceRole _role = DeviceRole::Keypad;
    DeviceFeatures _features{};
    ControlState _controls{};
    ControlEdges _edges{};
    DeviceCounters _counters{};
    LookupRecord _lastLookup{};
    std::array<CidReporting, kMaximumSavedControls> _savedControls{};
    uint32_t _savedControlCount = 0;
    uint32_t _divertedDialButtons = 0;
    bool _connected = false;
    bool _restored = true;
    std::array<TraceEntry, kTraceSlots> _trace{};
    uint32_t _traceHead = 0;
    uint32_t _traceCount = 0;
    std::array<uint8_t, kVlpImageReportBytes> _packet{};
};
} // namespace Logicon
