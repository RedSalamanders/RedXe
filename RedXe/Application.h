#pragma once

#include "DashboardHost.h"
#include "PageEdgeAffordance.h"
#include "PluginManager.h"
#include "Renderer.h"
#include "Settings.h"
#include "SettingsWatcher.h"
#include "WidgetRaise.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

class ApplicationDropTarget;
class WidgetTextClient;
class AccessibilityHost;
namespace DxUi
{
class TextInputServices;
}

class Application final
{
  public:
    // Posted by a host-owned native container when the pointer moves over it, so edge-band hover works over a
    // window widget as well as over a GPU tile. The container cannot forward WM_MOUSEMOVE directly because its
    // coordinates are in the container's client space.
    static constexpr UINT kPageEdgeHoverMessage = WM_APP + 4;

    Application(HINSTANCE instance, bool forceWarp) noexcept;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    friend class ApplicationDropTarget;

    // Production entry point: create the window, then run the frame loop until the window closes.
    int Run(int showCommand, std::wstring_view settingsPath = {}) noexcept;
    // Hidden startup validation for `--self-test`. It shares this class's startup steps but never enters the
    // frame loop, so Run itself carries no test branches.
    int RunSelfTest(std::wstring_view settingsPath = {}) noexcept;

    // Documentation capture for `--screenshot`: Run shows the dashboard normally, jumps to pageId (empty = the
    // start page) once the renderer is live, waits delayMilliseconds for widgets and services to settle, captures
    // its own window through Windows.Graphics.Capture into pngPath, and closes. ScreenshotResult reports it.
    void RequestScreenshot(std::wstring_view pngPath, std::wstring_view pageId, uint32_t delayMilliseconds,
                           uint32_t widgetOrdinal) noexcept;
    [[nodiscard]] HRESULT ScreenshotResult() const noexcept
    {
        return _screenshot.result;
    }

  private:
    static constexpr wchar_t kWindowClassName[] = L"RedXe.Window";
    static constexpr wchar_t kSettingsDialogClassName[] = L"RedXe.SettingsError";
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK SettingsDialogProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

