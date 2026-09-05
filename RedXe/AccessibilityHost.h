#pragma once
#include "PlugInterfaces/Widget.h"
#include <memory>
#include <span>
#include <wil/com.h>

struct AccessibleWidgetView final
{
    size_t index = 0;
    IRedXeAccessibilityWidget* widget = nullptr; // Borrowed only during Update.
    uint32_t viewId = 0;
    RECT screenBounds{};
    bool keyboardFocused = false;
    bool prepared = false;
};
struct AccessibilityRequest final
{
    size_t index = 0;
    uint32_t viewId = 0;
    bool focus = false;
    HRESULT action = S_OK;
};

// Lazy application-owned UIA root. Bounded to one dashboard page; no polling, worker or extra HWND.
class AccessibilityHost final
{
  public:
    static HRESULT Create(HWND window, std::unique_ptr<AccessibilityHost>& result) noexcept;
    ~AccessibilityHost();
    HRESULT Update(std::span<const AccessibleWidgetView> views) noexcept;
    void ClearViews() noexcept;
    void Disconnect() noexcept;
    [[nodiscard]] IRawElementProviderSimple* Provider() const noexcept;
    [[nodiscard]] bool IsMessage(UINT message, WPARAM cookie) noexcept;
    [[nodiscard]] bool TakeRequest(AccessibilityRequest& request) noexcept;

  private:
    struct State;
    struct Root;
    struct Site;
    AccessibilityHost() = default;
    std::shared_ptr<State> _state;
    // State's root pointer is borrowed; this owner is cleared after Disconnect retires every site.
    wil::com_ptr_nothrow<IRawElementProviderSimple> _root;
};
