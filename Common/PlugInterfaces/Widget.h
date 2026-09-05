#pragma once

#include <unknwn.h>
#include <windows.h>

#include <UIAutomationCore.h>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <dxgiformat.h>

// Every sizeBytes field must equal the current record's sizeof value. Each record below is pinned with a size
// assertion, and every record carrying a pointer is pinned with offset assertions, so a layout change that preserves
// size cannot pass the runtime sizeBytes guard unnoticed.

// Scheduling behavior advertised by a widget type.
enum RedXeWidgetFlags : uint32_t
{
    RedXeWidgetFlagNone = 0,
    RedXeWidgetFlagContinuousAnimation = 1U << 0U,
};

// Module-owned metadata for one creatable widget type.
struct RedXeWidgetTypeDescriptor final
{
    uint32_t sizeBytes;
    const char* typeId;
    const wchar_t* displayName;
    const wchar_t* description;
    float defaultWidth;
    float defaultHeight;
    float minimumWidth;
    float minimumHeight;
    uint32_t flags;
};

static_assert(sizeof(RedXeWidgetTypeDescriptor) == 56);
static_assert(offsetof(RedXeWidgetTypeDescriptor, typeId) == 8);
static_assert(offsetof(RedXeWidgetTypeDescriptor, displayName) == 16);
static_assert(offsetof(RedXeWidgetTypeDescriptor, description) == 24);
static_assert(offsetof(RedXeWidgetTypeDescriptor, flags) == 48);

// Borrowed pixel size and timing for one widget render call.
struct RedXeWidgetFrameContext final
{
    uint32_t sizeBytes;
    uint32_t widthPixels;
    uint32_t heightPixels;
    uint32_t dpi;
    float elapsedSeconds;
    float deltaSeconds;
};

static_assert(sizeof(RedXeWidgetFrameContext) == 24);

// Borrowed D3D11 device details supplied during device-resource creation.
struct RedXeGpuDeviceContext final
{
    uint32_t sizeBytes;
    ID3D11Device* device;
    DXGI_FORMAT targetFormat;
    D3D_FEATURE_LEVEL featureLevel;
};

static_assert(sizeof(RedXeGpuDeviceContext) == 24);
static_assert(offsetof(RedXeGpuDeviceContext, device) == 8);
static_assert(offsetof(RedXeGpuDeviceContext, targetFormat) == 16);

// Borrowed D3D11 state supplied for one widget render call.
//
// viewport is the widget's full design-canvas placement, including during a page swipe. TopLeftX/Y MAY be negative
// and Width/Height MAY extend past the render target; they MUST NOT be treated as invalid. widget->widthPixels and
// widget->heightPixels stay that full size so layout does not reflow as the tile slides. Direct3D clips to the
// target. A position-only change does not call OnTargetSizeChanged.
struct RedXeGpuFrameContext final
{
    uint32_t sizeBytes;
    const RedXeWidgetFrameContext* widget;
    ID3D11DeviceContext* deviceContext;
    D3D11_VIEWPORT viewport;
    // 0 is the ordinary tile, 1 is the separately prepared raised view. Position/animation does not change identity.
    uint32_t viewId = 0;
};

static_assert(sizeof(RedXeGpuFrameContext) == 56);
static_assert(offsetof(RedXeGpuFrameContext, viewId) == 48);

inline constexpr uint32_t RedXeAppearanceDark = 1U;
inline constexpr uint32_t RedXeAppearanceHighContrast = 2U;
// Cached by the host on Windows appearance notifications, never queried from a rendering callback. Colors are
// opaque ARGB (not COLORREF). A copied value is valid until the next preparation; no HWND or theme object crosses.
struct RedXeAppearance final
{
    uint32_t flags = RedXeAppearanceDark;
    uint32_t window = 0xff000000, windowText = 0xffffffff;
    uint32_t highlight = 0xffffff00, highlightText = 0xff000000;
    uint32_t button = 0xff000000, buttonText = 0xffffffff, disabledText = 0xff808080;
    bool operator==(const RedXeAppearance&) const noexcept = default;
};
static_assert(sizeof(RedXeAppearance) == 32);

