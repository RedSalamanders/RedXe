# Running only the tests your change affects

```powershell
./Test-Changes.ps1 -Explain
./Test-Changes.ps1 -Configuration Debug
./Test-Changes.ps1 -Mode PrePush -Configuration Release
./Test-Changes.ps1 -Mode Full -Configuration Debug
```

The default uses local merge-base changes plus staged, unstaged and untracked work. Read the explanation before a large run. Unknown code/build inputs widen coverage. Selected work is partial evidence, and a plan with nothing selected ends with `NOTHING_SELECTED; repository NOT_EVALUATED`: run `./test.ps1 -Full` for a full gate. PR delegation is pending CI work (`CI_PENDING`). Only an enabled workflow with matching reviewed candidate bytes, a runner `origin/main` has not changed since the merge base, and a PR check that `main` requires can justify it; PrePush prints `PR delegation not used: <reason>` otherwise. The tooling check covers `BuildProcess` for every profile, the `native (x64, Release)` check only x64 Release.

Native test files use `Scope.Tests.Something.h/.cpp`; register new active files in `native-test-files.json`. Outside `Tests/`, a `Test`, `Mock` or `Fake` name component (`FakeClock.h`) or a `.Tests.` segment marks a test source; `Attestation.h` or `LatestRelease.cpp` does not. `test-scopes.json` owns standalone scope/PR metadata and the suite names `test.ps1` runs; `ScopedTesting.Tests.ps1` fails when a rule misses a suite that builds its paths or matches nothing. Shared-library or runtime byte changes conservatively invalidate local standalone reuse. No consumer dependency pin changes are part of this workflow.

The `HostPlugin` scope includes isolated 60-second Matrix, Studio Clock and Desk Clock WARP resource guards.
Matrix warms for 1,024 frames; clocks warm for 20 seconds at their scheduled cadence. Five-second and final
samples in uninstrumented builds enforce peak growth of at most 4 MiB private bytes and 8 MiB working set above the warmed baseline,
alongside fixed object/resource counts, scheduling/draw bounds and device teardown. Per-soak logs are saved in
`.build/<Platform>/<Configuration>/HostPluginTests.<name>-soak.log`. The separate five-minute plugin qualification
still uses `HostPluginTests.exe --matrix-soak`, `--studio-clock-soak` or `--desk-clock-soak` with `--seconds=300`.
ASan retains its default quarantine and lifetime checks; it reports process bytes diagnostically and checks the
same object/resource counts, scheduling and teardown, without applying the uninstrumented byte-growth bar.

Desk Clock uses two draws in static and moving states, preserving the 40/46 instance and 400-byte changed-only
upload bounds. Independent pipeline queries verify 243/279 total vertices and unchanged 81/93 triangles.
`DeskClockTests` compares at least 40 complete frames to frozen original vertex/pixel shaders and the offset-buffer
draw sequence. It covers static/flip/date phases and landscape/portrait/minimum/DPI extents at both initialized
atlas tiers, including unequal/equal-length date-only fades. Contrasting target clears verify complete opaque
background overwrite; its temporary reference and query resources exist only in the test. Scheduled memory guards
retain their existing 4/8-MiB limits and DXGI pacing.
Release-only `DeskClockTests.exe --submission-probe [--hardware]` runs the same offscreen fixture against either
clock DLL, reports its draw counts without enforcing the candidate's two-draw contract, and performs no oracle
draws before timing. The optional hardware flag chooses the default adapter, not the XENEON adapter; live XENEON
cadence still needs the production probe. Keep identical probe executable, runtime and backend identities in pairs.

`PluginContractTests.exe --shader-setup-attribution --warp <new-raw-path>` records six call-local Shaders setup phases
and saves every production background mip texel. `--hardware` selects the default hardware adapter and prints its
identity; it does not select the XENEON adapter or show a window. The regular `PluginContract` scope compares all
349,525 texels to the independent original pixel-major oracle and checks diagnostic refusal, output bounds and
temporary-owner retirement. Native setup times include driver/API work and retirement, not GPU completion or
displayed/input latency. Keep old/new runtime and raw-payload hashes with any comparison.

For independent resource investigations, force fresh execution and retain paired fixture/build provenance. Timing, native ARM64, real devices, IME/assistive technology and foreground input remain separate qualification requirements. These commands do not manufacture those claims.
```powershell
./Test-Changes.ps1 -Scopes <name> -SkipBuild
./Test-Changes.ps1 -Mode Full -Force
./test.ps1 -Full
```

Repeated unchanged scopes print REUSED. Force reruns them. SkipBuild requires an attestation established by a prior Test-Changes build; stale binaries are rejected. Test-Changes stamps its builds with the commit count of the merge base with `origin/main` (printed as `Build number:`), so committing does not invalidate results; `./test.ps1 -Full -SkipBuild` or `-Suites` on those binaries needs `-BuildNumber <that number>`. `-Scopes A,B` also works from bash, cmd or a hook. If files change while tests run, the run still passes with `SELECTED_PASSED; NOT_RECORDED` and a warning naming the scopes it could not record; run again to cover the current tree. Explicit `test.ps1 -Suites` calls retain the lower-level diagnostic interface. Its ordinary call now routes to affected iteration; Full is explicit. Full includes the BuildProcess tooling suite, which needs Python with `Build/requirements-validation.txt`; SkipTooling leaves that suite to the CI tooling job. Without Git, `origin/main` or history shared with it (a source archive, another remote name, a shallow clone), the ordinary call says why and runs every suite. An unknown local comparison ref or one without shared history fails `Test-Changes.ps1` with the failing Git command and what to do rather than silently selecting no work.


Generated test output stays under `.build/evidence/test-runs/<id>/payload` on success, failure and interruption.
See the [retention contract](../Specs/Build/Build_TestRunEvidence.md) for verification and qualification boundaries.

RedMonitor controls verify the two vendored files entirely offline: exact bytes, missing/tampered inputs, invalid locks and reparse paths. No source checkout or viewer build is required. Actual application --redmonitor-probe tests cover cross-process Unicode/severity/PID/TID; installer closure tests belong to RedMonitor.
