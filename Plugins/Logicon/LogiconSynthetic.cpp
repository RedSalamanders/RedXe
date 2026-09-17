#include "LogiconSynthetic.h"

#include <algorithm>
#include <cstring>

namespace Logicon
{
namespace
{
// The feature indexes the reference implementations observed; the synthetic device answers root lookups with them.
constexpr uint8_t kSyntheticDisplayIndex = 0x02;
constexpr uint8_t kSyntheticReprogIndex = 0x0B;
constexpr uint8_t kSyntheticBrightnessIndex = 0x0F;
// The dialpad captured on 2026-09-17 lists 0x1B04 at 0x0A and 29 features.
constexpr uint8_t kSyntheticDialpadReprogIndex = 0x0A;
constexpr uint8_t kSyntheticDialpadFeatureCount = 0x1D;

[[nodiscard]] uint32_t DialControlSlot(uint16_t control) noexcept
{
    for (uint32_t slot = 0; slot < kDialpadButtonCount; ++slot)
    {
        if (kDialpadControls[slot] == control)
        {
            return slot;
        }
    }
    return kDialpadButtonCount;
}
} // namespace

HRESULT SyntheticKeypad::Initialize() noexcept
{
    if (_readEvent)
    {
        return S_OK;
    }
    _readEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    return _readEvent ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

void SyntheticKeypad::Reset() noexcept
{
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    _queueHead = 0;
    _queueCount = 0;
    _imageCount = 0;
    _commands = 0;
    _brightness = 0;
    _streamRemaining = 0;
    _streaming = SyntheticImageRecord{};
    _pageFlags = {};
    _dialFlags = {};
    _resetSeen = false;
    if (_readEvent)
    {
        ResetEvent(_readEvent.get());
    }
}

void SyntheticKeypad::Enqueue(const uint8_t* report, uint32_t bytes) noexcept
{
    if (_queueCount >= kSyntheticQueueSlots)
    {
        return;
    }
    const uint32_t slot = (_queueHead + _queueCount) % kSyntheticQueueSlots;
    _queue[slot].fill(0);
    const uint32_t copied = std::min<uint32_t>(bytes, kVlpControlReportBytes);
    std::memcpy(_queue[slot].data(), report, copied);
    _queueBytes[slot] = copied;
    ++_queueCount;
    if (_readEvent)
    {
        SetEvent(_readEvent.get());
    }
}

HRESULT SyntheticKeypad::InjectReport(const uint8_t* report, uint32_t bytes) noexcept
{
    if (!report || bytes == 0)
    {
        return E_POINTER;
    }
    if (!_readEvent)
    {
        return E_UNEXPECTED;
    }
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    if (_queueCount >= kSyntheticQueueSlots)
    {
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
    Enqueue(report, bytes);
    return S_OK;
}

HRESULT SyntheticKeypad::InjectKeys(uint32_t keyMask) noexcept
{
    std::array<uint8_t, kVlpControlReportBytes> report{};
    report[0] = kReportVlpControl;
    report[1] = kDeviceIndexWired;
    report[2] = kSyntheticDisplayIndex;
    report[3] = 0x00;
    report[4] = 0x00;
    report[5] = kDisplayIndex;
    uint32_t written = 6;
    for (uint32_t key = 0; key < kKeyCount && written < report.size(); ++key)
    {
        if ((keyMask & (1U << key)) != 0)
        {
            report[written++] = static_cast<uint8_t>(key + 1);
        }
    }
    return InjectReport(report.data(), static_cast<uint32_t>(report.size()));
}

HRESULT SyntheticKeypad::InjectPageButtons(uint32_t pageMask) noexcept
{
    std::array<uint8_t, kLongReportBytes> report{};
    report[0] = kReportLong;
    report[1] = kDeviceIndexWired;
    report[2] = kSyntheticReprogIndex;
    report[3] = 0x00;
    uint32_t written = 4;
    if ((pageMask & 1U) != 0)
    {
        report[written++] = static_cast<uint8_t>(kControlPagePrevious >> 8U);
        report[written++] = static_cast<uint8_t>(kControlPagePrevious & 0xFFU);
    }
    if ((pageMask & 2U) != 0)
    {
        report[written++] = static_cast<uint8_t>(kControlPageNext >> 8U);
        report[written++] = static_cast<uint8_t>(kControlPageNext & 0xFFU);
    }
    return InjectReport(report.data(), static_cast<uint32_t>(report.size()));
}

HRESULT SyntheticKeypad::InjectDialButtons(uint32_t buttonMask) noexcept
{
    std::array<uint8_t, kLongReportBytes> report{};
    report[0] = kReportLong;
    report[1] = kDeviceIndexWired;
    report[2] = kSyntheticDialpadReprogIndex;
    report[3] = 0x00;
    uint32_t written = 4;
    for (uint32_t button = 0; button < kDialpadButtonCount && written + 1 < report.size(); ++button)
    {
        if ((buttonMask & (1U << button)) != 0)
        {
            report[written++] = static_cast<uint8_t>(kDialpadControls[button] >> 8U);
            report[written++] = static_cast<uint8_t>(kDialpadControls[button] & 0xFFU);
        }
    }
    return InjectReport(report.data(), static_cast<uint32_t>(report.size()));
}

uint32_t SyntheticKeypad::ImagesWritten() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return _imageCount;
}

uint32_t SyntheticKeypad::CommandsReceived() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return _commands;
}

uint32_t SyntheticKeypad::BrightnessPercent() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return _brightness;
}

