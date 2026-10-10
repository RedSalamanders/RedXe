# AV touch-row validation, 2026-09-05

Historical RedXe adoption evidence, relocated from DxUi on 2026-09-05. All eleven JSON files are preserved unchanged.
The underlying `complex-ui-v1` run measured DxUi offscreen rendering during the AV integration work; it did not run
the complete RedXe AV host. The status statements below describe the original capture, including then-pending CI.
DxUi's subsequent standalone workload uses its own sample, fixture identity and measurements in the DxUi project.

The per-instance ComboBox row minimum adds one float and changes existing geometry calculations. Unchanged controls
keep their theme height. Tests cover both density presets, boundaries, scrolling, keyboard visibility, live height
changes, invalid values and upward-opening popups. The generated gallery shows a 48-DIP Modern popup in five themes.

## Paired performance receipts

The original [baseline](AVTouch-before-Release.json) was retained before implementation. Every candidate below uses
the same complex-ui-v1 fixture, machine, configuration and WARP renderer. Receipts contain executable, benchmark and
source fingerprints. Measurements are completed offscreen frames with readback, not presented hardware FPS.

- [Immediate post-build sample](AVTouch-after-build-Release.json): severe transient slowdown (49 clean / 90 dirty FPS)
  while the machine was under compilation load. Retained and rejected as acceptance evidence.
- [Quiet sample](AVTouch-after-quiet-Release.json): about 2,584 clean / 507 dirty FPS versus baseline 2,456 / 507.
  Surface and allocation counts were unchanged. Private-memory comparisons flagged +2.43% clean / +2.11% dirty.
- Three serial repeats against the original baseline remain available: [first](AVTouch-repeat-1-Release.json),
  [second](AVTouch-repeat-2-Release.json), [third](AVTouch-repeat-3-Release.json), each with its comparison JSON.
  The first two flagged small timing variations; neither reproduced the memory increase. The third was within every
  comparison noise band (about 512 dirty FPS). No sample or original baseline was replaced to make a check pass.

These samples do not establish a repeatable regression from this change. Cached surface residency remains 3,686,400
bytes, composition allocations zero and dirty-round C++ allocations 2,200. Keep the noisy observations when comparing
future changes; hardware presentation, touch latency and long-duration acceptance still require separate evidence.

## Functional validation

All 18 suites passed in x64 Debug and Release, and both ARM64 configurations cross-built. Each local x64 run recorded
nine native Menu capability skips on the noninteractive desktop; these are not native menu passes. All nine skill
checks, specification/dependency/test-port validators, 32 tool tests and formatting checks passed.

Gallery capture now waits for the visible popup instead of its hidden measurement HWND. The Menu flood test also
waits for modal capture and adopts the popup DPI context before delivering coordinates. Its two 800 ms latency
assertions are unchanged, and a failure now records actual hover, capture, DPI and render state. This is a fixture
readiness correction pending native CI, not a claim that the previous CI failure has been resolved.