// Final physical extents for independently prepared tile/raised layouts. A zero raised extent means no raised view.
struct RedXeGpuPreparationContext final
{
    uint32_t sizeBytes;
    uint32_t dpi;
    uint32_t tileWidthPixels;
    uint32_t tileHeightPixels;
    uint32_t raisedWidthPixels;
    uint32_t raisedHeightPixels;
    RedXeAppearance appearance;
};
static_assert(sizeof(RedXeGpuPreparationContext) == 56);

// Borrowed target size supplied when the host's viewport for a GPU widget changes.
//
// This reports the LARGEST viewport the host will draw this widget at in the current composition. A raised widget is
// drawn twice in one frame -- once at its tile and once at the overlay slice -- so a single size must cover both, and
// the larger one is the one worth sizing resources for; the smaller draw is a minification the sampler handles.
struct RedXeGpuTargetSizeContext final
{
    uint32_t sizeBytes;
    uint32_t widthPixels;
    uint32_t heightPixels;
    uint32_t dpi;
};

static_assert(sizeof(RedXeGpuTargetSizeContext) == 16);
static_assert(offsetof(RedXeGpuFrameContext, widget) == 8);
static_assert(offsetof(RedXeGpuFrameContext, deviceContext) == 16);
static_assert(offsetof(RedXeGpuFrameContext, viewport) == 24);

// Borrowed host child-window details supplied during attachment.
struct RedXeWindowWidgetAttachContext final
{
    uint32_t sizeBytes;
    HWND container;
    uint32_t widthPixels;
    uint32_t heightPixels;
    uint32_t dpi;
};

static_assert(sizeof(RedXeWindowWidgetAttachContext) == 32);
static_assert(offsetof(RedXeWindowWidgetAttachContext, container) == 8);
static_assert(offsetof(RedXeWindowWidgetAttachContext, widthPixels) == 16);

// Borrowed pixel size and DPI supplied after container resize.
struct RedXeWindowWidgetSizeContext final
{
    uint32_t sizeBytes;
    uint32_t widthPixels;
    uint32_t heightPixels;
    uint32_t dpi;
};

static_assert(sizeof(RedXeWindowWidgetSizeContext) == 16);

// Borrowed pointer sample forwarded by the host to a GPU interactive widget. Coordinates are widget-local pixels
// with origin at the tile's top-left, including while the tile is raised.
enum RedXePointerKind : uint32_t
{
    RedXePointerKindMouse = 0,
    RedXePointerKindTouch = 1,
    RedXePointerKindPen = 2,
};

enum RedXePointerPhase : uint32_t
{
    RedXePointerPhaseDown = 0,
    RedXePointerPhaseMove = 1,
    RedXePointerPhaseUp = 2,
    RedXePointerPhaseCancel = 3,
    // Independent vertical wheel sample. It neither starts nor ends gesture capture. Unhandled samples return S_FALSE.
    RedXePointerPhaseWheel = 4,
};

struct RedXePointerEvent final
{
    uint32_t sizeBytes;
    uint32_t pointerId;
    uint32_t kind;
    uint32_t phase;
    float x;
    float y;
    uint32_t viewId = 0;
    uint32_t widthPixels = 0;
    uint32_t heightPixels = 0;
    uint32_t dpi = 96;
    uint32_t modifiers = 0;
    float wheelDelta = 0; // Win32 wheel units: one detent is WHEEL_DELTA (120); high-resolution fractions are retained.
};

static_assert(sizeof(RedXePointerEvent) == 48);
static_assert(offsetof(RedXePointerEvent, viewId) == 24);
static_assert(offsetof(RedXePointerEvent, wheelDelta) == 44);

// An interactive widget returns this only for a handled Down that must own the entire gesture (for example a
// slider). The host captures the mouse/touch/pen and suppresses page/edge navigation until Up or Cancel. S_OK
// preserves the existing click-with-possible-page-pan behavior. Failure to acquire OS capture sends Cancel.
inline constexpr HRESULT RedXePointerCapture = MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_ITF, 0x301);
// Committed Up may request generic raise/dismiss after the host releases gesture capture. These are consumed
// results, never double-activate candidates. The normal advertised extent and host bounds still govern raising.
inline constexpr HRESULT RedXePointerRaise = MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_ITF, 0x302);
inline constexpr HRESULT RedXePointerDismiss = MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_ITF, 0x303);

