#pragma once

#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <dxgiformat.h>
#include <unknwn.h>
#include <windows.h>

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
};

static_assert(sizeof(RedXeGpuFrameContext) == 48);

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
};

struct RedXePointerEvent final
{
    uint32_t sizeBytes;
    uint32_t pointerId;
    uint32_t kind;
    uint32_t phase;
    float x;
    float y;
};

static_assert(sizeof(RedXePointerEvent) == 24);

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
// allocate. OnTargetSizeChanged is where that work belongs: it is called off the steady path whenever the host's
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
    // This is the only callback where a GPU widget may rasterize, create textures, or allocate. A widget with no
    // resolution-dependent resources returns S_OK and does nothing. A failure is isolated: the host keeps the
    // widget's previous resources and continues rendering.
    virtual HRESULT STDMETHODCALLTYPE OnTargetSizeChanged(const RedXeGpuTargetSizeContext* context) noexcept = 0;
    // Draws one frame inside the supplied viewport. Runs on the RedXe UI thread, non-reentrant. The viewport may sit
    // partly off the render target; the widget MUST still draw its full composition and MUST NOT reject a finite
    // negative origin. MUST NOT call IRedXeHost::Log.
    virtual HRESULT STDMETHODCALLTYPE Render(const RedXeGpuFrameContext* context) noexcept = 0;
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
