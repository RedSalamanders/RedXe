# RedXe

A clean Win32 + Direct3D 11 foundation for the XENEON EDGE dashboard. The application opens a per-monitor-DPI-aware
native window and renders an animated RGB triangle through a flip-model DXGI swap chain. WIL owns Windows/COM
resources and yyjson supplies the settings layer; both dependencies are pinned with vcpkg.

The executable and native window use a high-resolution Xenon periodic-table icon. Its 1254×1254 master artwork and
multi-resolution Windows icon live under `src/RedXe/assets/`.

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
manifest from `vcpkg.json`. All tool, package, download, and installed state stays under `.build`. x64 and ARM64 use
separate install roots so their manifest metadata cannot purge one another. Run `vcpkg-install.ps1` once before the
first direct Visual Studio build.

Press **Escape** to close the running sample. Pass `--warp` to force the Windows software renderer. The test entrypoint
uses `--self-test --warp` to create a hidden window, compile both shaders, draw and present one frame, then exit.

Debug builds open as a standard titled window on the XENEON monitor when one is active, otherwise they use normal
shell-selected placement. Release builds search the active display topology for a CORSAIR XENEON monitor and open
borderless fullscreen on that monitor. If no XENEON monitor is present, RedXe asks whether it should continue in a
standard titled window. Every standard-window path uses a 2560×720 logical client canvas at 96 DPI, matching the
XENEON EDGE native 32:9 canvas. Per-monitor-v2 scaling derives the initial physical size from the window's actual
monitor and recalculates the non-client frame during `WM_DPICHANGED`, preserving the exact logical canvas when moving
between monitors with different zoom levels. The title bar and borders sit outside the render area. The self-test
remains hidden and noninteractive in every configuration and verifies the DPI-adjusted default client dimensions.

## Specifications and plans

[`Specs/README.md`](Specs/README.md) defines the repository's specification authority and change workflow. Current
product behavior belongs in normative domain specs such as
[`Specs/UI/UI_XeneonDisplayWindowing.md`](Specs/UI/UI_XeneonDisplayWindowing.md). Multi-step or undecided work may use
an indexed plan under `Specs/Plans/WIP/`; completed plans move to `Specs/Plans/Done/` only after durable requirements
have been merged into the owning domain spec.

## Layout

```text
.agents/skills/       Repo-local Codex skills
src/RedXe/            Win32 and Direct3D source
build.ps1             Build, clean, rebuild, and optionally run
test.ps1              GPU-independent runtime smoke test
format.ps1            clang-format entrypoint
validate-skills.ps1   Validate every repository-local skill
Directory.Build.props Shared MSBuild output and compiler defaults
vcpkg-install.ps1     Pinned dependency bootstrap and manifest install
vcpkg.json            WIL and yyjson dependency manifest
Specs/                Normative contracts plus WIP and completed plan history
```
