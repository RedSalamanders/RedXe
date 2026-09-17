#pragma once

// The default action namespaces the host hardcodes: page, widget, redxe, system, keys, and mouse. They need no
// DLL and no registration; a publisher contract that names one of them is a collision. The tables use the same
// RedXeActionNamespace / RedXeActionDescriptor records a published contract uses, so validation, the monitor tile,
// and documentation treat default and published actions alike. Pure: no Win32 calls, no allocation.

#include "PlugInterfaces/Action.h"

#include <cstdint>
#include <string_view>

namespace HostActionCatalog
{
// Borrowed table of the default namespaces.
[[nodiscard]] const RedXeActionNamespace* Namespaces(uint32_t& count) noexcept;

// True when actionNamespace (the first segment only) is a default namespace.
[[nodiscard]] bool IsDefaultNamespace(std::string_view actionNamespace) noexcept;

// The descriptor of a default action, or null when no default namespace defines it.
[[nodiscard]] const RedXeActionDescriptor* Find(std::string_view actionName) noexcept;

// True when the action belongs to a namespace Application executes itself (page, widget, redxe) rather than
// HostActions::Execute (system, keys, mouse).
[[nodiscard]] bool IsApplicationNamespace(std::string_view actionName) noexcept;

// The namespace segment of an action name (empty when the name has none).
[[nodiscard]] std::string_view NamespaceOf(std::string_view actionName) noexcept;

// Whether a document may bind this name: the grammar holds and either a default namespace defines the verb or the
// namespace is registered to a publisher in BundledPlugins.h (its verbs resolve at runtime). "none" is not a name.
[[nodiscard]] bool IsKnownActionName(std::string_view actionName) noexcept;
[[nodiscard]] bool IsRegisteredNamespace(std::string_view actionNamespace) noexcept;
} // namespace HostActionCatalog