// One dropped filesystem path or full URL. target is borrowed UTF-16 for this call only.
struct RedXeDropItem final
{
    uint32_t sizeBytes;
    uint32_t reserved;
    const wchar_t* target;
};

static_assert(sizeof(RedXeDropItem) == 16);
static_assert(offsetof(RedXeDropItem, target) == 8);

struct RedXeDropEvent final
{
    uint32_t sizeBytes;
    float x;
    float y;
    uint32_t itemCount;
    const RedXeDropItem* items;
};

static_assert(sizeof(RedXeDropEvent) == 24);
static_assert(offsetof(RedXeDropEvent, items) == 16);

inline constexpr uint32_t kRedXeMaximumScheduledFrameDelayMilliseconds = 86'400'000U;
inline constexpr uint32_t kRedXeMaximumDropItems = 8;

// A bounded unit of local device work. The host owns the single lazy worker and retains this COM object through
// completion. Run executes on its MTA worker; Complete executes on the UI thread outside Render and input dispatch.
// Run MUST honor cancellation and finish within 3 seconds. Potentially hanging device/driver calls belong in an
// owned, terminable helper process, never directly in this worker. No Direct3D, persistence or UI mutation in Run.
// Complete may publish model state, request a frame and queue another unit. No Complete is delivered during shutdown.
interface __declspec(uuid("B44CFD33-5DE8-4A37-92B1-D5663AFA2181")) __declspec(novtable) IRedXeControlWork : IUnknown
{
    // timeoutMilliseconds is the remaining part of the three-second enqueue-to-result budget, at least 1.
    virtual HRESULT STDMETHODCALLTYPE Run(HANDLE cancelEvent, uint32_t timeoutMilliseconds) noexcept = 0;
    virtual void STDMETHODCALLTYPE Complete(HRESULT result) noexcept = 0;
};

// Fraction of the client rectangle a widget requests when the host raises it.
enum RedXeRaisedExtent : uint32_t
{
    RedXeRaisedExtentQuarter = 1,
    RedXeRaisedExtentThird = 2,
    RedXeRaisedExtentHalf = 3,
    RedXeRaisedExtentFull = 4,
};

// Generic widget identity and visibility. Rendering mechanisms are sibling interfaces on the same object, never
// bases of this one, so adding a mechanism never changes an existing vtable.
//
// A widget object exposes IRedXeWidget plus each mechanism it supports, and QueryInterface(IID_IUnknown) MUST return
// the same controlling IUnknown pointer from every one of them. RedXeComObject in FactoryImpl.h provides that.
//
// Threading: every call on this interface and on IRedXeWindowWidget, IRedXeScheduledWidget, IRedXeRaisedWidget,
// and IRedXeInteractiveWidget runs synchronously on the RedXe UI thread and is non-reentrant. A widget MUST NOT
// call back into the host from inside one of these calls, except IRedXeHost::RequestFrame and IRedXeHost::Log.
// RequestFrame, Log, and IRedXeHost::PersistWidgetSettings MAY also be called from OnPointer (committed click) and
// OnDrop. Log MUST NOT be called from Render or GDI paint and MUST NOT emit per-frame success.
// CollectPersistentSettings is invoked by the host after SetVisible(FALSE) and before Detach; the widget answers
// with JSON or S_FALSE (nothing to save) and MUST NOT write the settings file or call PersistWidgetSettings.
// IRedXeNetworkWidget::RunNetworkWork runs on the host network worker, never on the UI thread.
//
// RedXe is pre-production: this vtable MAY grow. Rebuild every source-coordinated consumer together.
interface __declspec(uuid("62DB9FB4-AF7B-47C0-BBF9-B7D5CA535502")) __declspec(novtable) IRedXeWidget : IUnknown
{
    // Enables visible work. Widgets are created invisible. This call is synchronous and idempotent.
    //
    // FALSE MUST quiesce every animation, timer, data subscription, and other visibility-dependent activity the
    // widget owns before returning. TRUE may resume only what visible content requires. The host enables visibility
    // only after the selected rendering mechanism is ready, and disables it before that mechanism is detached.
    // Network work is not widget-owned: the host cancels and drains IRedXeNetworkWidget::RunNetworkWork before it
    // calls SetVisible(FALSE), so this callback MUST NOT wait on the network worker or re-enter PluginHost.
    virtual HRESULT STDMETHODCALLTYPE SetVisible(BOOL visible) noexcept = 0;

