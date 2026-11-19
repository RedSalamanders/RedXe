# RedXe Development Guidelines

## Project overview

RedXe is the native Windows foundation for the XENEON EDGE dashboard, built with Win32, Direct3D 11, DXGI, WIL,
yyjson, and modern C++. WIL and yyjson are pinned through the repository vcpkg manifest.

## Requirements

- Windows 10 or Windows 11
- Visual Studio 2026 Insiders (18.x) whose default MSVC build tools are 14.52.x (toolset `v145`), with the components
  in `.vsconfig`; `.github/actions/setup-msvc/Setup.ps1 -VerifyOnly` checks an installation read-only
- Windows 11 SDK 10.0.26100
- MSVC in `stdcpplatest` mode
- Unicode Win32 APIs
- Debug, Release and ASan Debug project configurations on x64 and ARM64, including real sanitizer instrumentation
- Git for the pinned vcpkg bootstrap
- PowerShell 7 for every repository script: `build.ps1`, `test.ps1`, `package.ps1`, `restore-dxui.ps1`,
  `vcpkg-install.ps1` and `validate-skills.ps1` declare `#Requires -Version 7.0`

## Drive-root output policy

Do not create scratch, caches, evidence, copied checkouts, or other output directories directly at a drive root.
Use repository-local generated outputs or an existing approved workspace. The only permitted new drive-root
output name is `<Project>.Perf` (for example `RedXe.Perf`, `RedSalamander.Perf`, or `RedPrism.Perf`), and the
user must explicitly authorize the exact path before its first creation or initialization. A matching name,
environment variable, low disk space, or authorization for another drive/project is not authorization.
Keep outputs beneath the approved root; do not invent sibling root folders. Preserve runner-specific ownership
markers and authorization. Historical cleanup inventories and preserves useful evidence/recovery data, checks
process use, repairs live junction/worktree references, and verifies preservation before removal. Never follow
junctions during cleanup or leave compatibility junctions at forbidden old roots.

## Canonical project guidance

- This file is the repository-wide instruction source.
- `Specs/README.md` defines specification authority and the plan lifecycle.
- Normative product behavior lives in domain contracts under `Specs/<Domain>/`; read the owning contract before
  changing durable behavior.
- Repo-scoped skills live in `.agents/skills/` and must pass `.\validate-skills.ps1`, which runs the repository-owned
  `Build/SkillValidation.psm1` validator. Routine repository validation requires no Python or external YAML module.
- Keep reusable build entrypoints at the repository root.
- Generated build products belong under `.build/`; never commit them.

## Mandatory performance and resource policy

- [`Specs/Core/Core_PerformanceAndResources.md`](Specs/Core/Core_PerformanceAndResources.md) applies to every design,
  implementation, review, and validation task.
- Performance and low resource consumption are primary requirements. Among correct designs, prefer the one with the
  lowest steady-state CPU, memory, allocation, copy, synchronization, wake-up, and GPU-submission cost.
- Steady-state hot paths must use bounded reusable storage and avoid heap allocation. Cache derived state and batch
  compatible work.
- Idle, hidden, minimized, suspended, and occluded execution must block on events; busy polling is prohibited.
- Do not trade correctness, security, visual quality, or required behavior for an unmeasured micro-optimization.
- Any intentional resource regression requires measurement and explicit justification in the owning spec or WIP plan.

## Skills

| Skill | Use it for |
| --- | --- |
| [plugin-development](.agents/skills/plugin-development/SKILL.md) | Native plugin ABI, factories, loading, widget instances, and bundled DLLs |

Cross-repository `spec-workflow`, `d3d11-win32-host`, `performance-resources`, `modern-cpp-windows`, `wil-raii`, and `yyjson` skills live in the sibling `RedHome` repository. So do the shared skills that replaced two local ones, each with RedXe's facts in its `references/repository-facts.md`:

| Shared skill | Use it for |
| --- | --- |
| `native-build` | Building, cleaning, running, smoke-testing, and packaging (was `build-redxe`) |
| `app-settings` | Settings paths, schema, recovery, stamps, watching, and live apply (was `settings-store`) |

