# DxUi integration

Status: current normative consumer contract
Last reviewed: 2026-10-01

This contract owns how RedXe consumes the standalone DxUi library: the exact source pin, restore/build isolation,
which process modules link `DxUi.lib`, and the COM/POD boundary that keeps DxUi C++ objects inside those modules.
Library behavior remains in DxUi's own contracts. AV audio/camera backends, real IME/touch/screen-reader acceptance,
and matched text/UIA performance remain in [`Plugins_AVControl.md`](../Plugins/Plugins_AVControl.md) and the active
[AV plan](../Plans/WIP/RFC_Plugins_AVControl.md). RedSalamander owns its I19 migration and release decision.

## Pin and restore

`Dependencies/DxUi.lock.json` identifies `https://github.com/RedSalamanders/DxUi`, one 40-character commit, API
revision 3, and lock target `["DxUi"]`. The API revision is DxUi's compatibility number. The pinned source names its own
in `capabilities.json`, `Tools/validate_consumer.ps1` rejects a lock that names another, and `Build/DxUiRestore.psm1`
accepts only the revision this product is adapted to. Moving to a new revision is therefore one reviewed change: the
lock, that number and the adapters the revision needs. `Update-DxUi.ps1` changes only the commit. Revision 3 needed no
product source change: the product uses none of the renamed `IGridModel`, `IGridDelegate`, `ITreeModel` and
`ITreeDelegate` interfaces, no Python tool of the library and no `WM_APP` value of DxUi's (see Host bridges), and CI runs
the library's PowerShell `validate-build-matrix.ps1`.

`build.ps1` runs `vcpkg-install.ps1` and then `restore-dxui.ps1` for the selected platform, and both restore the pin
through `Build/DxUiRestore.psm1`. Restore clones that exact commit under `.build/dependencies/DxUi/source/<commit>`, from
a sibling `DxUi` checkout that holds it and otherwise from the canonical repository, and checks it out detached.
- The clone and its checkout use Git long paths. `git clone -c core.longpaths=true` keeps the setting in that clone's own
  configuration; no user or global Git setting changes.
- The working tree is sparse. `Measurements/`, `docs/gallery/` and `Specs/` are left out, because the product neither
  builds nor reads them. Everything the restore, the build and DxUi's consumer interface use stays: `capabilities.json`,
  `Tools/`, `Build/`, `src/`, `include/`, the vcpkg files and the root scripts. A sparse checkout reads as clean to
  `git status`, which `Tools/validate_consumer.ps1` requires.
- DxUi keeps every tracked path within 150 characters, so a product root of up to 35 characters restores even without
  long paths. The setting covers deeper roots, such as a CI runner's, where a longer path made the restore fail as a
  dirty checkout. `Tests/BuildProcessTests/DxUiRestoreTests.ps1` restores a fixture from a root deep enough to pass 259
  characters and requires an exact, clean, sparse checkout with the long file written.

Restore isolates vcpkg/library outputs under a
fingerprint that includes commit, API revision, target architecture, evaluated compiler host, compiler/linker/MSBuild
hashes, SDK version/header/import-library hashes, CRT family and sanitizer annotation policy. The output folder is
`.build/dependencies/DxUi/<first 16 fingerprint hex digits>` so vcpkg's deepest tool paths stay under `MAX_PATH`; the
full fingerprint stays in the identity file and the product provenance. Restore never checks out, resets, or edits a
sibling `DxUi` working tree. A mismatched or dirty pin fails the consumer restore.

`vcpkg-install.ps1` builds the manifest packages with the Visual Studio installation and the default MSVC toolset that
MSBuild compiles with, not the newest toolset vcpkg would find. The two differ when a newer toolset is installed beside
the default and lacks a compiler for a target: a VS 18 Insiders' 14.52 has no x64-hosted ARM64 compiler beside the
default 14.51, and every fresh ARM64 restore failed.
- The installation is the one that holds the MSBuild the build runs (`build.ps1` passes its own as `-MSBuildPath`; a
  standalone run uses `MSBUILD_EXE_PATH`, else the newest installation with MSBuild). The toolset is that installation's
  `VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt`. Neither is hard-coded, and a missing or malformed version
  file fails before vcpkg is cloned.
- For each triplet it installs, the script writes the overlay `.build/vcpkg-triplets/<platform>/<triplet>.cmake`, which is
  the pinned vcpkg checkout's triplet plus `VCPKG_VISUAL_STUDIO_PATH` and `VCPKG_PLATFORM_TOOLSET_VERSION`, rewritten only
  when its bytes change, and passes `--overlay-triplets`. A changed triplet changes vcpkg's package ABI hash, so the first
  install after a change rebuilds the packages.
- The discovery and the overlay writer are DxUi's (`Tools/VisualStudio.psm1` and `Tools/VcpkgTriplet.psm1`, part of its
  consumer interface), imported from the pinned source, which `vcpkg-install.ps1` restores first when it is missing. That
  keeps `build.ps1`'s order: the dependencies, then the DxUi restore. DxUi's own `vcpkg-install.ps1`, which the DxUi
  restore runs, applies the same pin to its dependencies.

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
The producer locates the archive through the resolved output root and verifies each actual linker command names
it; test.ps1 rejects wrong pins, profiles, missing/duplicate modules and replaced binaries before product regressions.
The ordinary sidecar is product evidence, not a separate release or qualification system.

