#pragma once

// Segoe Fluent Icons glyph names accepted by binding `icon` members (Logicon key faces, Launcher shortcut tiles).
// Private-use code points are written as escapes so they survive every editor. Shared by every consumer so one
// table decides which names exist.

#include <cstdint>
#include <cstring>
#include <string_view>

namespace RedXeActions
{
struct NamedGlyph final
{
    const char* name;
    wchar_t glyph;
};

// Segoe Fluent Icons private-use code points, written as escapes so they survive every editor.
inline constexpr NamedGlyph kFluentGlyphs[] = {
    {"ChevronLeft", L'\xE76B'}, {"ChevronRight", L'\xE76C'}, {"ChevronUp", L'\xE70E'}, {"ChevronDown", L'\xE70D'},
    {"Back", L'\xE72B'},        {"Forward", L'\xE72A'},      {"Home", L'\xE80F'},      {"Settings", L'\xE713'},
    {"Play", L'\xE768'},        {"Pause", L'\xE769'},        {"Stop", L'\xE71A'},      {"Next", L'\xE893'},
    {"Previous", L'\xE892'},    {"Volume", L'\xE767'},       {"Mute", L'\xE74F'},      {"Microphone", L'\xE720'},
    {"Camera", L'\xE722'},      {"Video", L'\xE714'},        {"Search", L'\xE721'},    {"Refresh", L'\xE72C'},
    {"Sync", L'\xE895'},        {"Add", L'\xE710'},          {"Remove", L'\xE738'},    {"Cancel", L'\xE711'},
    {"Accept", L'\xE8FB'},      {"Pin", L'\xE718'},          {"Folder", L'\xE8B7'},    {"Document", L'\xE8A5'},
    {"Link", L'\xE71B'},        {"Lock", L'\xE72E'},         {"Power", L'\xE7E8'},     {"Brightness", L'\xE706'},
    {"Keyboard", L'\xE765'},    {"Mouse", L'\xE962'},        {"Globe", L'\xE774'},     {"Mail", L'\xE715'},
    {"Calendar", L'\xE787'},    {"Clock", L'\xE823'},        {"Info", L'\xE946'},      {"Warning", L'\xE7BA'},
    {"Error", L'\xEA39'},       {"Help", L'\xE897'},         {"Star", L'\xE734'},      {"Heart", L'\xEB51'},
    {"FullScreen", L'\xE740'},  {"BackToWindow", L'\xE73F'}, {"Save", L'\xE74E'},      {"Share", L'\xE72D'},
    {"Copy", L'\xE8C8'},        {"Undo", L'\xE7A7'},         {"Redo", L'\xE7A6'},      {"Diagnostic", L'\xE9D9'},
    {"AllApps", L'\xE71D'},     {"Map", L'\xE707'},          {"Music", L'\xE8D6'},     {"Photo", L'\xE91B'},
    {"Headphone", L'\xE7F6'},   {"Phone", L'\xE717'},        {"Print", L'\xE749'},     {"Tag", L'\xE8EC'},
    {"Bookmarks", L'\xE8A4'},   {"Repair", L'\xE90F'},       {"Cloud", L'\xE753'},     {"Download", L'\xE896'},
    {"Upload", L'\xE898'},      {"Delete", L'\xE74D'},       {"Edit", L'\xE70F'},      {"Filter", L'\xE71C'},
    {"Zoom", L'\xE71E'},        {"ZoomOut", L'\xE71F'},      {"View", L'\xE890'},      {"List", L'\xEA37'},
    {"Emoji", L'\xE76E'},       {"Game", L'\xE7FC'},         {"Tv", L'\xE7F4'},        {"Devices", L'\xE772'},
    {"Bluetooth", L'\xE702'},   {"Wifi", L'\xE701'},         {"Lightbulb", L'\xEA80'}, {"Dashboard", L'\xF246'},
};

// The code point for a glyph name, or 0 when the name is unknown.
[[nodiscard]] inline wchar_t FluentGlyphFromName(std::string_view name) noexcept
{
    for (const NamedGlyph& candidate : kFluentGlyphs)
    {
        if (name == candidate.name)
        {
            return candidate.glyph;
        }
    }
    return 0;
}

[[nodiscard]] inline uint32_t FluentGlyphNameCount() noexcept
{
    return static_cast<uint32_t>(sizeof(kFluentGlyphs) / sizeof(kFluentGlyphs[0]));
}

[[nodiscard]] inline const char* FluentGlyphNameAt(uint32_t index) noexcept
{
    return index < FluentGlyphNameCount() ? kFluentGlyphs[index].name : nullptr;
}
} // namespace RedXeActions
