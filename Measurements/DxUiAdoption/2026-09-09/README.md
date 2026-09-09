# DxUi adoption evidence — 2026-09-09

Baseline product: `75545887d002b5075d170d98b7bfd3a77d8d7f61`, DxUi `d192e474e540adc2656e69b8e8150b0f7a05d63f`.
Candidate product code: `4f1d4eb`, DxUi `8c548fe2de3cdb5af858c457e9bef47d81506878`.
The source paths in raw receipts identify the actual original/short checkout used for that run.

| Configuration | x64 | ARM64 |
|---|---|---|
| Debug | Full product tests pass | Cross-build pass, native execution pending |
| Release | Full product tests pass | Cross-build pass, native execution pending |
| ASan Debug | Full product tests and deliberate fault detection pass | Instrumented cross-build pass, native execution pending |

Every candidate profile has an actual linker/module provenance receipt. The previous Release
package was retained before replacement, verified against `baseline-package.json`, extracted after
candidate qualification and exercised through the AV suite, host/plugin suite and hidden WARP
application smoke. See `rollback-x64-Release.json` and its three logs.

These runs validate the recorded behavior and static archive identity. Concurrent build activity
means their timings are not matched performance acceptance. Native ARM64 CI needs private DxUi
read access. Real IME, screen-reader, touch and AV hardware qualification remain with the existing
AV release gates; this adoption does not certify them.