`test-evidence`, `crash-dumps`, and `file-io-safety` in `RedHome` cover tests and run evidence, crash capture, and safe file saves, again with RedXe's facts.

Shared skill index: `../RedHome/AGENTS.md`; packages: `../RedHome/.agents/skills/`. If `../RedHome` is missing, clone https://github.com/RedSalamanders/RedHome.git to that path. This repository's guidance and domain specs own product behavior and validation.

## Specification workflow

- Start with [`Specs/README.md`](Specs/README.md), then read the owning domain spec. XENEON display, window, fallback,
  fullscreen, and DPI behavior is owned by
  [`Specs/UI/UI_XeneonDisplayWindowing.md`](Specs/UI/UI_XeneonDisplayWindowing.md).
- Native factory, generic widget mechanisms, bundled plugin, and plugin-lifetime behavior is owned by
  [`Specs/Plugins/Plugins_API.md`](Specs/Plugins/Plugins_API.md).
- Adding or removing a settings-visible bundled widget requires one aligned change to `RedXe/BundledPlugins.h`, the
  schema/parser, its plugin contract, real placed examples in both Debug and Release settings templates, and the
  matching user-guide page under `docs/plugins/`. The only exception is a widget listed in
  `kRedXeOptInBundledWidgetIds` (today the GdiOrbit native-window example): it stays catalogued, schema-accepted,
  documented, and covered by `HostPluginTests`, but no shipped page places it because its child HWND forces composed
  presentation for the whole window. A widget listed in `kRedXeDebugOnlyBundledWidgetIds` (today the Logicon
  monitor) is placed only by the Debug template and constructed only by Debug builds; Release refuses the instance.
  Adding a service plugin requires `kRedXeBundledServices`, the `services` parser and schema, both templates, its
  plugin contract, and the `docs/plugins/` page in the same change.
- The command line is declared once in `RedXe/CommandLine.h`, the catalog that `--help` (`-h`, `/?`) prints and that
  `RedXe/Main.cpp` parses through. Adding, renaming, or removing a switch MUST update that catalog, the "Command
  line" section of `docs/usage.md`, and the owning row of the mode table in `Specs/UI/UI_XeneonDisplayWindowing.md`
  in the same change; `SettingsTests` and the `--help` step of `test.ps1` fail otherwise. Never parse a switch from
  a string literal outside the catalog.
- Screenshots for `docs/`, specs, and UI reviews MUST be generated by the application itself:
  `RedXe.exe --screenshot <png> --page <id> --widget <ordinal> [--after <ms>]` on a Debug build with a portable
  `--settings` scene (Windows.Graphics.Capture of its own window, no activation, no cursor). Never use Computer Use or
  a desktop screenshot tool, and never substitute a mockup for a live capture. The helper is `Common/WindowCapture.cpp`;
  tests capture their own windows through it.
- Mandatory performance and resource behavior is owned by
  [`Specs/Core/Core_PerformanceAndResources.md`](Specs/Core/Core_PerformanceAndResources.md).
- User settings files, schema, cold recovery, and live reload are owned by
  [`Specs/Core/Core_Settings.md`](Specs/Core/Core_Settings.md).
- Fatal-process capture, local minidumps, prior-crash UI, and the crash harness are owned by
  [`Specs/Core/Core_CrashHandling.md`](Specs/Core/Core_CrashHandling.md).
- Pinned DxUi restore, host/plugin adapters, and the COM/POD boundary are owned by
  [`Specs/Core/Core_DxUiIntegration.md`](Specs/Core/Core_DxUiIntegration.md).
- Dashboard pages, the placement grid, active-page composition, and widget instance layout are owned by
  [`Specs/UI/UI_Dashboard.md`](Specs/UI/UI_Dashboard.md).
- Domain specs describe current behavior. `Specs/Plans/WIP/` is non-normative active work and
  `Specs/Plans/Done/` is historical context.
