#include "LogiconHid.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

#include <initguid.h>

// hidusage.h defines USAGE before hidpi.h and hidsdi.h need it; hidclass.h supplies GUID_DEVINTERFACE_HID.
#include <hidusage.h>

#include <hidpi.h>

#include <hidclass.h>
#include <hidsdi.h>

namespace Logicon
{
namespace
{
using unique_preparsed =
    wil::unique_any<PHIDP_PREPARSED_DATA, decltype(&HidD_FreePreparsedData), HidD_FreePreparsedData>;

[[nodiscard]] bool IsDeviceGone(DWORD error) noexcept
{
    return error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_GEN_FAILURE || error == ERROR_INVALID_HANDLE ||
           error == ERROR_OPERATION_ABORTED || error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_SUCH_DEVICE;
}

// Collects the report ids a collection describes for one report type by walking its value and button caps.
[[nodiscard]] uint32_t CollectReportMask(PHIDP_PREPARSED_DATA preparsed, HIDP_REPORT_TYPE type, USHORT valueCount,
                                         USHORT buttonCount) noexcept
{
    uint32_t mask = 0;
    if (valueCount != 0)
    {
        const size_t count = std::min<size_t>(valueCount, 64);
        std::unique_ptr<HIDP_VALUE_CAPS[]> caps{new (std::nothrow) HIDP_VALUE_CAPS[count]};
        USHORT length = static_cast<USHORT>(count);
        if (caps && HidP_GetValueCaps(type, caps.get(), &length, preparsed) == HIDP_STATUS_SUCCESS)
        {
            for (USHORT index = 0; index < length; ++index)
            {
                if (caps[index].ReportID < 32)
                {
                    mask |= 1U << caps[index].ReportID;
                }
            }
        }
    }
    if (buttonCount != 0)
    {
        const size_t count = std::min<size_t>(buttonCount, 64);
        std::unique_ptr<HIDP_BUTTON_CAPS[]> caps{new (std::nothrow) HIDP_BUTTON_CAPS[count]};
        USHORT length = static_cast<USHORT>(count);
        if (caps && HidP_GetButtonCaps(type, caps.get(), &length, preparsed) == HIDP_STATUS_SUCCESS)
        {
            for (USHORT index = 0; index < length; ++index)
            {
                if (caps[index].ReportID < 32)
                {
                    mask |= 1U << caps[index].ReportID;
                }
            }
        }
    }
    return mask;
}

[[nodiscard]] HRESULT DescribeCollection(const wchar_t* path, uint16_t vendorId, uint16_t productId, uint16_t usagePage,
                                         HidCollectionInfo& info, bool& matches) noexcept
{
    matches = false;
    // Zero access: attributes and caps are readable without opening the device for I/O, so a collection owned
    // exclusively by another process is still described.
    wil::unique_hfile handle{
        CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)};
    if (!handle)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    HIDD_ATTRIBUTES attributes{};
    attributes.Size = sizeof(attributes);
    if (!HidD_GetAttributes(handle.get(), &attributes))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    // productId 0 and usagePage 0 are wildcards for the discovery probe.
    if (attributes.VendorID != vendorId || (productId != 0 && attributes.ProductID != productId))
    {
        return S_OK;
    }
    unique_preparsed preparsed;
    if (!HidD_GetPreparsedData(handle.get(), preparsed.put()))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    HIDP_CAPS caps{};
    if (HidP_GetCaps(preparsed.get(), &caps) != HIDP_STATUS_SUCCESS)
    {
        return E_FAIL;
    }
    if (usagePage != 0 && caps.UsagePage != usagePage)
    {
        return S_OK;
    }
    info = HidCollectionInfo{};
    info.vendorId = attributes.VendorID;
    info.productId = attributes.ProductID;
    info.usagePage = caps.UsagePage;
    info.usage = caps.Usage;
    info.inputReportBytes = caps.InputReportByteLength;
    info.outputReportBytes = caps.OutputReportByteLength;
    info.featureReportBytes = caps.FeatureReportByteLength;
    info.inputReportMask =
        CollectReportMask(preparsed.get(), HidP_Input, caps.NumberInputValueCaps, caps.NumberInputButtonCaps);
    info.outputReportMask =
        CollectReportMask(preparsed.get(), HidP_Output, caps.NumberOutputValueCaps, caps.NumberOutputButtonCaps);
    info.featureReportMask =
        CollectReportMask(preparsed.get(), HidP_Feature, caps.NumberFeatureValueCaps, caps.NumberFeatureButtonCaps);
    const size_t pathLength = wcsnlen_s(path, kMaximumHidPathCharacters);
    if (pathLength >= kMaximumHidPathCharacters)
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    std::memcpy(info.path.data(), path, (pathLength + 1) * sizeof(wchar_t));
    matches = true;
    return S_OK;
}
} // namespace