    HRESULT RegisterWindowClass() noexcept;
    HRESULT CreateMainWindow(bool visible, const RECT* targetBounds, bool fullscreen) noexcept;
    HRESULT InitializeDashboardRuntime() noexcept;
    HRESULT ApplySettings(std::unique_ptr<AppSettings> settings) noexcept;
    void OnSettingsChanged() noexcept;
    void ShowSettingsError(std::wstring_view message) noexcept;
    void CloseSettingsError() noexcept;
    HRESULT UpdateDashboardVisibility() noexcept;
    void CloseMainWindow() noexcept;
    [[nodiscard]] bool DashboardRequiresContinuousFrames() const noexcept;
    [[nodiscard]] bool PageNavigationInProgress() const noexcept;
    [[nodiscard]] bool OverlayMotionInProgress() const noexcept;
    void ResumePageSettleIfNeeded() noexcept;
    // Pushes the current raise (dim, shadow, close, hover) and edge-band state to the renderer's host chrome and
    // invalidates one frame when it changed. Chrome lives in the swap chain; there is no chrome HWND.
    void PushHostChrome() noexcept;
    void RefreshScheduledFrameDeadline() noexcept;
    void ClearScheduledFrameDeadline() noexcept;
    bool WaitUntilMessage() noexcept;
    // Waits for a free swap-chain back buffer before a frame is built. Returns false when a message arrived first,
    // so the loop dispatches input before rendering instead of blocking inside Present.
    bool WaitForFrameLatency() noexcept;
    // Adapter-of-output: after a move, size/move end, DPI change, or display-topology change, rebuild the device
    // when the window's monitor is now scanned out by another GPU.
    void CheckDeviceAdapter() noexcept;
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT OnSize(HWND window, UINT width, UINT height) noexcept;
    LRESULT OnDpiChanged(HWND window, UINT dpi, const RECT* suggestedBounds) noexcept;
    void OnPointerDown(HWND window, WPARAM wParam, LPARAM lParam) noexcept;
    void OnPointerUpdate(HWND window, WPARAM wParam, LPARAM lParam) noexcept;
    void OnPointerUp(HWND window, WPARAM wParam, LPARAM lParam) noexcept;
    void OnMouseButtonDown(HWND window, LPARAM lParam) noexcept;
    void OnMouseButtonUp(HWND window, LPARAM lParam) noexcept;
    void OnClientActivateAttempt(HWND window, POINT position, ULONGLONG tick) noexcept;
    void OnRaisedContentActivateAttempt(HWND window, POINT position, ULONGLONG tick) noexcept;
    [[nodiscard]] bool TryNavigateFromPageEdge(HWND window, POINT position) noexcept;
    HRESULT TryRaiseWidgetAt(HWND window, size_t widgetIndex) noexcept;
    void DismissWidgetRaise(bool animate = true) noexcept;
    HRESULT ApplyRaiseVisual(const RaisedLayout& layout, BYTE dimAlpha) noexcept;
    void BeginRaiseSettle(const RaisedLayout& from, const RaisedLayout& to, BYTE dimFrom, BYTE dimTo,
                          bool dismissing) noexcept;
    void TickRaiseSettle() noexcept;
    void CompleteRaiseSettle() noexcept;
    void CompleteDismissImmediate() noexcept;
    void SetRaiseCloseHovered(bool hovered) noexcept;
    void CancelPageNavigation() noexcept;
    void ReleasePagePointerCaptures(HWND window) noexcept;
    void AdoptPageTouches(const UINT32* ids, const POINT* positions, uint32_t count, bool grabbingSettle) noexcept;
    [[nodiscard]] LONG PageTouchDeltaX() const noexcept;
    [[nodiscard]] LONG PageTouchDeltaY() const noexcept;
    [[nodiscard]] bool PageTouchesContain(UINT32 pointerId) const noexcept;
    [[nodiscard]] PageEdgeState CurrentPageEdgeState() const noexcept;
    // Client rectangle with work-area width clipping, always full client height. Edge bands hug the reachable
    // left/right so they stay pointer-reachable when the window is wider than its monitor, and they span the window
    // from top to bottom.
    [[nodiscard]] RECT ReachableClientRect() const noexcept;
    // Recomputes which edge band the pointer is over, from the live cursor position. A layered band at zero alpha is
    // click-through, so hover cannot be detected by the band itself; the top-level window owns it.
    void UpdatePageEdgeHover() noexcept;
    void ClearPageEdgeHover() noexcept;
    void RefreshPageEdgeAffordances() noexcept;
    void DestroyPageEdgeAffordances() noexcept;
    HRESULT NavigateToAdjacentPage(int direction) noexcept;
    void ApplyPageOffset(LONG offset, LONG clientWidth) noexcept;
    void FlushPendingTransitionStage() noexcept;
    void BeginPageSettle(LONG targetOffset, bool commit) noexcept;
    void TickPageSettle() noexcept;
    // Advances a `--screenshot` request from the frame loop: jump, wait, capture. Returns true once the capture
    // has run (successfully or not) so the loop closes the window.
    [[nodiscard]] bool TickScreenshot() noexcept;
    HRESULT PromoteTransitionPage() noexcept;
    // Stages the adjacent page in `direction`, or, when targetPageIndex is supplied, that page directly (a host
    // action jump); the direction then only decides which side the staged page slides in from.
    HRESULT StageTransitionPage(int direction, const uint32_t* targetPageIndex = nullptr) noexcept;
    void ClearTransitionPage() noexcept;
    [[nodiscard]] bool TryPointerClientPosition(HWND window, UINT32 pointerId, POINT& position, UINT64& qpc,
                                                LPARAM lParam) const noexcept;
    [[nodiscard]] bool PointInPageEdgeBand(HWND window, POINT position) const noexcept;
    [[nodiscard]] bool HitInteractiveLocal(POINT client, size_t& widgetIndex, float& localX,
                                           float& localY) const noexcept;
    HRESULT ForwardInteractivePointer(POINT client, uint32_t pointerId, uint32_t kind, uint32_t phase, bool* consumed,
                                      uint32_t modifiers = 0, float wheelDelta = 0) noexcept;
    void CancelInteractivePointer() noexcept;
    void ClearDoubleActivateCandidate() noexcept;
    void CompleteInteractivePointerUp(HWND window, POINT position, bool havePosition) noexcept;
    void ClearKeyboardFocus() noexcept;
    void RefreshAppearance() noexcept;
    bool FocusKeyboardWidget(size_t index) noexcept;
    bool AdvanceKeyboardWidget(bool reverse) noexcept;
    bool ForwardWidgetKey(uint32_t key, bool down) noexcept;
    bool ForwardWidgetCharacter(uint32_t character) noexcept;
    bool HandleKeyboardResult(HRESULT result) noexcept;
    void ClearTextServices() noexcept;
    void RefreshAccessibility(uint64_t preparedWidgets = 0) noexcept;
    void HandleAccessibilityRequests() noexcept;
    std::unique_ptr<AccessibilityHost> _accessibility;
    void RefreshTextServices(bool layoutPrepared = false) noexcept;
    HRESULT HandleOleDragOver(POINT client, DWORD* effect) noexcept;
    void HandleOleDragLeave() noexcept;
    HRESULT HandleOleDrop(POINT client, const wchar_t* const* targets, uint32_t count, DWORD* effect) noexcept;
    HRESULT ApplyWidgetSettingsPersist(const char* instanceId, const char* settingsJsonUtf8,
                                       uint32_t settingsBytes) noexcept;
    static HRESULT SettingsPersistThunk(void* context, const char* instanceId, const char* settingsJsonUtf8,
                                        uint32_t settingsBytes) noexcept;
    // The page, widget, and redxe action namespaces (HostActionCatalog.h), executed on the UI thread outside input
    // and render dispatch from the host-action drain or IRedXeHost::ExecuteAction. A page or widget action during
    // a swipe, raise settle, or settings error returns ERROR_BUSY / E_NOT_VALID_STATE and is dropped.
    static HRESULT HostActionThunk(void* context, const char* actionUtf8, const char* targetUtf8) noexcept;
    static void HostActionCompletedThunk(void* context) noexcept;
    HRESULT HandleHostAction(std::string_view action, std::string_view target) noexcept;
    // Shows the host's action publisher notices (namespace collisions, missing or unloadable publishers) in the
    // settings-error dialog once per change; the dashboard stays active.
    void ShowActionNotices() noexcept;
    // Slides directly to a non-adjacent page: stages it as the transition page and settles once.
    HRESULT NavigateToPage(uint32_t pageIndex) noexcept;
    // Fans the current page, raise, visibility, and busy state out to started services (IRedXeService::OnHostState).
    void PublishHostState() noexcept;
    uint32_t _shownActionNoticeGeneration = 0;