- Small settled changes may update the spec, implementation, and validation directly. Multi-step, risky, or undecided
  work requires one indexed WIP plan naming the domain specs it expects to change.
- A change is not complete while durable requirements exist only in code, tests, commentary, or a plan. Merge them
  into the authoritative domain spec during closeout.
- When every plan item is implemented, its tests and required validation pass, and its durable behavior is persisted
  in the normative contracts, the plan MUST be moved from WIP to Done and removed from the active index. A completed
  plan MUST NOT remain under `Specs/Plans/WIP/`.
- When that finished spec change impacts an end-user scenario, update `docs/` in the same closeout. `docs/` is the
  user guide (global usage plus one page per settings-visible widget). It is not a product-behavior authority.

## Architecture

[`Specs/Core/Core_SourceLayout.md`](Specs/Core/Core_SourceLayout.md) maps the repository: every directory and the files
each one owns, from the plugin ABI headers and host classes to the bundled plugins, test projects, build modules, and
tools. Read it before adding, moving, or retiring a file it lists, and update it in the same change.

Keep the boundary explicit:

- `Application` owns the HWND and translates messages into narrow operations.
- `CrashHandler` owns fatal-process registration and best-effort local artifacts; it creates no background work and
  never uploads dumps.
- `SettingsStore` owns typed settings validation, user/deployed paths, the sibling Logs directory path, cold recovery,
  and stamp deduplication.
- `SettingsWatcher` owns one event-blocked directory watcher and only posts a coalesced UI message; settings and
  dashboard mutation remain on `Application`'s UI thread.
- `TrayIcon` owns the notification-area icon and its hidden owner window and only posts commands to the main window;
  `Application` decides from `trayIcon` whether it exists and performs Edit settings and Exit.
- `PluginHost` is process scoped. One instance owns every mapped module, every data source, the single acquisition
  worker, the optional network worker, the started services with their device-lane threads, the host action ring,
  the lazy launch worker, and the JSONL log writer for the whole application, including the dashboard page staged
  during a swipe. Optional `RedXePluginShutdown` runs once per module at process teardown.
- A service (`Service.h`) runs without a placed widget: `Application` starts it after the first page is live, feeds it
  host state, drains its host actions on the UI thread, and stops it before the process runtime shuts down. A
  service's device I/O runs only on its host-owned lane, never on a plugin-created thread.
- `PluginManager` borrows that runtime and owns only provider/widget COM references for one page. A per-instance
  construction failure becomes a host-drawn placeholder tile; it does not fail the page.
- `DashboardHost` owns design-canvas placements, native child containers, and frame-scheduling policy.
- `Renderer` owns host COM graphics resources, cached viewports, device notifications, and presentation; it has no
  message-dispatch or plugin-specific drawing logic.
- The ABI headers carry the consumer-facing contract on the declarations: thread affinity, reentrancy limits, borrow
  lifetime, and the GPU pipeline-state guarantee. Every public record is pinned by `sizeof` and, when it carries a
  pointer, by `offsetof` assertions.
- `Widget.h` owns every widget declaration. Widgets expose supported GPU, scheduled, native-window, or raised-overlay
  mechanisms as sibling COM interfaces queried by IID.
- GPU widgets receive the borrowed D3D11 device during setup and immediate context during rendering, but never the
  HWND, swap chain, or back buffer. The host binds only render target and viewport before each callback, so a widget
  binds every other state it depends on, including scissor state.
- A GPU widget creates its device resources in `OnDeviceCreated`, which MAY allocate bounded resources and rasterize
  what does not depend on the drawn size, such as per-device lookup tables (5H4D3R5), and rebuilds
  resolution-dependent resources in `OnTargetSizeChanged`, which the host calls only when the largest viewport it will
  draw that widget at actually changes. Those two and the optional `IRedXePreparedGpuWidget::Prepare` are the only GPU
  callbacks allowed to rasterize, create textures, or allocate (`Specs/Plugins/Plugins_API.md`); `Render` stays
  allocation-free.
