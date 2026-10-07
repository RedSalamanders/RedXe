#pragma once

#include "DashboardHost.h"
#include "DockOptions.h"
#include "DockPlacement.h"
#include "PageEdgeAffordance.h"
#include "PluginManager.h"
#include "Renderer.h"
#include "Settings.h"
#include "SettingsWatcher.h"
#include "TrayIcon.h"
#include "WheelNavigation.h"
#include "WidgetRaise.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
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
    // App-bar callback registered with the shell for the dock window kind (ABN_POSCHANGED, ABN_FULLSCREENAPP, ...).
    static constexpr UINT kDockAppBarMessage = WM_APP + 6;
    static constexpr UINT kScreenshotCompleteMessage = WM_APP + 7;

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

    // Documentation capture for `--screenshot` and the `redxe.screenshot` action: the frame loop jumps to pageId
    // (empty = the start page) once the renderer is live, waits delayMilliseconds for widgets and services to settle,
    // and captures its own window through Windows.Graphics.Capture into pngPath. Only closeWhenDone (the command
    // line) closes the window after the capture; an action capture keeps RedXe running. ERROR_BUSY while another
    // request is pending.
    HRESULT RequestScreenshot(std::wstring_view pngPath, std::wstring_view pageId, uint32_t delayMilliseconds,
                              uint32_t widgetOrdinal, bool closeWhenDone) noexcept;
    // After Run returns for `--screenshot`: joins a capture worker the closed window left running and returns the
    // request's result, ERROR_CANCELLED when the run ended before the capture finished.
    [[nodiscard]] HRESULT FinishScreenshot() noexcept;
    // --dock* command-line overrides, pinned over the document's `dock` object for this process (DockOptions.h).
    // RunSelfTest replaces them with an edge pinned to none: the self-test keeps its hidden titled window.
    void SetDockOverrides(const DockOverrides& overrides) noexcept
    {
        _dockOverrides = overrides;
    }

  private:
    static constexpr wchar_t kWindowClassName[] = L"RedXe.Window";
    static constexpr wchar_t kSettingsDialogClassName[] = L"RedXe.SettingsError";
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK SettingsDialogProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

    HRESULT RegisterWindowClass() noexcept;
    HRESULT CreateMainWindow(bool visible, const RECT* targetBounds, bool fullscreen) noexcept;
    HRESULT RegisterDisplayPowerNotification(HWND window) noexcept;
    // Dock window kind (UI_XeneonDisplayWindowing.md "Dock window kind"): a borderless topmost tool window on one
    // edge of the monitor the effective dock selects. Placement, app-bar registration, DPI, and monitor changes are
    // owned by PlaceDock; autohide reveal and hide by the DockPlacement.h state machine.
    struct DockMonitorPlacement final
    {
        RECT monitor{};
        RECT work{};
        UINT dpi = USER_DEFAULT_SCREEN_DPI;
        bool fellBack = false;
    };
    HRESULT CreateDockWindow(bool visible) noexcept;
    [[nodiscard]] bool ResolveDockMonitor(DockMonitorPlacement& placement) noexcept;
    // Recomputes the bar rectangle for the current monitor, DPI, mode, and reveal state, registers or updates the
    // app bar, and moves the window. With resizeDashboard the dashboard and swap chain follow the full rectangle.
    HRESULT PlaceDock(bool resizeDashboard) noexcept;
    HRESULT ResizeDockDashboard() noexcept;
    void RegisterDockAppBar() noexcept;
    void UnregisterDockAppBar() noexcept;
    // TaskbarCreated: a restarted Explorer knows no app bar, so an active dock registers and places itself again.
    void OnTaskbarCreated() noexcept;
    void ApplyDockZOrder() noexcept;
    // True while the shell reports a full-screen application (ABN_FULLSCREENAPP) and the foreground window fills the
    // dock's own monitor: only then does the bar step beneath it.
    [[nodiscard]] bool DockYieldsToFullscreen() const noexcept;
    // Live reload: re-places the bar for the document's changed `dock` members, and `none` <-> an edge switches the
    // window kind. A failed switch is rolled back to the previous kind and its failure returned, so the caller rejects
    // the reload; RedXe exits only when the rollback leaves no renderer.
    HRESULT ApplyDockSettings(const DockSettings& documentDock) noexcept;
    // Live reload between `none` and an edge (UI_XeneonDisplayWindowing.md "Switching the window kind"): the same
    // window is hidden, restyled, placed as the other kind, and shown again without activation, and the swap chain is
    // rebuilt for that kind's scaling. Widgets, services, native containers, the settings watcher, and the drop target
    // stay bound to the window. A failed step leaves the window hidden; the caller rolls back to the previous kind
    // with `rollback`, which keeps the forward switch's standard-window monitor and logs no switch.
    // SwitchWindowKind = RestyleWindowKind, RebuildPresentation, FinishWindowKindSwitch; a reload that also rebuilds
    // the page runs InitializeDashboardRuntime between the two halves instead, so the renderer is created once.
    HRESULT SwitchWindowKind(const DockSettings& next, bool rollback) noexcept;
    // Ends the interactions, takes the presentation down, and hides, restyles, and places the window as the kind
    // `next` selects. The window stays hidden with no renderer until FinishWindowKindSwitch.
    HRESULT RestyleWindowKind(const DockSettings& next, bool rollback) noexcept;
    // Shows the restyled window without activation, settles the standard kind's placement, the holds, and the chrome,
    // and logs the switch unless it is a rollback. Needs the renderer of the new kind.
    HRESULT FinishWindowKindSwitch(bool rollback) noexcept;
    // The standard kind for a window that already exists, placed by the startup rows of the mode table without the
    // missing-display prompt: Release fullscreen on the XENEON's rcMonitor; the titled window at the XENEON origin;
    // without a XENEON, the titled window at the work-area origin of `fallbackMonitor`. Idempotent.
    HRESULT PlaceStandardWindow(bool fullscreen, HMONITOR fallbackMonitor) noexcept;
    // The canvas the dashboard and the swap chain use: the client, or the full bar for a dock (which may be its
    // strip at the moment), at the matching DPI.
    HRESULT PresentationCanvas(UINT& width, UINT& height, UINT& dpi) const noexcept;
    // After a window-kind switch: resizes the dashboard to the new canvas and re-initializes the renderer with that
    // kind's presentation. Widget instances and native containers stay; GPU widgets see one OnDeviceLost and
    // OnDeviceCreated pair, as on an adapter change.
    HRESULT RebuildPresentation() noexcept;
    // Ends a dock drag without committing it and kills every dock timer; the placement is left alone.
    void StopDockInteraction() noexcept;
    // Forgets the bar's placement and reveal state so the next PlaceDock starts from a revealed, unplaced bar.
    void ResetDockPlacementState() noexcept;
    // The reveal state asks for the peek strip: the bar is hidden or sliding out. Input on it asks for a reveal.
    [[nodiscard]] bool DockHiddenOrHiding() const noexcept
    {
        return _dockActive && _dock.mode == DockMode::Autohide && DockStateShowsStrip(_dockReveal);
    }
    // The window is the settled peek strip: the dashboard is not visible and the host blocks after the grip frame. A
    // bar sliding out stays visible until its slide ends.
    [[nodiscard]] bool DockHidden() const noexcept
    {
        return DockHiddenOrHiding() && !_dockSlideActive;
    }
    // Autohide slide (DockPlacement.h DockSlide*): the window's visible thickness travels between the peek strip and
    // the full bar over dock.animationMilliseconds, one SetWindowPos per presented frame, with the full-size
    // dashboard translated for top and left bars. The reveal state stays the authority; the slide only follows it.
    [[nodiscard]] LONG DockFullPixels() const noexcept;
    [[nodiscard]] LONG DockPeekPixels() const noexcept;
    // Sizes the window to `visiblePx` of the full bar and translates the dashboard while a slide runs.
    void SetDockVisiblePixels(LONG visiblePx) noexcept;
    void ApplyDockSlideOffset(POINT offset) noexcept;
    void TickDockSlide() noexcept;
    // Ends a running slide at the size the reveal state asks for; geometry-bound work (a pointer contact, a page or
    // widget change, a drag, a placement, a reload) starts from a settled bar.
    void SettleDockSlide() noexcept;
    // What follows a reveal or hide reaching its window size: focus and hover cleanup for a hidden bar, dashboard
    // visibility, host chrome (the grip), and one frame.
    void FinishDockRevealChange() noexcept;
    void OnDockEvent(DockRevealEvent event) noexcept;
    // Drag-to-resize on the bar's inner edge (DockPlacement.h `DockResizeBandRect`): the window and dashboard follow
    // the pointer live; the shell reservation and the settings file (`dock.thickness`) update on release.
    [[nodiscard]] RECT DockResizeBand() const noexcept;
    [[nodiscard]] bool PointInDockResizeBand(POINT client) const noexcept;
    void BeginDockResize(HWND window) noexcept;
    void UpdateDockResize() noexcept;
    void EndDockResize() noexcept;
    void FlushDockDashboardResize() noexcept;
    void EvaluateDockHolds() noexcept;
    [[nodiscard]] DockHolds CurrentDockHolds() const noexcept;
    void ApplyDockRevealState(DockRevealState state) noexcept;
    void ArmDockTimer(uint32_t delayMilliseconds) noexcept;
    void KillDockTimer() noexcept;
    HRESULT InitializeDashboardRuntime() noexcept;
    // Records the failure that stops the frame loop (exit code 5) as one Error JSONL record naming the site, so a
    // scripted or unattended run leaves a diagnosis behind rather than only a debugger string.
    void RecordRuntimeFailure(HRESULT result, const char* eventId) noexcept;
    HRESULT ApplySettings(std::unique_ptr<AppSettings> settings) noexcept;
    // Shows or removes the notification-area icon for the current `trayIcon`; an interactive run only
    // (UI_XeneonDisplayWindowing.md "Notification-area icon").
    void ApplyTrayIconSettings() noexcept;
    void OnTrayCommand(TrayCommand command) noexcept;
    // Opens the settings file this process watches with its default app, the shell's UI allowed.
    void EditSettingsFile() noexcept;
    // Loads and applies a changed settings file. A reload that arrives inside the titled window's move/size loop, or
    // that would rebuild the page of a minimized standard window (whose 0x0 client cannot size it), is neither
    // applied nor rejected: it waits for ReplayDeferredSettingsReload.
    void OnSettingsChanged() noexcept;
    // Posts the deferred reload once the move/size loop has ended and the window is not minimized.
    void ReplayDeferredSettingsReload() noexcept;
    void ShowSettingsError(std::wstring_view message) noexcept;
    void CloseSettingsError() noexcept;
    HRESULT UpdateDashboardVisibility() noexcept;
    void CloseMainWindow() noexcept;
    // WM_ENDSESSION with wParam TRUE: logs `session-ending`, runs CloseMainWindow, and flushes the log within a bound
    // before Windows ends the process (UI_XeneonDisplayWindowing.md "Window and rendering lifecycle").
    void OnEndSession(LPARAM reason) noexcept;
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
    // WM_MOUSEWHEEL / WM_MOUSEHWHEEL. The first sample of a wheel sequence goes to the interactive widget under the
    // pointer; its answer latches the sequence to that widget or to the host, which turns each whole detent into one
    // adjacent-page navigation (WheelNavigation.h).
    void OnMouseWheel(HWND window, WPARAM wParam, LPARAM lParam, WheelAxis axis) noexcept;
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
    // Advances a screenshot request from the frame loop: jump, wait, capture. Returns true once a closeWhenDone
    // capture has run (successfully or not) so the loop closes the window; an action's capture returns false.
    [[nodiscard]] bool TickScreenshot() noexcept;
    // Ends the pending request with `result`, logs `screenshot-failed` for a failure, and returns closeWhenDone.
    bool EndScreenshot(HRESULT result) noexcept;
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
    // Widget-local pixels of a client point for a known slot: the overlay content rectangle while that widget is
    // raised, otherwise its current tile bounds.
    void WidgetLocalPoint(size_t widgetIndex, POINT client, float& localX, float& localY) const noexcept;
    // targetWidget bypasses hit testing for a wheel sample that belongs to an already latched widget.
    HRESULT ForwardInteractivePointer(POINT client, uint32_t pointerId, uint32_t kind, uint32_t phase, bool* consumed,
                                      uint32_t modifiers = 0, float wheelDelta = 0,
                                      size_t targetWidget = SIZE_MAX) noexcept;
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
    // The page, widget, and redxe action namespaces (HostActionCatalog.h), executed on the UI thread from the
    // host-action drain or from IRedXeHost::ExecuteAction inside a widget's own input callback. Actions that release
    // widgets (redxe.settings.reload, redxe.quit) are therefore only posted and return S_FALSE. A page or widget
    // action during a swipe, raise settle, or settings error returns ERROR_BUSY / E_NOT_VALID_STATE and is dropped.
    static HRESULT HostActionThunk(void* context, const char* actionUtf8, const char* targetUtf8) noexcept;
    static void HostActionCompletedThunk(void* context) noexcept;
    HRESULT HandleHostAction(std::string_view action, std::string_view target) noexcept;
    // Shows the host's action publisher notices (namespace collisions, missing or unloadable publishers) in one
    // modeless notice window once per change, and closes that window once a change clears them; the dashboard stays
    // active.
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
    HWND _actionNoticeDialog = nullptr;
    wil::unique_hpowernotify _displayPowerNotification;
    std::unique_ptr<PluginManager> _pluginManager;
    std::unique_ptr<DashboardHost> _dashboardHost;
    Renderer _renderer;
    SettingsStore _settingsStore;
    SettingsWatcher _settingsWatcher;
    TrayIcon _trayIcon;
    // Set by Run once the main window is up; RunSelfTest never shows the icon, whatever the document says.
    bool _trayIconAllowed = false;
    // The last TrayIcon::Show result, so a reload that gets the same failure logs no second tray-icon-failed.
    HRESULT _trayIconResult = S_OK;
    // The registered TaskbarCreated message (OnTaskbarCreated); 0 until Run creates the window.
    UINT _taskbarCreatedMessage = 0;
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
        // Set only by the `--screenshot` command line: the window closes once the capture has run.
        bool closeWhenDone = false;
        bool navigated = false;
        bool complete = false;
        ULONGLONG dueTick = 0;
        // A request that ends before its capture finishes reports this; the capture's own result replaces it.
        HRESULT result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    };
    static constexpr UINT_PTR kScreenshotTimerId = 0x5C5;
    // One-shot dwell or hide timer of the autohide dock; at most one is armed and every state exit kills it.
    static constexpr UINT_PTR kDockTimerId = 0x5C6;
    static constexpr UINT_PTR kDockDashboardResizeTimerId = 0x5C8;

    ScreenshotRequest _screenshot{};
    // Joinable exactly while a capture runs. The worker writes its result before it ends, and the UI thread reads it
    // only after join(), which orders the two.
    std::jthread _screenshotWorker;
    HRESULT _screenshotWorkerResult = S_OK;
    DockOverrides _dockOverrides{};
    // Effective dock: the document's `dock` with the command-line overrides applied, re-merged on every live reload.
    // `edge` is None for the titled and fullscreen kinds.
    DockSettings _dock{};
    bool _dockActive = false;
    // Standard-kind placement inputs kept from RestyleWindowKind for FinishWindowKindSwitch. A rollback keeps the
    // monitor the forward switch recorded, so a restored standard window returns to the monitor it was on.
    HMONITOR _kindSwitchFallbackMonitor = nullptr;
    bool _kindSwitchFullscreen = false;
    RECT _xeneonBounds{};
    bool _xeneonFound = false;
    RECT _dockMonitorRect{};
    RECT _dockWorkRect{};
    RECT _dockFullRect{};
    UINT _dockDpi = USER_DEFAULT_SCREEN_DPI;
    bool _dockAppBarRegistered = false;
    bool _dockAutohideRegistered = false;
    DockEdge _dockAutohideEdge = DockEdge::None;
    RECT _dockAutohideMonitor{};
    bool _dockReserved = false;
    bool _dockMonitorFellBack = false;
    bool _dockThicknessClamped = false;
    // The last ABN_FULLSCREENAPP state, for any monitor; DockYieldsToFullscreen narrows it to the dock's.
    bool _dockFullscreenAppActive = false;
    DockRevealState _dockReveal = DockRevealState::Revealed;
    bool _dockSlideActive = false;
    bool _dockSlideRevealing = false;
    LONG _dockSlideFromPx = 0;
    LONG _dockSlideToPx = 0;
    // The window's cross size while a slide runs, so a reversal starts where the bar is.
    LONG _dockVisiblePx = 0;
    UINT64 _dockSlideStartQpc = 0;
    UINT64 _dockSlideDurationQpc = 0;
    // Set around the SetWindowPos of a reveal or hide so OnSize does not treat the strip as a dashboard resize.
    bool _dockResizing = false;
    bool _dockPlacing = false;
    bool _dockPointerInside = false;
    bool _dockPinnedByAction = false;
    bool _dockTimerArmed = false;
    bool _dockResizeDrag = false;
    bool _dockDashboardResizePending = false;
    bool _dockDashboardResizeTimerArmed = false;
    // Distance from the inner edge to the pointer at the press, so the edge keeps its offset under the pointer.
    LONG _dockResizeGrabPx = 0;
    bool _windowActive = false;
    // Between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE: the system move/size loop owns the window's rectangle.
    bool _inSizeMove = false;
    // A settings reload waits for the end of the move/size loop or for the restore of a minimized window.
    bool _settingsReloadDeferred = false;
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
    WheelNavigator _wheel{};
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