    HINSTANCE _instance = nullptr;
    wil::unique_hwnd _window;
    // Index 0 is the previous-page band on the left edge; index 1 is the next-page band on the right edge. The bands
    // are host chrome drawn into the swap chain; the top-level window owns their hover and click.
    std::array<bool, 2> _pageEdgeRevealed{};
    std::array<RECT, 2> _pageEdgeBands{};
    // Last state the bands were built for. RefreshPageEdgeAffordances is called from the frame loop, so an unchanged
    // state must perform no work at all.
    PageEdgeState _pageEdgeApplied{};
    bool _pageEdgeMouseTracking = false;
    SIZE _pageEdgeAppliedClient{};
    RECT _pageEdgeAppliedReachable{};
    UINT _pageEdgeAppliedDpi = 0;
    bool _pageEdgeApplyValid = false;
    HWND _settingsErrorDialog = nullptr;
    wil::unique_hpowernotify _displayPowerNotification;
    std::unique_ptr<PluginManager> _pluginManager;
    std::unique_ptr<DashboardHost> _dashboardHost;
    Renderer _renderer;
    SettingsStore _settingsStore;
    SettingsWatcher _settingsWatcher;
    std::unique_ptr<AppSettings> _settings;
    std::unique_ptr<AppSettings> _transitionSettings;
    std::unique_ptr<PluginManager> _transitionPluginManager;
    std::unique_ptr<DashboardHost> _transitionDashboardHost;
    struct ScreenshotRequest final
    {
        std::wstring path;
        std::string pageIdUtf8;
        uint32_t delayMilliseconds = 3000;
        // Crop to this widget ordinal on the captured page; UINT32_MAX captures the whole window.
        uint32_t widgetOrdinal = UINT32_MAX;
        bool pending = false;
        bool navigated = false;
        ULONGLONG dueTick = 0;
        HRESULT result = S_OK;
    };
    static constexpr UINT_PTR kScreenshotTimerId = 0x5C5;