- A window widget receives only a host-owned child container, never the top-level HWND, and destroys all plugin-owned
  children before detach returns.
- Device-independent state survives swap-chain recreation; device resources are rebuilt together after device loss.

## Build and validation

```powershell
.\vcpkg-install.ps1 -Platform All
.\build.ps1
.\build.ps1 -Configuration Release
.\build.ps1 -Platform ARM64
.\build.ps1 -Rebuild
.\build.ps1 -Run
.\Test-Changes.ps1 -Explain                     # which suites your changes select, and why; builds and runs nothing
.\test.ps1                                      # affected suites (Test-Changes.ps1); nothing when nothing changed
.\test.ps1 -Full                                # every suite: the full gate
.\validate-skills.ps1
.\package.ps1 -Platform x64                     # portable ZIP stamped 1.0.<commit count>, smoke-tested from a clean extraction
.\winget-manifest.ps1 -Version 1.0.<n>          # winget manifest from both platform packages
```

The version is `major.minor` from `Common/Version.h` plus the build number: `-BuildNumber` when given,
otherwise the commit count of HEAD, which the release workflow stamps too (one commit, one version). Packaging, the in-package installer, the `RedXe` alias launcher, and the
release and winget workflows are owned by [`Specs/Build/Build_Packaging.md`](Specs/Build/Build_Packaging.md).

`build.ps1` rejects only a running `RedXe.exe` whose normalized executable path exactly matches the selected
`.build/<Platform>/<Configuration>/RedXe.exe`. It MUST identify that process and MUST NOT terminate it; same-name
processes from other paths do not block the build.

Before declaring a change complete, build the affected configuration, run `test.ps1 -Full` (or
`Test-Changes.ps1 -Mode PrePush`, which accounts for the same coverage), and satisfy the validation contract in the
owning domain spec. Rendering changes must keep the WARP smoke test green so CI and GPU-independent hosts can validate
device creation, embedded shader bytecode, resize, drawing, and presentation.
`test.ps1 -Full` also validates crash capture by launching an isolated child process; it requires no desktop automation
and must not write to the user's normal crash directory.

## Host chrome iconography

- Every icon in host-drawn chrome comes from `RedXe/FluentIcons.h`. Do not hand-draw arrows, glyphs, or symbols with
  quads or path geometry: font glyphs are hinted, scale correctly with DPI, and match the Windows 11 shell, which
  hand-drawn shapes do not.
- The font order is Segoe Fluent Icons, then Segoe MDL2 Assets for older builds, then a standard Unicode stand-in in
  the normal UI font. Every glyph constant MUST have a Unicode fallback so chrome never renders a missing-glyph box.
  `FluentIcons::ResolveIconFamily` selects the family from the DirectWrite system font collection and reports which
  glyph set applies; `FluentIcons::SelectGlyph` picks the matching code point.
- Add new glyphs to `FluentIcons.h` and to the `HostChromeGlyph` atlas slots in `RedXe/HostChrome.h` rather than
  inline in a draw routine, so the icon set stays reviewable in one place.
- Host chrome is Direct3D: `RedXe/HostChrome.*` rasterizes the glyph atlas with DirectWrite once per DPI and draws
  every band, dim, shadow, and close quad into the swap chain. There is no chrome HWND and no GDI in the host;
  [`Specs/Core/Core_PerformanceAndResources.md`](Specs/Core/Core_PerformanceAndResources.md) owns the budget.

## C++ and Win32 rules

- Use WIL `wil::com_ptr_nothrow<T>` for COM ownership in HRESULT-based code and the matching WIL unique wrapper for
  owned Windows resources.
- Do not manually release or destroy an owned resource; reset its WIL owner and make cleanup idempotent.
- Keep the static window procedure small. Bind the instance at `WM_NCCREATE`, then route to `Application::HandleMessage`.
- Do not allow exceptions to cross `wWinMain` or a Win32 callback boundary.
- Use `HRESULT` for DirectX and Windows failures. Handle expected device-loss codes by rebuilding resources.
- Treat zero-sized `WM_SIZE` as suspension, not an error.
- Follow `UI_XeneonDisplayWindowing.md` for `WM_DPICHANGED`: fullscreen popups use the suggested monitor rectangle;
  titled windows use its destination position and recalculate the outer frame for the exact DPI-adjusted client
  canvas.