    // Host-owned buffer. S_FALSE means nothing to save (writtenBytes is 0). S_OK writes a complete settings object
    // or a mergeable subset; writtenBytes is the JSON length excluding a terminator. A null writtenBytes returns
    // E_POINTER. The widget MUST NOT persist from inside this call.
    virtual HRESULT STDMETHODCALLTYPE CollectPersistentSettings(char* jsonUtf8, uint32_t capacityBytes,
                                                                uint32_t* writtenBytes) noexcept = 0;
};

inline HRESULT RedXeCollectNoPersistentSettings(char* jsonUtf8, uint32_t capacityBytes, uint32_t* writtenBytes) noexcept
{
    if (writtenBytes)
    {
        *writtenBytes = 0;
    }
    if (!writtenBytes)
    {
        return E_POINTER;
    }
    (void)jsonUtf8;
    (void)capacityBytes;
    return S_FALSE;
}

// Enumerates widget types and creates independent widget instances.
interface __declspec(uuid("231AC0E8-1204-4BFF-BCEA-7CACF11F439D")) __declspec(novtable) IRedXeWidgetProvider : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetWidgetTypes(const RedXeWidgetTypeDescriptor** descriptors,
                                                     uint32_t* count) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWidget(const char* typeId, const char* instanceId,
                                                   IRedXeWidget** widget) noexcept = 0;
};

// Direct3D 11 rendering mechanism.
//
// RESOLUTION-DEPENDENT RESOURCES. Render is on the steady path and MUST NOT rasterize glyphs, build textures, or
// allocate. OnTargetSizeChanged, or the separate optional IRedXePreparedGpuWidget::Prepare phase, owns that work:
// size notification is called off the steady path whenever the host's
// viewport for this widget changes, so a widget whose resources depend on how large it is drawn -- a glyph atlas
// above all -- can rebuild them at the right resolution instead of being frozen at whatever size existed when the
// device was created.
//
// PIPELINE STATE CONTRACT. Before every Render call the host binds exactly two things: its render target through
// OMSetRenderTargets, and this widget's viewport through RSSetViewports. Nothing else is reset between widgets.
// Blend, depth-stencil, rasterizer state, the scissor rectangle and ScissorEnable, input layout, primitive topology,
// shaders, shader resource views, samplers, and constant buffers all carry over from whichever widget drew last.
//
// Therefore a widget MUST bind every state it depends on, including scissor state, and MUST NOT rely on any state it
// did not set itself. Leaving unusual state behind is legal but hostile: it will surface as a rendering defect in a
// sibling widget that depends on tile ordering, so prefer restoring anything exotic. Debug builds of the host verify
// a bounded subset of this between widgets.
//
// The host owns the device, immediate context, swap chain, render target, viewport placement, device-loss sequence,
// and presentation. A widget never receives the swap chain, back buffer, or top-level HWND, and MUST NOT retain the
// immediate context or any frame record past the call that supplied it, present, or resize the swap chain.
interface __declspec(uuid("355C7084-286B-409F-9FD3-A7695DEF2A33")) __declspec(novtable) IRedXeGpuWidget : IUnknown
{
    // Supplies the borrowed device, target format, and feature level. The widget may create and retain its own device
    // resources here and may share immutable resources across instances. The host hides and drains widget work
    // before device setup/teardown and restores visibility only after successful setup.
    virtual HRESULT STDMETHODCALLTYPE OnDeviceCreated(const RedXeGpuDeviceContext* context) noexcept = 0;
    // Idempotent. MUST release every plugin-owned device resource before the host releases its device.
    virtual void STDMETHODCALLTYPE OnDeviceLost() noexcept = 0;
    // Reports the largest viewport the host will draw this widget at. Called on the RedXe UI thread, synchronously
    // and non-reentrantly, after OnDeviceCreated and before the first Render, and again whenever size or DPI changes:
    // resize, DPI change, layout change, and raise or dismiss. It is NOT called for a position-only change such as a
    // page-swipe offset, and never once per frame. A DPI-only change still notifies. The widget may also draw at a
    // smaller tile or animated overlay size: its geometry and hit targets must follow the actual Render dimensions.
    //
    // Along with device creation and optional IRedXePreparedGpuWidget::Prepare, this callback may rasterize,
    // create textures, or allocate. A widget with no
    // resolution-dependent resources returns S_OK and does nothing. A failure is isolated: the host keeps the
    // widget's previous resources and continues rendering.
    virtual HRESULT STDMETHODCALLTYPE OnTargetSizeChanged(const RedXeGpuTargetSizeContext* context) noexcept = 0;
    // Draws one frame inside the supplied viewport. Runs on the RedXe UI thread, non-reentrant. The viewport may sit
    // partly off the render target; the widget MUST still draw its full composition and MUST NOT reject a finite
    // negative origin. MUST NOT call IRedXeHost::Log.
    virtual HRESULT STDMETHODCALLTYPE Render(const RedXeGpuFrameContext* context) noexcept = 0;
};

