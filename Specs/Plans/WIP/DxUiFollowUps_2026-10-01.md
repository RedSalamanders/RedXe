# DxUi follow-ups: registered host messages, vcpkg toolset and pin restore

Status: `ACTIVE`
Created: 2026-10-01
Owner: the next DxUi pin update (`Update-DxUi.ps1`) and the build scripts
Source: the DxUi plan `MergeAndHostHygiene_2026-10-01` (RedSalamanders/DxUi). Current requirements stay in
`Specs/Core/Core_DxUiIntegration.md`.

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

- [ ] When adopting the DxUi pin with registered messages:
  - confirm that every window that attaches a `DxUi::WindowHost` forwards all messages, registered ones (0xC000–0xFFFF) included;
  - update the "Host bridges" text of `Core_DxUiIntegration.md`.
- [ ] Make this repository's `vcpkg-install.ps1` use the Visual Studio installation the build discovers and its default
  toolset, as DxUi's now does.
- [ ] Restore the pin with `-c core.longpaths=true` on the clone and the checkout, or as a sparse checkout that leaves
  out `Measurements/`, `docs/gallery` and `Specs/`. Add a restore check from a deep root.
- [ ] Land the pending pin update (branch `update-DxUi-2026-09-30`, which also moves CI to DxUi's PowerShell
  `validate-build-matrix.ps1`) at a DxUi main that includes these changes. Run `test.ps1` in the three x64
  configurations, record the rollback pin, then move this plan to Done.
