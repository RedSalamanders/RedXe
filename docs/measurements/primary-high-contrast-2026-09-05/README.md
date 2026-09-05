# Primary-button high-contrast validation

This is library evidence for the isolated primary-button color-pair fix, based on commit
`56574a483c9c16b408a40b106823254dd22926b4`. It does not close RedXe AV hardware, presented-frame,
IME/UIA adoption or native ARM64 consumer acceptance. The canonical independent-sample checkout was not edited.

The fix preserves exact system selection/selection-text colors, opaque fill/text, border and focus in high contrast.
Tests cover both light/dark palettes, all 32 enabled/hover/pressed/focus/modality combinations and partial animation
strength. Normal-theme expectations remain unchanged. All six regenerated gallery images were visually reviewed.

All 18 native x64 suites passed in each configuration. Menu recorded nine capability skips in each configuration;
these are skips, not passes for those capabilities. ARM64 Debug/Release cross-builds and the four repository
validators passed. `validation.json` records suite executable hashes and exact skip descriptions.

## Matched performance evidence

The retained original Release baseline predates the source change. Debug baselines and the later A/B controls
rebuilt the unchanged library implementation; their source fingerprints match the original unchanged source.
The fixture remains complex-ui-v1: 83 controls, 1,000 model rows, five rounds of 40 completed offscreen WARP frames
per clean/dirty scenario at 1280×720 / 96 DPI. These are completed offscreen FPS, not displayed FPS.

Initial unrestricted measurements were inconsistent. The large early slowdown overlapped unrelated compiler
activity. Subsequent unchanged-source A/B runs also triggered timing flags against the original baselines,
including clean composition that does not execute the changed color-resolution path. No repeatable fix-related
slowdown was identified. The original receipts and all separately named repeats/comparisons are retained here;
early test.ps1 default-path receipts were superseded, so those early failures have retained console-log evidence only.

The Debug candidate passed against its retained unchanged-source baseline. For an additional controlled Release
comparison, both unchanged and fixed implementations inherited affinity 0xFFFF from the owned measurement process.
No other process or system power policy changed; the measurement process restored its prior affinity afterward.
This additional matched fixture passed the existing 5% timing/FPS and 2% process-memory investigation bands.
No threshold, workload, original baseline or library resource limit was changed to obtain these results.

| Matched comparison | Completed FPS, before → after | Frame p95, before → after |
| --- | --- | --- |
| Debug, unrestricted, clean | 2290.1 → 2404.0 | 0.5106 → 0.4755 ms |
| Debug, unrestricted, dirty | 392.9 → 402.3 | 2.9529 → 2.7769 ms |
| Release, affinity 0xFFFF, clean | 1832.7 → 1804.6 | 0.6582 → 0.6327 ms |
| Release, affinity 0xFFFF, dirty | 529.6 → 544.6 | 2.1353 → 2.0165 ms |

See `Contrast-ab-candidate-Debug.json.comparison.json` and
`Contrast-affinity-candidate-Release.json.comparison.json` for every metric and delta, including memory,
allocations and surfaces. Composition allocations and hidden work remain zero; surface/replacement payload
remains 3,686,400 bytes. Small bidirectional timing changes remain measurement noise, not a permitted regression
budget. This short fixture does not establish hardware latency or retention/soak acceptance.

Every raw receipt retains source/content, executable and benchmark fingerprints, compiler, OS, WARP, CPU,
power-policy and fixture metadata. Affinity receipts additionally identify the constrained CPU mask. Do not compare
an affinity receipt against an unrestricted one or substitute the newer independent-sample v2 workload.
