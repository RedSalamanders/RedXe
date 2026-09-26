#pragma once

// Windows HID transport for Logicon: collection discovery through CfgMgr32 + hid.dll, one overlapped read kept
// armed per opened collection, bounded padded writes with cancellation, and a hotplug watcher that only signals an
// event. HidPort is the seam the device session talks through so tests substitute an in-memory device.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <windows.h>

#include <cfgmgr32.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace Logicon
{
inline constexpr uint32_t kMaximumHidCollections = 4;
inline constexpr uint32_t kMaximumHidPathCharacters = 512;
inline constexpr uint32_t kMaximumHidReportBytes = 4096;

// One HID top-level collection of the keypad. Report-id masks carry one bit per id below 32; every Logicon report
// id (0x11, 0x13, 0x14, 0x03) fits.
struct HidCollectionInfo final
{
    uint16_t vendorId = 0;
    uint16_t productId = 0;
    uint16_t usagePage = 0;
    uint16_t usage = 0;
    uint32_t inputReportBytes = 0;
    uint32_t outputReportBytes = 0;
    uint32_t featureReportBytes = 0;
    uint32_t inputReportMask = 0;
    uint32_t outputReportMask = 0;
    uint32_t featureReportMask = 0;
    std::array<wchar_t, kMaximumHidPathCharacters> path{};

    [[nodiscard]] bool SupportsInput(uint8_t reportId) const noexcept
    {
        return reportId < 32 && (inputReportMask & (1U << reportId)) != 0;
    }
    [[nodiscard]] bool SupportsOutput(uint8_t reportId) const noexcept
    {
        return reportId < 32 && (outputReportMask & (1U << reportId)) != 0;
    }
    [[nodiscard]] bool SupportsFeature(uint8_t reportId) const noexcept
    {
        return reportId < 32 && (featureReportMask & (1U << reportId)) != 0;
    }
};

struct HidCollections final
{
    std::array<HidCollectionInfo, kMaximumHidCollections> items{};
    uint32_t count = 0;
};

// Lists every present HID collection of vendorId:productId on usagePage. Allocation happens only here, on the
// discovery path; the result is bounded to kMaximumHidCollections. productId 0 and usagePage 0 are wildcards
// (discovery probe only).
[[nodiscard]] HRESULT EnumerateVendorCollections(uint16_t vendorId, uint16_t productId, uint16_t usagePage,
                                                 HidCollections& collections) noexcept;
[[nodiscard]] HRESULT EnumerateVendorCollections(uint16_t vendorId, uint16_t productId, uint16_t usagePage,
                                                 HidCollectionInfo* items, uint32_t capacity, uint32_t& count) noexcept;

// Transport seam. Every method runs on the device lane; nothing blocks except Write, which honors stopEvent and its
// timeout. Reports carry the report id in byte 0.
class HidPort
{
  public:
    virtual ~HidPort() = default;
    [[nodiscard]] virtual const HidCollectionInfo& Info() const noexcept = 0;
    // Signaled while an input report is ready for TakeReport.
    [[nodiscard]] virtual HANDLE ReadEvent() const noexcept = 0;
    // Copies the next input report and re-arms the read. S_FALSE when none is ready. A device-gone failure marks
    // the port disconnected.
    [[nodiscard]] virtual HRESULT TakeReport(uint8_t* buffer, uint32_t capacity, uint32_t& bytes) noexcept = 0;
    // Writes one output report, padded to the collection's output length. Returns ERROR_CANCELLED when stopEvent
    // fires first and ERROR_TIMEOUT when the write does not complete in time; both cancel the I/O before returning.
    [[nodiscard]] virtual HRESULT Write(const uint8_t* report, uint32_t bytes, HANDLE stopEvent,
                                        uint32_t timeoutMilliseconds) noexcept = 0;
    [[nodiscard]] virtual HRESULT SetFeature(const uint8_t* report, uint32_t bytes) noexcept = 0;
    [[nodiscard]] virtual bool Disconnected() const noexcept = 0;
    // Cancels outstanding I/O so a blocked lane can return.
    virtual void Cancel() noexcept = 0;
};

class WindowsHidPort final : public HidPort
{
  public:
    WindowsHidPort() = default;
    ~WindowsHidPort() override;
    WindowsHidPort(const WindowsHidPort&) = delete;
    WindowsHidPort& operator=(const WindowsHidPort&) = delete;

    [[nodiscard]] HRESULT Open(const HidCollectionInfo& info) noexcept;
    void Close() noexcept;

    [[nodiscard]] const HidCollectionInfo& Info() const noexcept override;
    [[nodiscard]] HANDLE ReadEvent() const noexcept override;
    [[nodiscard]] HRESULT TakeReport(uint8_t* buffer, uint32_t capacity, uint32_t& bytes) noexcept override;
    [[nodiscard]] HRESULT Write(const uint8_t* report, uint32_t bytes, HANDLE stopEvent,
                                uint32_t timeoutMilliseconds) noexcept override;
    [[nodiscard]] HRESULT SetFeature(const uint8_t* report, uint32_t bytes) noexcept override;
    [[nodiscard]] bool Disconnected() const noexcept override;
    void Cancel() noexcept override;

  private:
    [[nodiscard]] HRESULT ArmRead() noexcept;
    void NoteFailure(DWORD error) noexcept;

    // Keep every buffer, OVERLAPPED, event, and file handle in one backing block. If a broken driver does not
    // complete canceled I/O within the close budget, that block is retired intact rather than freed under I/O.
    struct IoState final
    {
        wil::unique_hfile handle;
        wil::unique_event_nothrow readEvent;
        wil::unique_event_nothrow writeEvent;
        OVERLAPPED readOverlapped{};
        OVERLAPPED writeOverlapped{};
        std::array<uint8_t, kMaximumHidReportBytes> readBuffer{};
        std::array<uint8_t, kMaximumHidReportBytes> writeBuffer{};
        bool readArmed = false;
        bool writeArmed = false;
    };

    HidCollectionInfo _info{};
    std::unique_ptr<IoState> _io;
    bool _disconnected = false;
};

// Signals wakeEvent from a CfgMgr32 callback whenever a HID interface arrives or leaves. Registration and
// unregistration run on the device lane; the callback does nothing else.
class HotplugWatcher final
{
  public:
    HotplugWatcher() = default;
    ~HotplugWatcher();
    HotplugWatcher(const HotplugWatcher&) = delete;
    HotplugWatcher& operator=(const HotplugWatcher&) = delete;

    [[nodiscard]] HRESULT Start(HANDLE wakeEvent) noexcept;
    void Stop() noexcept;
    [[nodiscard]] bool Running() const noexcept;
    // True once per arrival or removal seen since the previous call, so the lane re-enumerates only for those wakes.
    [[nodiscard]] bool TakeChange() noexcept;

  private:
    static DWORD CALLBACK Notify(HCMNOTIFICATION notification, void* context, CM_NOTIFY_ACTION action,
                                 CM_NOTIFY_EVENT_DATA* data, DWORD dataBytes) noexcept;

    HCMNOTIFICATION _notification = nullptr;
    HANDLE _wakeEvent = nullptr;
    std::atomic<uint32_t> _changes{0};
};
} // namespace Logicon
