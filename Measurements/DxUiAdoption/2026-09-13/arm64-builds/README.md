# Final local ARM64 cross-builds

Frozen RedXe `f727932` (including the current AV measurement fixture) builds against DxUi
`ecad70704fd5e46672586ddbe4ddfeb5db01d1a4` on the local x64 Windows host on 2026-09-13.
The official root build ran serially with `-Platform ARM64 -MaxCpuCount 2` and a process-only
`PreferredToolArchitecture=x64` setting. No hosted CI ran.

| Configuration | Build duration | Warnings | Errors | Verified archive consumers |
| --- | --- | --- | --- | --- |
| Debug | 1m44.580s | 0 | 0 | 3 |
| Release | 1m21.331s | 0 | 0 | 3 |
| ASan Debug | 1m08.371s | 0 | 0 | 3 |

The official build verifies the actual linker commands and writes each retained provenance
receipt. A subsequent independent SHA-256 check matched every recorded `RedXe.exe`,
`Plugins/AVControl.dll` and `AVControlTests.exe` to the bytes under the selected output profile.
All receipts identify the expected pin, configuration, ARM64 target and x64 compiler host.
`retained-files.json` records the original local paths, lengths and hashes of the six raw files.

The unpublished pin was resolved through a process-only Git URL rewrite to the canonical local
DxUi repository. There was no persistent Git setting or sibling-source edit. These are compilation
and provenance results, not native ARM64 runtime or sanitizer-detection results. Previous native
CI receipts remain historical evidence for their own source revisions only.