// Optional preparation phase for retained UI. The host calls this before frame construction, with final layout
// extents, on the UI thread. Dirty controls request a coalesced frame through IRedXeHost::RequestFrame. A clean call
// returns S_FALSE without allocation, layout or rasterization. Idle/hidden hosts do not invoke this callback.
// Changed preparation MAY allocate/rasterize within bounded resource budgets; Render remains allocation-free.
// Requests raised during preparation remain pending for a later frame. Failed preparation disables affected
// input/composition until a later explicit request or extent change succeeds; no automatic retry loop is allowed.
interface __declspec(uuid("D3E26E68-1F73-47DA-9542-0A44BAE90251")) __declspec(novtable) IRedXePreparedGpuWidget
    : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Prepare(const RedXeGpuPreparationContext* context) noexcept = 0;
};

// Optional low-cadence frame deadline mechanism, for content that changes on a schedule the widget knows in advance,
// such as the next second or minute boundary. For a change the widget cannot predict, call IRedXeHost::RequestFrame
// instead of returning a short delay purely to be polled.
interface __declspec(uuid("1B6B4F9E-5421-4B4E-BC2D-190EE6CE86CB")) __declspec(novtable) IRedXeScheduledWidget : IUnknown
{
    // S_OK writes a relative delay from 1 through kRedXeMaximumScheduledFrameDelayMilliseconds. S_FALSE requests no
    // deadline. A null output returns E_POINTER; a present output is cleared first.
    //
    // The host calls this after a successful static frame, on the UI thread. The implementation MUST NOT allocate,
    // perform I/O, wait, synchronize, touch the device, or re-enter the host.
    virtual HRESULT STDMETHODCALLTYPE GetNextFrameDelayMilliseconds(uint32_t* delayMilliseconds) noexcept = 0;
};

// Native child-window rendering mechanism. The host owns one WS_CHILD container per selected window widget and
// lends it to the plugin. The plugin never receives the top-level HWND.
//
// A widget that exposes both this and IRedXeGpuWidget gets the GPU mechanism; host policy chooses without changing
// this header. Native children compose above the parent's Direct3D surface and cannot be alpha-composited with GPU
// widgets.
interface __declspec(uuid("3219FA78-260B-416A-BB76-6331DBF30593")) __declspec(novtable) IRedXeWindowWidget : IUnknown
{
    // Creates plugin content for the supplied physical pixel size and DPI. The container HWND may be retained only
    // from a successful Attach until Detach begins. The plugin MUST NOT subclass, destroy, reparent, or restyle it.
    // A failed call MUST leave the widget detached.
    virtual HRESULT STDMETHODCALLTYPE Attach(const RedXeWindowWidgetAttachContext* context) noexcept = 0;
    // Reports the new physical container size and destination-monitor DPI after the host has repositioned the
    // container. It MUST perform no work when size and DPI are unchanged.
    virtual HRESULT STDMETHODCALLTYPE Resize(const RedXeWindowWidgetSizeContext* context) noexcept = 0;
    // Before this returns, the plugin MUST stop every callback and timer, destroy every child HWND it created, and
    // release every resource it created inside the container. Repeated Detach is safe.
    virtual void STDMETHODCALLTYPE Detach() noexcept = 0;
};

