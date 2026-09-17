#include "LogiconProtocol.h"

#include <algorithm>
#include <cstring>

namespace Logicon
{
namespace
{
constexpr uint32_t kLongParameterBytes = 16;

void PutBigEndian16(uint8_t* destination, uint16_t value) noexcept
{
    destination[0] = static_cast<uint8_t>(value >> 8U);
    destination[1] = static_cast<uint8_t>(value & 0xFFU);
}

[[nodiscard]] uint16_t GetBigEndian16(const uint8_t* source) noexcept
{
    return static_cast<uint16_t>((static_cast<uint16_t>(source[0]) << 8U) | source[1]);
}
} // namespace

uint32_t BuildLongReport(uint8_t deviceIndex, uint8_t featureIndex, uint8_t function, const uint8_t* params,
                         uint32_t paramBytes, uint8_t* report, uint32_t capacity) noexcept
{
    if (!report || capacity < kLongReportBytes || function > 0x0F || paramBytes > kLongParameterBytes ||
        (paramBytes != 0 && !params))
    {
        return 0;
    }
    std::memset(report, 0, kLongReportBytes);
    report[0] = kReportLong;
    report[1] = deviceIndex;
    report[2] = featureIndex;
    report[3] = static_cast<uint8_t>((function << 4U) | kSoftwareId);
    if (paramBytes != 0)
    {
        std::memcpy(report + 4, params, paramBytes);
    }
    return kLongReportBytes;
}

uint32_t BuildGetFeatureReport(uint8_t deviceIndex, uint16_t featureId, uint8_t* report, uint32_t capacity) noexcept
{
    uint8_t params[3]{};
    PutBigEndian16(params, featureId);
    return BuildLongReport(deviceIndex, 0x00, 0, params, sizeof(params), report, capacity);
}

uint32_t BuildGetCidReportingReport(uint8_t deviceIndex, uint8_t featureIndex, uint16_t controlId, uint8_t* report,
                                    uint32_t capacity) noexcept
{
    uint8_t params[2]{};
    PutBigEndian16(params, controlId);
    return BuildLongReport(deviceIndex, featureIndex, 2, params, sizeof(params), report, capacity);
}

uint32_t BuildSetCidReportingReport(uint8_t deviceIndex, uint8_t featureIndex, uint16_t controlId, uint8_t flags,
                                    uint16_t remap, uint8_t flags2, uint8_t* report, uint32_t capacity) noexcept
{
    uint8_t params[6]{};
    PutBigEndian16(params, controlId);
    params[2] = flags;
    PutBigEndian16(params + 3, remap);
    params[5] = flags2;
    return BuildLongReport(deviceIndex, featureIndex, 3, params, sizeof(params), report, capacity);
}

uint32_t BuildSetBrightnessReport(uint8_t deviceIndex, uint8_t featureIndex, uint32_t percent, uint8_t* report,
                                  uint32_t capacity) noexcept
{
    // 0 resets the device; the reference clamps to 1.
    const uint16_t value = static_cast<uint16_t>(std::clamp(percent, 1U, 100U));
    uint8_t params[2]{};
    PutBigEndian16(params, value);
    return BuildLongReport(deviceIndex, featureIndex, 2, params, sizeof(params), report, capacity);
}

uint32_t BuildResetToLogoFeatureReport(uint8_t* report, uint32_t capacity) noexcept
{
    if (!report || capacity < kFeatureResetReportBytes)
    {
        return 0;
    }
    std::memset(report, 0, capacity);
    report[0] = 0x03;
    report[1] = 0x02;
    return capacity;
}

bool ParseHidppFrame(const uint8_t* report, uint32_t bytes, HidppFrame& frame) noexcept
{
    frame = HidppFrame{};
    if (!report || bytes < 5)
    {
        return false;
    }
    if (report[0] != kReportLong && report[0] != kReportVlpControl)
    {
        return false;
    }
    frame.reportId = report[0];
    frame.deviceIndex = report[1];
    if (report[0] == kReportLong && report[2] == kErrorMarker && bytes >= 6)
    {
        frame.error = true;
        frame.featureIndex = report[3];
        frame.function = static_cast<uint8_t>(report[4] >> 4U);
        frame.softwareId = static_cast<uint8_t>(report[4] & 0x0FU);
        frame.errorCode = report[5];
        frame.params = report + 6;
        frame.paramBytes = bytes - 6;
        return true;
    }
    frame.featureIndex = report[2];
    frame.function = static_cast<uint8_t>(report[3] >> 4U);
    frame.softwareId = static_cast<uint8_t>(report[3] & 0x0FU);
    frame.params = report + 4;
    frame.paramBytes = bytes - 4;
    return true;
}

bool FrameAnswers(const HidppFrame& frame, uint8_t featureIndex, uint8_t function) noexcept
{
    return frame.featureIndex == featureIndex && frame.softwareId == kSoftwareId &&
           (frame.error || frame.function == function);
}

bool ParseCidReporting(const HidppFrame& frame, CidReporting& reporting) noexcept
{
    reporting = CidReporting{};
    if (frame.error || !frame.params || frame.paramBytes < 6)
    {
        return false;
    }
    reporting.controlId = GetBigEndian16(frame.params);
    reporting.flags = frame.params[2];
    reporting.remap = GetBigEndian16(frame.params + 3);
    reporting.flags2 = frame.params[5];
    return true;
}

bool ParseInputEvent(const uint8_t* report, uint32_t bytes, uint8_t displayFeatureIndex, uint8_t reprogFeatureIndex,
                     InputEvent& event) noexcept
{
    event = InputEvent{};
    if (!report || bytes < 6 || report[1] != kDeviceIndexWired)
    {
        return false;
    }
    if (report[0] == kReportVlpControl)
    {
        // [FF][display feature][00][?][01 display][pressed key ids …][0]
        if (report[2] != displayFeatureIndex || report[3] != 0x00 || report[5] != kDisplayIndex)
        {
            return false;
        }
        event.kind = InputKind::DisplayKeys;
        for (uint32_t index = 6; index < bytes; ++index)
        {
            const uint8_t key = report[index];
            if (key == 0)
            {
                break;
            }
            if (key >= kControlLcdKeyFirst && key <= kControlLcdKeyLast && event.keyCount < event.keys.size())
            {
                event.keys[event.keyCount++] = key;
                event.mask |= 1U << (key - 1U);
            }
        }
        return true;
    }
    if (report[0] == kReportLong)
    {
        // [FF][reprog feature][00 event 0, software id 0][cid][cid][cid][cid]
        if (report[2] != reprogFeatureIndex || report[3] != 0x00)
        {
            return false;
        }
        event.kind = InputKind::DivertedButtons;
        for (uint32_t index = 4; index + 1 < bytes && event.controlCount < event.controls.size(); index += 2)
        {
            const uint16_t control = GetBigEndian16(report + index);
            if (control == 0)
            {
                break;
            }
            event.controls[event.controlCount++] = control;
            if (control == kControlPagePrevious)
            {
                event.mask |= 1U;
            }
            else if (control == kControlPageNext)
            {
                event.mask |= 2U;
            }
        }
        return true;
    }
    return false;
}

uint32_t DialpadButtonMask(const InputEvent& event) noexcept
{
    if (event.kind != InputKind::DivertedButtons)
    {
        return 0;
    }
    uint32_t mask = 0;
    for (uint32_t index = 0; index < event.controlCount && index < event.controls.size(); ++index)
    {
        for (uint32_t button = 0; button < kDialpadButtonCount; ++button)
        {
            if (event.controls[index] == kDialpadControls[button])
            {
                mask |= 1U << button;
            }
        }
    }
    return mask;
}

uint32_t KeyColumn(uint32_t slot) noexcept
{
    return slot % kKeyColumns;
}

uint32_t KeyRow(uint32_t slot) noexcept
{
    return (slot / kKeyColumns) % kKeyRows;
}

ImageRegion KeyRegion(uint32_t slot) noexcept
{
    ImageRegion region{};
    region.x = static_cast<uint16_t>(kGridOriginX + KeyColumn(slot) * (kKeySize + kKeyGap));
    region.y = static_cast<uint16_t>(kGridOriginY + KeyRow(slot) * (kKeySize + kKeyGap));
    region.width = static_cast<uint16_t>(kKeySize);
    region.height = static_cast<uint16_t>(kKeySize);
    return region;
}

ImageRegion GridRegion() noexcept
{
    ImageRegion region{};
    region.x = static_cast<uint16_t>(kGridOriginX);
    region.y = static_cast<uint16_t>(kGridOriginY);
    region.width = static_cast<uint16_t>(kGridSize);
    region.height = static_cast<uint16_t>(kGridSize);
    return region;
}

uint32_t VlpPacketCount(uint32_t imageBytes) noexcept
{
    if (imageBytes == 0)
    {
        return 0;
    }
    const uint32_t firstPayload = kVlpImageReportBytes - kVlpFirstHeaderBytes;
    if (imageBytes <= firstPayload)
    {
        return 1;
    }
    const uint32_t remaining = imageBytes - firstPayload;
    const uint32_t continuationPayload = kVlpImageReportBytes - kVlpContinuationHeaderBytes;
    return 1 + (remaining + continuationPayload - 1) / continuationPayload;
}

uint8_t VlpSequenceByte(uint32_t index, bool first, bool last) noexcept
{
    uint8_t value = static_cast<uint8_t>((index & 0x0FU) | 0x20U);
    if (first)
    {
        value |= 0x80U;
    }
    if (last)
    {
        value |= 0x40U;
    }
    return value;
}

bool BeginVlpImageStream(uint8_t featureIndex, const ImageRegion& region, bool deferUpdate, const uint8_t* image,
                         uint32_t imageBytes, VlpImageStream& stream) noexcept
{
    stream = VlpImageStream{};
    if (!image || imageBytes == 0 || imageBytes > kMaximumImageBytes || region.width == 0 || region.height == 0 ||
        region.x + region.width > kPanelSize || region.y + region.height > kPanelSize)
    {
        return false;
    }
    stream.image = image;
    stream.imageBytes = imageBytes;
    stream.featureIndex = featureIndex;
    stream.deferUpdate = deferUpdate;
    stream.region = region;
    return true;
}

bool NextVlpPacket(VlpImageStream& stream, uint8_t* packet, uint32_t capacity) noexcept
{
    if (!packet || capacity < kVlpImageReportBytes || !stream.image || stream.offset >= stream.imageBytes)
    {
        return false;
    }
    const bool first = stream.offset == 0;
    const uint32_t headerBytes = first ? kVlpFirstHeaderBytes : kVlpContinuationHeaderBytes;
    const uint32_t chunk = std::min(stream.imageBytes - stream.offset, kVlpImageReportBytes - headerBytes);
    const bool last = stream.offset + chunk >= stream.imageBytes;
    ++stream.sequence;

    std::memset(packet, 0, kVlpImageReportBytes);
    packet[0] = kReportVlpImage;
    packet[1] = kDeviceIndexWired;
    packet[2] = stream.featureIndex;
    packet[3] = static_cast<uint8_t>((2U << 4U) | kSoftwareId);
    packet[4] = VlpSequenceByte(stream.sequence, first, last);
    if (first)
    {
        packet[5] = kDisplayIndex;
        packet[6] = stream.deferUpdate ? 1 : 0;
        packet[7] = 1; // image count
        packet[8] = 0; // JPEG
        PutBigEndian16(packet + 9, stream.region.x);
        PutBigEndian16(packet + 11, stream.region.y);
        PutBigEndian16(packet + 13, stream.region.width);
        PutBigEndian16(packet + 15, stream.region.height);
        packet[17] = static_cast<uint8_t>((stream.imageBytes >> 16U) & 0xFFU);
        PutBigEndian16(packet + 18, static_cast<uint16_t>(stream.imageBytes & 0xFFFFU));
    }
    std::memcpy(packet + headerBytes, stream.image + stream.offset, chunk);
    stream.offset += chunk;
    return true;
}
} // namespace Logicon
