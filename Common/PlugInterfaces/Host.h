#pragma once

#include "Data.h"

#include <cstddef>
#include <cstdint>
#include <unknwn.h>

// Every sizeBytes field must equal the current record's sizeof value.

// Condition a widget reports about its own content.
enum RedXeWidgetStatus : uint32_t
{
    // The widget can present its content.
    RedXeWidgetStatusOk = 0,
    // The widget is waiting for its first usable input and is expected to recover without user action.
    RedXeWidgetStatusInitializing = 1,
    // The widget is presenting reduced or stale content.
    RedXeWidgetStatusDegraded = 2,
    // The widget cannot present content. The host draws its own placeholder over the tile.
    RedXeWidgetStatusUnavailable = 3,
};

// Borrowed status report for one widget instance. The host copies every field synchronously and retains no pointer.
struct RedXeWidgetStatusReport final
{
    uint32_t sizeBytes;
    uint32_t status;
    // Optional borrowed UTF-16 reason, truncated by the host. Null means no reason text.
    const wchar_t* reason;
};

static_assert(sizeof(RedXeWidgetStatusReport) == 16);
static_assert(offsetof(RedXeWidgetStatusReport, reason) == 8);

// Services supplied to plugins by the RedXe host.
//
// Threading and reentrancy: GetDataProvider and ReportWidgetStatus are synchronous and non-reentrant, and run on the
// caller's thread. RequestFrame is the exception: it is safe from any thread, including a data-sink callback on the
// host acquisition worker. No host service may be called from inside IRedXeDataSink::OnDataSnapshot except
// RequestFrame. A plugin borrows IRedXeHost for the lifetime of the host runtime and MUST NOT retain it past the
// release of the object it was supplied to.
interface __declspec(uuid("052F039E-794D-4221-9CF2-28B9208F446F")) __declspec(novtable) IRedXeHost : IUnknown
{
    // Returns shared host-managed access to one data-source plugin.
    virtual HRESULT STDMETHODCALLTYPE GetDataProvider(const char* providerId,
                                                      IRedXeDataProvider** provider) noexcept = 0;

    // Coalesces one host frame for a widget whose own state changed. Safe from any thread, allocation-free, and never
    // blocking. It does not force a frame: hidden, minimized, suspended, display-off, and occluded hosts still block.
    // Use it instead of declaring RedXeWidgetFlagContinuousAnimation for motion that is not continuous, and instead of
    // returning a short IRedXeScheduledWidget delay purely to be polled.
    virtual HRESULT STDMETHODCALLTYPE RequestFrame() noexcept = 0;

    // Records the current condition of one widget instance. instanceId is the value the host passed to CreateWidget.
    // Reporting RedXeWidgetStatusUnavailable makes the host draw its placeholder over that tile instead of the
    // widget's own output. Repeat reports are idempotent; only a change coalesces a frame.
    virtual HRESULT STDMETHODCALLTYPE ReportWidgetStatus(const char* instanceId,
                                                         const RedXeWidgetStatusReport* report) noexcept = 0;
};
