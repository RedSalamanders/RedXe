#pragma once

// HID++ 2.0 framing and the MX Creative Keypad contextual-display (0x19A1) image stream. Pure functions over caller
// buffers: nothing here touches a device, allocates, or blocks, so the byte layout is testable against the
// reference packets without hardware. Byte positions follow Specs/Plans/WIP/LogiconPlugin_2026-09-16.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace Logicon
{
inline constexpr uint16_t kVendorId = 0x046D;
inline constexpr uint16_t kKeypadProductId = 0xC354;
// The MX Creative Dialpad over Bluetooth LE: a mouse collection (dial and roller as wheels) and one 0x11 vendor
// collection (HID++ buttons). Over a Bolt receiver it would be a receiver child; not driven yet.
inline constexpr uint16_t kDialpadProductId = 0xBC00;
inline constexpr uint16_t kVendorUsagePage = 0xFF43;

inline constexpr uint8_t kReportLong = 0x11;       // HID++ long: 20 bytes including the report id
inline constexpr uint8_t kReportVlpControl = 0x13; // VLP control/event: 32 bytes
inline constexpr uint8_t kReportVlpImage = 0x14;   // VLP image stream: 4095 bytes
inline constexpr uint32_t kLongReportBytes = 20;
inline constexpr uint32_t kVlpControlReportBytes = 32;
inline constexpr uint32_t kVlpImageReportBytes = 4095;
inline constexpr uint32_t kMaximumInputReportBytes = 64;
inline constexpr uint32_t kFeatureResetReportBytes = 32;

inline constexpr uint8_t kDeviceIndexWired = 0xFF;
inline constexpr uint8_t kSoftwareId = 0x0B;
inline constexpr uint8_t kErrorMarker = 0xFF;

inline constexpr uint16_t kFeatureRoot = 0x0000;
inline constexpr uint16_t kFeatureSet = 0x0001;
inline constexpr uint16_t kFeatureReprogControls = 0x1B04;
inline constexpr uint16_t kFeatureContextualDisplay = 0x19A1;
inline constexpr uint16_t kFeatureBrightness = 0x8040;

inline constexpr uint16_t kControlPagePrevious = 0x01A1;
inline constexpr uint16_t kControlPageNext = 0x01A2;
inline constexpr uint16_t kControlLcdKeyFirst = 0x0001;
inline constexpr uint16_t kControlLcdKeyLast = 0x0009;

// The dialpad's four 0x1B04 controls as captured on 2026-09-17 (firmware REV 0016): Back, Forward, "Button 6",
// "Left Scroll As Button 7". Dialpad button n is bit n of a dial-button mask.
inline constexpr uint32_t kDialpadButtonCount = 4;
inline constexpr std::array<uint16_t, kDialpadButtonCount> kDialpadControls{0x0053, 0x0056, 0x0059, 0x005A};
// The dialpad also lists feature 0x4610 (index 0x0D on this firmware); its functions are not decoded, so the dial
// and roller are read as the mouse collection's wheels through Raw Input instead.
inline constexpr uint16_t kFeatureDialpadRotary = 0x4610;

// Divert + dvalid: the device reports the control as a diverted-button event instead of a keyboard usage.
inline constexpr uint8_t kCidReportingDivert = 0x03;

inline constexpr uint8_t kDisplayIndex = 1;
inline constexpr uint32_t kPanelSize = 480;
inline constexpr uint32_t kKeySize = 118;
inline constexpr uint32_t kKeyGap = 40;
inline constexpr uint32_t kGridOriginX = 23;
inline constexpr uint32_t kGridOriginY = 6;
inline constexpr uint32_t kGridSize = 434;
inline constexpr uint32_t kKeyColumns = 3;
inline constexpr uint32_t kKeyRows = 3;
inline constexpr uint32_t kKeyCount = kKeyColumns * kKeyRows;

inline constexpr uint32_t kVlpFirstHeaderBytes = 20;
inline constexpr uint32_t kVlpContinuationHeaderBytes = 5;
inline constexpr uint32_t kMaximumImageBytes = 0xFFFFFF;

// Builds one HID++ long report (0x11). params may be null when paramBytes is 0; at most 16 bytes are copied.
// Returns the report length (kLongReportBytes) or 0 when the buffer or parameters are invalid.
[[nodiscard]] uint32_t BuildLongReport(uint8_t deviceIndex, uint8_t featureIndex, uint8_t function,
                                       const uint8_t* params, uint32_t paramBytes, uint8_t* report,
                                       uint32_t capacity) noexcept;

// Root getFeature(featureId): the response's first parameter is the feature index, 0 when absent.
[[nodiscard]] uint32_t BuildGetFeatureReport(uint8_t deviceIndex, uint16_t featureId, uint8_t* report,
                                             uint32_t capacity) noexcept;
// 0x1B04 getCidReporting(cid) / setCidReporting(cid, flags, remap, flags2).
[[nodiscard]] uint32_t BuildGetCidReportingReport(uint8_t deviceIndex, uint8_t featureIndex, uint16_t controlId,
                                                  uint8_t* report, uint32_t capacity) noexcept;
[[nodiscard]] uint32_t BuildSetCidReportingReport(uint8_t deviceIndex, uint8_t featureIndex, uint16_t controlId,
                                                  uint8_t flags, uint16_t remap, uint8_t flags2, uint8_t* report,
                                                  uint32_t capacity) noexcept;
// 0x8040 setBrightness(percent 1..100).
[[nodiscard]] uint32_t BuildSetBrightnessReport(uint8_t deviceIndex, uint8_t featureIndex, uint32_t percent,
                                                uint8_t* report, uint32_t capacity) noexcept;
// Feature report 0x03 [02 00…]: resets the panel to the Logi splash. Fills capacity bytes (at least 32).
[[nodiscard]] uint32_t BuildResetToLogoFeatureReport(uint8_t* report, uint32_t capacity) noexcept;

// A parsed HID++ frame. params borrows the caller's report buffer.
struct HidppFrame final
{
    uint8_t reportId = 0;
    uint8_t deviceIndex = 0;
    uint8_t featureIndex = 0;
    uint8_t function = 0;
    uint8_t softwareId = 0;
    bool error = false;
    uint8_t errorCode = 0;
    const uint8_t* params = nullptr;
    uint32_t paramBytes = 0;
};

// Splits a 0x11 or 0x13 report into its HID++ fields. An error frame (0x11 FF FF feat func err) sets error and
// reports the failing feature index and function. Returns false for other report ids or short buffers.
[[nodiscard]] bool ParseHidppFrame(const uint8_t* report, uint32_t bytes, HidppFrame& frame) noexcept;

// True when a frame answers the command sent to featureIndex/function with this software id.
[[nodiscard]] bool FrameAnswers(const HidppFrame& frame, uint8_t featureIndex, uint8_t function) noexcept;

// Decoded 0x1B04 getCidReporting response.
struct CidReporting final
{
    uint16_t controlId = 0;
    uint8_t flags = 0;
    uint16_t remap = 0;
    uint8_t flags2 = 0;
};

[[nodiscard]] bool ParseCidReporting(const HidppFrame& frame, CidReporting& reporting) noexcept;

enum class InputKind : uint8_t
{
    None = 0,
    // 0x1B04 event 0: up to four pressed diverted control ids.
    DivertedButtons,
    // 0x19A1 event 0 on 0x13: the LCD keys currently pressed (1..9).
    DisplayKeys,
};

struct InputEvent final
{
    InputKind kind = InputKind::None;
    uint32_t controlCount = 0;
    std::array<uint16_t, 4> controls{};
    uint32_t keyCount = 0;
    std::array<uint8_t, kKeyCount> keys{};
    // Bit n set = LCD key n+1 pressed (DisplayKeys), or page button pressed (bit 0 previous, bit 1 next).
    uint32_t mask = 0;
};

// Classifies an unsolicited input report given the feature indexes resolved at connect. Responses and acks (a
// non-zero software id) are not input events.
[[nodiscard]] bool ParseInputEvent(const uint8_t* report, uint32_t bytes, uint8_t displayFeatureIndex,
                                   uint8_t reprogFeatureIndex, InputEvent& event) noexcept;
// Bit n set = dialpad button n (kDialpadControls order) is among the event's pressed controls.
[[nodiscard]] uint32_t DialpadButtonMask(const InputEvent& event) noexcept;

struct ImageRegion final
{
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t width = 0;
    uint16_t height = 0;
};

// Panel rectangle of one LCD key (slot 0..8, reading order) or of the 434×434 grid covering all nine.
[[nodiscard]] ImageRegion KeyRegion(uint32_t slot) noexcept;
[[nodiscard]] ImageRegion GridRegion() noexcept;
// Column/row of a slot and the pixel offset of that key inside the grid image.
[[nodiscard]] uint32_t KeyColumn(uint32_t slot) noexcept;
[[nodiscard]] uint32_t KeyRow(uint32_t slot) noexcept;

// Number of 0x14 reports needed to carry imageBytes.
[[nodiscard]] uint32_t VlpPacketCount(uint32_t imageBytes) noexcept;

// Streams one JPEG through 4095-byte VLP reports. Call NextPacket until it returns false; each call fills packet
// (kVlpImageReportBytes bytes, zero padded).
struct VlpImageStream final
{
    const uint8_t* image = nullptr;
    uint32_t imageBytes = 0;
    uint32_t offset = 0;
    uint32_t sequence = 0;
    uint8_t featureIndex = 0;
    bool deferUpdate = false;
    ImageRegion region{};
};

[[nodiscard]] bool BeginVlpImageStream(uint8_t featureIndex, const ImageRegion& region, bool deferUpdate,
                                       const uint8_t* image, uint32_t imageBytes, VlpImageStream& stream) noexcept;
[[nodiscard]] bool NextVlpPacket(VlpImageStream& stream, uint8_t* packet, uint32_t capacity) noexcept;
// The VLP sequence byte: 1-based index in the low nibble, 0x20 data, 0x80 first, 0x40 last.
[[nodiscard]] uint8_t VlpSequenceByte(uint32_t index, bool first, bool last) noexcept;
} // namespace Logicon
