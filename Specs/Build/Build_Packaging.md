# RedXe packaging, release, and winget contract

Status: current normative repository contract
Last reviewed: 2026-09-20
Owner: `package.ps1`, `winget-manifest.ps1`, `Build/Versioning.psm1`, `Build/Package.psm1`, `Build/Winget.psm1`,
`Installer/`, `RedXeLauncher/`, `.github/workflows/release.yml`, `.github/workflows/winget-release.yml`

## Scope

This contract owns the product version stamp, the portable ZIP package and its clean-extraction smoke, the installer
that ships inside the package, the command-alias launcher, the release workflow, and winget publication. Build output
selection and the running-target preflight remain in [`Build_Process.md`](Build_Process.md).

## Version

- `Common/Version.h` is the single source of `REDXE_VERSION_MAJOR` and `REDXE_VERSION_MINOR`. Each MUST stay a
  one-line `#define` with a single integer literal so `Build/Versioning.psm1` can read it.
- The third component is the build number. Its default is the commit count of `HEAD` (`git rev-list --count HEAD`,
  `Get-RedXeDefaultBuildNumber`), the same formula locally and in the release workflow, so one commit is one version
  and the number only grows on `main`, which forbids force pushes. `build.ps1`, `test.ps1`, and `package.ps1` resolve
  it once through `Resolve-RedXeBuildNumber` (an explicit positive `-BuildNumber` wins) and pass it to MSBuild as
  `RedXeBuildNumber`, which `Directory.Build.props` defines for the resource compiler only as `REDXE_VERSION_BUILD`.
  A checkout without git history yields 0 with a warning. A new build number MUST NOT recompile C++ translation
  units. CI checkouts that build MUST fetch the full history (`fetch-depth: 0`); a shallow clone would count 1.
- Every shipped executable's version resource includes `Common/Version.h`: `FILEVERSION`/`PRODUCTVERSION`
  `major,minor,build,0`, `ProductName` `RedXe`, `CompanyName` `RedSalamanders`. `test.ps1` MUST verify that
  `RedXe.exe` and `RedXeLauncher.exe` report the file version expected for its `-BuildNumber`.
- The package version, GitHub release tag, and winget `PackageVersion` are the same `major.minor.build`
  (`1.0.183`, tag `v1.0.183`). A winget manifest requires a positive build number, which only a checkout without git
  history fails to provide.

## Command-alias launcher (`RedXeLauncher.exe`)

