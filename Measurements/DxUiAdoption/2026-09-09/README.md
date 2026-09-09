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
