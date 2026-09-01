#pragma once

#include "PlugInterfaces/Data.h"

#include <cstdint>

// Test-only sibling interface. It is not part of the published plugin ABI or a host dependency.
struct SystemDataTestDiagnostics final
{
    std::uint32_t sizeBytes;
    std::uint64_t sourceStorageBytes;
    std::uint32_t maximumProcessRows;
    std::uint32_t processNameCharacters;
    std::uint32_t sourceOwnedWorkerCount;
    std::uint32_t sourceOwnedTimerCount;
};

interface __declspec(uuid("1D56CC3A-AB5C-4ED5-8877-AD2D0FD01858")) __declspec(novtable) IRedXeSystemDataTestSource
    : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetTestDiagnostics(SystemDataTestDiagnostics * diagnostics) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE CollectSyntheticProcessSnapshot(std::uint32_t requestedRows,
                                                                      const RedXeDataSnapshot** snapshot) noexcept = 0;
};