## Manual update loop

The root build requests one bounded advisory about newer main with successful DxUi CI. A newer main commit without a
successful completed validation is red; a validated available update is yellow and prints both the normal
`Update-DxUi.ps1` and the external-validation `Update-DxUi.ps1 -UpdateOnly` commands on separate yellow lines. The color presents the pinned helper's read-only decision and never
edits the lock; an unavailable network/read credential cannot fail a valid pinned build. A maintainer changes the
exact lock on a product branch, builds and runs the product suite, then submits its ordinary PR. A shared-control
regression is fixed and tested in DxUi before updating the consumer pin and repeating product tests. Consumer CI runs the six native
configurations using public HTTPS dependency access. No PAT or organization secret is required. The automatic job
token supplies advisory GitHub API rate allowance. Library success alone does not qualify this product.
`Update-DxUi.ps1` selects only a current `main` commit with successful completed DxUi CI, atomically changes the
lock, and runs `test.ps1`; `Update-DxUi.ps1 -UpdateOnly` skips that local product suite only when equivalent product
validation was completed elsewhere. Neither mode auto-commits, and a local validation failure leaves the changed lock
on the branch for diagnosis.
Pull requests run one x64 Release leg for each update; feature-branch pushes do not start a duplicate matrix.
Pushes to main and explicit workflow dispatch retain the full six-configuration validation entrypoints.
After the product tests, every leg runs the pinned library's `validate-build-matrix.ps1 -Root <checkout>`, which fails
when a native project or the solution does not map all six configurations. It is a PowerShell script at API revision 3;
the Python validator it replaced is gone.
CI validates repository skill metadata with the repository-owned validator and pinned Python dependency before
building. Clean runners require no developer-specific Codex installation or home-directory scripts.

Rollback reverts the complete product adoption change, including adapter/build changes, and rebuilds/tests that
previous source revision. Retain the previously qualified product package; a pin-only edit cannot restore an older
library that predates required integration helpers. Current candidate qualification and rollback evidence are tracked
in the completed adoption records: [2026-09-09](../Plans/Done/DxUiAdoption_2026-09-09.md) and, for the pin at API
revision 3, whose rollback pin is `40c6c215`, [2026-10-01](../Plans/Done/DxUiFollowUps_2026-10-01.md).
The user deferred further ARM64 and ASan qualification on 2026-09-13; the cross-product
follow-up is `Specs/Plans/WIP/DxUi_DeferredPlatformQualification_2026-09-13.md` in
RedSalamander. That deferral is not a runtime pass or a change to the supported matrix.

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

DxUi registers each private window message by name (`RedSalamanders.DxUi.<Component>.<Purpose>.v1`), so its values lie in
0xC000-0xFFFF and no `WM_APP` value belongs to DxUi. A window that attaches a DxUi component passes every message to that
component's `HandleMessage`, registered ones included.
- The product attaches one: `DxUi::TextInputServices` on the main window, which posts its deferred TSF lock there.
  `Application::HandleMessage` passes every message to `TextInputServices::HandleMessage` before its own dispatch, with no
  range or message filter, so the registered message reaches it. A filter placed in front of that call would stop text
  input from being granted its lock.
- The product attaches no `DxUi::ControlHost` to a window: its views are `EmbeddedHost`s, which own no window. So no
  other DxUi message reaches a product window. A window that attached a `ControlHost` would forward every message to its
  `HandleMessage` too, and would post a menu-bar hover with `ContextMenu::PostMenuBarHover`, never a message of its own.
- `AccessibilityHost::IsMessage` runs first and claims only the product's own registered `RedXe.Accessibility.Pending.v1`
  with its cookie. The product's `WM_APP + n` messages cannot meet DxUi's, which are registered.

Text and accessibility ABI records stay in `Widget.h`: a 9,312-byte text snapshot and a 56-byte physical placement
record. `Common/DxUiTextTransport.h` converts between those records and DxUi snapshots inside each module.

## Supported versus remaining

Synthetic host/plugin tests, WARP composition, and the relocated DxUi consumer check establish the pin and adapters.
They do not establish a real IME session, a screen-reader pass, physical touch, displayed-frame resource acceptance,
or AV hardware backends. Those remain AV release gates. The user accepted the bounded I19
offscreen adoption costs under the resource contract; original investigation-band failures
remain evidence. That decision does not qualify the AV plan's broader real-client text/UIA,
hardware or presented-frame resource gates.

Native test executables run through the existing streaming-process runner, which captures standard output and
standard error in per-executable logs alongside the build output. CI retains these logs on failure; a child test
failure must expose its own assertion message in addition to its exit code. Hidden execution preserves desktop focus.
A failed runtime check in a Debug-family test ends the process with its report and exit code 3, never a dialog
([`Build_Process.md`](../Build/Build_Process.md)).