bool SyntheticKeypad::PageButtonsDiverted() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return (_pageFlags[0] & kCidReportingDivert) == kCidReportingDivert &&
           (_pageFlags[1] & kCidReportingDivert) == kCidReportingDivert;
}

bool SyntheticKeypad::DialButtonsDiverted() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    for (const uint8_t flags : _dialFlags)
    {
        if ((flags & kCidReportingDivert) != kCidReportingDivert)
        {
            return false;
        }
    }
    return true;
}

bool SyntheticKeypad::ResetToLogoSeen() const noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    return _resetSeen;
}

uint32_t SyntheticKeypad::CopyImageRecords(SyntheticImageRecord* records, uint32_t capacity) const noexcept
{
    if (!records || capacity == 0)
    {
        return 0;
    }
    const auto guard = wil::AcquireSRWLockShared(&_lock);
    const uint32_t count = std::min(capacity, _imageCount);
    for (uint32_t index = 0; index < count; ++index)
    {
        records[index] = _images[index];
    }
    return count;
}

HANDLE SyntheticKeypad::ReadEvent() const noexcept
{
    return _readEvent.get();
}

void SyntheticKeypad::RecordImage(const uint8_t* packet, uint32_t bytes) noexcept
{
    if (bytes < kVlpFirstHeaderBytes)
    {
        return;
    }
    const uint8_t sequence = packet[4];
    if ((sequence & 0x80U) != 0)
    {
        _streaming = SyntheticImageRecord{};
        _streaming.region.x = static_cast<uint16_t>((packet[9] << 8U) | packet[10]);
        _streaming.region.y = static_cast<uint16_t>((packet[11] << 8U) | packet[12]);
        _streaming.region.width = static_cast<uint16_t>((packet[13] << 8U) | packet[14]);
        _streaming.region.height = static_cast<uint16_t>((packet[15] << 8U) | packet[16]);
        _streaming.deferUpdate = packet[6] != 0;
        _streaming.bytes =
            (static_cast<uint32_t>(packet[17]) << 16U) | (static_cast<uint32_t>(packet[18]) << 8U) | packet[19];
        std::memcpy(_streaming.head.data(), packet + kVlpFirstHeaderBytes,
                    std::min<size_t>(_streaming.head.size(), bytes - kVlpFirstHeaderBytes));
        const uint32_t firstPayload = std::min(_streaming.bytes, kVlpImageReportBytes - kVlpFirstHeaderBytes);
        _streamRemaining = _streaming.bytes - firstPayload;
    }
    else
    {
        const uint32_t payload = std::min(_streamRemaining, kVlpImageReportBytes - kVlpContinuationHeaderBytes);
        _streamRemaining -= payload;
    }
    if ((sequence & 0x40U) != 0)
    {
        if (_imageCount < _images.size())
        {
            _images[_imageCount++] = _streaming;
        }
        else
        {
            // Keep the most recent records once the bounded log is full.
            for (uint32_t index = 1; index < _images.size(); ++index)
            {
                _images[index - 1] = _images[index];
            }
            _images[_images.size() - 1] = _streaming;
        }
        _streamRemaining = 0;
    }
}

