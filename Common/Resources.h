#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <windows.h>

namespace RedXeResources
{
[[nodiscard]] inline std::wstring_view LoadEmbeddedStringResourceView(HMODULE module, UINT id) noexcept
{
    const HMODULE instance = module ? module : GetModuleHandleW(nullptr);
    if (! instance)
        return {};
    const wchar_t* value = nullptr;
    const int length     = LoadStringW(instance, id, reinterpret_cast<LPWSTR>(&value), 0);
    return length > 0 && value ? std::wstring_view(value, static_cast<size_t>(length)) : std::wstring_view{};
}

[[nodiscard]] inline std::wstring LoadEmbeddedStringResource(HMODULE module, UINT id) noexcept
{
    return std::wstring(LoadEmbeddedStringResourceView(module, id));
}

[[nodiscard]] inline std::wstring LoadStringResource(HMODULE module, UINT id) noexcept
{
    return LoadEmbeddedStringResource(module, id);
}

template <class... Args> [[nodiscard]] inline std::wstring FormatEmbeddedStringResource(HMODULE module, UINT id, Args&&... args) noexcept
{
    std::wstring pattern = LoadEmbeddedStringResource(module, id);
    if (pattern.empty())
        return {};
    try
    {
        return std::vformat(std::wstring_view(pattern), std::make_wformat_args(args...));
    }
    catch (const std::format_error&)
    {
        return pattern;
    }
}

template <class... Args> [[nodiscard]] inline std::wstring FormatStringResource(HMODULE module, UINT id, Args&&... args) noexcept
{
    return FormatEmbeddedStringResource(module, id, std::forward<Args>(args)...);
}

template <class... Args> inline int MessageBoxResource(HWND owner, HMODULE module, UINT textId, UINT captionId, UINT type, Args&&... args) noexcept
{
    const auto text = [&]()
    {
        if constexpr (sizeof...(Args) == 0)
            return LoadStringResource(module, textId);
        else
            return FormatStringResource(module, textId, std::forward<Args>(args)...);
    }();
    const auto caption = LoadStringResource(module, captionId);
    return MessageBoxW(owner, text.c_str(), caption.c_str(), type);
}
} // namespace RedXeResources
