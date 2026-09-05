#pragma once
#include "PlugInterfaces/Widget.h"
#include <cmath>

// Pure validation shared by host and plugin adapters. No allocation, Windows calls or document mutation.
[[nodiscard]] inline bool IsValidRedXeTextState(const RedXeTextState& state) noexcept
{
    constexpr uint32_t known = RedXeTextReadOnly | RedXeTextMasked | RedXeTextMultiline | RedXeTextComposing |
                               RedXeTextCaretBounds | RedXeTextViewportBounds;
    if (state.sizeBytes != sizeof(state) || !state.revision || !state.focusId || (state.flags & ~known) ||
        state.textLength > kRedXeMaximumTextUnits || state.clauseCount > kRedXeMaximumTextClauses ||
        state.caret > state.textLength || (state.anchor != kRedXeNoTextIndex && state.anchor > state.textLength))
        return false;
    uint32_t lineCount = 0;
    for (uint32_t i = 0; i < state.textLength; ++i)
    {
        const auto unit = static_cast<uint16_t>(state.text[i]);
        if (!unit)
            return false;
        if (unit == L'\n')
            ++lineCount;
        if (unit >= 0xd800 && unit <= 0xdbff)
        {
            if (++i == state.textLength || state.text[i] < 0xdc00 || state.text[i] > 0xdfff)
                return false;
        }
        else if (unit >= 0xdc00 && unit <= 0xdfff)
            return false;
    }
    if (state.firstVisibleLine > lineCount)
        return false;
    const auto validRect = [](const RedXeTextRectangle& r) noexcept
    {
        return std::isfinite(r.left) && std::isfinite(r.top) && std::isfinite(r.right) && std::isfinite(r.bottom) &&
               r.left <= r.right && r.top <= r.bottom;
    };
    if (((state.flags & RedXeTextCaretBounds) && !validRect(state.caretBounds)) ||
        ((state.flags & RedXeTextViewportBounds) && !validRect(state.viewportBounds)))
        return false;
    if (!(state.flags & RedXeTextComposing))
        return state.compositionStart == kRedXeNoTextIndex && state.compositionEnd == kRedXeNoTextIndex &&
               state.compositionCursor == kRedXeNoTextIndex && state.conversionStart == kRedXeNoTextIndex &&
               state.conversionEnd == kRedXeNoTextIndex && !state.clauseCount;
    if (state.compositionStart > state.compositionEnd || state.compositionEnd > state.textLength)
        return false;
    const auto inComposition = [&](uint32_t index) noexcept
    { return index >= state.compositionStart && index <= state.compositionEnd; };
    if (state.compositionCursor != kRedXeNoTextIndex && !inComposition(state.compositionCursor))
        return false;
    if (state.conversionStart != kRedXeNoTextIndex || state.conversionEnd != kRedXeNoTextIndex)
        if (!inComposition(state.conversionStart) || !inComposition(state.conversionEnd) ||
            state.conversionStart > state.conversionEnd)
            return false;
    for (uint32_t i = 0; i < state.clauseCount; ++i)
        if (!inComposition(state.clauses[i]) || (i && state.clauses[i - 1] > state.clauses[i]))
            return false;
    return true;
}