HRESULT SyntheticKeypad::HandleWrite(const uint8_t* report, uint32_t bytes) noexcept
{
    if (!report || bytes == 0)
    {
        return E_POINTER;
    }
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    if (report[0] == kReportVlpImage)
    {
        RecordImage(report, bytes);
        return S_OK;
    }
    if (report[0] != kReportLong || bytes < kLongReportBytes)
    {
        return S_OK;
    }
    ++_commands;
    const uint8_t featureIndex = report[2];
    const uint8_t function = static_cast<uint8_t>(report[3] >> 4U);
    const uint8_t softwareId = static_cast<uint8_t>(report[3] & 0x0FU);
    std::array<uint8_t, kLongReportBytes> response{};
    response[0] = kReportLong;
    response[1] = report[1];
    response[2] = featureIndex;
    response[3] = report[3];
    const uint8_t reprogIndex = _dialpad ? kSyntheticDialpadReprogIndex : kSyntheticReprogIndex;
    if (featureIndex == 0x00 && function == 0)
    {
        const uint16_t featureId = static_cast<uint16_t>((report[4] << 8U) | report[5]);
        uint8_t index = 0;
        if (featureId == kFeatureContextualDisplay && !_dialpad)
        {
            // Shipping firmware hides the display feature from the root; the feature set still lists it.
            index = _hideDisplayFromRoot ? 0 : kSyntheticDisplayIndex;
        }
        else if (featureId == kFeatureReprogControls)
        {
            index = reprogIndex;
        }
        else if (featureId == kFeatureBrightness && !_dialpad)
        {
            index = kSyntheticBrightnessIndex;
        }
        else if (featureId == kFeatureRoot || featureId == kFeatureSet)
        {
            index = static_cast<uint8_t>(featureId);
        }
        response[4] = index;
        response[5] = 0;
        response[6] = 1;
    }
    else if (featureIndex == 0x01 && function == 0)
    {
        // IFeatureSet.getCount: the highest populated index.
        response[4] = _dialpad ? kSyntheticDialpadFeatureCount : kSyntheticBrightnessIndex;
    }
    else if (featureIndex == 0x01 && function == 1)
    {
        // IFeatureSet.getFeatureID(index): the few features the synthetic device implements, zero elsewhere.
        const uint8_t queried = report[4];
        uint16_t featureId = 0;
        if (queried == kSyntheticDisplayIndex && !_dialpad)
        {
            featureId = kFeatureContextualDisplay;
        }
        else if (queried == reprogIndex)
        {
            featureId = kFeatureReprogControls;
        }
        else if (queried == kSyntheticBrightnessIndex && !_dialpad)
        {
            featureId = kFeatureBrightness;
        }
        else if (queried == 1)
        {
            featureId = kFeatureSet;
        }
        response[4] = static_cast<uint8_t>(featureId >> 8U);
        response[5] = static_cast<uint8_t>(featureId & 0xFFU);
        response[6] = queried == kSyntheticDisplayIndex && !_dialpad ? 0x40 : 0x00; // hidden flag
    }
    else if (featureIndex == reprogIndex && (function == 2 || function == 3))
    {
        const uint16_t control = static_cast<uint16_t>((report[4] << 8U) | report[5]);
        uint8_t* flags = nullptr;
        if (_dialpad)
        {
            const uint32_t slot = DialControlSlot(control);
            flags = slot < kDialpadButtonCount ? &_dialFlags[slot] : nullptr;
        }
        else if (control == kControlPagePrevious || control == kControlPageNext)
        {
            flags = &_pageFlags[control == kControlPagePrevious ? 0U : 1U];
        }
        if (!flags)
        {
            response[2] = kErrorMarker;
            response[3] = featureIndex;
            response[4] = report[3];
            response[5] = 1;
        }
        else
        {
            if (function == 3)
            {
                *flags = report[6];
            }
            response[4] = report[4];
            response[5] = report[5];
            response[6] = *flags;
        }
    }
    else if (featureIndex == kSyntheticBrightnessIndex && function == 2 && !_dialpad)
    {
        _brightness = static_cast<uint32_t>((report[4] << 8U) | report[5]);
    }
    else if (featureIndex == kSyntheticDisplayIndex && !_dialpad)
    {
        // Display acks carry no parameters.
    }
    else
    {
        response[2] = kErrorMarker;
        response[3] = featureIndex;
        response[4] = report[3];
        response[5] = 6;
    }
    (void)softwareId;
    Enqueue(response.data(), static_cast<uint32_t>(response.size()));
    return S_OK;
}