    ScreenshotRequest _screenshot{};
    bool _forceWarp = false;
    bool _classRegistered = false;
    bool _rendererReady = false;
    bool _windowVisible = false;
    bool _displayPoweredOn = true;
    bool _occlusionStatusChanged = false;
    bool _frameInvalidated = true;
    ULONGLONG _scheduledFrameDeadlineTick = 0;
    HRESULT _runtimeFailure = S_OK;
    UINT64 _pagePointerQpc = 0;
    UINT64 _qpcFrequency = 0;
    float _pageVelocityPxPerSec = 0.0f;
    static constexpr uint32_t kPageSwipeMaxTouches = 3;
    struct PageSwipeTouch final
    {
        UINT32 id = 0;
        LONG startX = 0;
        LONG startY = 0;
        LONG x = 0;
        LONG y = 0;
        bool captured = false;
    };
    std::array<PageSwipeTouch, kPageSwipeMaxTouches> _pageTouches{};
    uint32_t _pageTouchCount = 0;
    LONG _pageCentroidX = 0;
    LONG _pageCurrentOffset = 0;
    bool _pagePointerActive = false;
    bool _pagePointerCaptured = false;
    bool _pagePanStarted = false;
    bool _pageGestureIgnored = false;
    bool _pageSettleActive = false;
    bool _pageSettleCommit = false;
    LONG _pageSettleStart = 0;
    LONG _pageSettleTarget = 0;
    UINT64 _pageSettleStartQpc = 0;
    UINT64 _pageSettleDurationQpc = 0;
    int _pageStagePendingDirection = 0;
    int _pageTransitionDirection = 0;
    bool _raisedActive = false;
    size_t _raisedWidgetIndex = SIZE_MAX;
    RaisedLayout _raisedLayout{};
    RECT _raiseTile{};
    SIZE _raiseTargetPixels{};
    BYTE _raiseDimAlpha = 0;
    bool _raiseSettleActive = false;
    bool _raiseDismissing = false;
    RaisedLayout _raiseSettleFrom{};
    RaisedLayout _raiseSettleTo{};
    BYTE _raiseDimFrom = 0;
    BYTE _raiseDimTo = 0;
    UINT64 _raiseSettleStartQpc = 0;
    UINT64 _raiseSettleDurationQpc = 0;
    bool _raiseCloseHovered = false;
    ULONGLONG _activateTick = 0;
    POINT _activatePoint{};
    size_t _activateWidgetIndex = SIZE_MAX;
    size_t _interactivePointerWidget = SIZE_MAX;
    bool _interactivePointerConsumed = false;
    bool _interactiveOwnsPointer = false;
    uint32_t _interactivePointerId = 0;
    uint32_t _interactivePointerKind = RedXePointerKindMouse;
    wil::com_ptr_nothrow<IRedXeKeyboardWidget> _keyboardWidget;
    std::unique_ptr<DxUi::TextInputServices> _textServices;
    std::shared_ptr<WidgetTextClient> _textClient;
    size_t _keyboardWidgetIndex = SIZE_MAX;
    uint32_t _keyboardView = 0;
    size_t _dragWidgetIndex = SIZE_MAX;
    bool _oleInitialized = false;
    bool _dropRegistered = false;
    bool _persistSettingsToDisk = true;
    std::unique_ptr<ApplicationDropTarget> _dropTarget;
};
