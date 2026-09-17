#pragma once

// The Launcher shortcut binding shared by the host parser (SettingsV4.cpp, Settings.cpp), Launcher.dll, and the
// tests: one closed object per shortcut with the action binding core (`action`, `target`) and the presentation
// member `icon`. One validator is the single source of truth for the closed shape; target semantics (whether a
// launch target is an absolute path, whether a published verb exists) are resolved by the host at runtime and drawn
// as a warning tile, never as a document error.

#include "Actions/ActionTargets.h"
#include "PlugInterfaces/Action.h"

#include <cstdint>
#include <cstring>
#include <string_view>
#include <windows.h>

#include <yyjson.h>

namespace Launcher
{
inline constexpr char kDefaultAction[] = "system.launch";
inline constexpr uint32_t kMaximumIconBytes = 260;

// A Segoe Fluent Icons glyph name ("Video") or "png:<absolute path>".
[[nodiscard]] inline bool IsIconSyntax(std::string_view icon) noexcept
{
    if (icon.empty() || icon.size() > kMaximumIconBytes)
    {
        return false;
    }
    if (icon.starts_with("png:"))
    {
        return RedXeActions::IsAbsolutePath(icon.substr(4));
    }
    for (const char character : icon)
    {
        const bool allowed = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                             (character >= '0' && character <= '9');
        if (!allowed)
        {
            return false;
        }
    }
    return true;
}

// Whether two shortcuts bind the same thing: equal actions and, for a launch target, a case-insensitive path
// compare (paths) or an exact compare (URIs and every other target).
[[nodiscard]] inline bool ShortcutsEqual(std::string_view leftAction, std::string_view leftTarget,
                                         std::string_view rightAction, std::string_view rightTarget) noexcept
{
    if (leftAction != rightAction)
    {
        return false;
    }
    if (leftAction == kDefaultAction && RedXeActions::IsAbsolutePath(leftTarget) &&
        RedXeActions::IsAbsolutePath(rightTarget))
    {
        wchar_t leftWide[kRedXeMaximumActionTargetBytes + 1]{};
        wchar_t rightWide[kRedXeMaximumActionTargetBytes + 1]{};
        const int leftCount =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, leftTarget.data(), static_cast<int>(leftTarget.size()),
                                leftWide, static_cast<int>(std::size(leftWide) - 1));
        const int rightCount =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rightTarget.data(), static_cast<int>(rightTarget.size()),
                                rightWide, static_cast<int>(std::size(rightWide) - 1));
        if (leftCount > 0 && rightCount > 0)
        {
            return CompareStringOrdinal(leftWide, leftCount, rightWide, rightCount, TRUE) == CSTR_EQUAL;
        }
    }
    return leftTarget == rightTarget;
}

struct ShortcutItem final
{
    std::string_view action = kDefaultAction;
    std::string_view target;
    std::string_view icon;
};

// Validates the closed shape of one shortcuts[] item and returns its borrowed members. `isKnownAction` decides
// whether an action name resolves (the host checks the default namespaces and the registry; a plugin that has no
// registry accepts the grammar). On failure `error` names the problem and the function returns false.
[[nodiscard]] inline bool ParseShortcutItem(yyjson_val* item, bool (*isKnownAction)(std::string_view) noexcept,
                                            ShortcutItem& parsed, const char** error) noexcept
{
    parsed = ShortcutItem{};
    *error = nullptr;
    if (!yyjson_is_obj(item))
    {
        *error = "shortcuts items must be objects.";
        return false;
    }
    bool sawTarget = false;
    yyjson_obj_iter iterator = yyjson_obj_iter_with(item);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        if (name == "action")
        {
            if (!yyjson_is_str(value) || yyjson_get_len(value) > kRedXeMaximumActionNameBytes)
            {
                *error = "action must be an action name such as system.launch.";
                return false;
            }
            const std::string_view action(yyjson_get_str(value), yyjson_get_len(value));
            if (action == "none" || !RedXeIsActionNameSyntax(action.data()) ||
                (isKnownAction && !isKnownAction(action)))
            {
                *error = "action is not a known action name.";
                return false;
            }
            parsed.action = action;
        }
        else if (name == "target")
        {
            if (!yyjson_is_str(value) || yyjson_get_len(value) > kRedXeMaximumActionTargetBytes)
            {
                *error = "target must be a string of at most 512 bytes.";
                return false;
            }
            parsed.target = std::string_view(yyjson_get_str(value), yyjson_get_len(value));
            sawTarget = true;
        }
        else if (name == "icon")
        {
            if (!yyjson_is_str(value) || !IsIconSyntax(std::string_view(yyjson_get_str(value), yyjson_get_len(value))))
            {
                *error = "icon must be a Segoe Fluent Icons glyph name or png:<absolute path>.";
                return false;
            }
            parsed.icon = std::string_view(yyjson_get_str(value), yyjson_get_len(value));
        }
        else
        {
            *error = "shortcuts items accept only action, target, and icon.";
            return false;
        }
    }
    if (parsed.action == kDefaultAction && (!sawTarget || parsed.target.empty()))
    {
        *error = "target is required for system.launch.";
        return false;
    }
    if (parsed.action != kDefaultAction && parsed.icon.empty())
    {
        *error = "icon is required for an action other than system.launch.";
        return false;
    }
    return true;
}
} // namespace Launcher
