#include "../../RedXe/WidgetTextClient.h"
#include "DxUiTextTransport.h"
#include "PlugInterfaces/FactoryImpl.h"
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
class TextWidget final : public RedXeComObject<TextWidget, IRedXeTextInputWidget>
{
  public:
    RedXeTextState state;
    uint32_t applies = 0, cancels = 0;
    bool unavailable = false, invalidGeometry = false;
    float lastX = 0, lastY = 0;
    HRESULT STDMETHODCALLTYPE ReadTextState(uint32_t view, RedXeTextState* output) noexcept override
    {
        if (!output)
            return E_POINTER;
        *output = {};
        if (view != 1)
            return E_INVALIDARG;
        if (unavailable)
            return S_FALSE;
        *output = state;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ApplyTextState(uint32_t view, uint32_t action,
                                             const RedXeTextState* input) noexcept override
    {
        if (view != 1 || action > RedXeTextCancel || !input || !IsValidRedXeTextState(*input))
            return E_INVALIDARG;
        if (input->revision != state.revision || input->focusId != state.focusId)
            return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        state = *input;
        ++state.revision;
        ++applies;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CancelTextInput(uint32_t view, uint64_t focus) noexcept override
    {
        if (view != 1)
            return E_INVALIDARG;
        if (focus != state.focusId)
            return S_FALSE;
        ++cancels;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HitTestText(uint32_t view, uint64_t revision, float x, float y,
                                          uint32_t* index) noexcept override
    {
        if (!index)
            return E_POINTER;
        *index = 0;
        if (view != 1 || revision != state.revision)
            return E_INVALIDARG;
        lastX = x;
        lastY = y;
        *index = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextRangeBounds(uint32_t view, uint64_t revision, uint32_t start, uint32_t end,
                                                 RedXeTextRectangle* bounds, BOOL* clipped) noexcept override
    {
        if (!bounds || !clipped)
            return E_POINTER;
        *bounds = {};
        *clipped = TRUE;
        if (view != 1 || revision != state.revision || start > end || end > state.textLength)
            return E_INVALIDARG;
        *bounds = {10, 20, 40, 50};
        *clipped = FALSE;
        if (invalidGeometry)
            bounds->left = std::numeric_limits<float>::quiet_NaN();
        return S_OK;
    }
};
} // namespace
uint32_t RunWidgetTextClientTests()
{
    uint32_t checks = 0;
    const auto check = [&](bool condition, const char* reason)
    {
        ++checks;
        if (!condition)
            throw std::runtime_error(reason);
    };
    wil::com_ptr_nothrow<TextWidget> widget;
    widget.attach(new TextWidget());
    DxUi::NativeTextInputState initial;
    initial.text = L"Tokyo \u6771\u4eac";
    initial.caretIndex = initial.text.size();
    check(RedXeTextTransport::EncodeTextState(7, 11, initial, D2D1::RectF(10, 20, 11, 50), D2D1::RectF(5, 10, 100, 60),
                                              96, widget->state) == S_OK,
          "host text fixture encodes an owned Unicode snapshot");
    WidgetTextClient client(widget.get(), 1, 11, {-200, 100});
    DxUi::EmbeddedTextInputSnapshot snapshot;
    check(client.Read(snapshot) == S_OK && snapshot.state.text == initial.text && snapshot.revision == 7 &&
              snapshot.focusId == 11,
          "host client decodes only its immutable focus session");
    check(!snapshot.caretBoundsDip && !snapshot.viewportBoundsDip,
          "host does not mislabel physical transport rectangles as DIPs");
    RECT bounds{};
    bool clipped = true;
    size_t index = 99;
    check(client.ViewportBounds(bounds) == S_OK && bounds.left == -195 && bounds.top == 110 && bounds.right == -100,
          "host adds negative screen origin exactly once");
    check(client.HitTest({-180, 130}, index) == S_OK && index == 1 && widget->lastX == 20 && widget->lastY == 30,
          "host converts point into the same widget-local physical coordinates");
    check(client.RangeBounds(0, 3, bounds, clipped) == S_OK && bounds.left == -190 && bounds.bottom == 150 && !clipped,
          "host maps a revisioned text range to screen pixels");
    widget->invalidGeometry = true;
    check(client.RangeBounds(0, 3, bounds, clipped) == E_INVALIDARG && bounds.left == 0 && clipped,
          "host rejects non-finite provider geometry with cleared outputs");
    widget->invalidGeometry = false;
    client.SetScreenOrigin({300, -100});
    check(client.ViewportBounds(bounds) == S_OK && bounds.left == 305 && bounds.top == -90,
          "moving the window updates geometry without changing text focus identity");
    initial.text = L"New profile";
    initial.caretIndex = initial.text.size();
    check(client.Apply(7, initial, DxUi::EmbeddedTextInputAction::Commit) == S_OK && widget->applies == 1 &&
              widget->state.revision == 8,
          "host forwards one current-revision commit");
    check(client.Apply(7, initial, DxUi::EmbeddedTextInputAction::Commit) ==
                  HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH) &&
              widget->applies == 1,
          "host rejects stale edit before calling plugin mutation");
    client.Cancel();
    check(widget->cancels == 1, "host forwards focus-bound cancellation");
    widget->state.focusId = 12;
    check(client.Read(snapshot) == S_FALSE && !snapshot.revision && snapshot.state.text.empty(),
          "replacement focus disconnects old client reads");
    check(client.Apply(8, initial, DxUi::EmbeddedTextInputAction::Commit) == S_FALSE && widget->applies == 1,
          "old host client cannot edit replacement focus");
    client.Cancel();
    check(widget->cancels == 1, "old cancellation cannot reach replacement focus");
    widget->state.focusId = 11;
    widget->state.textLength = kRedXeMaximumTextUnits + 1;
    check(client.Read(snapshot) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA) && snapshot.state.text.empty(),
          "malformed plugin snapshot rejected by host");
    widget->state.textLength = static_cast<uint32_t>(initial.text.size());
    HRESULT foreign = S_OK;
    std::thread worker(
        [&]
        {
            DxUi::EmbeddedTextInputSnapshot state;
            foreign = client.Read(state);
            client.Cancel();
        });
    worker.join();
    check(foreign == RPC_E_WRONG_THREAD && widget->cancels == 1,
          "foreign-thread service cannot reach UI-affine plugin");
    client.Disconnect();
    check(FAILED(client.Read(snapshot)) && !client.Matches(widget.get(), 1, 11),
          "explicit detach releases plugin and invalidates retained client");
    return checks;
}
