# DxUi integration

Status: current normative consumer contract
Last reviewed: 2026-09-09

This contract owns how RedXe consumes the standalone DxUi library: the exact source pin, restore/build isolation,
which process modules link `DxUi.lib`, and the COM/POD boundary that keeps DxUi C++ objects inside those modules.
Library behavior remains in DxUi's own contracts. AV audio/camera backends, real IME/touch/screen-reader acceptance,
and matched text/UIA performance remain in [`Plugins_AVControl.md`](../Plugins/Plugins_AVControl.md) and the active
[AV plan](../Plans/WIP/RFC_Plugins_AVControl.md). RedSalamander migration is owned by its active I19 plan.

## Pin and restore

`Dependencies/DxUi.lock.json` identifies `https://github.com/RedSalamanders/DxUi`, one 40-character commit, API
revision 2, and lock target `["DxUi"]`. `build.ps1` runs `restore-dxui.ps1` for the selected platform. Restore clones
that exact commit under `.build/dependencies/DxUi/source/<commit>` and isolates vcpkg/library outputs under a
fingerprint that includes commit, API revision, target architecture, evaluated compiler host, compiler/linker/MSBuild
hashes, SDK version/header/import-library hashes, CRT family and sanitizer annotation policy. It never checks out, resets, or edits a sibling
`DxUi` working tree. A mismatched or dirty pin fails the consumer restore.

The pinned library releases the cached surface
of a hidden or zero-extent `EmbeddedHost`, marks a view dirty only through control invalidation, and bounds its
solid-brush and configured-text-format caches (256 and 96 entries, reported through `EmbeddedStatistics`).
Its `Slider` uses a 6 DIP capsule track, a fixed 20 DIP gray chrome disc, and an accent inner thumb (6 DIP rest, 16 hover,
12 pressed). An unpainted 48 DIP pointer band stays centered on the track so a finger can grab the thumb without seeking.
Painted chrome and hit testing are independent. Pointer hit-testing stays valid while the cached surface is paint-dirty. Consumers
rely on `SetVisible(false)` alone to drop a hidden tile's or an unraised overlay's surface; the next sized `Prepare`
reallocates exactly one surface. The resource consequences for RedXe are owned by
[`Core_PerformanceAndResources.md`](Core_PerformanceAndResources.md).

`RedXe`, `AVControl`, and `AVControlTests` import `Build/RedXe.DxUi.props` / `.targets`, which reference
`src/DxUi.vcxproj` once. Project references keep Configuration/Platform; they do not enumerate library `.cpp` files.
Debug and ASan Debug use `/MDd`, Release `/MD`, matching DxUi. ASan Debug requires actual instrumentation and
the architecture-matching sanitizer runtime. RedXe retains the default STL annotations. There is no `DxUi.dll` to stage.

Restore writes separate resolved properties and evaluated identity files for each platform. The canonical imports
preserve the chosen configuration and platform, and fail before compilation if the consumer's actual toolchain
differs from the restored identity. Builds produce `DxUi.provenance.json` beside the application: exact source/API,
public-header tree, archive hash, build identity and linked-module hashes for RedXe, AVControl and AVControlTests.
The producer verifies each actual linker command names the selected archive; test.ps1 rejects wrong pins, profiles,
missing/duplicate modules and replaced binaries before product regressions. The ordinary sidecar is product evidence,
not a separate release or qualification system.

## Manual update loop

The root build requests one bounded advisory about newer main with successful DxUi CI. It never edits the lock and
an unavailable network/read credential cannot fail a valid pinned build. A maintainer changes the exact lock on a
product branch, builds and runs the product suite, then submits its ordinary PR. A shared-control regression is fixed
and tested in DxUi before updating the consumer pin and repeating product tests. Consumer CI runs the six native
configurations using public HTTPS dependency access. No PAT or organization secret is required. The automatic job
token supplies advisory GitHub API rate allowance. Library success alone does not qualify this product.
CI validates repository skill metadata with the repository-owned validator and pinned Python dependency before
building. Clean runners require no developer-specific Codex installation or home-directory scripts.

Rollback reverts the complete product adoption change, including adapter/build changes, and rebuilds/tests that
previous source revision. Retain the previously qualified product package; a pin-only edit cannot restore an older
library that predates required integration helpers. Current candidate qualification and rollback evidence are tracked
in the [adoption plan](../Plans/WIP/DxUiAdoption_2026-09-09.md).

## Module ownership

`AVControl.dll` owns retained control trees and the shared supplied-device `GraphicsDevice` pool for its instances.
The RedXe host links the same archive for application-side `TextInputServices` and the accessibility root. DxUi C++,
STL, exceptions, and callbacks never cross the plugin ABI. Plugins that do not import DxUi must not depend on its
headers.

## Host bridges

The host owns the HWND, swap chain, presentation, OS focus, TSF/IME association, clipboard owner window, and screen
origin. `WidgetTextClient` adapts `IRedXeTextInputWidget` to `DxUi::TextInputClient`. `AccessibilityHost` owns
`WM_GETOBJECT`, generation-bound sites, and prepared publication. AV adapts `IRedXeAccessibilitySite` to
`DxUi::EmbeddedAccessibilitySite` inside the plugin module.

Text and accessibility ABI records stay in `Widget.h`: a 9,312-byte text snapshot and a 56-byte physical placement
record. `Common/DxUiTextTransport.h` converts between those records and DxUi snapshots inside each module.

## Supported versus remaining

Synthetic host/plugin tests, WARP composition, and the relocated DxUi consumer check establish the pin and adapters.
They do not establish a real IME session, a screen-reader pass, physical touch, displayed-frame resource acceptance,
or AV hardware backends. Those remain AV release gates. Library matched-performance archives that flagged investigation
bands stay open in the AV plan; they are not waived by this contract.

Native test executables run through the existing streaming-process runner, which captures standard output and
standard error in per-executable logs alongside the build output. CI retains these logs on failure; a child test
failure must expose its own assertion message in addition to its exit code. Hidden execution preserves desktop focus.
