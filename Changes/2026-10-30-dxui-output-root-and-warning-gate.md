- Make the DxUi output root and the zero-warning gate safe for parallel and repeated builds. Restores of one new
  build fingerprint that start together, such as parallel builds in one checkout, all get the root. Before, every
  restore but the first failed on the identity marker the first one wrote. `Initialize-RedXeDxUiOutputRoot` renews
  the root's lease, then checks or creates the root and its marker under the root's own mutex. It rewrites an empty
  marker that a killed restore left alone in its root, and any other refusal names the folder to delete. A build the
  zero-warning gate refuses now resets the MSBuild tracking logs of every project its diagnostics name, so an
  unchanged rerun compiles those files again and keeps failing instead of reporting green. The diagnostic summary
  uses the gate's grammar, so a warning without a code no longer prints as "0 warning(s)" before the gate refuses
  it. `-Run` checks the OS architecture instead of the PowerShell process's. The entry scripts declare PowerShell 7.
  `DxUiRestoreTests.ps1` runs 40 parallel first initializations (10 of them fail without the mutex). It also checks
  the lease of a refused root, both empty-marker cases and the 16-digit source folders, and replaces an ordering
  check that could not fail. `BuildProcessTests.ps1` covers the shared grammar, the tracking reset and the
  `build.ps1` gate wiring.
