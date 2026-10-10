#pragma once

#include <cstddef>
#include <limits>
#include <span>
#include <windows.h>
#include <winternl.h>

namespace RedXeNativeProcessQuery
{
using QueryFunction = NTSTATUS(NTAPI*)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);

// Query the full bounded caller-owned buffer once. Starting smaller repeats the expensive system-wide walk even
// though the storage is already allocated. A failed or out-of-bounds query publishes no readable bytes.
[[nodiscard]] inline bool Collect(QueryFunction query, std::span<std::byte> buffer, ULONG& readableBytes) noexcept
{
    readableBytes = 0;
    if (!query || buffer.empty() || buffer.size() > (std::numeric_limits<ULONG>::max)())
        return false;
    ULONG returned = 0;
    const NTSTATUS status =
        query(SystemProcessInformation, buffer.data(), static_cast<ULONG>(buffer.size()), &returned);
    if (status < 0 || returned == 0 || returned > buffer.size())
        return false;
    readableBytes = returned;
    return true;
}
} // namespace RedXeNativeProcessQuery
