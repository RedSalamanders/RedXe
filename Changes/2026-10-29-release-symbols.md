- Releases now publish a symbols ZIP per platform and the package carries only the RedMonitor client's MIT notice.
  `package.ps1` writes `RedXe-<version>-<platform>-Symbols.zip` beside the portable ZIP, with a `.sha256` sidecar in
  the portable ZIP's form (both written by one helper): exactly the PDBs of the first-party executables and DLLs the
  portable ZIP ships, taken from the same Release output at the package's relative paths, so a crash dump of the
  optimized /LTCG binaries can be symbolized. Runtime and vcpkg DLLs and test PDBs are left out; packaging fails naming
  the binary when a first-party binary has no PDB or one more than 2 s older than it (the Debug linker output shows the
  PDB landing within milliseconds on either side). `release.yml` uploads the symbols ZIP with the package, requires it
  for every built platform, verifies its sidecar, adds it to `checksums.sha256` and publishes it; the winget workflow
  and manifest tooling select the Portable ZIPs by exact name and are unaffected. `RedMonitor-LICENSE.txt` was the
  whole RedSalamander licence with notices for components RedXe does not ship; it is now the MIT section extracted from
  the unchanged, hash-pinned vendored file, packaging fails unless that section occurs exactly once, and the package
  smoke re-checks the shipped file. `PackagingTests.ps1` covers both from fake outputs, fixtures and the real vendored
  file (17 new checks, whole script passed under pwsh); defects injected into a scratch copy of the module were each
  caught, and the `release.yml` verification step ran against fabricated assets. Not run here: a real Release
  `package.ps1`, `test.ps1`, or a Release workflow dispatch, so the first published symbols ZIP still needs a look.
