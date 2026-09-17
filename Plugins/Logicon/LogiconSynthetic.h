#pragma once

// An in-memory MX Creative Keypad: answers the HID++ commands Logicon sends, records every image region it is
// given, and lets a test or the Debug monitor inject key and page-button reports. Selected only through the test
// contract; never by a setting.

#include "LogiconHid.h"
#include "LogiconProtocol.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace Logicon
{
inline constexpr uint32_t kSyntheticQueueSlots = 32;
inline constexpr uint32_t kSyntheticRegionSlots = 64;

struct SyntheticImageRecord final
{
    ImageRegion region{};
    uint32_t bytes = 0;
    bool deferUpdate = false;
    // First bytes of the JPEG so a test can check the SOI marker.
    std::array<uint8_t, 4> head{};
};

// Shared between the lane (through the port) and the injecting thread. One instance lives for the DLL.
class SyntheticKeypad final
{
  public:
    SyntheticKeypad() = default;
    SyntheticKeypad(const SyntheticKeypad&) = delete;
    SyntheticKeypad& operator=(const SyntheticKeypad&) = delete;

    [[nodiscard]] HRESULT Initialize() noexcept;
    void Reset() noexcept;

    // Queues one input report (report id first) for the port to hand out.
    [[nodiscard]] HRESULT InjectReport(const uint8_t* report, uint32_t bytes) noexcept;
    // Convenience: key mask (bit n = LCD key n+1) as a 0x19A1 event, page mask as a 0x1B04 event, dialpad button
    // mask (kDialpadControls order) as the dialpad's 0x1B04 event.
    [[nodiscard]] HRESULT InjectKeys(uint32_t keyMask) noexcept;
    [[nodiscard]] HRESULT InjectPageButtons(uint32_t pageMask) noexcept;
    [[nodiscard]] HRESULT InjectDialButtons(uint32_t buttonMask) noexcept;

    [[nodiscard]] uint32_t ImagesWritten() const noexcept;
    [[nodiscard]] uint32_t CommandsReceived() const noexcept;
    [[nodiscard]] uint32_t BrightnessPercent() const noexcept;
    [[nodiscard]] bool PageButtonsDiverted() const noexcept;
    [[nodiscard]] bool DialButtonsDiverted() const noexcept;
    [[nodiscard]] bool ResetToLogoSeen() const noexcept;
    [[nodiscard]] uint32_t CopyImageRecords(SyntheticImageRecord* records, uint32_t capacity) const noexcept;
    [[nodiscard]] HANDLE ReadEvent() const noexcept;
    // When true the root answers index 0 for 0x19A1, as shipping firmware does, so the feature-set walk is used.
    void HideDisplayFromRoot(bool hide) noexcept
    {
        _hideDisplayFromRoot = hide;
    }
    // When true the device answers as the dialpad's vendor collection does: 0x1B04 at 0x0A, no display, no
    // brightness, the four dial controls, 20-byte reports only.
    void ActAsDialpad(bool dialpad) noexcept
    {
        _dialpad = dialpad;
    }
    [[nodiscard]] bool IsDialpad() const noexcept
    {
        return _dialpad;
    }

    // Port side.
    [[nodiscard]] HRESULT HandleWrite(const uint8_t* report, uint32_t bytes) noexcept;
    [[nodiscard]] HRESULT HandleFeature(const uint8_t* report, uint32_t bytes) noexcept;
    [[nodiscard]] HRESULT TakeQueued(uint8_t* buffer, uint32_t capacity, uint32_t& bytes) noexcept;

  private:
    void Enqueue(const uint8_t* report, uint32_t bytes) noexcept;
    void RecordImage(const uint8_t* packet, uint32_t bytes) noexcept;

    mutable SRWLOCK _lock = SRWLOCK_INIT;
    wil::unique_event_nothrow _readEvent;
    std::array<std::array<uint8_t, kVlpControlReportBytes>, kSyntheticQueueSlots> _queue{};
    std::array<uint32_t, kSyntheticQueueSlots> _queueBytes{};
    uint32_t _queueHead = 0;
    uint32_t _queueCount = 0;
    std::array<SyntheticImageRecord, kSyntheticRegionSlots> _images{};
    uint32_t _imageCount = 0;
    uint32_t _commands = 0;
    uint32_t _brightness = 0;
    uint32_t _streamRemaining = 0;
    SyntheticImageRecord _streaming{};
    std::array<uint8_t, 2> _pageFlags{};
    std::array<uint8_t, kDialpadButtonCount> _dialFlags{};
    bool _resetSeen = false;
    bool _hideDisplayFromRoot = false;
    bool _dialpad = false;
};

class SyntheticHidPort final : public HidPort
{
  public:
    explicit SyntheticHidPort(SyntheticKeypad& keypad) noexcept;
    [[nodiscard]] const HidCollectionInfo& Info() const noexcept override;
    [[nodiscard]] HANDLE ReadEvent() const noexcept override;
    [[nodiscard]] HRESULT TakeReport(uint8_t* buffer, uint32_t capacity, uint32_t& bytes) noexcept override;
    [[nodiscard]] HRESULT Write(const uint8_t* report, uint32_t bytes, HANDLE stopEvent,
                                uint32_t timeoutMilliseconds) noexcept override;
    [[nodiscard]] HRESULT SetFeature(const uint8_t* report, uint32_t bytes) noexcept override;
    [[nodiscard]] bool Disconnected() const noexcept override;
    void Cancel() noexcept override;

  private:
    SyntheticKeypad& _keypad;
    HidCollectionInfo _info{};
};
} // namespace Logicon
