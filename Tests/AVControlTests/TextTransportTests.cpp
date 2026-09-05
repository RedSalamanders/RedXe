#include "DxUiTextTransport.h"
#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<RedXeTextState>);
static_assert(std::is_standard_layout_v<RedXeTextState>);
static_assert(std::is_base_of_v<IUnknown, IRedXeTextInputWidget>);
static_assert(!std::is_base_of_v<IRedXeWidget, IRedXeTextInputWidget>);

uint32_t RunTextTransportTests()
{
    uint32_t checks = 0;
    const auto check = [&](bool condition, const char* reason)
    {
        ++checks;
        if (!condition)
            throw std::runtime_error(reason);
    };
    auto wire = std::make_unique<RedXeTextState>();
    DxUi::NativeTextInputState text;
    text.text = L"A\U0001F642\n\u6771\u4EAC";
    text.caretIndex = text.text.size();
    text.selectionAnchorIndex = 4;
    text.multiline = true;
    text.firstVisibleLine = 1;
    text.compositionStartIndex = 4;
    text.compositionEndIndex = 6;
    text.compositionCursorIndex = 5;
    text.conversionTargetStartIndex = 4;
    text.conversionTargetEndIndex = 5;
    text.compositionClauseBoundaries = {4, 5, 6};
    const auto caret = D2D1::RectF(20, 30, 21, 48), viewport = D2D1::RectF(5, 10, 200, 58);
    check(RedXeTextTransport::EncodeTextState(17, 9, text, caret, viewport, 144, *wire) == S_OK,
          "text snapshot encodes Unicode composition");
    check(wire->textLength == 6 && wire->caret == 6 && wire->anchor == 4 && wire->revision == 17 && wire->focusId == 9,
          "wire indexes use UTF-16 units and retain the opaque revision");
    check(wire->caretBounds.left == 30 && wire->caretBounds.bottom == 72 && wire->viewportBounds.left == 7.5f,
          "text geometry converts DIPs to physical pixels exactly once");
    DxUi::NativeTextInputState decoded;
    check(RedXeTextTransport::DecodeTextState(*wire, decoded) == S_OK && decoded.text == text.text &&
              decoded.caretIndex == text.caretIndex && decoded.selectionAnchorIndex == text.selectionAnchorIndex &&
              decoded.compositionClauseBoundaries == text.compositionClauseBoundaries &&
              decoded.compositionCursorIndex == text.compositionCursorIndex &&
              decoded.conversionTargetEndIndex == text.conversionTargetEndIndex && decoded.multiline &&
              decoded.firstVisibleLine == 1,
          "bounded text transport preserves composition and selection");
    using Corrupt = void (*)(RedXeTextState&);
    const std::array<Corrupt, 21> corruptions{{[](auto& s) { --s.sizeBytes; },
                                               [](auto& s) { s.revision = 0; },
                                               [](auto& s) { s.focusId = 0; },
                                               [](auto& s) { s.flags |= 0x80000000; },
                                               [](auto& s) { s.textLength = kRedXeMaximumTextUnits + 1; },
                                               [](auto& s) { s.caret = 7; },
                                               [](auto& s) { s.anchor = 7; },
                                               [](auto& s) { s.firstVisibleLine = 2; },
                                               [](auto& s) { s.text[1] = 0; },
                                               [](auto& s) { s.text[2] = L'a'; },
                                               [](auto& s) { s.text[0] = 0xdc00; },
                                               [](auto& s) { s.compositionStart = 7; },
                                               [](auto& s) { s.compositionEnd = 7; },
                                               [](auto& s) { s.compositionCursor = 3; },
                                               [](auto& s) { s.conversionEnd = kRedXeNoTextIndex; },
                                               [](auto& s) { s.clauseCount = kRedXeMaximumTextClauses + 1; },
                                               [](auto& s) { s.clauses[1] = 3; },
                                               [](auto& s) { s.clauses[1] = 7; },
                                               [](auto& s) { s.flags &= ~RedXeTextComposing; },
                                               [](auto& s)
                                               { s.caretBounds.left = std::numeric_limits<float>::quiet_NaN(); },
                                               [](auto& s) { s.viewportBounds.right = s.viewportBounds.left - 1; }}};
    auto invalid = std::make_unique<RedXeTextState>();
    for (const auto corrupt : corruptions)
    {
        *invalid = *wire;
        corrupt(*invalid);
        decoded.text = L"old";
        check(!IsValidRedXeTextState(*invalid) &&
                  RedXeTextTransport::DecodeTextState(*invalid, decoded) == E_INVALIDARG && decoded.text.empty(),
              "malformed text transport is rejected before mutation and output is cleared");
    }
    text = {};
    text.text.assign(kRedXeMaximumTextUnits, L'x');
    text.caretIndex = text.text.size();
    check(RedXeTextTransport::EncodeTextState(18, 9, text, {}, {}, 96, *wire) == S_OK &&
              wire->textLength == kRedXeMaximumTextUnits,
          "full-capacity text is length-delimited without requiring a terminator");
    text.text.push_back(L'x');
    check(RedXeTextTransport::EncodeTextState(18, 9, text, {}, {}, 96, *wire) ==
                  HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW) &&
              !wire->revision && !wire->textLength,
          "oversized text is rejected instead of truncated");
    text = {};
    text.selectionAnchorIndex = std::numeric_limits<size_t>::max();
    check(RedXeTextTransport::EncodeTextState(19, 9, text, {}, {}, 96, *wire) == E_INVALIDARG && !wire->revision,
          "invalid optional index cannot silently become an absent index");
    text = {};
    text.compositionStartIndex = 0;
    text.compositionEndIndex = 0;
    text.compositionClauseBoundaries.assign(kRedXeMaximumTextClauses, 0);
    check(RedXeTextTransport::EncodeTextState(20, 9, text, {}, {}, 96, *wire) == S_OK,
          "empty composition accepts bounded clause list");
    text.compositionClauseBoundaries.push_back(0);
    check(RedXeTextTransport::EncodeTextState(20, 9, text, {}, {}, 96, *wire) ==
              HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW),
          "oversized clause list rejected");
    check(RedXeTextTransport::EncodeTextState(20, 9, {}, {}, {}, 0, *wire) == E_INVALIDARG,
          "invalid text geometry DPI rejected");
    check(RedXeTextTransport::EncodeTextState(20, 9, {}, {}, {}, std::numeric_limits<float>::infinity(), *wire) ==
              E_INVALIDARG,
          "non-finite text geometry DPI rejected");
    check(RedXeTextTransport::EncodeTextState(20, 0, {}, {}, {}, 96, *wire) == E_INVALIDARG && !wire->revision,
          "missing focus identity rejected before exposing state");
    return checks;
}
