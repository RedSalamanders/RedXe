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
```

Outputs are always `.build/<Platform>/<Configuration>/`. Intermediate files are under `.build/Intermediate/`.
`build.ps1` ensures manifest dependencies are installed first. The pinned vcpkg checkout and all package state also
stay beneath `.build/`; do not substitute a developer-global install path.

When diagnosing a failure:

1. For dependency failures, run `vcpkg-install.ps1` for the exact platform and fix the first vcpkg diagnostic.
2. Re-run `build.ps1` with the exact configuration and platform that failed.
3. Fix the first project-code diagnostic; warnings are errors.
4. Run `test.ps1` after a successful x64 build. It validates the factory ABI, borrowed descriptors, COM identity, and
   rendering-IID negotiation, then validates settings parsing, device creation, embedded shader-bytecode loading,
   drawing, and presentation.
   without requiring a hardware GPU.
5. Build both Debug and Release when changing project properties, manifests, or compiler behavior. Build x64 and ARM64
   when changing vcpkg, platform mapping, or dependency paths.

`build.ps1` discovers stable and prerelease Visual Studio instances. Do not hardcode a developer's installation path
in project files or scripts.

Scheduling changes also require the live hidden, minimized, suspended, and occluded idle checks in
`Specs/Core/Core_PerformanceAndResources.md`.

Before closeout, confirm the authoritative domain spec matches the validated behavior. If the work finishes a WIP
plan, move it to `Specs/Plans/Done/` only after merging durable requirements into that spec. Apply the `spec-workflow`
skill when specification or plan state changes.