HRESULT SyntheticKeypad::HandleFeature(const uint8_t* report, uint32_t bytes) noexcept
{
    if (!report || bytes < 2)
    {
        return E_POINTER;
    }
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    if (report[0] == 0x03 && report[1] == 0x02)
    {
        _resetSeen = true;
    }
    return S_OK;
}

HRESULT SyntheticKeypad::TakeQueued(uint8_t* buffer, uint32_t capacity, uint32_t& bytes) noexcept
{
    bytes = 0;
    if (!buffer || capacity == 0)
    {
        return E_POINTER;
    }
    const auto guard = wil::AcquireSRWLockExclusive(&_lock);
    if (_queueCount == 0)
    {
        if (_readEvent)
        {
            ResetEvent(_readEvent.get());
        }
        return S_FALSE;
    }
    const uint32_t slot = _queueHead;
    const uint32_t copied = std::min(_queueBytes[slot], capacity);
    std::memcpy(buffer, _queue[slot].data(), copied);
    bytes = copied;
    _queueHead = (_queueHead + 1) % kSyntheticQueueSlots;
    --_queueCount;
    if (_queueCount == 0 && _readEvent)
    {
        ResetEvent(_readEvent.get());
    }
    return S_OK;
}

SyntheticHidPort::SyntheticHidPort(SyntheticKeypad& keypad) noexcept : _keypad(keypad)
{
    _info.vendorId = kVendorId;
    _info.usagePage = kVendorUsagePage;
    _info.usage = 0x0202;
    if (keypad.IsDialpad())
    {
        // The dialpad's one vendor collection: 20-byte 0x11 in and out, nothing else.
        _info.productId = kDialpadProductId;
        _info.inputReportBytes = kLongReportBytes;
        _info.outputReportBytes = kLongReportBytes;
        _info.inputReportMask = 1U << kReportLong;
        _info.outputReportMask = 1U << kReportLong;
        const wchar_t path[] = L"synthetic://mx-creative-dialpad";
        std::memcpy(_info.path.data(), path, sizeof(path));
        return;
    }
    _info.productId = kKeypadProductId;
    _info.inputReportBytes = kVlpControlReportBytes;
    _info.outputReportBytes = kVlpImageReportBytes;
    _info.featureReportBytes = kFeatureResetReportBytes;
    _info.inputReportMask = (1U << kReportLong) | (1U << kReportVlpControl);
    _info.outputReportMask = (1U << kReportLong) | (1U << kReportVlpControl) | (1U << kReportVlpImage);
    _info.featureReportMask = 1U << 0x03;
    const wchar_t path[] = L"synthetic://mx-creative-keypad";
    std::memcpy(_info.path.data(), path, sizeof(path));
}

const HidCollectionInfo& SyntheticHidPort::Info() const noexcept
{
    return _info;
}

HANDLE SyntheticHidPort::ReadEvent() const noexcept
{
    return _keypad.ReadEvent();
}

HRESULT SyntheticHidPort::TakeReport(uint8_t* buffer, uint32_t capacity, uint32_t& bytes) noexcept
{
    return _keypad.TakeQueued(buffer, capacity, bytes);
}

HRESULT SyntheticHidPort::Write(const uint8_t* report, uint32_t bytes, HANDLE, uint32_t) noexcept
{
    return _keypad.HandleWrite(report, bytes);
}

HRESULT SyntheticHidPort::SetFeature(const uint8_t* report, uint32_t bytes) noexcept
{
    return _keypad.HandleFeature(report, bytes);
}

bool SyntheticHidPort::Disconnected() const noexcept
{
    return false;
}

void SyntheticHidPort::Cancel() noexcept {}
} // namespace Logicon