HRESULT EnumerateVendorCollections(uint16_t vendorId, uint16_t productId, uint16_t usagePage,
                                   HidCollections& collections) noexcept
{
    collections = HidCollections{};
    return EnumerateVendorCollections(vendorId, productId, usagePage, collections.items.data(),
                                      static_cast<uint32_t>(collections.items.size()), collections.count);
}

HRESULT EnumerateVendorCollections(uint16_t vendorId, uint16_t productId, uint16_t usagePage, HidCollectionInfo* items,
                                   uint32_t capacity, uint32_t& count) noexcept
{
    count = 0;
    if (!items || capacity == 0)
    {
        return E_INVALIDARG;
    }
    GUID hidInterface = GUID_DEVINTERFACE_HID;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        ULONG characters = 0;
        CONFIGRET result = CM_Get_Device_Interface_List_SizeW(&characters, &hidInterface, nullptr,
                                                              CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (result != CR_SUCCESS || characters == 0)
        {
            return HRESULT_FROM_WIN32(CM_MapCrToWin32Err(result, ERROR_GEN_FAILURE));
        }
        std::unique_ptr<wchar_t[]> list{new (std::nothrow) wchar_t[characters]};
        if (!list)
        {
            return E_OUTOFMEMORY;
        }
        result = CM_Get_Device_Interface_ListW(&hidInterface, nullptr, list.get(), characters,
                                               CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (result == CR_BUFFER_SMALL)
        {
            continue;
        }
        if (result != CR_SUCCESS)
        {
            return HRESULT_FROM_WIN32(CM_MapCrToWin32Err(result, ERROR_GEN_FAILURE));
        }
        for (const wchar_t* path = list.get(); *path != L'\0'; path += wcslen(path) + 1)
        {
            if (count >= capacity)
            {
                break;
            }
            HidCollectionInfo info{};
            bool matches = false;
            if (SUCCEEDED(DescribeCollection(path, vendorId, productId, usagePage, info, matches)) && matches)
            {
                items[count++] = info;
            }
        }
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
}

WindowsHidPort::~WindowsHidPort()
{
    Close();
}

HRESULT WindowsHidPort::Open(const HidCollectionInfo& info) noexcept
{
    Close();
    // An output-only collection (no input reports) is legal: it is written to and never read.
    if (info.path[0] == L'\0' || info.inputReportBytes > kMaximumHidReportBytes ||
        info.outputReportBytes > kMaximumHidReportBytes || (info.inputReportBytes == 0 && info.outputReportBytes == 0))
    {
        return E_INVALIDARG;
    }
    _io.reset(new (std::nothrow) IoState());
    if (!_io)
    {
        return E_OUTOFMEMORY;
    }
    _io->readEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    _io->writeEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!_io->readEvent || !_io->writeEvent)
    {
        const DWORD error = GetLastError();
        Close();
        return HRESULT_FROM_WIN32(error);
    }
    wil::unique_hfile handle{CreateFileW(info.path.data(), GENERIC_READ | GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_OVERLAPPED, nullptr)};
    if (!handle)
    {
        const DWORD error = GetLastError();
        Close();
        return HRESULT_FROM_WIN32(error);
    }
    // Keep a few reports buffered by the class driver so a burst of key events survives a slow write.
    (void)HidD_SetNumInputBuffers(handle.get(), 32);
    _info = info;
    _io->handle = std::move(handle);
    _disconnected = false;
    const HRESULT armed = ArmRead();
    if (FAILED(armed))
    {
        Close();
        return armed;
    }
    return S_OK;
}

void WindowsHidPort::Close() noexcept
{
    if (_io && _io->handle)
    {
        (void)CancelIoEx(_io->handle.get(), nullptr);
        DWORD transferred = 0;
        constexpr DWORD kCanceledIoDrainMilliseconds = 100;
        const auto drain = [&](bool armed, HANDLE event, OVERLAPPED& overlapped) noexcept
        {
            if (!armed)
            {
                return true;
            }
            if (WaitForSingleObject(event, kCanceledIoDrainMilliseconds) != WAIT_OBJECT_0)
            {
                return false;
            }
            return GetOverlappedResult(_io->handle.get(), &overlapped, &transferred, FALSE) ||
                   GetLastError() != ERROR_IO_INCOMPLETE;
        };
        const bool readDone = drain(_io->readArmed, _io->readEvent.get(), _io->readOverlapped);
        const bool writeDone = drain(_io->writeArmed, _io->writeEvent.get(), _io->writeOverlapped);
        if (!readDone || !writeDone)
        {
            // The driver still owns an OVERLAPPED and buffer. The exceptional backing block is intentionally
            // retained until process exit; the host also tombstones a lane that itself fails to return.
            (void)_io.release();
        }
    }
    _io.reset();
    _info = HidCollectionInfo{};
    _disconnected = false;
}

const HidCollectionInfo& WindowsHidPort::Info() const noexcept
{
    return _info;
}

HANDLE WindowsHidPort::ReadEvent() const noexcept
{
    return _io ? _io->readEvent.get() : nullptr;
}

void WindowsHidPort::NoteFailure(DWORD error) noexcept
{
    if (IsDeviceGone(error))
    {
        _disconnected = true;
    }
}

HRESULT WindowsHidPort::ArmRead() noexcept
{
    if (!_io || !_io->handle || _io->readArmed || _info.inputReportBytes == 0)
    {
        return _io && _io->handle ? S_OK : E_UNEXPECTED;
    }
    _io->readOverlapped = OVERLAPPED{};
    _io->readOverlapped.hEvent = _io->readEvent.get();
    ResetEvent(_io->readEvent.get());
    if (!ReadFile(_io->handle.get(), _io->readBuffer.data(), _info.inputReportBytes, nullptr, &_io->readOverlapped))
    {
        const DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING)
        {
            NoteFailure(error);
            return HRESULT_FROM_WIN32(error);
        }
    }
    _io->readArmed = true;
    return S_OK;
}

