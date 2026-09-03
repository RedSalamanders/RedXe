#pragma once

#include <strsafe.h>
#include <windows.h>

// Shared icon glyph source for RedXe host chrome.
//
// Host-drawn iconography uses Segoe Fluent Icons rather than hand-drawn shapes: the glyphs are hinted, they scale
// correctly with DPI, and they match the rest of the Windows 11 shell. Segoe MDL2 Assets covers older builds where
// the Fluent family is absent, and a standard Unicode glyph in the normal UI font covers the case where neither icon
// font is installed, so chrome never renders as a missing-glyph box.
//
// Add new glyphs here rather than drawing them, and give every glyph a Unicode fallback.
namespace FluentIcons
{
inline constexpr wchar_t kFontFamily[] = L"Segoe Fluent Icons";
inline constexpr wchar_t kLegacyFontFamily[] = L"Segoe MDL2 Assets";
inline constexpr wchar_t kTextFontFamily[] = L"Segoe UI";

// Segoe Fluent Icons private-use glyphs.
// https://learn.microsoft.com/windows/apps/design/style/segoe-fluent-icons-font
inline constexpr wchar_t kChevronLeft = L'';
inline constexpr wchar_t kChevronRight = L'';
inline constexpr wchar_t kChevronUp = L'';
inline constexpr wchar_t kChevronDown = L'';
inline constexpr wchar_t kClear = L'';
inline constexpr wchar_t kWarning = L'';
inline constexpr wchar_t kError = L'';
inline constexpr wchar_t kInfo = L'';

// Standard Unicode stand-ins used when no icon font is installed.
inline constexpr wchar_t kFallbackChevronLeft = L'‹';  // <
inline constexpr wchar_t kFallbackChevronRight = L'›'; // >
inline constexpr wchar_t kFallbackChevronUp = L'▴';
inline constexpr wchar_t kFallbackChevronDown = L'▾';
inline constexpr wchar_t kFallbackClear = L'×'; // x
inline constexpr wchar_t kFallbackWarning = L'⚠';
inline constexpr wchar_t kFallbackError = L'✖';
inline constexpr wchar_t kFallbackInfo = L'ℹ';

// Which glyph set a created font can actually draw.
enum class IconFont
{
    Fluent,
    Legacy,
    // Neither icon font is installed; draw the Unicode fallback glyph in the normal UI font.
    TextFallback,
};

[[nodiscard]] inline int CALLBACK IconFontProbe(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) noexcept
{
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

[[nodiscard]] inline bool HasFontFamily(const wchar_t* family) noexcept
{
    if (!family || family[0] == L'\0')
    {
        return false;
    }
    const HDC screen = GetDC(nullptr);
    if (!screen)
    {
        return false;
    }
    LOGFONTW request{};
    request.lfCharSet = DEFAULT_CHARSET;
    if (FAILED(StringCchCopyW(request.lfFaceName, LF_FACESIZE, family)))
    {
        (void)ReleaseDC(nullptr, screen);
        return false;
    }
    bool found = false;
    (void)EnumFontFamiliesExW(screen, &request, IconFontProbe, reinterpret_cast<LPARAM>(&found), 0);
    (void)ReleaseDC(nullptr, screen);
    return found;
}

// Creates the best available icon font at the requested pixel height and reports which glyph set it can draw.
[[nodiscard]] inline HFONT CreateIconFont(int pixelHeight, IconFont& selected) noexcept
{
    selected = IconFont::TextFallback;
    const wchar_t* family = kTextFontFamily;
    if (HasFontFamily(kFontFamily))
    {
        selected = IconFont::Fluent;
        family = kFontFamily;
    }
    else if (HasFontFamily(kLegacyFontFamily))
    {
        selected = IconFont::Legacy;
        family = kLegacyFontFamily;
    }
    return CreateFontW(-pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family);
}

[[nodiscard]] constexpr wchar_t SelectGlyph(IconFont font, wchar_t iconGlyph, wchar_t fallbackGlyph) noexcept
{
    return font == IconFont::TextFallback ? fallbackGlyph : iconGlyph;
}
} // namespace FluentIcons
