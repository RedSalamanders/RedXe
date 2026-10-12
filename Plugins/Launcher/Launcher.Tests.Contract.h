#pragma once

#include <cstddef>
#include <cstdint>
#include <shlobj.h>
#include <windows.h>

struct LauncherTestDiagnostics final
{
    uint32_t sizeBytes;
    uint32_t authoredCount;
    uint32_t displayCount;
    uint32_t usingTaskbarPins;
    uint32_t columns;
    uint32_t rows;
    uint32_t lastInstanceCount;
    uint32_t lastDrawCount;
    uint32_t lastLaunchKind;
    uint32_t lastShellMask;
    uint32_t lastVerbWasNull;
    uint32_t lastShow;
    uint64_t extractCalls;
    uint64_t textureUploads;
    uint64_t drawCalls;
    uint64_t launchCount;
    uint64_t shellExecuteCount;
    uint32_t largestIconEdge;
    uint32_t liveWidgets;
    uint32_t pageCount;
    uint32_t pageIndex;
    int32_t pageSlidePx;
    uint32_t pageSettling;
    uint32_t iconHalfExtentPx;
};

enum class LauncherShellPhase : uint32_t
{
    CreateItem,
    BindHandler,
    GetLocation,
    HandlerExtraction,
    DecodeIcon,
    GetImage,
    DecodeImage,
    ImageList,
    ShieldCheck,
    CacheValidation,
    CacheRead,
    CacheStore,
    ParentItem,
    ParentFolder,
    ChildParse,
    ParentMetadata,
    ParentContext,
};
inline constexpr uint32_t kLauncherShellPhaseCount = 17;

// Call-local wall-clock attribution. A clear completedMask bit means unavailable, not zero cost.
// GetImage/ImageList cover fallback work; ImageList includes its decoding. Hashing is outside totalTicks.
// ParentItem/ParentFolder/ChildParse split relative setup; CreateItem is full-path setup, BindHandler the extractor.
// ShieldCheck includes the combined location query; GetLocation records the ordinary fallback/reference query.
// ParentMetadata/ParentContext cover fresh filesystem data and call-local binding-context setup. ParentItem
// includes assisted creation and any ordinary retry; repeated calls accumulate ticks and retain the last result.
struct LauncherShellTimings final
{
    uint32_t sizeBytes;
    uint32_t completedMask;
    uint64_t frequency;
    uint64_t totalTicks;
    uint64_t phaseTicks[kLauncherShellPhaseCount];
    uint32_t phaseCalls[kLauncherShellPhaseCount];
    HRESULT phaseResults[kLauncherShellPhaseCount];
    uint64_t pixelHash;
    uint32_t sourceEdge;
    uint32_t sourceKind; // 0 none, 1 handler, 2 image factory, 3 image list, 4 stock image list, 5 cached handler.
};
static_assert(sizeof(LauncherShellTimings) == 312);
static_assert(offsetof(LauncherShellTimings, phaseTicks) == 24);
static_assert(offsetof(LauncherShellTimings, pixelHash) == 296);

enum class LauncherShellParentFailure : uint32_t
{
    None,
    Metadata,
    Allocation,
    BindContext,
    Registration,
    PartialItem,
    NullItem,
    ReparseMetadata,
    FileMetadata,
};

struct LauncherIconCacheDiagnostics final
{
    uint32_t sizeBytes;
    uint32_t capacity;
    uint32_t residentCount;
    uint32_t allocatedBytes;
    uint64_t hits;
    uint64_t stores;
    uint64_t evictions;
};
static_assert(sizeof(LauncherIconCacheDiagnostics) == 40);

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_LAUNCHER_TEST_API __declspec(dllexport)
#else
#define REDXE_LAUNCHER_TEST_API
#endif