// Optional raised-overlay mechanism. The host asks for the extent before raising a tile that is not already
// full-client; it never invents one.
interface __declspec(uuid("A7E4C19B-2F58-4D13-9C6A-80B1D4E7F203")) __declspec(novtable) IRedXeRaisedWidget : IUnknown
{
    // Writes exactly one RedXeRaisedExtent value. A null output returns E_POINTER. Any other value, a failed query,
    // or a missing interface leaves the tile in standard layout.
    virtual HRESULT STDMETHODCALLTYPE GetRaisedExtent(RedXeRaisedExtent * extent) noexcept = 0;
    // Synchronous, idempotent, on the RedXe UI thread. TRUE means the widget now occupies the larger overlay content
    // rectangle and should present more of its content; FALSE restores compact tile presentation. The call MUST NOT
    // allocate, wait, or re-enter the host.
    virtual HRESULT STDMETHODCALLTYPE SetRaised(BOOL raised) noexcept = 0;
};

// Optional pointer and OLE-drop mechanism for GPU tiles that have no child HWND. The host hit-tests, converts to
// widget-local pixels, and parses OLE formats; the plugin never sees IDataObject or the top-level HWND.
//
// OnPointer returns S_OK when the contact is consumed (for example a click on an icon) and S_FALSE on a miss
// (padding or empty cell). A consumed Down/Up MUST NOT count toward double-activate raise. Page pan that locks
// horizontal sends Cancel and does not launch. Edge-band clicks never reach the widget.
interface __declspec(uuid("E4C2A91B-7D3E-4F18-B6A5-2C9D8E0F1744")) __declspec(novtable) IRedXeInteractiveWidget
    : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE OnPointer(const RedXePointerEvent* event) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE OnDragOver(float x, float y) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE OnDragLeave() noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE OnDrop(const RedXeDropEvent* event) noexcept = 0;
};

struct RedXeKeyEvent final
{
    uint32_t sizeBytes;
    uint32_t virtualKey;
    uint32_t modifiers; // MK_SHIFT / MK_CONTROL; bit 0x20 denotes Alt.
    uint32_t viewId;
    BOOL down;
};
static_assert(sizeof(RedXeKeyEvent) == 20);
inline constexpr HRESULT RedXeKeyboardBoundary = MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_ITF, 0x304);

// Optional keyboard mechanism for GPU controls. UI-thread only. No HWND is exposed. A handled key/character
// returns S_OK; S_FALSE leaves host shortcuts available. Committed activation may return PointerRaise/Dismiss.
// Tab/Shift+Tab may return KeyboardBoundary to move to another widget; temporary editors trap their own Tab order.
// Settings persistence and local-work enqueue are allowed for committed keyboard activation, as for pointer Up.
interface __declspec(uuid("62B1D6F0-D3DB-48B1-9E17-8A4B0397492C")) __declspec(novtable) IRedXeKeyboardWidget : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetKeyboardFocus(BOOL focused, uint32_t viewId) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE OnKey(const RedXeKeyEvent* event) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE OnCharacter(uint32_t character, uint32_t modifiers, uint32_t viewId) noexcept = 0;
};

inline constexpr uint32_t kRedXeMaximumTextUnits = 4096;
inline constexpr uint32_t kRedXeMaximumTextClauses = 256;
inline constexpr uint32_t kRedXeNoTextIndex = UINT32_MAX;
enum RedXeTextFlags : uint32_t
{
    RedXeTextReadOnly = 1U << 0,
    RedXeTextMasked = 1U << 1,
    RedXeTextMultiline = 1U << 2,
    RedXeTextComposing = 1U << 3,
    RedXeTextCaretBounds = 1U << 4,
    RedXeTextViewportBounds = 1U << 5,
};
enum RedXeTextAction : uint32_t
{
    RedXeTextPreview,
    RedXeTextCommit,
    RedXeTextCancel,
};
struct RedXeTextRectangle final
{
    float left = 0, top = 0, right = 0, bottom = 0;
};
static_assert(sizeof(RedXeTextRectangle) == 16);

