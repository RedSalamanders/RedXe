#pragma once

#include "Data.h"

#include <cstddef>
#include <cstdint>
#include <unknwn.h>

interface IRedXeControlWork;

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

// Severity for IRedXeHost::Log. Debug is omitted from Release files.
enum RedXeLogLevel : uint32_t
{
    RedXeLogLevelError = 0,
    RedXeLogLevelWarning = 1,
    RedXeLogLevelInfo = 2,
    RedXeLogLevelDebug = 3,
};

inline constexpr uint32_t kRedXeMaximumLogEventBytes = 64;
inline constexpr uint32_t kRedXeMaximumLogMessageBytes = 384;

// Borrowed log line. The host copies every field synchronously and retains no pointer. sizeBytes must equal sizeof.
struct RedXeLogRecord final
{
    uint32_t sizeBytes;
    uint32_t level;
    // Optional machine id of the bundled plugin. Null omits the field.
    const char* pluginId;
    // Optional CreateWidget instance id. Null omits the field.
    const char* instanceId;
    // Required short event token, for example "module-map-failed".
    const char* eventId;
    // Required UTF-8 message. The host truncates to kRedXeMaximumLogMessageBytes.
    const char* messageUtf8;
    // Optional HRESULT. S_OK omits the field.
    HRESULT code;
};

static_assert(sizeof(RedXeLogRecord) == 48);
static_assert(offsetof(RedXeLogRecord, pluginId) == 8);
static_assert(offsetof(RedXeLogRecord, instanceId) == 16);
static_assert(offsetof(RedXeLogRecord, eventId) == 24);
static_assert(offsetof(RedXeLogRecord, messageUtf8) == 32);
static_assert(offsetof(RedXeLogRecord, code) == 40);

// Services supplied to plugins by the RedXe host.
//
// Threading and reentrancy: GetDataProvider and ReportWidgetStatus are synchronous and non-reentrant, and run on the
// caller's thread. RequestFrame and Log are the exceptions: they are safe from any thread, including a data-sink
// callback on the host acquisition worker and RunNetworkWork on the host network worker. No host service may be called
// from inside IRedXeDataSink::OnDataSnapshot except RequestFrame and Log. A plugin borrows IRedXeHost for the lifetime
// of the host runtime and MUST NOT retain it past the release of the object it was supplied to.
//
// RedXe is pre-production: this vtable MAY grow when a host service is added. Rebuild every source-coordinated
// consumer together. Do not add a sibling QueryInterface only to avoid growing this vtable. Widget settings persist
// is a host service on this interface (full object or a mergeable subset), not a separate editor IID.
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

    // Asks the host to persist this instance's plugin settings object (not the factory envelope). The widget may
    // send the complete object or a subset of members. The host merges supplied members into the stored instance
    // settings, validates the complete result, and may write the user document. It MUST NOT destroy or detach the
    // calling widget. A failed validation or file replacement leaves typed settings and the source document intact.
    // An older queued patch for this instance is delivered first, so it cannot later overwrite this newer edit.
    //
    // UI thread only, synchronous, non-reentrant. Forbidden from device, size, visibility, raise, Render, and
    // CollectPersistentSettings. Allowed from OnPointer, OnKey/OnCharacter (committed activation), and OnDrop. A null
    // instanceId, a null JSON pointer, or zero bytes returns E_INVALIDARG.
    virtual HRESULT STDMETHODCALLTYPE PersistWidgetSettings(const char* instanceId, const char* settingsJsonUtf8,
                                                            uint32_t settingsBytes) noexcept = 0;

    // Appends one diagnostic line to the host JSONL log. Safe from any thread, including the acquisition and network
    // workers. Copies bounded fields into a ring and never blocks on disk. MUST NOT be called from Render or GDI
    // paint. MUST NOT emit per-frame success. A null record, a mismatched sizeBytes, a missing event or message, or
    // an unknown level returns E_INVALIDARG / E_POINTER. Truncation preserves complete UTF-8, JSON, the optional
    // HRESULT, and a trailing newline; malformed UTF-8 bytes are replaced with ASCII '?'.
    virtual HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord* record) noexcept = 0;

    // UI-thread only. Retains a bounded local-work unit for the host's lazy MTA control lane (16 slots). Repeated
    // submission of the same object coalesces one rerun. S_FALSE means coalesced; ERROR_BUSY means not accepted.
    // Allowed from committed input, control completion, Prepare, and visibility changes (bounded enqueue only).
    // Never from Render/paint/device callbacks. Preparation/visibility may schedule observation or cleanup, not
    // implicit device-setting changes. User mutations require explicit committed input.
    // Completion is posted to the UI thread and never invokes the widget recursively. No work starts in self-tests.
    virtual HRESULT STDMETHODCALLTYPE QueueControlWork(IRedXeControlWork * work) noexcept = 0;
};

// Optional asynchronous settings delivery for discovered state, including imports during visibility callbacks.
// Query as a sibling of IRedXeHost.
interface __declspec(uuid("A07AB01E-67EB-466E-A7E0-9F9608539F8D")) __declspec(novtable) IRedXeSettingsQueue : IUnknown
{
    // Any thread except Render/paint/scheduling callbacks. Copies at most 4096 UTF-8 bytes plus a 127-byte instance
    // ID, queues at most eight pending instances, and posts one coalesced UI notification. S_OK means accepted,
    // not committed; validation and disk replacement run on the UI thread. Later submissions for an instance
    // replace earlier pending ones; a full queue returns ERROR_BUSY. Keep CollectPersistentSettings as fallback.
    // Does not call back into the widget. The host discards pending entries when that instance is torn down.
    virtual HRESULT STDMETHODCALLTYPE QueueWidgetSettings(const char* instanceId, const char* jsonUtf8,
                                                          uint32_t bytes) noexcept = 0;
};

inline HRESULT RedXeHostLog(IRedXeHost* host, uint32_t level, const char* pluginId, const char* instanceId,
                            const char* eventId, const char* messageUtf8, HRESULT code = S_OK) noexcept
{
    if (!host)
    {
        return E_POINTER;
    }
    const RedXeLogRecord record{sizeof(RedXeLogRecord), level, pluginId, instanceId, eventId, messageUtf8, code};
    return host->Log(&record);
}
