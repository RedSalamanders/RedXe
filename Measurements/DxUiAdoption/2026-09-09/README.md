# DxUi adoption evidence — 2026-09-09

Baseline product: `75545887d002b5075d170d98b7bfd3a77d8d7f61`, DxUi `d192e474e540adc2656e69b8e8150b0f7a05d63f`.
Candidate product code: `4f1d4eb`, DxUi `8c548fe2de3cdb5af858c457e9bef47d81506878`.
The source paths in raw receipts identify the actual original/short checkout used for that run.
The subsequent product `1bf6662` with DxUi `52da33d` passes all six native profiles in
[CI run 34368430957](https://github.com/RedSalamanders/RedXe/actions/runs/34368430957);
`native-ci-1bf6662.json` retains the job identities and outcomes. Its full local x64 Debug,
Release and ASan Debug suites also pass. The older local receipts below remain retained.

| Configuration | x64 | ARM64 |
|---|---|---|
| Debug | Full product tests pass | Native CI passes at 1bf6662 |
| Release | Full product tests pass | Native CI passes at 1bf6662 |
| ASan Debug | Full product tests and deliberate fault detection pass | Native CI and deliberate fault detection pass at 1bf6662 |

Every candidate profile has an actual linker/module provenance receipt. The previous Release
package was retained before replacement, verified against `baseline-package.json`, extracted after
candidate qualification and exercised through the AV suite, host/plugin suite and hidden WARP
application smoke. See `rollback-x64-Release.json` and its three logs.

These runs validate the recorded behavior and static archive identity. Concurrent build activity
means their timings are not matched performance acceptance. DxUi is public; the consumer needs
no private-read token or custom secret. Real IME, screen-reader, touch and AV hardware qualification remain with the existing
AV release gates; this adoption does not certify them.

The ecad707 candidate passes complete local x64 Debug, Release and ASan Debug
product suites with zero build warnings/errors. The duplicate PR run at the earlier
1bf6662 product revision (34368436753) exposed a Process Viewer test assumption:
its process-lifetime delivery counter need not start at zero after previous hosts.
The successful earlier run remains retained alongside this failure, not a flake waiver.
Repeating the subscription case in one process reproduces the assertion failure.
The corrected test checks delivery deltas and requires new samples after visibility;
it retains the hidden drain and object-count assertions. Full x64 Debug passes with
the repeat. `process-counter-regression.json` binds both logs. Complete local
Release and ASan Debug suites also pass with the repeat. Product a812cec passes
all six native profiles in [CI run 34374884161](https://github.com/RedSalamanders/RedXe/actions/runs/34374884161);
`native-ci-a812cec.json` retains the complete job outcomes.

The new `measure-av-views.ps1` fixture measures the same production AV views against
the original and candidate pins. Its initial Debug smoke is functional harness
validation under concurrent builds, not performance acceptance. The original pin also passes that smoke,
and the complete local AV suite passes Debug/Release/ASan Debug after the harness addition.
Eight [matched runs on September 12](../2026-09-12/README.md) now verify source identity and
surface/hidden-work assertions. Timing acceptance remains open; all raw results and repeat comparisons are retained.

The subsequent 80b02a3 CI jobs did not start. GitHub's annotation in
`native-ci-80b02a3-billing-block.json` reports an account payment/spending-limit restriction;
it is an infrastructure block, not a compiler or test result. The preceding a812cec six-profile receipt remains retained.
