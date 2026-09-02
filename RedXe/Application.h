#pragma once

#include "DashboardHost.h"
#include "PluginManager.h"
#include "Renderer.h"
#include "Settings.h"
#include "SettingsWatcher.h"
#include "WidgetRaise.h"

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
    Application(HINSTANCE instance, bool forceWarp) noexcept;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    int Run(int showCommand, bool selfTest, std::wstring_view settingsPath = {}) noexcept;

  private:
    static constexpr wchar_t kWindowClassName[] = L"RedXe.Window";
    static constexpr wchar_t kSettingsDialogClassName[] = L"RedXe.SettingsError";
    static constexpr wchar_t kRaiseOverlayClassName[] = L"RedXe.RaiseOverlay";
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK SettingsDialogProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK RaiseOverlayProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

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