winget exposes a portable package's command as a symbolic link in its `Links` directory. A process started through
that link resolves app-local DLLs and `GetModuleFileNameW` against the link's directory, so `RedXe.exe` (which loads
`yyjson.dll`, the CRT, `Plugins\`, and `Settings\` from its own directory) MUST NOT be the alias target.

- `RedXeLauncher/` builds a console-subsystem executable that links the CRT statically and imports only system
  DLLs (ASan Debug keeps the shared debug CRT required by `Directory.Build.targets`). Its manifest declares
  `consoleAllocationPolicy` `detached` so a double-click on Windows 11 24H2+ never flashes a console.
- It resolves its own final path through every link (`GetFinalPathNameByHandleW`), takes that directory as the
  package root, and starts `<root>\RedXe.exe` by absolute path with the root as working directory and every argument
  passed through with `CommandLineToArgvW`-compatible quoting. It hands its standard handles to the child so
  `RedXe --help > file` and terminal output work although `RedXe.exe` is a GUI-subsystem image.
- A normal dashboard launch returns immediately with exit code 0. For `--help`, `-h`, `/?`, `-?`, `--self-test`,
  `--screenshot`, `--crash-test`, `--crash-test-stack-overflow`, and `--crash-test-directory=` it waits for
  `RedXe.exe` and returns its exit code. Adding a self-terminating switch to `RedXe/CommandLine.h` MUST add it to
  `kAwaitedSwitches` in `RedXeLauncher/Main.cpp`.
- `test.ps1` MUST run `RedXeLauncher.exe --help` and check that the RedXe help text is relayed with exit code 0, and
  that an unknown switch through the launcher yields exit code 2.

## Portable ZIP (`package.ps1`, `Build/Package.psm1`)

`package.ps1 -Platform <x64|ARM64> -BuildNumber <n>` builds Release (unless `-SkipBuild`) and writes
`.build/packages/RedXe-<version>-<Platform>-Portable.zip` plus a `.sha256` sidecar (`<hash> *<name>`).

The package MUST contain, with forward-slash entry names and no directory entries outside these roots:

| Entry | Source |
| --- | --- |
| `RedXe.exe`, `RedXeLauncher.exe`, `yyjson.dll` | `.build/<Platform>/Release/` |
| `DxUi.provenance.json` | build output, when present |
| `Settings/RedXe.settings.json`, `Settings/RedXe-debug.settings.json`, `Settings/RedXe.settings.schema.json` | build output |
| `Plugins/**` | build output `Plugins\`, minus build artifacts (`.pdb .lib .exp .ilk .iobj .ipdb .obj .log .tlog`) and `Plugins\ZoomSdk\` |
| Visual C++ runtime DLLs | the newest installed `VC\Redist\MSVC\<v>\<arch>\Microsoft.VC*.CRT`, copied to the root and to `Plugins/` (the helper executables run as separate processes) |
| `Install-RedXe.ps1`, `install.cmd`, `uninstall.cmd` | `Installer/` |
| `README.txt` | generated with the version and platform |
| `LICENSE.txt` | repository root (required) |

- Every DLL named in `RedXe/BundledPlugins.h`, the AV Control helpers (`AVControlBroker.exe`,
  `AVControlCamera.dll`, `AVControlCameraSetup.exe`), `WeatherLocation.exe`, `libcurl.dll`, `Plugins/yyjson.dll`,
  the Weather Icons font and its `OFL.txt`, and `msvcp140.dll`, `msvcp140_atomic_wait.dll`, `vcruntime140.dll`,
  `vcruntime140_1.dll` at both levels are required. A missing required entry fails packaging.
- Build artifacts, `*Tests.exe`, `SystemDataPhase0.exe`, and anything under `Plugins/ZoomSdk/` are forbidden. The
  Zoom Plugin SDK is licensed to each developer by Zoom and MUST NOT be redistributed; the shipped
  `zoom.action.dll` runs its synthetic session and reports `zoom-sdk-unavailable`.
- Both `RedXe.exe` and `RedXeLauncher.exe` MUST be Release images of the target architecture stamped with the
  package version; a Debug or mismatched stamp fails packaging.
- The package is never written until the clean-extraction smoke accepts a staged copy: the archive is expanded into
  a fresh directory, its entry list is checked against the rules above, and when the host can run the platform
  (`x64` on x64/ARM64 Windows, `ARM64` only on ARM64 Windows) the packaged `RedXe.exe --self-test --warp` MUST exit
  0 and `RedXeLauncher.exe --help` MUST exit 0 with the help text. The extraction is removed afterwards.

## In-package installer (`Installer/Install-RedXe.ps1`)

RedXe runs from any folder; the installer adds only what a folder cannot. It MUST run on Windows PowerShell 5.1
(`install.cmd` and `uninstall.cmd` invoke `powershell.exe -ExecutionPolicy Bypass`, so a Mark-of-the-Web package
works without changing the execution policy) and on PowerShell 7, per user, without elevation, and MUST support
`-WhatIf` without changing state or printing module noise.

- `Install` (default) copies the package to `%LocalAppData%\Programs\RedXe` (or `-Destination`), creates
  `RedXe.lnk` in the user's Start Menu Programs folder pointing at the installed `RedXe.exe`, registers
  `HKCU\...\Uninstall\RedXe` (DisplayName, DisplayVersion, Publisher `RedSalamanders`, InstallLocation, DisplayIcon,
  `UninstallString` = `powershell.exe -NoProfile -ExecutionPolicy Bypass -File "<dest>\Install-RedXe.ps1" -Action Remove`,
  `NoModify`, `NoRepair`, `EstimatedSize`), and with `-StartAtSignIn` writes `HKCU\...\Run\RedXe`. `-Launch` starts
  the installed copy.
- The destination MUST be empty, missing, or a previous installation carrying `redxe-install.json`. A reinstall
  deletes only files listed in the previous manifest that the new package no longer contains; foreign files are
  never deleted and a non-empty foreign folder is refused. The manifest records product, version, timestamp,
  in-place flag, shortcut path, registry root, and the file list.
- `-InPlace` registers the package folder itself (no copy, no Apps entry). It is implied inside a winget package
  folder (`\Microsoft\WinGet\Packages\`), because winget owns those files and their Apps entry.
- `Remove` deletes the shortcut, the Run value, the Apps entry, and the files listed in the manifest (then empty
  directories and the folder), using the locations the manifest recorded. `%LocalAppData%\RedXe` (settings, logs,
  crash dumps) survives unless `-PurgeUserData` is given.
- A running `RedXe.exe` from the target folder blocks both actions with its PID and is never terminated.
- `-StartMenuDirectory` and `-RegistryRoot` redirect every location so the repository test can install and remove
  in isolation; they are not user-facing.

`Tests/BuildProcessTests/PackagingTests.ps1` (run by `test.ps1`) MUST cover version parsing, the required and
forbidden entry rules, CRT discovery, manifest generation, and an install/upgrade/remove round-trip of a synthetic
package under both PowerShell hosts against scratch locations. It MUST leave no registry key, shortcut, or folder
behind.

## Winget manifest (`winget-manifest.ps1`, `Build/Winget.psm1`)

- Package identifier `RedSalamanders.RedXe`, multi-file manifest schema 1.12.0 from `Installer/winget/templates/`:
  `InstallerType: zip`, `NestedInstallerType: portable`, `NestedInstallerFiles` `RedXeLauncher.exe` with
  `PortableCommandAlias: RedXe`, `Commands: [RedXe]`, `MinimumOSVersion: 10.0.19041.0`, `UpgradeBehavior: install`,
  and one installer per architecture (`x64`, `arm64`) whose `InstallerUrl` is
  `https://github.com/RedSalamanders/RedXe/releases/download/v<version>/RedXe-<version>-<x64|ARM64>-Portable.zip`.
  Do not use `InstallerType: portable` for a ZIP, and do not add `Icons` until the publisher is verified.
- Generation requires the exact three-part version with a positive build, both ZIPs named exactly as `package.ps1`
  names them, and a `yyyy-MM-dd` `ReleaseDate` (default today UTC; the workflow passes the release's `published_at`
  date so a rerun produces an identical manifest). Output goes under `.build/packages/winget-manifest/<version>/`
  with LF line endings and no unresolved `{PLACEHOLDER}`.
- `winget validate --manifest` MUST pass. "Manifest validation succeeded with warnings" from an older client is
  accepted; the warnings are shown.
- `License: MIT` and `LicenseUrl` in the locale manifest mirror the repository's `LICENSE.txt` (MIT, with the
  carve-outs it lists: the CC BY-NC-SA 3.0 Shadertoy ports, the OFL Weather Icons font, and the never-redistributed
  Zoom SDK). The package always ships `LICENSE.txt`; a license change updates the file and the template together.

## Release workflow (`.github/workflows/release.yml`)

`workflow_dispatch` only, so every release is deliberate.

1. `version` checks out the full history and resolves `major.minor.<commit count>` from `Common/Version.h` and
   `git rev-list --count HEAD`. It fails when the tag `v<version>` already exists: a commit is released at most
   once, and published assets are never replaced. Releasing again means merging a new commit to `main`.
2. `build` runs `test.ps1 -Configuration Release -Platform <P> -BuildNumber <n>` then
   `package.ps1 -Platform <P> -BuildNumber <n> -SkipBuild` natively on `windows-2025-vs2026` (x64) and
   `windows-11-vs2026-arm` (ARM64, unless `build_arm64` is off), and uploads the ZIP and its sidecar.
3. `release` requires the exact expected asset set, verifies each sidecar hash, writes `checksums.sha256`, and
   creates the GitHub release `v<version>` at the workflow commit with generated notes. A missing or partial matrix
   never becomes a release.
4. `winget` calls `winget-release.yml` with `submit: true` when `publish_winget` and `build_arm64` are both on. An
   x64-only release is published on GitHub but never submitted, because the manifest lists both architectures.

All third-party actions are pinned to full commit SHAs.

## Winget publication (`.github/workflows/winget-release.yml`)

Reusable (`workflow_call`) and manual (`workflow_dispatch`, `submit` off by default). Serialized per version by
concurrency group. On `windows-latest`:

1. Checks out the release tag so the templates match the released product.
2. Reads the release through the GitHub API, requires both portable assets, downloads them, records the release
   date, and verifies each asset is downloadable **without credentials**. winget-pkgs and every winget client fetch
   `InstallerUrl` anonymously, so a private repository fails here with an explicit message instead of failing later
   in the community repository; the repository (at least its releases) must be public before publication.
3. Runs `winget-manifest.ps1` (generation plus `winget validate`).
4. Enables `LocalManifestFiles`, runs `winget install --manifest`, checks `winget list`, and runs
   `%LocalAppData%\Microsoft\WinGet\Links\RedXe.exe --help` and `--self-test --warp` through the alias winget
   created. This is the end-to-end proof that the launcher resolves the package root and RedXe loads its plugins
   from there. The package is always uninstalled afterwards, including after a failure.
5. When submitting: installs the reviewed winget-create `1.12.8.0`, verifies its banner version, requires the
   `WINGET_TOKEN` secret (a classic personal access token with `public_repo`; exposed only as
   `WINGET_CREATE_GITHUB_TOKEN`, never on a command line), treats an already published version directory or an
   already open pull request for the exact version as success without submitting, chooses `New package:` or
   `Update:` for the title, runs `wingetcreate submit --no-open`, and reports the pull request URL.

## Validation

Changes to this contract require:

```powershell
.\test.ps1 -Configuration Debug -Platform x64          # includes the launcher checks and PackagingTests.ps1
.\package.ps1 -Platform x64 -BuildNumber 1
.\winget-manifest.ps1 -Version 1.0.1 -Arm64ZipPath <an ARM64 package or a copy for local validation>
```

and, for workflow changes, one `Release` run with `publish_winget` off followed by a manual `Publish to winget`
run with `submit` off against that release.
