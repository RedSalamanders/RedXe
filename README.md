# RedXe

A clean Win32 + Direct3D 11 foundation for the XENEON EDGE dashboard. The application opens a per-monitor-DPI-aware
native window and hosts rendering-neutral widget instances through negotiated GPU and native-window interfaces.
Release starts with the deterministic low-resource Matrix Rain GPU plugin; both shipped defaults include a second
gallery page demonstrating every bundled plugin. Debug's first page also renders two independent Direct3D triangles
and a double-buffered GDI Xenon orbit in a host-owned child container. WIL owns
Windows/COM resources and yyjson supplies the settings layer; both dependencies are pinned with vcpkg.

The executable and native window use a high-resolution Xenon periodic-table icon. Its 1254×1254 master artwork and
multi-resolution Windows icon live under `RedXe/assets/`.

## Start

Prerequisites: Visual Studio 2026 (18.x) with **Desktop development with C++**, Windows 11 SDK 10.0.26100, and Git for
Windows. Opening `.vsconfig` in Visual Studio offers the required workload. The project intentionally matches the
`v145` toolchain and pinned-vcpkg model used by the RedSalamander reference.

```powershell
# Debug x64 build
.\build.ps1

# Optional: install both platform dependency trees up front
.\vcpkg-install.ps1 -Platform All

# Build and launch
.\build.ps1 -Run

# Release or ARM64
.\build.ps1 -Configuration Release
.\build.ps1 -Platform ARM64
.\test.ps1 -Configuration 'ASan Debug' # Native sanitizer detection and product regressions

# Rebuild and validate with the software Direct3D driver
.\build.ps1 -Rebuild
.\test.ps1

# Validate all repository-local Codex skills
.\validate-skills.ps1
```

Open `RedXe.sln` for IDE development. Binaries are written to:

```text
.build\x64\Debug\RedXe.exe
.build\x64\Release\RedXe.exe
.build\ARM64\Debug\RedXe.exe
.build\ARM64\Release\RedXe.exe
```

The first command-line build clones and bootstraps the vcpkg commit pinned in `vcpkg-tool.json`, then installs the
manifest from `vcpkg.json`. That manifest's `builtin-baseline` MUST be the same commit so the versions database can
resolve every port. All tool, package, download, and installed state stays under `.build`. x64 and ARM64 use
separate install roots so their manifest metadata cannot purge one another. Run `vcpkg-install.ps1` once before the
first direct Visual Studio build.

Before mutating an output profile, `build.ps1` identifies running `RedXe.exe` processes by full executable path. An
instance executing that exact `.build\<Platform>\<Configuration>\RedXe.exe` blocks the build with identifying
diagnostics and is never terminated. Same-name processes from other checkouts or output profiles do not block it.

Press **Escape** to close the running sample. Pass `--warp` to force the Windows software renderer. The test entrypoint
runs ABI, settings, and production host/plugin harnesses, then uses `--self-test --warp` to create a hidden window,
load the build-time-compiled embedded shaders, draw and present one frame, then exit. The host/plugin harness uses a
hidden off-screen HWND and WARP; it does not automate the desktop. The same entrypoint launches an intentionally
crashing child into an isolated `.build` directory and verifies its production minidump and marker without touching
the user's normal crash directory.

Debug builds open as a standard titled window on the XENEON monitor when one is active, otherwise they use normal
shell-selected placement. Release builds search the active display topology for a CORSAIR XENEON monitor and open
borderless fullscreen on that monitor. If no XENEON monitor is present, RedXe asks whether it should continue in a
standard titled window. Every standard-window path uses a 2560×720 logical client canvas at 96 DPI, matching the
XENEON EDGE native 32:9 canvas. Per-monitor-v2 scaling derives the initial physical size from the window's actual
monitor and recalculates the non-client frame during `WM_DPICHANGED`, preserving the exact logical canvas when moving
between monitors with different zoom levels. The title bar and borders sit outside the render area. The self-test
remains hidden and noninteractive in every configuration and verifies the DPI-adjusted default client dimensions.

## Settings and live reload

Normal runs keep editable settings under `%LocalAppData%\RedXe\Settings`. Debug uses
`RedXe-debug.settings.json`; Release uses `RedXe.settings.json`. Schema compatibility comes from the document's
`version` member, not its filename. The user schema is installed beside
them as `RedXe.settings.schema.json`. Schema version 5 defines reusable declarations, flattened plugin keys, ordered
swipeable pages, and adaptive ratio layouts that reflow between landscape and portrait without an orientation field.
The first page is selected on launch. A live settings reload keeps that page when it still exists in the new
document. Valid changes and page switches recreate the dashboard transactionally;
inactive pages own no runtime resources except the adjacent staged page during a swipe. The directory watcher blocks
on Windows events and does no
periodic polling. Invalid live edits leave the previous dashboard active. The hidden self-test reads only the template
deployed under the build output's `Settings` directory and never touches user settings.

## Crash diagnostics

RedXe installs a best-effort fatal-process handler before application startup. A fatal SEH exception, C++ terminate,
purecall, or invalid-parameter failure writes a bounded local minidump, sibling UTF-16 call-stack report, and one-shot
marker under `%LocalAppData%\RedXe\Crashes`. On the next normal launch, RedXe offers to open that folder after the
main window is ready. Dumps are never uploaded and may contain sensitive process-memory fragments. The test harness
uses `--crash-test` with an isolated directory override; normal users do not need this switch.

## Specifications and plans

[`Specs/README.md`](Specs/README.md) defines the repository's specification authority and change workflow. Current
product behavior belongs in normative domain specs such as
[`Specs/Core/Core_Settings.md`](Specs/Core/Core_Settings.md),
[`Specs/Core/Core_CrashHandling.md`](Specs/Core/Core_CrashHandling.md),
[`Specs/UI/UI_Dashboard.md`](Specs/UI/UI_Dashboard.md), and
[`Specs/UI/UI_XeneonDisplayWindowing.md`](Specs/UI/UI_XeneonDisplayWindowing.md). Multi-step or undecided work may use
an indexed plan under `Specs/Plans/WIP/`. Once its implementation, tests, required validation, and normative contracts
are complete, the plan must move to `Specs/Plans/Done/` and must not remain under WIP.

## Layout

```text
.agents/skills/       Repo-local Codex skills
Common/               Shared native plugin contracts
Plugins/              Bundled plugin implementations
RedXe/                 Win32 host and Direct3D orchestration
Settings/              Debug and Release settings templates
Tests/                 ABI, settings, and production host/plugin tests
Build/                 Exact-output build-process safety helper
build.ps1             Build, clean, rebuild, and optionally run
test.ps1              GPU-independent contract, host/plugin, and runtime tests
format.ps1            clang-format entrypoint
validate-skills.ps1   Validate every repository-local skill
Directory.Build.props Shared MSBuild output and compiler defaults
vcpkg-install.ps1     Pinned dependency bootstrap and manifest install
vcpkg.json            WIL and yyjson dependency manifest
Specs/                Normative contracts plus WIP and completed plan history
```