extern "C"
{
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherSetTestPinDirectory(const wchar_t* directory) noexcept;
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherGetTestDiagnostics(
        LauncherTestDiagnostics* diagnostics) noexcept;
    REDXE_LAUNCHER_TEST_API void __stdcall RedXeLauncherResetTestDiagnostics() noexcept;
    // 0 clears; 1 fails replacement texture creation; 2 fails its view, after texture allocation. Automated hosts only.
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherSetTestIconFailure(uint32_t phase) noexcept;
    // Automated hosts only, UI thread. Borrows the extractor only for this call; exercises the production
    // handler/decode path. Outputs are zero on refusal/failure. Adds no retained test override or COM owner.
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherTestShellHandler(IExtractIconW* extractor,
                                                                            uint64_t* pixelHash,
                                                                            uint32_t* sourceEdge) noexcept;
    // Automated hosts only, UI-thread/STA. Exactly one of target/extractor is non-null. Borrows them for
    // this call through the production path; no retained override, owner or timing state. Exact-size output
    // records clear before validation, then retain completed failure phases; failed pixels stay zero.
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherTestShellIcon(const wchar_t* target,
                                                                         IExtractIconW* extractor,
                                                                         LauncherShellTimings* timings) noexcept;
    // Automated hosts only, UI-thread/STA. Borrows 1..32 targets through one production refresh batch.
    // Size validation is atomic and leaves every record untouched on mismatch. Otherwise all records
    // clear before authorization/target validation; no target is processed until every target is valid.
    // Attempts all targets and returns the first failure. Shared folder ownership ends before return.
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherTestShellBatch(const wchar_t* const* targets, uint32_t count,
                                                                          LauncherShellTimings* timings) noexcept;
    // Automated hosts only, UI-thread/STA. One diagnostic metadata-assisted refresh with a call-local setup fault.
    // Ordinary host/batch refreshes never select this rejected alternative. The fault and all temporary
    // filesystem/context owners end before return; output rules match ShellIcon.
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherTestShellParent(const wchar_t* target,
                                                                           LauncherShellParentFailure failure,
                                                                           LauncherShellTimings* timings) noexcept;
    // Automated hosts only, UI-thread/STA. Borrows a real file target and extractor for the cache path.
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherTestCachedShellHandler(
        const wchar_t* target, IExtractIconW* extractor, LauncherShellTimings* timings) noexcept;
    // Automated hosts only, UI thread. 0 reads; 1 clears; 2 fails the next clone; 3 fails the next store.
    // Exact-size records clear before authorization/operation checks. Shutdown releases all cached bytes.
    REDXE_LAUNCHER_TEST_API HRESULT __stdcall RedXeLauncherTestIconCache(
        uint32_t operation, LauncherIconCacheDiagnostics* diagnostics) noexcept;
}

using LauncherSetTestPinDirectoryFn = decltype(&RedXeLauncherSetTestPinDirectory);
using LauncherGetTestDiagnosticsFn = decltype(&RedXeLauncherGetTestDiagnostics);
using LauncherResetTestDiagnosticsFn = decltype(&RedXeLauncherResetTestDiagnostics);
using LauncherSetTestIconFailureFn = decltype(&RedXeLauncherSetTestIconFailure);
using LauncherTestShellHandlerFn = decltype(&RedXeLauncherTestShellHandler);
using LauncherTestShellIconFn = decltype(&RedXeLauncherTestShellIcon);
using LauncherTestShellBatchFn = decltype(&RedXeLauncherTestShellBatch);
using LauncherTestShellParentFn = decltype(&RedXeLauncherTestShellParent);
using LauncherTestCachedShellHandlerFn = decltype(&RedXeLauncherTestCachedShellHandler);
using LauncherTestIconCacheFn = decltype(&RedXeLauncherTestIconCache);

inline constexpr char kLauncherSetTestPinDirectoryExport[] = "RedXeLauncherSetTestPinDirectory";
inline constexpr char kLauncherGetTestDiagnosticsExport[] = "RedXeLauncherGetTestDiagnostics";
inline constexpr char kLauncherResetTestDiagnosticsExport[] = "RedXeLauncherResetTestDiagnostics";
inline constexpr char kLauncherSetTestIconFailureExport[] = "RedXeLauncherSetTestIconFailure";
inline constexpr char kLauncherTestShellHandlerExport[] = "RedXeLauncherTestShellHandler";
inline constexpr char kLauncherTestShellIconExport[] = "RedXeLauncherTestShellIcon";
inline constexpr char kLauncherTestShellBatchExport[] = "RedXeLauncherTestShellBatch";
inline constexpr char kLauncherTestShellParentExport[] = "RedXeLauncherTestShellParent";
inline constexpr char kLauncherTestCachedShellHandlerExport[] = "RedXeLauncherTestCachedShellHandler";
inline constexpr char kLauncherTestIconCacheExport[] = "RedXeLauncherTestIconCache";

#undef REDXE_LAUNCHER_TEST_API
