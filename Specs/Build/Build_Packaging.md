# RedXe packaging, release, and winget contract

Status: current normative repository contract
Last reviewed: 2026-10-29
Owner: `package.ps1`, `winget-manifest.ps1`, `Build/Versioning.psm1`, `Build/Package.psm1`, `Build/Winget.psm1`,
`Installer/`, `RedXeLauncher/`, `.github/workflows/qualify.yml`, `.github/workflows/publish.yml`

## Scope

This contract owns the product version stamp, the portable ZIP package and its clean-extraction smoke, the symbols ZIP
released beside it, the installer that ships inside the package, the command-alias launcher, the release workflow, and
winget publication. Build output selection and the running-target preflight remain in
[`Build_Process.md`](Build_Process.md).

## Version

- `Common/Version.h` is the single source of `REDXE_VERSION_MAJOR` and `REDXE_VERSION_MINOR`. Each MUST stay a
  one-line `#define` with a single integer literal so `Build/Versioning.psm1` can read it.
- The third component is the build number. Its default is the commit count of `HEAD` (`git rev-list --count HEAD`,
  `Get-RedXeDefaultBuildNumber`), the same formula locally and in the release workflow, so one commit is one version
  and the number only grows on `main`, which forbids force pushes. `build.ps1`, `test.ps1`, and `package.ps1` resolve
  it once through `Resolve-RedXeBuildNumber` (an explicit positive `-BuildNumber` wins) and pass it to MSBuild as
  `RedXeBuildNumber`, which `Directory.Build.props` defines for the resource compiler only as `REDXE_VERSION_BUILD`.
  A directory that is not itself the root of a checkout (an archive extracted inside another repository) and a
  shallow clone whose count would be truncated yield 0 with a warning; the release workflow fails on 0 and runs one
  release at a time. A new build number MUST NOT recompile C++ translation units. CI checkouts that build MUST
  fetch the full history (`fetch-depth: 0`).
- Every shipped executable's version resource includes `Common/Version.h`: `FILEVERSION`/`PRODUCTVERSION`
  `major,minor,build,0`, `ProductName` `RedXe`, `CompanyName` `RedSalamanders`. `test.ps1` MUST verify that
  `RedXe.exe` and `RedXeLauncher.exe` report the file version expected for its `-BuildNumber`.
- The package version, GitHub release tag, and winget `PackageVersion` are the same `major.minor.build`
  (`1.0.183`, tag `v1.0.183`). A winget manifest requires a positive build number, which an incomplete checkout (no
  history, a shallow clone, or a directory that is not the checkout root) does not provide.

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
  `--screenshot`, `--crash-test`, and `--crash-test-stack-overflow`, and for any line with an argument that
  `RedXeFindUnknownArgument` rejects (RedXe.exe reports it and exits 2 at once), it waits for `RedXe.exe` and returns
  its exit code unchanged, including an `NTSTATUS` above `INT_MAX` such as `0xC0000409` from a fail-fast exit.
  `--crash-test-directory=` only changes where a crash writes its dump; it does not make an ordinary launch
  self-terminating. `RedXe/CommandLine.h` owns the launcher's wait flag for each switch and the unknown-argument
  scanner both executables use.
- `test.ps1` MUST run `RedXeLauncher.exe --help` and check that the RedXe help text is relayed with exit code 0, and
  that an unknown switch alone through the launcher relays its error with exit code 2.

## Portable ZIP (`package.ps1`, `Build/Package.psm1`)

