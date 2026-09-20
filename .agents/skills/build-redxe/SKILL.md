---
name: build-redxe
description: Build, clean, run, or smoke-test the RedXe Visual Studio solution. Use for compilation failures, output locations, build configurations, or validating a change; do not use for graphics implementation decisions.
---

# Build RedXe

Read the validation section of the owning normative spec before choosing the final build matrix. Use
`Specs/UI/UI_XeneonDisplayWindowing.md` for display, window, fullscreen, fallback, manifest-DPI, and resize work.

Use the repository entrypoints instead of assembling ad hoc MSBuild commands:

```powershell
.\vcpkg-install.ps1 -Platform All        # Optional up-front install
.\build.ps1                              # Debug x64
.\build.ps1 -Configuration Release
.\build.ps1 -Platform ARM64
.\build.ps1 -Rebuild
.\build.ps1 -Run
.\test.ps1                               # Build + hidden WARP smoke test
.\build.ps1 -Configuration Release                   # Stamps 1.0.<commit count of HEAD>; -BuildNumber 42 overrides
.\package.ps1 -Platform x64                          # Portable ZIP under .build/packages, smoke-tested
.\winget-manifest.ps1 -Version 1.0.42                # Winget manifest from both platform packages
```

Packaging, the in-package installer, the `RedXe` command-alias launcher (`RedXeLauncher/`), and the release and
winget workflows follow `Specs/Build/Build_Packaging.md`; a new bundled plugin DLL becomes a required package entry
automatically through `RedXe/BundledPlugins.h`, and a new self-terminating command-line switch must be added to the
launcher's awaited list.

Outputs are always `.build/<Platform>/<Configuration>/`. Intermediate files are under `.build/Intermediate/`.
`build.ps1` ensures manifest dependencies are installed first. The pinned vcpkg checkout and all package state also
stay beneath `.build/`; do not substitute a developer-global install path.

Every build starts with the RedXe build-signal banner. Plain interactive consoles retain MSBuild's native color;
Codex, Windows Terminal, redirected, and non-interactive hosts use colored line replay. Each invocation captures a
plain-text log beneath `.build/logs/` and reports diagnostic counts plus elapsed time. Use the captured log when a
diagnostic is truncated in the terminal.

Every build, clean, and rebuild preflights the selected executable output. A running `RedXe.exe` whose normalized path
exactly matches `.build/<Platform>/<Configuration>/RedXe.exe` blocks the build with identifying diagnostics and is
never terminated; same-name processes from another checkout or profile are ignored. If this preflight changes,
follow `Specs/Build/Build_Process.md` and keep `Tests/BuildProcessTests/BuildProcessTests.ps1` green.

When diagnosing a failure:

1. For dependency failures, run `vcpkg-install.ps1` for the exact platform and fix the first vcpkg diagnostic.
2. Re-run `build.ps1` with the exact configuration and platform that failed.
3. Fix the first project-code diagnostic; warnings are errors.
4. Run `test.ps1` after a successful x64 build. It validates the factory ABI, borrowed descriptors, COM identity, and
   rendering-IID negotiation, then validates settings parsing, device creation, embedded shader-bytecode loading,
   drawing, and presentation.
   without requiring a hardware GPU.
5. Build Debug, Release and ASan Debug when changing project properties, manifests, or compiler behavior. Build x64 and ARM64
   when changing vcpkg, platform mapping, or dependency paths.
   Run ASan Debug tests natively; the deliberate isolated probe must produce an AddressSanitizer diagnostic.

`build.ps1` discovers stable and prerelease Visual Studio instances. Do not hardcode a developer's installation path
in project files or scripts.

Scheduling changes also require the live hidden, minimized, suspended, and occluded idle checks in
`Specs/Core/Core_PerformanceAndResources.md`.

Before closeout, confirm the authoritative domain spec matches the validated behavior. If the work finishes a WIP
plan, move it to `Specs/Plans/Done/` only after merging durable requirements into that spec. Apply the `spec-workflow`
skill when specification or plan state changes.
