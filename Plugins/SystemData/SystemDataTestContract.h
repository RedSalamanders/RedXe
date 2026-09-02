#pragma once

#include "PlugInterfaces/Data.h"

#include <cstdint>

// Test-only sibling interface. It is not part of the published plugin ABI or a host dependency.
struct SystemDataTestDiagnostics final
{
    uint32_t sizeBytes;
    uint64_t sourceStorageBytes;
    uint32_t maximumProcessRows;
    uint32_t processNameCharacters;
    uint32_t sourceOwnedWorkerCount;
    uint32_t sourceOwnedTimerCount;
};

interface __declspec(uuid("1D56CC3A-AB5C-4ED5-8877-AD2D0FD01858")) __declspec(novtable) IRedXeSystemDataTestSource
    : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetTestDiagnostics(SystemDataTestDiagnostics * diagnostics) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE CollectSyntheticProcessSnapshot(uint32_t requestedRows,
                                                                      const RedXeDataSnapshot** snapshot) noexcept = 0;
};
