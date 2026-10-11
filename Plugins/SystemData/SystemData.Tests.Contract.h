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
    uint64_t arenaReservedBytes;
    uint64_t arenaCommittedBytes;
    uint64_t arenaCommittedFamilies;
    const void* arenaBase;
    uint32_t arenaCommitAttempts;
    uint32_t controlStorageBytes;
};
static_assert(sizeof(SystemDataTestDiagnostics) == 104);

struct SystemDataTestArenaOwnership final
{
    uint32_t sizeBytes;
    uint32_t sourceCount;
    uint64_t reservedBytes;
    uint64_t committedBytes;
    uint32_t gpuOwners;
    uint32_t netOwners;
    uint32_t powerOwners;
    uint32_t reserved;
};
static_assert(sizeof(SystemDataTestArenaOwnership) == 40);

// Quiescent test-only module controls, resolved by name; no host or public ABI dependency.
inline constexpr char kSystemDataTestArenaOwnershipExport[] = "RedXeSystemDataTestArenaOwnership";
inline constexpr char kSystemDataTestAllocationFaultExport[] = "RedXeSystemDataTestAllocationFault";
inline constexpr char kSystemDataTestDecodeWalkExport[] = "RedXeSystemDataTestDecodeWalk";
using SystemDataTestArenaOwnershipFn = HRESULT(WINAPI*)(SystemDataTestArenaOwnership*) noexcept;
using SystemDataTestAllocationFaultFn = HRESULT(WINAPI*)(uint32_t) noexcept;
using SystemDataTestDecodeWalkFn = BOOL(WINAPI*)(const void*, uint32_t, RedXeNativeWalkSample*) noexcept;

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
    // Fails one actual first-use commitment after the requested number of successful new commits.
    // Reusing an already committed family neither consumes the countdown nor performs a commit.
    virtual HRESULT STDMETHODCALLTYPE FailArenaCommitForTest(uint32_t successfulNewCommitsBeforeFailure) noexcept = 0;
};
