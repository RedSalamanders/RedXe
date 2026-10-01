# DxUi follow-ups: registered host messages, vcpkg toolset and pin restore

Status: DONE (2026-10-01)
Created: 2026-10-01
Owner: the DxUi pin update (`Update-DxUi.ps1`) and the build scripts. The contracts that carry what it decided are
[`Core_DxUiIntegration.md`](../../Core/Core_DxUiIntegration.md) and [`Build_Process.md`](../../Build/Build_Process.md).
Source: the DxUi plan `MergeAndHostHygiene_2026-10-01` (RedSalamanders/DxUi). Current requirements stay in the contracts above.

## Why

- **Host messages.** DxUi's private window messages were `WM_APP` offsets on the host window. DxUi now registers every
  private message by name (`RedSalamanders.DxUi.<Component>.<Purpose>.v1`) and uses no `WM_APP` value. This product's
  own messages (`Renderer.h` `WM_APP + 1`, `PluginHost.h` `+ 3` and `+ 5`, `Application.h` `+ 4`, `+ 6` and `+ 7`) never
  collided with DxUi's, but every window that attaches a `DxUi::WindowHost` must forward registered messages too.
- **vcpkg toolset.** vcpkg builds dependencies with the newest MSVC toolset of the Visual Studio installation, while
  MSBuild uses its default toolset. On a machine whose newest toolset lacks the x64-hosted ARM64 compiler, every fresh
  ARM64 restore failed. DxUi's `vcpkg-install.ps1`, which the DxUi restore runs, now pins the discovered default
  toolset. This repository's own `vcpkg-install.ps1` does not yet.
- **Pin restore.** `restore-dxui.ps1` clones and checks out the pin without Git long paths (lines 21 and 23). From pin
  `6b34456` until DxUi kept every tracked path within 150 characters, `main` CI failed at restore on all six legs,
  because the runner's prefix (90 characters) and DxUi's longest paths (up to 176) passed 259.

## Tasks

- [x] When adopting the DxUi pin with registered messages:
  - confirm that every window that attaches a `DxUi::WindowHost` forwards all messages, registered ones (0xC000–0xFFFF) included;
  - update the "Host bridges" text of `Core_DxUiIntegration.md`.

  The product attaches exactly one DxUi component to a window: `DxUi::TextInputServices` on the main window, which posts
  its deferred TSF lock there. `Application::HandleMessage` passes every message to `TextInputServices::HandleMessage` before
  its own dispatch, with no range or message filter, so the registered message reaches it. The product attaches no
  `DxUi::ControlHost` (the window host) to any window: its views are `EmbeddedHost`s, which own no window. Host bridges now
  says so.
- [x] Make this repository's `vcpkg-install.ps1` use the Visual Studio installation the build discovers and its default
  toolset, as DxUi's now does. It takes the installation that holds the MSBuild `build.ps1` passes (`-MSBuildPath`; a
  standalone run uses `MSBUILD_EXE_PATH`, else the newest installation with MSBuild), that installation's default toolset,
  and writes an overlay triplet per platform. DxUi's discovery and overlay writer are imported from the pinned source,
  which the script restores first, so `build.ps1` keeps its order.
- [x] Restore the pin with `-c core.longpaths=true` on the clone and the checkout, or as a sparse checkout that leaves
  out `Measurements/`, `docs/gallery` and `Specs/`. Add a restore check from a deep root. Both: the clone and checkout use
  Git long paths, and the checkout is sparse. `Tests/BuildProcessTests/DxUiRestoreTests.ps1` restores a fixture from a deep
  root, and `test.ps1` runs it.
- [x] Land the pending pin update (branch `update-DxUi-2026-09-30`, which also moves CI to DxUi's PowerShell
  `validate-build-matrix.ps1`) at a DxUi main that includes these changes. Run `test.ps1` in the three x64
  configurations, record the rollback pin, then move this plan to Done. The pin is DxUi `271bd54be24e`, main with its PRs
  42, 44 and 45 merged, at API revision 3.

## Decided during the work

- Test processes never open a dialog. A Debug or ASan Debug test could open the CRT's modal Abort/Retry/Ignore box for a
  failed runtime check, and none of the test executables routed those reports. Every native test executable now calls
  `Tests/Support/FailureReports.h` first, and `test.ps1` runs a hidden self-test (`Build_Process.md`). This is why the
  Debug and ASan Debug suites could run on a developer's desktop for this update.

## Closeout evidence

- **Pin and rollback.** The lock moves from `40c6c215736197654d80648dc7712b28c470fa9e` (the rollback target, set by
  `fcbb21c`, product `main` at `a4671df`) to `271bd54be24eadf3f03ba5221d1be7be069860f2`, and its `apiRevision` from 2 to 3.
  Rolling back reverts the complete adoption change, the CI step included: the old pin ships
  `Tools/validate_build_matrix.py` and lacks `validate-build-matrix.ps1`. Revision 3 changed no product source; the product
  uses none of the renamed `IGridModel`, `IGridDelegate`, `ITreeModel` and `ITreeDelegate`.
- **Builds.** x64 and ARM64 Debug, Release and ASan Debug build through `build.ps1` with no warnings and no errors, each
  with a `DxUi.provenance.json` that names the pin and its three linked modules. The ARM64 builds are compile-only on this
  x64 host. None of them used an environment overlay or a Git setting: `vcpkg-install.ps1 -Platform All` installed both
  platforms with MSVC 14.51.36231, and a fresh ARM64 restore of DxUi compiled with `Hostx64\arm64\cl.exe` of the same
  toolset, where the newest installed toolset, 14.52, has no x64-hosted ARM64 compiler.
- **Tests.** `test.ps1` passes in x64 Debug, Release and ASan Debug, including `DxUiRestoreTests.ps1`, the AddressSanitizer
  probe and the failure-report self-test (exit code 3 in Debug and ASan Debug, 0 in Release). The pinned
  `validate-build-matrix.ps1` reports 33 native project and solution files and no mapping errors, and `measure-av-views.ps1`,
  which CI runs after the suites, completed in Release and Debug. DxUi's own CI run for the pin completed successfully.
- **Restore.** The sparse checkout writes 210 of the 3,467 tracked files (5.9 of 28.6 MB) and reads clean to
  `git status`. A restore of the real repository from GitHub into a 162-character root was exact, clean and sparse. The
  fixture restore from a root deep enough to pass 259 characters fails without long paths and passes with them, and eight
  throwaway mutants of the restore module each fail the test.
- **Not established.** Native ARM64 runtime and sanitizer detection, real IME, screen-reader and touch sessions, AV
  hardware backends and displayed-frame resource acceptance, which stay with the AV release gates
  (`Core_DxUiIntegration.md`, Supported versus remaining). `validate-skills.ps1` was not run locally: PyYAML is not
  installed and CI runs it.
