# Generated test-run evidence

Status: normative current tooling contract
Last reviewed: 2026-10-29

After argument, desktop and build preflight, native test execution writes each invocation under `.build/evidence/test-runs/<UTC-pid-guid>/payload`.
The id is the Gregorian UTC time as `yyyyMMddTHHmmssfffZ`, formatted with the invariant culture whatever the current
culture or calendar, then the owner process id and a GUID. The repository MUST be a fully qualified local directory:
relative, drive-relative (`C:x`), rooted-only (`\x`), UNC and drive-root arguments are refused before anything is created.
It creates a new directory and exclusive owner lease; it never reuses an old run directory or sweeps older runs.
A flushed, create-only `started.json` records the owner process and start time plus the requested profile/scopes.
The `completed.json` record is staged, flushed, independently read back and renamed without overwrite only after
payload verification. A failed or interrupted invocation retains its output. Forced termination can leave only
`started.json` and partial data; absence of a valid completion record is never a successful result.
`test.ps1` completes the record before it prints a verdict. A completion failure after a passed run fails the run with
that error; after a failed run it is a warning naming the run directory, and the run's own failure stays the error
reported.

Verification includes hidden files, long Unicode paths, ordinary file contents and named data streams (SHA-256),
128-bit file identities, attributes, creation/write timestamps and owner/group/DACL metadata. Existing reparse
ancestors and payload links are refused without traversing or deleting their targets. Directory identity is freshly
checked; sharing flags alone do not prevent all ReFS directory renames. Files still open for writing fail verification.
A failure Windows reports for a path names the OS reason, then the path, and keeps the native error code.
Unique evidence stays in place; no archive payload copy, attribute normalization or read-only clearing occurs.
Shared build/dependency/qualification files remain outside the writable run, and their existing read-only rules apply.

The record establishes retention and the outcome of the selected invocation only. It does not replace exact source,
binary/dependency/profile/scenario attestation, qualify omitted tests, or turn a capability skip, pending CI check,
cross-build, synthetic probe or failing comparison into native/product qualification. Sandbox markers, path containment,
process ownership, watchdog, desktop consent and ReFS capability checks remain required by their owning contracts.

Generated evidence stays ignored by Git. Normal builds do not sweep it. CI collects the new evidence path, including
hidden files, where the branch has an enabled native artifact workflow. Transport retention and stream support are
separate from lossless local evidence. Historical cleanup requires a separate inventory, liveness check and verified
same-volume move or full independent copy verification; a timestamp or dead PID alone never authorizes deletion.

`test.ps1` writes the log of each native test executable and `RedXe.exe` run it launches into the payload, with RedXe's
own `--screenshot` run settings, PNG/JSONL output, crash dumps/reports/markers and isolated failure fixtures. Each
unattended argument-error case has its own log. Fixture executable/runtime copies remain exact run inputs; archival makes
no additional payload copy.

Native test output under `.build/test-artifacts` is not part of the payload. The AVControl size-matrix PNGs
(`.build/test-artifacts/AVControl`) and the CameraPackage fixtures (`.build/test-artifacts/CameraPackage/<guid>`) are
written there, and `test.ps1` does not pass `-CaptureNativeArtifacts`: the next AVControl run overwrites those PNGs,
nothing in the run removes or moves the CameraPackage folders, and CI uploads the folder beside the evidence path. The
switch is the opt-in that moves such a folder into a run (`previous-native-artifacts` when the run starts,
`native-artifacts` when it completes) under the intent and observed-inventory verification below.

Inventory attributes and creation/write times come from pinned file handles, rather than cached directory entries.
Compare inventories in canonical relative-path and stream-name order, ordinal by UTF-16 code unit and never the current
culture's, without relaxing any identity, hash, attribute, time or ACL check. The `payload`, `inventory` and `streams`
fields of the records are JSON arrays whatever their length: one entry or none is not unrolled. Native moves retain the
exact intent and observed post-move inventories before comparison; a mismatch retains both snapshots and the payload
and refuses completion. Independent regression controls keep stale provider metadata and reverse enumeration while
verifying the actual file and directory metadata exactly. Further controls pin the invariant run id under a
non-Gregorian culture, ordinal order, the refused repository forms, single-entry list fields and the OS reason of
Windows failures; every negative control asserts its specific error and cannot create output outside its scratch
directory.
