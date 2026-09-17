#pragma once

#include <cstddef>
#include <cstdint>
#include <unknwn.h>
#include <windows.h>

// Every sizeBytes field must equal the current record's sizeof value. Each record below is pinned with a size
// assertion, and every record carrying a pointer is pinned with offset assertions, so a layout change that preserves
// size cannot pass the runtime sizeBytes guard unnoticed.
//
// A service is a plugin object that runs without a placed widget. The host creates at most one service per
// catalogued service plugin ID through RedXeCreate(IID_IRedXeService, ...) with the compact
// {"plugin":{},"instance":<effective-settings>} envelope, exactly like a data provider, and drives its lifetime on
// the RedXe UI thread. A service exposes optional host-owned device work as the sibling IRedXeDeviceWorker.

// Flags for RedXeServiceStartContext.
enum RedXeServiceFlags : uint32_t
{
    RedXeServiceFlagNone = 0,
    // Set by --self-test and host tests: the service MUST NOT open a hardware device, register hotplug
    // notifications, or inject input. Everything else (settings parsing, model state, host actions) still runs.
    RedXeServiceFlagDeviceAccessDisabled = 1U << 0U,
};

// Borrowed start context. The host copies nothing from the service; the service copies what it needs.
struct RedXeServiceStartContext final
{
    uint32_t sizeBytes;
    uint32_t flags;
};

static_assert(sizeof(RedXeServiceStartContext) == 8);

// Flags for RedXeHostState.
enum RedXeHostStateFlags : uint32_t
{
    RedXeHostStateNone = 0,
    // A widget on the current page is raised; raisedWidgetOrdinal names it.
    RedXeHostStateRaised = 1U << 0U,
    // The dashboard window is visible and its display is powered on.
    RedXeHostStateVisible = 1U << 1U,
    // A page swipe or raise settle is in progress; page and widget actions are dropped until it completes.
    RedXeHostStateBusy = 1U << 2U,
};

// Borrowed dashboard state supplied to IRedXeService::OnHostState. Strings are valid for the duration of the call
// only. Ordinals are positions on the current page in authored order.
struct RedXeHostState final
{
    uint32_t sizeBytes;
    uint32_t flags;
    uint32_t pageIndex;
    uint32_t pageCount;
    // Borrowed UTF-8 authored or generated page id of the current page.
    const char* pageId;
    // Borrowed UTF-16 page name.
    const wchar_t* pageName;
    uint32_t widgetCount;
    uint32_t raisedWidgetOrdinal;
};

static_assert(sizeof(RedXeHostState) == 40);
static_assert(offsetof(RedXeHostState, pageId) == 16);
static_assert(offsetof(RedXeHostState, pageName) == 24);
static_assert(offsetof(RedXeHostState, widgetCount) == 32);

// Headless plugin service. Every call runs synchronously on the RedXe UI thread and is non-reentrant. A service
// MUST NOT call back into the host from inside these calls except IRedXeHost::RequestAction, RequestFrame, Log, and
// ValidateAction (from Start and ApplySettings only). Start and Stop are idempotent. A failed Start leaves the object
// created so a later ApplySettings can retry.
//
// The host calls Start after the first successful settings apply and before the first dashboard page is staged,
// ApplySettings whenever a live reload changes this service's effective settings object, OnHostState whenever the
// page, raise, visibility, or busy state changes, and Stop during shutdown after every widget is released. Stop
// MUST leave the hardware as the service found it and MUST return with no thread, timer, handle, or callback of the
// service still live.
interface __declspec(uuid("9D7C1E52-4B8A-4F6E-A1C3-7E2F5B9D0A64")) __declspec(novtable) IRedXeService : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Start(const RedXeServiceStartContext* context) noexcept = 0;
    // Complete effective settings object for this service (never the factory envelope). The service parses and
    // copies it before returning; it MUST NOT retain the borrowed pointer.
    virtual HRESULT STDMETHODCALLTYPE ApplySettings(const char* settingsJsonUtf8, uint32_t settingsBytes) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE OnHostState(const RedXeHostState* state) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop() noexcept = 0;
};

// Optional host-owned device lane. The host creates one thread per started service that exposes this interface
// (at most kRedXeMaximumDeviceWorkers process-wide), initializes it as an MTA apartment, and calls RunDeviceWork
// exactly once after Start succeeds. The call owns the thread until it returns.
//
// Inside the call the service owns device discovery, handles, overlapped I/O, and hotplug registration. It MUST
// block only in a wait on stopEvent, wakeEvent, and its own I/O events; it MUST NOT poll, sleep-loop, touch
// Direct3D, wait on the UI thread, create a thread, or re-enter the host except through RequestAction,
// RequestFrame, and Log. When stopEvent is signaled it MUST cancel outstanding I/O (CancelIoEx) and return within
// kRedXeDeviceWorkerDrainMilliseconds; the host logs an overrun once and continues shutdown. Both events are host
// owned: the service MAY signal wakeEvent from any thread while the call is running (for example after
// ApplySettings or OnHostState on the UI thread) and MUST NOT use either handle after the call returns.
interface __declspec(uuid("5A3E8C41-2D97-4B6F-9E15-C7D0F2A8B3E6")) __declspec(novtable) IRedXeDeviceWorker : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE RunDeviceWork(HANDLE stopEvent, HANDLE wakeEvent) noexcept = 0;
};

inline constexpr uint32_t kRedXeMaximumDeviceWorkers = 4;
inline constexpr uint32_t kRedXeDeviceWorkerDrainMilliseconds = 3000;