// Owned, bounded UTF-16 snapshot. Text is length-delimited, without a required terminator. All indexes count
// UTF-16 units; optional indexes use NoTextIndex. Geometry is widget-local physical pixels, including scroll.
// revision is opaque and belongs to this widget/view/attachment/focused control. Any intervening invalidation
// can reject an edit; read again after each accepted edit. focusId remains stable during one focus session and
// changes when the control loses/reacquires focus. Never truncate a larger document into this transport.
struct RedXeTextState final
{
    uint32_t sizeBytes = sizeof(RedXeTextState);
    uint32_t flags = 0;
    uint64_t revision = 0;
    uint64_t focusId = 0;
    uint32_t textLength = 0, caret = 0, anchor = kRedXeNoTextIndex, firstVisibleLine = 0;
    uint32_t compositionStart = kRedXeNoTextIndex, compositionEnd = kRedXeNoTextIndex;
    uint32_t compositionCursor = kRedXeNoTextIndex;
    uint32_t conversionStart = kRedXeNoTextIndex, conversionEnd = kRedXeNoTextIndex, clauseCount = 0;
    RedXeTextRectangle caretBounds, viewportBounds;
    wchar_t text[kRedXeMaximumTextUnits]{};
    uint32_t clauses[kRedXeMaximumTextClauses]{};
};
static_assert(sizeof(RedXeTextState) == 9312);
static_assert(offsetof(RedXeTextState, revision) == 8);
static_assert(offsetof(RedXeTextState, focusId) == 16);
static_assert(offsetof(RedXeTextState, text) == 96);
static_assert(offsetof(RedXeTextState, clauses) == 8288);

// Optional text services for retained GPU controls. All calls are synchronous, UI-thread-only, non-reentrant,
// outside Render. Host-owned TSF/IME and clipboard adapters use this transport; no HWND, DxUi or STL object crosses
// the ABI. Pointers are borrowed only for the call. The host retains this COM interface while services are active.
// Read clears its output before returning S_FALSE for absent/hidden/unprepared focus. Apply validates the entire
// record and revision before mutation; control policies remain authoritative. Preview never notifies the model;
// Commit is one edit relative to the composition base; Cancel restores the base without committing it.
interface __declspec(uuid("87529906-149E-4A99-91B3-2A8C65BCD206")) __declspec(novtable) IRedXeTextInputWidget : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE ReadTextState(uint32_t viewId, RedXeTextState* state) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE ApplyTextState(uint32_t viewId, uint32_t action,
                                                     const RedXeTextState* state) noexcept = 0;
    // Cancel only the identified focus session's composition, preserving focus and newer external text. S_FALSE
    // means that session no longer exists. Actual focus loss is handled separately by SetKeyboardFocus(FALSE).
    virtual HRESULT STDMETHODCALLTYPE CancelTextInput(uint32_t viewId, uint64_t focusId) noexcept = 0;
    // Geometry is queried outside Render against the same revision as ReadTextState. Clear outputs on failure;
    // stale/unprepared layout returns S_FALSE. Coordinates use the same widget-local physical space as the state.
    virtual HRESULT STDMETHODCALLTYPE HitTestText(uint32_t viewId, uint64_t revision, float x, float y,
                                                  uint32_t* index) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTextRangeBounds(uint32_t viewId, uint64_t revision, uint32_t start,
                                                         uint32_t end, RedXeTextRectangle* bounds,
                                                         BOOL* clipped) noexcept = 0;
};

