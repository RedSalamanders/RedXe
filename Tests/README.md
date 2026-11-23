# Running only the tests your change affects

```powershell
./Test-Changes.ps1 -Explain
./Test-Changes.ps1 -Configuration Debug
./Test-Changes.ps1 -Mode PrePush -Explain
./Test-Changes.ps1 -Mode Full -Configuration Debug
```

The pinned engine runs `Test-Changes.ps1`. Affected iteration starts from local changes or the prior eligible
iteration. PrePush selects all six profiles, including ARM64 cross-builds. `-Explain` lists the chosen scopes
and their inputs. The build process and formatter are independent scopes. See the
[scoped validation contract](../Specs/Build/Build_Process.md#scoped-iteration-and-qualification).

Native test files use `Scope.Tests.Something.h/.cpp`; register new active files in `native-test-files.json`. Outside `Tests/`, a `Test`, `Mock` or `Fake` name component (`FakeClock.h`) or a `.Tests.` segment marks a test source; `Attestation.h` or `LatestRelease.cpp` does not. `test-scopes.json` owns standalone scope and profile data and the suite names `test.ps1` runs; `ScopedTesting.Tests.ps1` fails when a rule misses a suite that builds its paths or matches nothing. Shared-library or runtime byte changes conservatively invalidate local standalone reuse. No consumer dependency pin changes are part of this workflow.

The `HostPlugin` scope includes isolated 60-second Matrix, Studio Clock and Desk Clock WARP resource guards.
Matrix warms for 1,024 frames; clocks warm for 20 seconds at their scheduled cadence. Five-second and final
samples in uninstrumented builds enforce peak growth of at most 4 MiB private bytes and 8 MiB working set above the warmed baseline,
alongside fixed object/resource counts, scheduling/draw bounds and device teardown. Per-soak logs are saved in
`.build/evidence/test-runs/<id>/payload/HostPluginTests.<name>-soak.log`. The separate five-minute plugin qualification
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

`REUSED` requires matching inputs and a successful suite report with no capability skip. `-Force` reruns the
selection, and `-SkipBuild` requires a matching attested scoped build. A direct `test.ps1 -Full -SkipBuild` or
`-Suites` run on shared-runner output needs its merge-base commit count as `-BuildNumber`. Direct `-Full` runs
the full noninteractive batch, while a plain call routes to affected iteration. If Git or the default comparison
base is unavailable, the plain call runs the full batch. `-SkipTooling` leaves BuildProcess and Format to their
own runs.


The logs of native test executables and `RedXe.exe` runs, receipts, and RedXe's own capture, crash and fixture output stay under `.build/evidence/test-runs/<id>/payload` on success, failure and interruption.
The AVControl size-matrix PNGs (`.build/test-artifacts/AVControl`) and the CameraPackage fixtures (`.build/test-artifacts/CameraPackage/<guid>`) are not part of it: `test.ps1` does not pass `-CaptureNativeArtifacts`, so the next AVControl run overwrites those PNGs, the CameraPackage folders stay behind unmanaged, and CI uploads the folder beside the evidence. Copy anything you need from there before running again.
See the [retention contract](../Specs/Testing/Testing_RunEvidence.md) for verification and qualification boundaries.

RedMonitor controls verify the two vendored files entirely offline: exact bytes, missing/tampered inputs, invalid locks and reparse paths. No source checkout or viewer build is required. `test.ps1` checks the `--redmonitor` and `--redmonitor-probe` command line without a viewer: both refuse other arguments, an absent viewer is named, and the probe reports a missing listener. RedMonitor's TransportRoundtripTests runs the actual `RedXe.exe --redmonitor-probe` for cross-process Unicode/severity/PID/TID; installer closure tests belong to RedMonitor.
