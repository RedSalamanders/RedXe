#pragma once

#include <dwrite.h>
#include <windows.h>

// Shared icon glyph source for RedXe host chrome.
//
// Host-drawn iconography uses Segoe Fluent Icons rather than hand-drawn shapes: the glyphs are hinted, they scale
// correctly with DPI, and they match the rest of the Windows 11 shell. Segoe MDL2 Assets covers older builds where
// the Fluent family is absent, and a standard Unicode glyph in the normal UI font covers the case where neither icon
// font is installed, so chrome never renders as a missing-glyph box.
//
// The renderer rasterizes the few glyphs it needs into one small atlas with DirectWrite at device creation and again
// only when the DPI changes; no GDI font or device context is involved. Add new glyphs here rather than drawing them,
// and give every glyph a Unicode fallback.
namespace FluentIcons
{
inline constexpr wchar_t kFontFamily[] = L"Segoe Fluent Icons";
inline constexpr wchar_t kLegacyFontFamily[] = L"Segoe MDL2 Assets";
inline constexpr wchar_t kTextFontFamily[] = L"Segoe UI";

// Segoe Fluent Icons private-use glyphs.
// https://learn.microsoft.com/windows/apps/design/style/segoe-fluent-icons-font
// Written as escapes so the private-use code points survive every editor and diff tool.
inline constexpr wchar_t kChevronLeft = L'\xE76B';
inline constexpr wchar_t kChevronRight = L'\xE76C';
inline constexpr wchar_t kChevronUp = L'\xE70E';
inline constexpr wchar_t kChevronDown = L'\xE70D';
inline constexpr wchar_t kClear = L'\xE894';
inline constexpr wchar_t kWarning = L'\xE7BA';
inline constexpr wchar_t kError = L'\xEA39';
inline constexpr wchar_t kInfo = L'\xE946';

// Standard Unicode stand-ins used when no icon font is installed.
inline constexpr wchar_t kFallbackChevronLeft = L'\x2039';  // single left-pointing angle quotation mark
inline constexpr wchar_t kFallbackChevronRight = L'\x203A'; // single right-pointing angle quotation mark
inline constexpr wchar_t kFallbackChevronUp = L'\x25B4';
inline constexpr wchar_t kFallbackChevronDown = L'\x25BE';
inline constexpr wchar_t kFallbackClear = L'\x00D7'; // multiplication sign
inline constexpr wchar_t kFallbackWarning = L'\x26A0';
inline constexpr wchar_t kFallbackError = L'\x2716';
inline constexpr wchar_t kFallbackInfo = L'\x2139';

// Which glyph set a resolved font family can actually draw.
enum class IconFont
{
    Fluent,
    Legacy,
    // Neither icon font is installed; draw the Unicode fallback glyph in the normal UI font.
    TextFallback,
};

[[nodiscard]] inline bool CollectionHasFamily(IDWriteFontCollection* collection, const wchar_t* family) noexcept
{
    if (!collection || !family || family[0] == L'\0')
    {
        return false;
    }
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(collection->FindFamilyName(family, &index, &exists)) && exists != FALSE;
}

// Picks the best installed icon family from a DirectWrite font collection and reports which glyph set it draws.
[[nodiscard]] inline const wchar_t* ResolveIconFamily(IDWriteFontCollection* collection, IconFont& selected) noexcept
{
    if (CollectionHasFamily(collection, kFontFamily))
    {
        selected = IconFont::Fluent;
        return kFontFamily;
    }
    if (CollectionHasFamily(collection, kLegacyFontFamily))
    {
        selected = IconFont::Legacy;
        return kLegacyFontFamily;
    }
    selected = IconFont::TextFallback;
    return kTextFontFamily;
}

[[nodiscard]] constexpr wchar_t SelectGlyph(IconFont font, wchar_t iconGlyph, wchar_t fallbackGlyph) noexcept
{
    return font == IconFont::TextFallback ? fallbackGlyph : iconGlyph;
}
} // namespace FluentIcons