// Optional virtual UIA subtree for prepared GPU controls. All calls run on the RedXe COM STA, outside Render.
// Each connection has a process-unique nonzero attachmentId. Physical-screen geometry describes the displayed
// prepared pixels exactly; no HWND or DxUi C++ ownership crosses this boundary.
struct RedXeAccessibilityPlacement final
{
    uint32_t sizeBytes = sizeof(RedXeAccessibilityPlacement);
    uint32_t viewId = 0;
    uint64_t attachmentId = 0;
    double left = 0, top = 0, width = 0, height = 0;
    BOOL keyboardFocused = FALSE;
    uint32_t reserved = 0;
};
static_assert(sizeof(RedXeAccessibilityPlacement) == 56);
static_assert(offsetof(RedXeAccessibilityPlacement, attachmentId) == 8);
static_assert(offsetof(RedXeAccessibilityPlacement, left) == 16);

interface __declspec(uuid("3C430805-12D0-49B0-AAB8-7C07F3909164")) __declspec(novtable) IRedXeAccessibilitySite
    : IUnknown
{
    // Return owned references, or S_OK/null when a neighbor does not exist. Retained sites are generation-bound;
    // calls after view removal fail instead of navigating or focusing a replacement widget.
    virtual HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                               IRawElementProviderFragment * *result) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFragmentRoot(IRawElementProviderFragmentRoot * *result) noexcept = 0;
    // Coalesce UI-thread work; these callbacks must not synchronously change the plugin tree or invoke UIA.
    virtual HRESULT STDMETHODCALLTYPE RequestFocus() noexcept = 0;
    virtual void STDMETHODCALLTYPE ActionCompleted() noexcept = 0;
};

interface __declspec(uuid("B8FA0B10-EE1D-44E8-8070-A7B80EEA7D4E")) __declspec(novtable) IRedXeAccessibilityWidget
    : IUnknown
{
    // Connect only a coherent prepared view. S_FALSE means not available yet; outputs clear on failure.
    // The plugin retains site until disconnect. A new connection disconnects the old provider identity first.
    // Providers may survive widget destruction; their code must remain mapped and all later actions must fail.
    virtual HRESULT STDMETHODCALLTYPE ConnectAccessibility(const RedXeAccessibilityPlacement* placement,
                                                           IRedXeAccessibilitySite* site,
                                                           IRawElementProviderFragmentRoot** result) noexcept = 0;
    // S_OK includes unchanged placement/state. S_FALSE means reconnect is required after preparing the view.
    virtual HRESULT STDMETHODCALLTYPE UpdateAccessibility(const RedXeAccessibilityPlacement* placement) noexcept = 0;
    virtual void STDMETHODCALLTYPE DisconnectAccessibility(uint32_t viewId) noexcept = 0;
    // Called after queued ActionCompleted. Consume one committed navigation result (S_OK, PointerRaise/Dismiss).
    // Pointer/keyboard dispatch consumes its own result before returning, so it cannot be replayed here.
    virtual HRESULT STDMETHODCALLTYPE TakeAccessibilityAction(uint32_t viewId, HRESULT* action) noexcept = 0;
};

// Optional host-scheduled network work. The widget owns curl (or any HTTP client) and parsers; the host owns the
// worker, cancellation, and shutdown drain. This interface is a sibling of IRedXeWidget, never a base of it.
//
// Threading: RunNetworkWork runs only on the host network worker, never on the UI thread, and never on the local
// data-acquisition worker. It is non-reentrant per widget. Inside the call the widget MAY perform bounded HTTP and
// copy a snapshot. It MUST return promptly when cancelEvent is signaled. It MUST NOT touch Direct3D, wait on the UI
// thread, create a thread, or re-enter the host except through IRedXeHost::RequestFrame and IRedXeHost::Log.
interface __declspec(uuid("3F8C1A70-9B24-4E61-A7D2-5C0E8B4F1D93")) __declspec(novtable) IRedXeNetworkWidget : IUnknown
{
    // Executes one network cycle. cancelEvent is borrowed for the duration of the call and is never null.
    //
    // A null nextDelayMilliseconds returns E_POINTER; a present output is cleared first.
    // S_OK writes a delay from 1 through kRedXeMaximumScheduledFrameDelayMilliseconds until the next run.
    // S_FALSE requests no further run until the host activates the widget again.
    virtual HRESULT STDMETHODCALLTYPE RunNetworkWork(HANDLE cancelEvent, uint32_t* nextDelayMilliseconds) noexcept = 0;
};
