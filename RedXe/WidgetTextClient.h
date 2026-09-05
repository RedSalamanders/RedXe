#pragma once
#include "PlugInterfaces/Widget.h"
#include <DxUi/TextInputServices.h>
#include <wil/com.h>

// One app-side client per immutable widget/view/focus session. No HWND or C++ library object crosses the ABI.
// Disconnect before releasing a dashboard, changing its displayed transform, or destroying the application HWND.
class WidgetTextClient final : public DxUi::TextInputClient
{
  public:
    WidgetTextClient(IRedXeTextInputWidget* widget, uint32_t view, uint64_t focus, POINT screenOrigin) noexcept;
    void Disconnect() noexcept;
    [[nodiscard]] bool Matches(IRedXeTextInputWidget* widget, uint32_t view, uint64_t focus) const noexcept;
    void SetScreenOrigin(POINT origin) noexcept
    {
        _origin = origin;
    }
    HRESULT Read(DxUi::EmbeddedTextInputSnapshot& snapshot) noexcept override;
    HRESULT Apply(uint64_t revision, const DxUi::NativeTextInputState& state,
                  DxUi::EmbeddedTextInputAction action) noexcept override;
    void Cancel() noexcept override;
    HRESULT HitTest(POINT screen, size_t& index) noexcept override;
    HRESULT RangeBounds(size_t start, size_t end, RECT& bounds, bool& clipped) noexcept override;
    HRESULT ViewportBounds(RECT& bounds) noexcept override;

  private:
    HRESULT ReadWire(RedXeTextState& state) noexcept;
    HRESULT ToScreen(const RedXeTextRectangle& bounds, RECT& result) const noexcept;
    wil::com_ptr_nothrow<IRedXeTextInputWidget> _widget;
    uint32_t _view;
    uint64_t _focus;
    POINT _origin;
    DWORD _thread;
};
