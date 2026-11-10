# I61 pinned SDK consumer qualification

Status: local implementation and the coverage below passed; hosted SDK-hit/runtime
acceptance and organization-wide acceleration remain open in the shared WIP plan.

Base: `7f135dcd005bd7b0e5c911cfe93e6563b1d7af30`. Shared controller/callers:
`008fb75d3a66138976b6beceb6df0d75dbb424cc`. The dependency remains the exact
`f2e328a0a26a18ca66bf4b2aea77bf010c9515e1`, API 4. No test selection,
assertion, warning threshold, native gate or package requirement was relaxed.

## Executed coverage

| Invocation | Result |
|---|---|
| Source fallback, x64 Release build and `test.ps1 -Full -SkipBuild -SkipTooling` | Zero warnings/errors; all 15 native scopes passed |
| Source x64 Debug and ASan Debug builds and the same Full native selection | Both builds zero warnings/errors; all 15 scopes passed in each profile |
| ASan fault, CRT report, abort and unbuffered-output probes | Passed, including actual sanitizer detection and expected exits 3/4 |
| Source ARM64 Debug, Release and ASan Debug cross-builds | All zero warnings/errors; no local ARM64 runtime claim |
| Exact native-host SDK test-enabled application build and Full native selection | Actual SDK hit; zero warnings/errors; all 15 scopes passed |
| Exact SDK production-only build and `package.ps1 -SkipBuild -DxUiSdkInputDescriptor ...` | Zero warnings/errors; clean-extraction package and symbol archive passed |
| `Tests/BuildProcessTests/Invoke-ToolingTests.ps1` | Complete independent tooling suite passed |
| Actual shared SDK MSBuild admission controls | 30 passed, including invalid per-file ABI and closed-reader controls |
| Actual adapter branches | 60 role/profile/architecture/availability combinations passed; the added cold-source control first reproduced the defect, then passed after repair |
| Compiler-host cleanup controls | Actual finally blocks preserve the caller's host and primary failure after SDK close errors |

The last tooling changes correct nested-error-boundary inspection and the hosted
cold-SDK validation path; they do not change native product code or test binaries.
The complete tooling pass preceded that final cold-path repair; its affected
60-combination adapter controls and workflow validation were rerun afterward.
The native runs are recorded as their actual executions, without rewriting
receipts to assert execution on a later documentation commit.

## SDK identity and retained evidence

The first SDK request correctly missed: the existing producer used a different
compiler-host profile. Read-only metadata comparison identified the changed host
and search-path properties. A separate SDK was built from the unchanged pin with
the product's native x64 compiler host, then qualified through all producer SDK
gates (391 public headers, native smoke, notices, PDB isolation/missing-PDB and
warning detection). Existing entries were retained.

- SDK key: `5868120cc91a06d158d7c4ccf617517e457546b0dcc92201099ad707a231fc98`.
- Entry manifest SHA-256: `8bb5b8de037adf5e4ee522bd96cfa8db9ea82629c10edae5121c205162499ee1`.
- Producer tooling: `6c89bb60c01e14ab4008e300ea73b44d206efb24`; storage engine: `e3c637bd9a44bd409f30b5a1c1f28f155d2319dd`.
- Package: `RedXe-1.0.447-x64-Portable.zip`, SHA-256 `dcef319db8dd35a4472ad74e44b54692c5934d26eb381fc9ddc4f5fbe31498ff`.
- Symbols: `RedXe-1.0.447-x64-Symbols.zip`, SHA-256 `3c4c5d74e6ae287c97562e23da960fa74c94253f34b8387429b9bf38b1e0b4f0`.

Raw logs, immutable native runs, SDK witnesses/controls and both build-role
provenance records remain beneath
`Z:/src/.worktrees/RedXe-i61-sdk-inputs-20261110/.build/`.
The separately qualified producer and host-comparison evidence remain beneath
`Z:/RedSalamander.Perf/evidence/i61-resume-20261110/` and the shared engine's
owned `.build/sdk-input-sources/` workspace. [evidence.json](evidence.json)
records the local log/record hashes. Generated payloads are not committed.

Source fallback took 5m07, the SDK test build 2m40 and SDK production 1m40.
These are different warm/cold states and roles, not a controlled speed comparison.
They establish operation and coverage, not a percentage saving or the shared
plan's 3x/2x targets. Hosted exact-key acquisition remains an independent gate.

Earlier failed dependency-lock, host-profile, package-reopen and tooling attempts
remain retained. The first package attempt correctly rejected the different
compiler-host recipe; packaging now selects/restores the same shared native-host
policy as build/restore, even on failure. No failed attempt is relabelled passed.
