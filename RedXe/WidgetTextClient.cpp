#include "WidgetTextClient.h"
#include "DxUiTextTransport.h"
#include <limits>
#include <msctf.h>

WidgetTextClient::WidgetTextClient(IRedXeTextInputWidget* widget, uint32_t view, uint64_t focus,
                                   POINT screenOrigin) noexcept
    : _widget(widget), _view(view), _focus(focus), _origin(screenOrigin), _thread(GetCurrentThreadId())
{
}
void WidgetTextClient::Disconnect() noexcept
{
    if (_thread == GetCurrentThreadId())
        _widget.reset();
}
bool WidgetTextClient::Matches(IRedXeTextInputWidget* widget, uint32_t view, uint64_t focus) const noexcept
{
    return _widget.get() == widget && _view == view && _focus == focus;
}
HRESULT WidgetTextClient::ReadWire(RedXeTextState& state) noexcept
{
    state = {};
    if (_thread != GetCurrentThreadId())
        return RPC_E_WRONG_THREAD;
    if (!_widget)
        return TF_E_DISCONNECTED;
    const auto widget = _widget;
    const HRESULT hr = widget->ReadTextState(_view, &state);
    if (hr != S_OK)
    {
        state = {};
        return hr;
    }
    if (!IsValidRedXeTextState(state))
    {
        state = {};
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (widget != _widget || state.focusId != _focus)
    {
        state = {};
        return S_FALSE;
    }
    return S_OK;
}
HRESULT WidgetTextClient::Read(DxUi::EmbeddedTextInputSnapshot& snapshot) noexcept
{
    snapshot = {};
    RedXeTextState wire;
    const HRESULT hr = ReadWire(wire);
    if (hr != S_OK)
        return hr;
    const HRESULT decoded = RedXeTextTransport::DecodeTextState(wire, snapshot.state);
    if (decoded != S_OK)
        return decoded;
    snapshot.revision = wire.revision;
    snapshot.focusId = wire.focusId;
    // Screen geometry is supplied by the separate callbacks; no pixel rectangles are mislabeled as DIPs.
    return S_OK;
}
HRESULT WidgetTextClient::Apply(uint64_t revision, const DxUi::NativeTextInputState& state,
                                DxUi::EmbeddedTextInputAction action) noexcept
{
    RedXeTextState wire;
    const HRESULT read = ReadWire(wire);
    if (read != S_OK)
        return read;
    if (wire.revision != revision)
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    uint32_t operation = RedXeTextCancel;
    switch (action)
    {
    case DxUi::EmbeddedTextInputAction::Preview:
        operation = RedXeTextPreview;
        break;
    case DxUi::EmbeddedTextInputAction::Commit:
        operation = RedXeTextCommit;
        break;
    case DxUi::EmbeddedTextInputAction::Cancel:
        return _widget->CancelTextInput(_view, _focus);
    default:
        return E_INVALIDARG;
    }
    const HRESULT encoded = RedXeTextTransport::EncodeTextState(revision, _focus, state, {}, {}, 96, wire);
    if (encoded != S_OK)
        return encoded;
    const auto widget = _widget;
    return widget->ApplyTextState(_view, operation, &wire);
}
void WidgetTextClient::Cancel() noexcept
{
    if (_thread == GetCurrentThreadId() && _widget)
    {
        const auto widget = _widget;
        (void)widget->CancelTextInput(_view, _focus);
    }
}
HRESULT WidgetTextClient::ToScreen(const RedXeTextRectangle& rect, RECT& result) const noexcept
{
    result = {};
    const double l = std::floor(double(rect.left) + _origin.x), t = std::floor(double(rect.top) + _origin.y);
    const double r = std::ceil(double(rect.right) + _origin.x), b = std::ceil(double(rect.bottom) + _origin.y);
    constexpr double minimum = std::numeric_limits<LONG>::min(), maximum = std::numeric_limits<LONG>::max();
    if (!std::isfinite(l) || !std::isfinite(t) || !std::isfinite(r) || !std::isfinite(b) || l < minimum ||
        t < minimum || r > maximum || b > maximum || l > r || t > b)
        return E_INVALIDARG;
    result = {static_cast<LONG>(l), static_cast<LONG>(t), static_cast<LONG>(r), static_cast<LONG>(b)};
    return S_OK;
}
HRESULT WidgetTextClient::HitTest(POINT screen, size_t& index) noexcept
{
    index = 0;
    RedXeTextState wire;
    const HRESULT read = ReadWire(wire);
    if (read != S_OK)
        return read;
    uint32_t acp = 0;
    const auto widget = _widget;
    const HRESULT hr = widget->HitTestText(_view, wire.revision, float(double(screen.x) - _origin.x),
                                           float(double(screen.y) - _origin.y), &acp);
    if (hr != S_OK)
        return hr;
    if (widget != _widget)
        return TF_E_DISCONNECTED;
    if (acp > wire.textLength)
        return E_INVALIDARG;
    index = acp;
    return S_OK;
}
HRESULT WidgetTextClient::RangeBounds(size_t start, size_t end, RECT& bounds, bool& clipped) noexcept
{
    bounds = {};
    clipped = true;
    RedXeTextState wire;
    const HRESULT read = ReadWire(wire);
    if (read != S_OK)
        return read;
    if (start > end || end > wire.textLength)
        return E_INVALIDARG;
    RedXeTextRectangle local;
    BOOL wasClipped = TRUE;
    const auto widget = _widget;
    const HRESULT hr = widget->GetTextRangeBounds(_view, wire.revision, static_cast<uint32_t>(start),
                                                  static_cast<uint32_t>(end), &local, &wasClipped);
    if (hr != S_OK)
        return hr;
    if (widget != _widget)
        return TF_E_DISCONNECTED;
    if (local.left == 0 && local.top == 0 && local.right == 0 && local.bottom == 0)
        return S_OK;
    const HRESULT converted = ToScreen(local, bounds);
    if (converted == S_OK)
        clipped = wasClipped != FALSE;
    return converted;
}
HRESULT WidgetTextClient::ViewportBounds(RECT& bounds) noexcept
{
    bounds = {};
    RedXeTextState wire;
    const HRESULT read = ReadWire(wire);
    if (read != S_OK)
        return read;
    return (wire.flags & RedXeTextViewportBounds) ? ToScreen(wire.viewportBounds, bounds) : S_FALSE;
}
