#pragma once

#include <cstddef>
#include <string_view>

namespace Launcher
{
// Only measured drive-qualified filesystem targets participate in folder reuse. UNC roots, shell
// namespaces, device paths and other parsing syntaxes retain the original full-path shell route.
// A zero result means no reusable parent; keep C:\ intact rather than turning it into drive-relative C:.
[[nodiscard]] constexpr size_t ShellIconParentLength(std::wstring_view target) noexcept
{
    if (target.size() < 4 || target.size() > 512 ||
        !((target[0] >= L'A' && target[0] <= L'Z') || (target[0] >= L'a' && target[0] <= L'z')) || target[1] != L':' ||
        target[2] != L'\\')
        return 0;
    const size_t separator = target.find_last_of(L'\\');
    const auto child = target.substr(separator + 1);
    if (child.empty() || child == L"." || child == L"..")
        return 0;
    return separator == 2 ? 3 : separator;
}
} // namespace Launcher