HRESULT WindowsHidPort::TakeReport(uint8_t* buffer, uint32_t capacity, uint32_t& bytes) noexcept
{
    bytes = 0;
    if (!buffer || capacity == 0)
    {
        return E_POINTER;
    }
    if (!_io || !_io->handle || _disconnected)
    {
        return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    }
    if (_info.inputReportBytes == 0)
    {
        return S_FALSE;
    }
    if (!_io->readArmed)
    {
        const HRESULT armed = ArmRead();
        if (FAILED(armed))
        {
            return armed;
        }
    }
    if (WaitForSingleObject(_io->readEvent.get(), 0) != WAIT_OBJECT_0)
    {
        return S_FALSE;
    }
    DWORD transferred = 0;
    _io->readArmed = false;
    if (!GetOverlappedResult(_io->handle.get(), &_io->readOverlapped, &transferred, FALSE))
    {
        const DWORD error = GetLastError();
        NoteFailure(error);
        return HRESULT_FROM_WIN32(error);
    }
    const uint32_t copied =
        std::min<uint32_t>(std::min<uint32_t>(transferred, capacity), static_cast<uint32_t>(_io->readBuffer.size()));
    std::memcpy(buffer, _io->readBuffer.data(), copied);
    bytes = copied;
    const HRESULT rearmed = ArmRead();
    return FAILED(rearmed) ? rearmed : S_OK;
}