- Do not render from `WM_PAINT`; this real-time app renders from the idle side of the message loop.
- Keep `/Wall`, `/permissive-`, SDL checks, and zero-warning validation with /WX disabled green for project code.
- Prefer targeted local warning suppression only when an SDK or tool header requires it, with a reason beside it.
- Treat `yyjson_val*` and strings returned by yyjson as borrowed from their document. Use WIL RAII for documents and
  copy dynamic strings into mutable documents.
- Public COM contracts use `interface __declspec(uuid(...)) __declspec(novtable) Name : IUnknown`. Do not declare
  them with `struct`, and do not use interface inheritance to couple generic widget identity to a rendering mechanism.
- Keep `Settings/`, `Specs/Settings.schema.json`, `Settings.*`, `Core_Settings.md`, and `SettingsTests` aligned. A
  settings change is incomplete if any one of these still describes the old document.

## Scoped testing policy (2026-10-05)

The user's accepted workflow replaces unconditional full-test iteration. Use `Test-Changes.ps1 -Explain` and the affected default while editing; use `-Mode PrePush` to account for full local/PR coverage without duplicate identical obligations. A forthcoming enabled CI gate is pending acceptance, never an already passed result, and only a check `main` requires can take work off the local run. An empty affected plan (`NOTHING_SELECTED`) evaluates nothing; explicit Full (`test.ps1 -Full`) remains the full gate. Active native test files use `Scope.Tests.Something.h/.cpp` and the native file inventory. Follow [Specs/Build/Build_Process.md](Specs/Build/Build_Process.md) and [the test guide](Tests/README.md). Focus-taking work requires agreement to the time; no scoped pass closes that gate. Build/runtime/platform qualification still applies to changed behavior, with exact prior evidence reusable only under the owning contract.

## Changes workflow

Record each change as one dated Markdown fragment under `Changes/`; follow [Changes/README.md](Changes/README.md)
and [Specs/Core/Core_Changes.md](Specs/Core/Core_Changes.md). Do not hand-edit generated `CHANGELOG.md` entries.
Keep the fragment in the change: never fold it in a feature change and never edit `CHANGELOG.md`. Fragments are
folded on `main` after merge by `Tools/Submit-ChangelogFold.ps1`, which opens a fold pull request of its own. Run
`Tools/Test-Changelog.ps1` while editing; PrePush and CI run it with `-Base`, which refuses any `CHANGELOG.md` change
that is not an exact fold of fragments already on `main`. Validation is read-only.

## Shared native build policy (2026-10-28)

Use /Wall with /WX disabled in every first-party profile. A build is accepted
only with zero warnings and zero errors; fix actionable diagnostics rather than
adding blanket suppressions. Ordinary Debug uses /ZI, ASan Debug uses /Zi.
Release explicitly enables /OPT:ICF; Debug/ASan explicitly disable it.
Follow Specs/Build/Build_ConsoleAndPaths.md for the common green result, safe
terminal launch links and short dependency folders with full identity checking.
## Unified CI policy

Use [Specs/Build/Build_UnifiedCI.md](Specs/Build/Build_UnifiedCI.md) for hosted CI. PRs run compiler-free checks and formatting feedback. Main native qualification starts after five continuous quiet minutes; both native Release test branches must pass before either production Release/ZIP branch starts. Scheduled dependency/security/status checks remain compiler-free. Formatting commits, extended qualification and ZIP/winget publication are explicit manual actions. PR checks do not satisfy native acceptance or waive applicable local scoped tests. Shared-engine pins in .ci/project.json and workflow callers must agree. Preserve dependency pins, native toolchain, evidence and desktop ownership contracts.