`package.ps1 -Platform <x64|ARM64> -BuildNumber <n>` builds Release (unless `-SkipBuild`) and writes
`.build/packages/RedXe-<version>-<Platform>-Portable.zip` plus a `.sha256` sidecar (`<hash> *<name>`: lowercase SHA-256,
one space, `*`, the file name, and a line feed, UTF-8 without a byte order mark). It then writes the
[symbols ZIP](#symbols-zip) beside it.

The package MUST contain, with forward-slash entry names and no directory entries outside these roots:

| Entry | Source |
| --- | --- |
| `RedXe.exe`, `RedXeLauncher.exe`, `yyjson.dll` | `.build/<Platform>/Release/` |
| `DxUi.provenance.json` | build output, when present |
| `Settings/RedXe.settings.json`, `Settings/RedXe-debug.settings.json`, `Settings/RedXe.settings.schema.json` | build output |
| `Plugins/**` | build output `Plugins\`, minus build artifacts (`.pdb .lib .exp .ilk .iobj .ipdb .obj .log .tlog`) and `Plugins\ZoomSdk\` |
| Visual C++ runtime DLLs | the newest `VC\Redist\MSVC\<v>\<arch>\Microsoft.VC*.CRT` of the Visual Studio installation that holds the MSBuild the build runs (`Find-RedXeMSBuild`), never another installation's; copied to the root and to `Plugins/` (the helper executables run as separate processes). A CRT older than the required MSVC family (`RequiredMsvcToolsVersion`) fails packaging, because a CRT shipped beside the binaries must be at least as new as their compiler and hosted CI installs that compiler outside Program Files, next to an older stock Visual Studio |
| `Install-RedXe.ps1`, `install.cmd`, `uninstall.cmd` | `Installer/` |
| `README.txt` | generated with the version and platform |
| `LICENSE.txt` | repository root (required) |
| `RedMonitor-LICENSE.txt` | the MIT section of `ThirdParty/RedMonitor/LICENSE.txt`, extracted at package time (required) |

- Every DLL named in `RedXe/BundledPlugins.h`, the AV Control helpers (`AVControlBroker.exe`,
  `AVControlCamera.dll`, `AVControlCameraSetup.exe`), `WeatherLocation.exe`, `libcurl.dll`, `Plugins/yyjson.dll`,
  the Weather Icons font and its `OFL.txt`, and `msvcp140.dll`, `msvcp140_atomic_wait.dll`, `vcruntime140.dll`,
  `vcruntime140_1.dll` at both levels are required. A missing required entry fails packaging.
- Build artifacts, `*Tests.exe`, `SystemDataPhase0.exe`, and anything under `Plugins/ZoomSdk/` are forbidden.
  `zoom.action.dll` opens Zoom's web join page and meeting invites in the default browser; it ships no Zoom SDK.
- Both `RedXe.exe` and `RedXeLauncher.exe` MUST be Release images of the target architecture stamped with the
  package version; a Debug or mismatched stamp fails packaging.
- `RedMonitor-LICENSE.txt` is the RedMonitor client's MIT notice and nothing else. The vendored
  `ThirdParty/RedMonitor/LICENSE.txt` is hash-pinned and is the whole RedSalamander licence, which also carries the
  notices of components RedXe does not ship (7-Zip, unRAR, LibRaw, FTXUI, pe-parse, and others), so it is never copied
  as is and never edited. `Get-RedXeRedMonitorLicenseNotice` extracts the MIT section: the `MIT License` heading, its
  copyright line, and the permission, condition, and warranty paragraphs, verbatim, with LF line endings and one final
  line feed whatever the checkout's line endings. Packaging fails with a message naming the file unless that section
  occurs exactly once, so a restructured vendored file stops the build instead of shipping other text. Interim: a
  vendored file that is only the client's MIT text satisfies the same extraction.
- The package is never written until the clean-extraction smoke accepts a staged copy: the archive is expanded into
  a fresh directory, its entry list is checked against the rules above, the packaged `RedMonitor-LICENSE.txt` is
  checked to be exactly the MIT section, and when the host can run the platform
  (`x64` on x64/ARM64 Windows, `ARM64` only on ARM64 Windows) the packaged `RedXe.exe --self-test --warp` MUST exit
  0 and `RedXeLauncher.exe --help` MUST exit 0 with the help text. Both run through `Invoke-RedXeStreamingProcess`
  with a time budget (fifteen and two minutes) and a log under `.build/logs/`, which outlives the extraction; the
  extraction is removed afterwards.

## Symbols ZIP

Release images are optimized and link-time code generated (`/GL`, `/LTCG`, `/OPT:REF`, `/OPT:ICF`), so a minidump
that RedXe wrote under `%LocalAppData%\RedXe\Crashes` can be symbolized only with the exact PDBs of the build that
produced the binaries, and the portable ZIP carries no PDB. `package.ps1` therefore also writes
`.build/packages/RedXe-<version>-<Platform>-Symbols.zip` beside the portable ZIP (`New-RedXeSymbolArchive`), with a
`.sha256` sidecar in exactly the same form (`Write-RedXePackageChecksum` writes both).

- It holds the PDB of every first-party executable and DLL that the portable ZIP lists, and nothing else: no
  test-executable PDBs, no other build products, no README or manifest. Entry names use forward slashes and the portable
  ZIP's layout (`RedXe.pdb`, `RedXeLauncher.pdb`, `Plugins/MatrixRain.pdb`, ...), so extracting it over an extracted
  package puts each PDB beside its module.
- A PDB is selected by base name from the same `.build/<Platform>/Release/` output the package was built from: the
  package entry `Plugins/X.dll` takes `Plugins/X.pdb` from the same folder. Shipped binaries that this repository does
  not build have no PDB there and are left out: the Visual C++ runtime DLLs and the vcpkg DLLs (`libcurl.dll`,
  `yyjson.dll`, and the zlib DLL), which the build copies without their PDBs. Any other shipped executable or DLL
  (for example `Plugins/LogiconProbe.exe`) contributes its PDB when it has one.
- Packaging fails, naming the binary, when a required first-party binary has no PDB: `RedXe.exe`, `RedXeLauncher.exe`,
  every DLL named in `RedXe/BundledPlugins.h`, and `AVControlBroker.exe`, `AVControlCamera.dll`,
  `AVControlCameraSetup.exe`, and `WeatherLocation.exe`. It also fails when a PDB's binary is not in that output, when a
  PDB is older than its binary by more than two seconds (the linker commits the two within milliseconds of each other,
  in either order, so a larger gap means the PDB was not written by the link that produced the binary), and when no
  PDB is found at all.
- The ZIP is staged under a temporary name, its entry names and sizes are read back against the selection, and only then
  is it moved into place. A failed run leaves neither the ZIP nor its sidecar, including those of an earlier run, so a
  symbols ZIP never sits beside the portable ZIP of another build.
- The ZIP exists to symbolize crash dumps of that release. The release publishes it per built platform and records it
  in the qualified source-bound receipt (see manual promotion below); because it is cut from the job output the portable ZIP was
  cut from, its PDBs match the released binaries exactly. To use it, extract it and add the extracted folder and its
  `Plugins` subfolder to the debugger's symbol path.

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
forbidden entry rules, CRT discovery, the RedMonitor notice extraction (the real vendored file read-only, and fixtures
that lack or repeat the MIT section), the symbols ZIP, manifest generation, and an install/upgrade/remove round-trip of
a synthetic package under both PowerShell hosts against scratch locations. The symbols ZIP is covered from a fake
Release output, a fake package listing, and a fake plugin catalog, with no real build: the exact PDBs and their paths,
the runtime DLLs left out, a missing or stale PDB failing with the binary named, no leftover ZIP after a failure, and
the sidecar form shared with the portable ZIP. It MUST leave no registry key, shortcut, or folder behind.

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
  carve-outs it lists: the CC BY-NC-SA 3.0 Shadertoy ports and the OFL Weather Icons font). The package always
  ships `LICENSE.txt`; a license change updates the file and the template together.

## Manual qualified-artifact promotion

The current workflow is .github/workflows/publish.yml and the pinned organization engine, governed by [Build_UnifiedCI.md](Build_UnifiedCI.md). The removed release/winget workflow files are historical, not current entrypoints.

An original successful main qualify.yml batch freezes source and version, executes test-enabled Release regressions on both native architectures, passes the global AND gate, and then builds production-only Release and both portable/symbol ZIPs. Publication never rebuilds, re-stamps or substitutes a failed/partial batch. Full Debug/ASan and other independent acceptance remain manual obligations.

The explicit main publication action defaults to Prepare. Release promotes only the retained ZIP bytes for the qualified version/source, verifies their hashes and anonymous URLs, and leaves existing conflicting tags/assets untouched. An identical retry revalidates/reuses its existing exact assets. A newer failed native input cannot borrow an older successful receipt; missing or expired proof requires a fresh explicit native batch.

Winget publication uses the two portable ZIPs, the configured RedXeLauncher.exe alias target, immutable installer URLs/hashes, matching native install/alias/cleanup proof and the dedicated publication credential. Symbols ZIPs never become installer payloads. The product clean-extraction WARP/launcher diagnostics remain required before ZIP qualification; post-install launcher diagnostics retain the existing product-specific requirements. Actual release/submission remains an operator action.

## Validation

Changes to this contract require:

```powershell
.\test.ps1 -Full -Configuration Debug -Platform x64    # includes the launcher checks and PackagingTests.ps1
.\package.ps1 -Platform x64 -BuildNumber 1             # the portable ZIP and the symbols ZIP, each with its sidecar
.\winget-manifest.ps1 -Version 1.0.1 -Arm64ZipPath <an ARM64 package or a copy for local validation>
```

For workflow changes, retain exact two-architecture batch receipts, a successful Prepare and the required refusal controls. Public release and native winget install/submission qualification are separate manual operator actions; no local fixture or Prepare pass establishes those live outcomes.

RedMonitor is installed independently. Application packages include its client MIT notice (`RedMonitor-LICENSE.txt`, the MIT section of the pinned licence file, as described above) and no viewer executable, runtime, satellite or theme payload. Normal builds verify only the pinned local header/license; see [RedMonitor integration](../Core/Core_RedMonitorIntegration.md).

The canonical package entrypoint builds ProductionOnly Release. With -SkipBuild it requires matching production-only DxUi provenance for the shipping host/plugin modules; test-role or role-less output is refused. The existing clean-extraction product diagnostic smoke and ZIP/symbol contracts remain required.
