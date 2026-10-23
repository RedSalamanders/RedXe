# Native compiler host qualification — 2026-10-23

Code commit: `1771f71efa56f06da9860bcdfbcf095711c26154`.
Compiled source identity: `8a2dc251d35d70ebf9355d3c7636d31b5cb8dde11ff475ac2c3a8e453915eec8`.
Client pin: `9ebea6577fe9a5ced7647283c918f51f67d666fd`; DxUi pin: `fa4da2cb112e029646835f81f14ee2e0185129a7`.

All six normal-wrapper builds passed, with the caller compiler-host environment checked unchanged after each.
All 15 x64 Full runtime scopes passed separately in Debug, Release and ASan Debug. The ASan profile reported
`PASS AddressSanitizer detection probe`. Independent tooling passed, including native OS policy and six real
MSBuild native/emulated-host and explicit-override controls. Original logs are under `.build/` as
`header-native-host-{platform}-{configuration}-build.log`, `header-native-host-x64-{configuration}-test.log`
and `header-native-host-tooling.log`; spaces in configuration names are replaced with hyphens.
The matching `DxUi.provenance.json` files remain beside each profile's application.

The production C++ and header closure is unchanged from be0e2bf. Read-only comparison confirmed that the three
current x64 executable hashes exactly match the already observed ETW and absent-helper probes retained at
`.build/header-final-probes/d61dbfd3b2914e1aaf63dde5ba7c3cc9/receipt.json`. Those probe observations are retained
for these identical binaries; they are not described as newly executed probes.

| x64 profile | Observed and current executable SHA-256 |
| --- | --- |
| Debug | `252EF66CDB6281BE1EC4AED8DB361F91A04F6CCB30A30F9009537B3AC72A99A6` |
| Release | `31E0C91AEF7DFCE0C467EB949BC12082653449D12AB858B1FA90DC95199598B7` |
| ASan Debug | `A958A096B1DABD1E73758F184DB94F348CFC39A4695762B6C9587010AAF4CACF` |

The current portable ZIP passed clean-extraction smoke checks. Read-only entry inspection found no viewer EXE
or satellites and confirmed `RedMonitor-LICENSE.txt`. Package path: `.build/packages/RedXe-1.0.349-x64-Portable.zip`;
SHA-256: `5A4A052202366649ABE32E2A839E7ABF45C8059A427828C2497DA0C2670822EC`.

Hosted [run 39943731651](https://github.com/RedSalamanders/RedXe/actions/runs/39943731651) passed tooling and all
three x64 jobs but failed the three ARM64 builds at the compiler-host identity guard. The revised native OS policy
was checked by [run 39947763357](https://github.com/RedSalamanders/RedXe/actions/runs/39947763357): tooling, all x64
jobs and native ARM64 Debug/ASan passed. Native ARM64 Release built and then failed the combined idle CPU/notification
assertion. The log cannot distinguish a CPU overrun from a late completion notification. A test-only follow-up
adds a deterministic late-notification witness, bounded quiescence and failure diagnostics while retaining the
original idle budget and sample interval. It needs new qualification; no repeat establishes the original cause.
Local ARM64 evidence establishes compilation only. Physical input/IME/AT,
paired Release performance and release publication remain separate WIP gates.

Final native matrix: [run 39954330299](https://github.com/RedSalamanders/RedXe/actions/runs/39954330299)
at test follow-up commit `a55901c1fad009dab2fbd068002846a33ead2160` passed tooling and all six native
Debug/Release/ASan jobs on x64 and ARM64. Each archived runtime receipt contains all 15 requested scopes
and a passed outcome. Every recorded payload stream hash was checked after download; the reviewed
started/completed, artifact archive and DxUi provenance identities are retained in
[native-hosted-validation.json](native-hosted-validation.json). The published ARM64 provenance records
compiler host x86, consistently for restore/consumer compilation; the wrapper preserves an explicit
host override. Native-OS default/override policy is also covered by the independent tooling controls.
Debug/Release AV resource fixtures passed. This closes the native ARM64 runtime gate for this commit;
it does not establish the original idle failure's cause or paired performance non-regression.
