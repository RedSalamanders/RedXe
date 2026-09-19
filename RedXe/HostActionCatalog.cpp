#include "HostActionCatalog.h"

#include "BundledPlugins.h"

#include <array>
#include <cstring>

namespace HostActionCatalog
{
namespace
{
constexpr uint32_t kSize = sizeof(RedXeActionDescriptor);

constexpr RedXeActionDescriptor Action(const char* name, const wchar_t* displayName, uint32_t targetKind,
                                       const wchar_t* targetSyntax, uint32_t flags = RedXeActionFlagNone,
                                       int32_t minimum = 0, int32_t maximum = 0, const char* options = nullptr) noexcept
{
    return RedXeActionDescriptor{kSize, flags, name, displayName, targetSyntax, targetKind, minimum, maximum, options};
}

constexpr std::array kPageActions{
    Action("page.next", L"Next page", RedXeActionTargetNone, L""),
    Action("page.previous", L"Previous page", RedXeActionTargetNone, L""),
    Action("page.first", L"First page", RedXeActionTargetNone, L""),
    Action("page.last", L"Last page", RedXeActionTargetNone, L""),
    Action("page.goto", L"Go to page", RedXeActionTargetPageRef, L"<page id> or <zero-based index>"),
};

constexpr std::array kWidgetActions{
    Action("widget.raise", L"Raise widget", RedXeActionTargetWidgetRef, L"<pageId>/<ordinal> or <ordinal>"),
    Action("widget.toggle", L"Raise or dismiss widget", RedXeActionTargetWidgetRef, L"<pageId>/<ordinal> or <ordinal>"),
    Action("widget.dismiss", L"Dismiss raised widget", RedXeActionTargetNone, L""),
    Action("widget.next", L"Raise next widget", RedXeActionTargetNone, L""),
    Action("widget.previous", L"Raise previous widget", RedXeActionTargetNone, L""),
};

constexpr std::array kRedXeActions{
    Action("redxe.settings.reload", L"Reload settings", RedXeActionTargetNone, L""),
    Action("redxe.settings.edit", L"Edit settings file", RedXeActionTargetNone, L""),
    Action("redxe.logs.open", L"Open logs folder", RedXeActionTargetNone, L""),
    Action("redxe.screenshot", L"Save screenshot", RedXeActionTargetText, L"<png path>[@<pageId>[/<ordinal>]]"),
    Action("redxe.quit", L"Quit RedXe", RedXeActionTargetEnum, L"now", RedXeActionFlagDestructive, 0, 0, "now"),
    Action("redxe.dock.show", L"Reveal the dock", RedXeActionTargetNone, L""),
    Action("redxe.dock.hide", L"Hide the dock", RedXeActionTargetNone, L""),
    Action("redxe.dock.toggle", L"Reveal or hide the dock", RedXeActionTargetNone, L""),
};

constexpr std::array kSystemActions{
    Action("system.launch", L"Launch", RedXeActionTargetPathOrUri, L"<absolute path> or <scheme>:<uri>"),
    Action("system.open", L"Open", RedXeActionTargetPathOrUri, L"<absolute path> or <scheme>:<uri>"),
    Action("system.run", L"Run process", RedXeActionTargetCommandLine, L"<absolute exe> [arguments]"),
    Action("system.lock", L"Lock workstation", RedXeActionTargetNone, L""),
    Action("system.sleep", L"Sleep", RedXeActionTargetEnum, L"now", RedXeActionFlagDestructive, 0, 0, "now"),
    Action("system.hibernate", L"Hibernate", RedXeActionTargetEnum, L"now", RedXeActionFlagDestructive, 0, 0, "now"),
    Action("system.logoff", L"Sign out", RedXeActionTargetEnum, L"now", RedXeActionFlagDestructive, 0, 0, "now"),
    Action("system.shutdown", L"Shut down", RedXeActionTargetNowOrSeconds, L"now or <seconds 0-3600>",
           RedXeActionFlagDestructive, 0, 3600),
    Action("system.restart", L"Restart", RedXeActionTargetNowOrSeconds, L"now or <seconds 0-3600>",
           RedXeActionFlagDestructive, 0, 3600),
    Action("system.shutdown.cancel", L"Cancel shutdown", RedXeActionTargetNone, L""),
    Action("system.process.close", L"Close application", RedXeActionTargetWindow,
           L"foreground, exe:<image.exe>, class:<class>, or title:<substring>"),
    Action("system.power.plan", L"Power plan", RedXeActionTargetText,
           L"balanced, highPerformance, powerSaver, or <GUID>"),
    Action("system.theme", L"Windows theme", RedXeActionTargetEnum, L"light, dark, or toggle", RedXeActionFlagNone, 0,
           0, "light|dark|toggle"),
    Action("system.taskManager", L"Task Manager", RedXeActionTargetNone, L""),
};

constexpr std::array kKeysActions{
    Action("keys.press", L"Press keys", RedXeActionTargetChords, L"<chord>[,<chord>...] such as Ctrl+Shift+Esc",
           RedXeActionFlagInjectsInput),
    Action("keys.down", L"Hold keys", RedXeActionTargetChords, L"<chord>", RedXeActionFlagInjectsInput),
    Action("keys.up", L"Release keys", RedXeActionTargetChords, L"<chord>", RedXeActionFlagInjectsInput),
    Action("keys.type", L"Type text", RedXeActionTargetText, L"<text>", RedXeActionFlagInjectsInput),
    Action("keys.media", L"Media key", RedXeActionTargetEnum,
           L"play-pause, stop, next-track, previous-track, volume-up, volume-down, or mute",
           RedXeActionFlagInjectsInput, 0, 0, "play-pause|stop|next-track|previous-track|volume-up|volume-down|mute"),
    Action("keys.lock", L"Toggle lock key", RedXeActionTargetEnum, L"caps, num, or scroll", RedXeActionFlagInjectsInput,
           0, 0, "caps|num|scroll"),
    Action("keys.layout", L"Keyboard layout", RedXeActionTargetText, L"<language tag such as en-US> or <KLID>"),
};

constexpr std::array kMouseActions{
    Action("mouse.move", L"Move cursor", RedXeActionTargetPoint, L"<x>,<y>, +<dx>,+<dy>, or center[@<monitor>]",
           RedXeActionFlagInjectsInput),
    Action("mouse.click", L"Click", RedXeActionTargetEnum, L"left, right, middle, x1, or x2",
           RedXeActionFlagInjectsInput, 0, 0, "left|right|middle|x1|x2"),
    Action("mouse.doubleClick", L"Double-click", RedXeActionTargetEnum, L"left, right, middle, x1, or x2",
           RedXeActionFlagInjectsInput, 0, 0, "left|right|middle|x1|x2"),
    Action("mouse.down", L"Hold button", RedXeActionTargetEnum, L"left, right, middle, x1, or x2",
           RedXeActionFlagInjectsInput, 0, 0, "left|right|middle|x1|x2"),
    Action("mouse.up", L"Release button", RedXeActionTargetEnum, L"left, right, middle, x1, or x2",
           RedXeActionFlagInjectsInput, 0, 0, "left|right|middle|x1|x2"),
    Action("mouse.scroll", L"Scroll", RedXeActionTargetDelta, L"+<n> or -<n> detents", RedXeActionFlagInjectsInput,
           -100, 100),
    Action("mouse.scroll.horizontal", L"Scroll horizontally", RedXeActionTargetDelta, L"+<n> or -<n> detents",
           RedXeActionFlagInjectsInput, -100, 100),
    Action("mouse.speed", L"Pointer speed", RedXeActionTargetInteger, L"1 through 20", RedXeActionFlagNone, 1, 20),
};

constexpr RedXeActionNamespace Namespace(const char* name, const RedXeActionDescriptor* actions, size_t count) noexcept
{
    return RedXeActionNamespace{sizeof(RedXeActionNamespace), static_cast<uint32_t>(count), name, actions};
}

constexpr std::array kNamespaces{
    Namespace("page", kPageActions.data(), kPageActions.size()),
    Namespace("widget", kWidgetActions.data(), kWidgetActions.size()),
    Namespace("redxe", kRedXeActions.data(), kRedXeActions.size()),
    Namespace("system", kSystemActions.data(), kSystemActions.size()),
    Namespace("keys", kKeysActions.data(), kKeysActions.size()),
    Namespace("mouse", kMouseActions.data(), kMouseActions.size()),
};

consteval bool CatalogIsValid() noexcept
{
    for (const RedXeActionNamespace& space : kNamespaces)
    {
        for (uint32_t index = 0; index < space.actionCount; ++index)
        {
            const RedXeActionDescriptor& descriptor = space.actions[index];
            if (!RedXeIsActionNameSyntax(descriptor.name) || !RedXeActionInNamespace(descriptor.name, space.name))
            {
                return false;
            }
            if (descriptor.targetKind == RedXeActionTargetEnum && !descriptor.targetOptions)
            {
                return false;
            }
            for (uint32_t previous = 0; previous < index; ++previous)
            {
                const char* left = space.actions[previous].name;
                const char* right = descriptor.name;
                size_t position = 0;
                while (left[position] != '\0' && left[position] == right[position])
                {
                    ++position;
                }
                if (left[position] == right[position])
                {
                    return false;
                }
            }
        }
    }
    return true;
}

static_assert(CatalogIsValid());
} // namespace

const RedXeActionNamespace* Namespaces(uint32_t& count) noexcept
{
    count = static_cast<uint32_t>(kNamespaces.size());
    return kNamespaces.data();
}

std::string_view NamespaceOf(std::string_view actionName) noexcept
{
    const size_t dot = actionName.find('.');
    return dot == std::string_view::npos ? std::string_view{} : actionName.substr(0, dot);
}

bool IsDefaultNamespace(std::string_view actionNamespace) noexcept
{
    for (const RedXeActionNamespace& space : kNamespaces)
    {
        if (actionNamespace == space.name)
        {
            return true;
        }
    }
    return false;
}

const RedXeActionDescriptor* Find(std::string_view actionName) noexcept
{
    const std::string_view prefix = NamespaceOf(actionName);
    for (const RedXeActionNamespace& space : kNamespaces)
    {
        if (prefix != space.name)
        {
            continue;
        }
        for (uint32_t index = 0; index < space.actionCount; ++index)
        {
            if (actionName == space.actions[index].name)
            {
                return &space.actions[index];
            }
        }
        return nullptr;
    }
    return nullptr;
}

bool IsApplicationNamespace(std::string_view actionName) noexcept
{
    const std::string_view prefix = NamespaceOf(actionName);
    return prefix == "page" || prefix == "widget" || prefix == "redxe";
}

bool IsRegisteredNamespace(std::string_view actionNamespace) noexcept
{
    for (const RedXeBundledActionNamespaceSpec& entry : kRedXeBundledActionNamespaces)
    {
        if (actionNamespace == entry.actionNamespace)
        {
            return true;
        }
    }
    return false;
}

bool IsKnownActionName(std::string_view actionName) noexcept
{
    if (actionName.size() > kRedXeMaximumActionNameBytes)
    {
        return false;
    }
    std::array<char, kRedXeMaximumActionNameBytes + 1> terminated{};
    std::memcpy(terminated.data(), actionName.data(), actionName.size());
    if (!RedXeIsActionNameSyntax(terminated.data()))
    {
        return false;
    }
    const std::string_view space = NamespaceOf(actionName);
    if (IsDefaultNamespace(space))
    {
        return Find(actionName) != nullptr;
    }
    return IsRegisteredNamespace(space);
}
} // namespace HostActionCatalog
