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
#include <string_view>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

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

    // Production entry point: create the window, then run the frame loop until the window closes.
    int Run(int showCommand, std::wstring_view settingsPath = {}) noexcept;
    // Hidden startup validation for `--self-test`. It shares this class's startup steps but never enters the
    // frame loop, so Run itself carries no test branches.
    int RunSelfTest(std::wstring_view settingsPath = {}) noexcept;

  private:
    static constexpr wchar_t kWindowClassName[] = L"RedXe.Window";
    static constexpr wchar_t kSettingsDialogClassName[] = L"RedXe.SettingsError";
    static constexpr wchar_t kRaiseOverlayClassName[] = L"RedXe.RaiseOverlay";
    static constexpr wchar_t kPageEdgeClassName[] = L"RedXe.PageEdge";
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK SettingsDialogProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK RaiseOverlayProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK PageEdgeProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

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
    void RefreshScheduledFrameDeadline() noexcept;
    void ClearScheduledFrameDeadline() noexcept;
    bool WaitUntilMessage() noexcept;
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT OnSize(HWND window, UINT width, UINT height) noexcept;
    LRESULT OnDpiChanged(HWND window, UINT dpi, const RECT* suggestedBounds) noexcept;
    void OnPointerDown(HWND window, WPARAM wParam) noexcept;
    void OnPointerUpdate(HWND window, WPARAM wParam) noexcept;
    void OnPointerUp(HWND window, WPARAM wParam) noexcept;
    void OnMouseButtonUp(HWND window, LPARAM lParam) noexcept;
    void OnClientActivateAttempt(HWND window, POINT position, ULONGLONG tick) noexcept;
    HRESULT TryRaiseWidgetAt(HWND window, size_t widgetIndex) noexcept;
    void DismissWidgetRaise() noexcept;
    LRESULT HandleRaiseOverlayMessage(HWND overlay, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    void PaintRaiseOverlay(HWND overlay) noexcept;
    void CancelPageNavigation() noexcept;
    [[nodiscard]] PageEdgeState CurrentPageEdgeState() const noexcept;
    // Client rectangle intersected with the display work area, in client coordinates. Edge bands are placed against
    // its edges so they stay reachable when the window is larger than its monitor.
    [[nodiscard]] RECT ReachableClientRect() const noexcept;
    // Recomputes which edge band the pointer is over, from the live cursor position. A layered band at zero alpha is
    // click-through, so hover cannot be detected by the band itself; the top-level window owns it.
    void UpdatePageEdgeHover() noexcept;
    void ClearPageEdgeHover() noexcept;
    void RefreshPageEdgeAffordances() noexcept;
    void DestroyPageEdgeAffordances() noexcept;
    [[nodiscard]] size_t PageEdgeIndex(HWND window) const noexcept;
    LRESULT HandlePageEdgeMessage(HWND edge, size_t index, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    void PaintPageEdge(HWND edge, size_t index) noexcept;
    void EnsurePageEdgeIconFont(UINT dpi) noexcept;
    void SetPageEdgeRevealed(size_t index, bool revealed) noexcept;
    HRESULT NavigateToAdjacentPage(int direction) noexcept;
    void ApplyPageOffset(LONG offset, LONG clientWidth) noexcept;
    void FlushPendingTransitionStage() noexcept;
    void BeginPageSettle(LONG targetOffset, bool commit) noexcept;
    void TickPageSettle() noexcept;
    HRESULT PromoteTransitionPage() noexcept;
    HRESULT StageTransitionPage(int direction) noexcept;
    void ClearTransitionPage() noexcept;
    [[nodiscard]] bool TryPointerClientPosition(HWND window, UINT32 pointerId, POINT& position,
                                                UINT64& qpc) const noexcept;

    HINSTANCE _instance = nullptr;
    wil::unique_hwnd _window;
    wil::unique_hwnd _raiseOverlay;
    // Index 0 is the previous-page band on the left edge; index 1 is the next-page band on the right edge.
    std::array<wil::unique_hwnd, 2> _pageEdges;
    std::array<bool, 2> _pageEdgeRevealed{};
    std::array<RECT, 2> _pageEdgeBands{};
    // Last state the bands were built for. RefreshPageEdgeAffordances is called from the frame loop, so an unchanged
    // state must perform no window operations at all.
    PageEdgeState _pageEdgeApplied{};
    bool _pageEdgeMouseTracking = false;
    SIZE _pageEdgeAppliedClient{};
    RECT _pageEdgeAppliedReachable{};
    // Icon font is created once per DPI, not per paint: Core_PerformanceAndResources.md forbids creating GDI handles
    // inside a paint callback.
    wil::unique_hfont _pageEdgeIconFont;
    UINT _pageEdgeIconFontDpi = 0;
    FluentIcons::IconFont _pageEdgeIconFontKind = FluentIcons::IconFont::TextFallback;
    UINT _pageEdgeAppliedDpi = 0;
    bool _pageEdgeApplyValid = false;
    bool _pageEdgeClassRegistered = false;
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
    bool _forceWarp = false;
    bool _classRegistered = false;
    bool _rendererReady = false;
    bool _windowVisible = false;
    bool _displayPoweredOn = true;
    bool _occlusionStatusChanged = false;
    bool _frameInvalidated = true;
    ULONGLONG _scheduledFrameDeadlineTick = 0;
    HRESULT _runtimeFailure = S_OK;
    UINT32 _pagePointerId = 0;
    LONG _pagePointerStartX = 0;
    LONG _pagePointerStartY = 0;
    LONG _pagePointerX = 0;
    LONG _pageCurrentOffset = 0;
    UINT64 _pagePointerQpc = 0;
    UINT64 _qpcFrequency = 0;
    float _pageVelocityPxPerSec = 0.0f;
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
    ULONGLONG _activateTick = 0;
    POINT _activatePoint{};
    size_t _activateWidgetIndex = SIZE_MAX;
};