HRESULT WindowsHidPort::Write(const uint8_t* report, uint32_t bytes, HANDLE stopEvent,
                              uint32_t timeoutMilliseconds) noexcept
{
    if (!report || bytes == 0)
    {
        return E_POINTER;
    }
    if (!_io || !_io->handle || _disconnected)
    {
        return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    }
    if (bytes > _info.outputReportBytes || _info.outputReportBytes > _io->writeBuffer.size())
    {
        return E_INVALIDARG;
    }
    // The HID class driver requires exactly OutputReportByteLength bytes; shorter reports are zero padded.
    if (_io->writeArmed)
    {
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
    std::memset(_io->writeBuffer.data(), 0, _info.outputReportBytes);
    std::memcpy(_io->writeBuffer.data(), report, bytes);
    _io->writeOverlapped = OVERLAPPED{};
    _io->writeOverlapped.hEvent = _io->writeEvent.get();
    ResetEvent(_io->writeEvent.get());
    if (!WriteFile(_io->handle.get(), _io->writeBuffer.data(), _info.outputReportBytes, nullptr, &_io->writeOverlapped))
    {
        const DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING)
        {
            NoteFailure(error);
            return HRESULT_FROM_WIN32(error);
        }
    }
    _io->writeArmed = true;
    HANDLE handles[2] = {stopEvent, _io->writeEvent.get()};
    const DWORD handleCount = stopEvent ? 2U : 1U;
    const DWORD waited =
        WaitForMultipleObjects(handleCount, stopEvent ? handles : &handles[1], FALSE, timeoutMilliseconds);
    const bool completed = stopEvent ? waited == WAIT_OBJECT_0 + 1 : waited == WAIT_OBJECT_0;
    DWORD transferred = 0;
    if (!completed)
    {
        (void)CancelIoEx(_io->handle.get(), &_io->writeOverlapped);
        if (WaitForSingleObject(_io->writeEvent.get(), 100) == WAIT_OBJECT_0)
        {
            (void)GetOverlappedResult(_io->handle.get(), &_io->writeOverlapped, &transferred, FALSE);
            _io->writeArmed = false;
        }
        else
        {
            _disconnected = true;
        }
        return HRESULT_FROM_WIN32(waited == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_CANCELLED);
    }
    _io->writeArmed = false;
    if (!GetOverlappedResult(_io->handle.get(), &_io->writeOverlapped, &transferred, FALSE))
    {
        const DWORD error = GetLastError();
        NoteFailure(error);
        return HRESULT_FROM_WIN32(error);
    }
    return S_OK;
}

HRESULT WindowsHidPort::SetFeature(const uint8_t* report, uint32_t bytes) noexcept
{
    if (!report || bytes == 0)
    {
        return E_POINTER;
    }
    if (!_io || !_io->handle || _disconnected)
    {
        return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    }
    if (_info.featureReportBytes == 0 || bytes > _info.featureReportBytes ||
        _info.featureReportBytes > _io->writeBuffer.size())
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    std::array<uint8_t, kMaximumHidReportBytes> padded{};
    std::memcpy(padded.data(), report, bytes);
    if (!HidD_SetFeature(_io->handle.get(), padded.data(), _info.featureReportBytes))
    {
        const DWORD error = GetLastError();
        NoteFailure(error);
        return HRESULT_FROM_WIN32(error);
    }
    return S_OK;
}

bool WindowsHidPort::Disconnected() const noexcept
{
    return _disconnected || !_io || !_io->handle;
}

void WindowsHidPort::Cancel() noexcept
{
    if (_io && _io->handle)
    {
        (void)CancelIoEx(_io->handle.get(), nullptr);
    }
}

HotplugWatcher::~HotplugWatcher()
{
    Stop();
}

DWORD CALLBACK HotplugWatcher::Notify(HCMNOTIFICATION, void* context, CM_NOTIFY_ACTION action, CM_NOTIFY_EVENT_DATA*,
                                      DWORD) noexcept
{
    auto* watcher = static_cast<HotplugWatcher*>(context);
    if (watcher && watcher->_wakeEvent &&
        (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL || action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL))
    {
        watcher->_changes.fetch_add(1, std::memory_order_release);
        SetEvent(watcher->_wakeEvent);
    }
    return ERROR_SUCCESS;
}

bool HotplugWatcher::TakeChange() noexcept
{
    return _changes.exchange(0, std::memory_order_acq_rel) != 0;
}

HRESULT HotplugWatcher::Start(HANDLE wakeEvent) noexcept
{
    if (!wakeEvent)
    {
        return E_POINTER;
    }
    if (_notification)
    {
        return S_OK;
    }
    _wakeEvent = wakeEvent;
    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_HID;
    const CONFIGRET result = CM_Register_Notification(&filter, this, &HotplugWatcher::Notify, &_notification);
    if (result != CR_SUCCESS)
    {
        _notification = nullptr;
        _wakeEvent = nullptr;
        return HRESULT_FROM_WIN32(CM_MapCrToWin32Err(result, ERROR_GEN_FAILURE));
    }
    return S_OK;
}

void HotplugWatcher::Stop() noexcept
{
    if (_notification)
    {
        // Blocks until in-flight callbacks return; never called from the callback itself.
        (void)CM_Unregister_Notification(_notification);
        _notification = nullptr;
    }
    _wakeEvent = nullptr;
}

bool HotplugWatcher::Running() const noexcept
{
    return _notification != nullptr;
}
} // namespace Logicon
