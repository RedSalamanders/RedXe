- `test.ps1` now completes the run's evidence record before it prints its verdict: when completion fails after a passed
  run, the run fails with that error instead of following a printed success, and after a failed run it is a warning that
  names the run directory while the run's own failure stays the reported error (an exception thrown from the `finally`
  block used to replace it). In `Build/TestRunEvidence.psm1`, Windows failures name the OS reason before the path and
  keep the native error code, run ids use the invariant Gregorian calendar under any current culture (th-TH no longer
  writes year 2569), inventories and named streams are compared in ordinal order instead of the current culture's, `\x`
  and `C:x` repository arguments are refused with the drive-root and UNC ones, and a one-entry or empty native-artifact
  inventory and a one-element record stay JSON arrays instead of a bare object or null. Every negative control in
  `TestRunEvidenceTests.ps1` now asserts its specific error and cannot create output outside its scratch directory, and
  each fix has a control that fails when the fix is reverted (74 checks on ReFS, 75 on NTFS). `Tests/README.md` and the
  evidence contract no longer say the AVControl PNGs and CameraPackage output are retained in the evidence: they are
  written to `.build/test-artifacts`, the next run overwrites them and `test.ps1` does not pass
  `-CaptureNativeArtifacts`. The self-test and soak log paths now point at `.build/evidence/test-runs/<id>/payload`.
  Validated with `pwsh -NoProfile -File Tests/BuildProcessTests/TestRunEvidenceTests.ps1`, a parse check of `test.ps1`,
  ten one-fix-reverted variants of the module, and the runner's own finalization statements executed for passed and
  failed runs with and without a completion failure; `test.ps1` itself and a native build were not run.
