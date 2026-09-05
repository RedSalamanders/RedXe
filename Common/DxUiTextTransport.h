#pragma once
#include "TextInputValidation.h"
#include <DxUi/Embedded.h>
#include <algorithm>
#include <exception>

namespace RedXeTextTransport
{
inline HRESULT EncodeTextState(uint64_t revision, uint64_t focusId, const DxUi::NativeTextInputState& source,
                               const std::optional<D2D1_RECT_F>& caret, const std::optional<D2D1_RECT_F>& viewport,
                               float dpi, RedXeTextState& target) noexcept
{
    target = {};
    if (!std::isfinite(dpi) || dpi <= 0)
        return E_INVALIDARG;
    if (source.text.size() > kRedXeMaximumTextUnits ||
        source.compositionClauseBoundaries.size() > kRedXeMaximumTextClauses)
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    const auto index = [](std::optional<size_t> value) noexcept -> uint32_t
    { return value ? static_cast<uint32_t>((std::min)(*value, size_t(UINT32_MAX - 1))) : kRedXeNoTextIndex; };
    target.revision = revision;
    target.focusId = focusId;
    target.textLength = static_cast<uint32_t>(source.text.size());
    std::copy(source.text.begin(), source.text.end(), target.text);
    target.caret = static_cast<uint32_t>((std::min)(source.caretIndex, size_t(UINT32_MAX)));
    target.anchor = index(source.selectionAnchorIndex);
    target.firstVisibleLine = static_cast<uint32_t>((std::min)(source.firstVisibleLine, size_t(UINT32_MAX)));
    target.flags = (source.readOnly ? RedXeTextReadOnly : 0U) | (source.masked ? RedXeTextMasked : 0U) |
                   (source.multiline ? RedXeTextMultiline : 0U) |
                   (source.compositionStartIndex ? RedXeTextComposing : 0U);
    target.compositionStart = index(source.compositionStartIndex);
    target.compositionEnd = index(source.compositionEndIndex);
    target.compositionCursor = index(source.compositionCursorIndex);
    target.conversionStart = index(source.conversionTargetStartIndex);
    target.conversionEnd = index(source.conversionTargetEndIndex);
    target.clauseCount = static_cast<uint32_t>(source.compositionClauseBoundaries.size());
    for (size_t i = 0; i < source.compositionClauseBoundaries.size(); ++i)
        target.clauses[i] =
            static_cast<uint32_t>((std::min)(source.compositionClauseBoundaries[i], size_t(UINT32_MAX)));
    const auto pixels = [dpi](const D2D1_RECT_F& r) noexcept -> RedXeTextRectangle
    {
        const float scale = dpi / 96.0f;
        return {r.left * scale, r.top * scale, r.right * scale, r.bottom * scale};
    };
    if (caret)
    {
        target.flags |= RedXeTextCaretBounds;
        target.caretBounds = pixels(*caret);
    }
    if (viewport)
    {
        target.flags |= RedXeTextViewportBounds;
        target.viewportBounds = pixels(*viewport);
    }
    if (IsValidRedXeTextState(target))
        return S_OK;
    target = {};
    return E_INVALIDARG;
}
inline HRESULT DecodeTextState(const RedXeTextState& source, DxUi::NativeTextInputState& target) noexcept
{
    target = {};
    if (!IsValidRedXeTextState(source))
        return E_INVALIDARG;
    try
    {
        const auto index = [](uint32_t value) noexcept -> std::optional<size_t>
        { return value == kRedXeNoTextIndex ? std::nullopt : std::optional<size_t>(value); };
        target.text.assign(source.text, source.textLength);
        target.caretIndex = source.caret;
        target.selectionAnchorIndex = index(source.anchor);
        target.firstVisibleLine = source.firstVisibleLine;
        target.readOnly = (source.flags & RedXeTextReadOnly) != 0;
        target.masked = (source.flags & RedXeTextMasked) != 0;
        target.multiline = (source.flags & RedXeTextMultiline) != 0;
        target.compositionStartIndex = index(source.compositionStart);
        target.compositionEndIndex = index(source.compositionEnd);
        target.compositionCursorIndex = index(source.compositionCursor);
        target.conversionTargetStartIndex = index(source.conversionStart);
        target.conversionTargetEndIndex = index(source.conversionEnd);
        target.compositionClauseBoundaries.assign(source.clauses, source.clauses + source.clauseCount);
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        target = {};
        return E_OUTOFMEMORY;
    }
}
} // namespace RedXeTextTransport
