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

For independent resource investigations, force fresh execution and retain paired fixture/build provenance. Timing, native ARM64, real devices, IME/assistive technology and foreground input remain separate qualification requirements. These commands do not manufacture those claims.
```powershell
./Test-Changes.ps1 -Scopes <name> -SkipBuild
./Test-Changes.ps1 -Mode Full -Force
./test.ps1 -Full
```

Repeated unchanged scopes print REUSED. Force reruns them. SkipBuild requires an attestation established by a prior Test-Changes build; stale binaries are rejected. Test-Changes stamps its builds with the commit count of the merge base with `origin/main` (printed as `Build number:`), so committing does not invalidate results; `./test.ps1 -Full -SkipBuild` or `-Suites` on those binaries needs `-BuildNumber <that number>`. `-Scopes A,B` also works from bash, cmd or a hook. If files change while tests run, the run still passes with `SELECTED_PASSED; NOT_RECORDED` and a warning naming the scopes it could not record; run again to cover the current tree. Explicit `test.ps1 -Suites` calls retain the lower-level diagnostic interface. Its ordinary call now routes to affected iteration; Full is explicit. Full includes the BuildProcess tooling suite, which needs Python with `Build/requirements-validation.txt`; SkipTooling leaves that suite to the CI tooling job. Without Git, `origin/main` or history shared with it (a source archive, another remote name, a shallow clone), the ordinary call says why and runs every suite. An unknown local comparison ref or one without shared history fails `Test-Changes.ps1` with the failing Git command and what to do rather than silently selecting no work.
