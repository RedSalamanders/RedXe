#pragma once

#include "PlugInterfaces/Data.h"

#include <cstdint>

struct RedXeNativeWalkSample;

// Test-only sibling interface. It is not part of the published plugin ABI or a host dependency.
struct SystemDataTestDiagnostics final
{
    uint32_t sizeBytes;
    uint64_t sourceStorageBytes;
    uint32_t maximumProcessRows;
    uint32_t processNameCharacters;
    uint32_t sourceOwnedWorkerCount;
    uint32_t sourceOwnedTimerCount;
    uint64_t processCollections;
    uint64_t processHandleOpens;
    uint64_t processHistoryCommits;
    uint32_t processValueStride;
    uint32_t processBackend;
};
static_assert(sizeof(SystemDataTestDiagnostics) == 64);

interface __declspec(uuid("1D56CC3A-AB5C-4ED5-8877-AD2D0FD01858")) __declspec(novtable) IRedXeSystemDataTestSource
    : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetTestDiagnostics(SystemDataTestDiagnostics * diagnostics) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE CollectSyntheticProcessSnapshot(uint32_t requestedRows,
                                                                      const RedXeDataSnapshot** snapshot) noexcept = 0;
    // Copies a bounded normalized walk and system-time epoch for the next batch requesting process/thread data.
    // An invalid walk exercises the ordinary Toolhelp fallback. No pointer is retained or production mode added.
    virtual HRESULT STDMETHODCALLTYPE SetNextProcessWalkForTest(const RedXeNativeWalkSample* sample,
                                                                uint64_t systemTotal100ns) noexcept = 0;
};
