# I61 qualified SDK v2 application run

The actual x64 Release SDK-backed test build passed with zero warnings/errors,
then all 15 native scopes passed, including production-host Matrix, Studio Clock
and Desk Clock resource guards, native failure/metadata controls and application
smoke. The test-disabled SDK production build, clean-extraction portable package
and exact symbol archive also passed. The DxUi pin remains
`f2e328a0a26a18ca66bf4b2aea77bf010c9515e1`, API 4.

Only the parent process's private hosted-origin resolver was controlled. SDK,
ABI reference, native SDK receipt, payload-byte verification, actual MSBuild,
linker provenance, application/runtime tests and package reopening were real.
This establishes a local application SDK hit against those exact inputs; it
does not establish live GitHub-origin authentication, a hosted SDK hit, native
ARM64 execution or organization-wide acceleration targets. The fixture producer
ID 6111102 is not a claimed GitHub run.

The executed controller was `64364b1e375b76c248b556fa70e5777fefa58ba3`.
Subsequent Linux case-preservation and documentation changes are not rewritten
into the original run. The complete sequence took 578,333 ms with no paired
baseline. Exact input, application/module and archive identities are in
[evidence.json](evidence.json).

Raw evidence remains in this worktree's `.build/`:

- `i61-qualified-sdk2-application-owned-paths.log`
- `sdk-v2-reader-controls/ed599dcd33b84932840a93b85872c78b/`
- Native run `20261110T123801886Z-41564-12b2dff9355b4394b01f70904feba310`

Earlier environment/property guard failures are retained separately. They exposed
real incompatibilities in the new guard and are not relabeled successful. The
final shared guard preserves default system search order, admits existing owned
headers and refuses foreign/reordered paths or unsupported compiler overrides.
Live hosted rollout results remain independent gates.

## Source-profile checks

All six Source builds completed with zero warnings/errors on the final controller.
Full native execution passed all 15 scopes in each x64 Debug, Release and ASan
Debug profile, including actual ASan detection. Complete independent tooling,
Source production compilation, portable extraction smoke and symbol packaging
passed. Three controls ran the real package command to reject unknown recipes
and either SDK recipe without a matching descriptor, restoring the exact original
provenance afterward. Their logs and hashes are in [source-profile-checks.json](source-profile-checks.json).
PrePush explanation and base-aware changelog/workflow checks were also run.
This patch changes no C++ files, so changed-source formatting has no selected
C++ work; the repository formatter has no read-only Check option.
These evidence additions follow the executions and do not rewrite their snapshots.
