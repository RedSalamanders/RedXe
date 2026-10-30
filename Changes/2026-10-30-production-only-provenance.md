- Let `build.ps1 -ProductionOnly`, the unified CI's production role, finish. Its DxUi provenance step read the link
  log and hash of `AVControlTests.exe`, which a production-only build never builds, so both hosted production jobs of
  the first qualification run (41119549299) failed after a clean MSBuild. The record of a production-only build now
  lists RedXe and AVControl only. `Get-RedXeDxUiProvenanceModules` is the one list the writer and
  `Assert-RedXeDxUiProvenance` share. `DxUiProvenanceTests.ps1` accepts each kind of record only as what it is, and
  checks that `build.ps1` passes `-ProductionOnly` to the writer.
