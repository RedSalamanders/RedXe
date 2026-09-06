# AV Control continuation checkpoint

Saved 2026-09-05 at approximately 21:15 UTC after the user requested saving everything to continue later.
Implementation is paused, not complete. This checkpoint supersedes older progress notes for current pins, results
and next actions. The normative contracts and WIP plans remain authoritative for scope.

## Resume first

1. Read `AGENTS.md`, `Specs/README.md`, `Specs/Plugins/Plugins_AVControl.md`, the
   [AV plan](RFC_Plugins_AVControl.md) and
   [DxUi integration contract](../../Core/Core_DxUiIntegration.md) plus historical
   [shared-library plan](../Done/RFC_Core_DxUiSharedProject.md).
2. Inspect actual Git status in all working directories. Preserve unrelated changes; do not reset, clean, stage
   everything or overwrite a checkout from this checkpoint.
3. Coordinate with the existing task **Update DxUi docs and workflow**, ID
   `01a07185-e32b-75a2-b206-b4f10cd51688`, host `local`. It owns the canonical DxUi checkout.
4. Finish isolated Menu fixture validation, investigate the distinct split-button CI failure, and resolve matched
   performance acceptance before adopting a new canonical RedXe dependency pin.
5. Refresh the consumer matrix, then complete real-device, usability and resource gates.

## Exact working state

| Directory | State at pause | Purpose |
| --- | --- | --- |
| `Z:/src/RedXe` | `main`, HEAD `91148b19e5f03e431dd49ce3af0d71d355507dc1`; many tracked/untracked changes | Canonical RedXe source/docs, including unrelated work. |
| `Z:/src/RxAv` | Detached at the same RedXe HEAD, dirty validation snapshot | Latest isolated AV build/test consumer. |
| `Z:/src/DxUi-worktrees/av-high-contrast` | `codex/av-text-bridge`, HEAD `26459b4b25c8573fccf049d4947a1d221f64b182`, pushed | Our isolated shared-library work. Menu fixture and EmbeddedUia measurement archive remain uncommitted. |
| `Z:/src/DxUI` | Managed by the coordinating task | Canonical standalone library, private `RedSalamanders/DxUi`, default `main`. Do not edit/build here without coordination. |
| `Z:/src/DxUi-worktrees/text-services-baseline` | Clean detached `ad888331931400063af71c4c567fcf502833d621` | Retained matching benchmark baseline; x64 Debug/Release built. |
| `Z:/src/DxUi-worktrees/av-input-baseline` | Detached `1947a5b` plus intentional independent-fixture changes | Older input comparison; do not clean. |

**Canonical `Dependencies/DxUi.lock.json` still selects `1947a5b91beb029e9b99d71e0893c6075bbb29ca`.**
Current canonical source calls newer text/UIA APIs and has not been built against that old pin. This is an
intermediate state awaiting dependency acceptance, not a buildable final delivery. Validated builds ran in
`Z:/src/RxAv`, whose lock selects `5b366f28f8c6377cd0cc27af556435e310d9be82`. Do not adopt that override in canonical
RedXe merely to make the build green while acceptance remains open.

One `DxUi.lib` owns public controls, embedded rendering, text services and accessibility. RedXe supplies the D3D11
device and owns its HWND/OS integration. RedSalamander migrates later. No upstream RedSalamander source tree is needed.

## Implemented behavior

- Profiles bind output, mic and webcam. The native responsive UI preserves three independent mute/off controls,
  both levels and direct Profile access down to 160x180, with 48-DIP targets. Confirmed toggle state changes only
  after acknowledgment. Profile definition is secondary to live actions. Larger layouts extend through 1280x720
  and supported portrait sizes.
- Bounded profile validation, transactions, rollback and safety-command precedence; module-shared event-driven
  coordination, endpoint notifications, isolated audio/camera brokers, virtual-camera source and package/setup
  scripts exist. Synthetic tests do not establish real audio-policy or camera interoperability.
- Application-owned text/TSF/clipboard transport, revision/focus identity and composition cancellation before
  retaining drafts are implemented. Escape cancels composition before closing Profiles. Real IME acceptance is open.
- Generic accessibility transport has a 56-byte physical placement record, `IRedXeAccessibilityWidget` and
  `IRedXeAccessibilitySite`. `RedXe/AccessibilityHost.*` owns a lazy WM_GETOBJECT root, 32 fixed slots, generation-bound
  sites, prepared publication and coalesced focus/actions. Hidden, modal, moving and retired views detach providers.
- AV adapts shared DxUi providers. Retained COM providers reject calls after detach/shutdown. First publication pins
  the plugin module image to preserve retained vtables; coordinator/device resources still tear down. Measure the
  image cost before acceptance. Latest root fix rejects focus when hidden/disabled and verifies OS focus.
- Tests use a real Windows MTA UIA client to discover the output slider and receive a confirmed range-value event.
  Toggle acknowledgment, queued navigation, stale generations, foreign-thread calls and late-provider lifetime are
  covered. Synthetic devices remain in use.
- The Profile chooser has a six-step **Camera setup guide**: Windows 11, package install, current-user registration,
  restart, selecting RedXe Camera in each calling app, physical-camera selection/off behavior. It performs no install
  or device changes. All six minimum captures and the full-size typography capture were visually reviewed.
- `Mockups/av-control.html` reflects the minimum live layout and Profile access. Its browser guide flow still needs
  the new native setup steps. Review URL: `http://127.0.0.1:8769/review.html`; do not assume the server survives pause.

Source entrypoints: `Plugins/AVControl/{AVControl.cpp,LiveView.*,ProfileControls.*,AccessibilitySite.h}`,
`Common/{AccessibilityValidation.h,DxUiTextTransport.h,PlugInterfaces/Widget.h}`,
`RedXe/{WidgetTextClient.*,AccessibilityHost.*,Application.*}`,
`Tests/AVControlTests/{AccessibilityHostTests.cpp,AccessibilityTestHelpers.h,ModuleTests.cpp,NativeViewTests.cpp}`.

Backend bounds retained: four profiles, bounded JSON/IDs, three-second transaction deadline with bounded undo and
rollback, five coalesced command lanes, bounded endpoint inventory, generation-checked notifications and one
event-blocked module worker. Camera brokers have deadlines/watchdogs, bounded mailbox/sample storage and explicit
busy/denied/failed retry. Off/stale/unplug uses a neutral frame. No idle physical capture is intended.

## Validation receipts

Unqualified `.build/` paths below are relative to canonical `Z:/src/RedXe`.

| Evidence | Result and limits |
| --- | --- |
| `.build/av-uia-full-debug.log`, `.build/av-uia-full-release.log` | Full RedXe root suites passed in RxAv at 5b366f2, including WARP/crash tests. Predates latest guide and disabled-root focus changes. |
| `.build/av-camera-guide-debug-build4.log`, `.build/av-camera-guide-debug-tests4.log` | Latest guide/root Debug AV suite: **7,370 checks passed**, zero build warnings/errors. Full root, Release and ARM64 refreshes remain. |
| `.build/av-uia-debug-tests5.log` | Earlier 7,203 checks, including actual OS UIA event delivery. |
| `.build/dxui-uia-header-debug.log`, `dxui-uia-header-release.log`, `dxui-uia-header-arm64-debug.log`, `dxui-uia-header-arm64-release.log`, `dxui-uia-header-format.log` | DxUi 26459b4: all 18 local suites in each x64 config, both ARM64 cross-builds and validators passed before the final uncommitted Menu changes. |
| `.build/dxui-5b366f2-external-consumer.log` | Relocated Release public consumer passed three rendered examples and five invalid pins at 5b366f2. DxUi fixture `.build/consumers/68b70a5f6170`. |
| `Z:/src/RxAv/.build/test-artifacts/AVControl/camera-setup-minimum-{1..6}.png`, `camera-setup-full.png` | Latest reviewed native guidance captures, retained in the recovery archive. |

Committed DxUi progression: `2ca8cc03aae08f943d00abdfeddfe6f87f7800cd` application text services;
`5b366f28f8c6377cd0cc27af556435e310d9be82` embedded UIA;
`26459b4b25c8573fccf049d4947a1d221f64b182` standalone public UIA header prerequisites. Preserve `unknwn.h` before
UIAutomationCore and the public-header-first compile test. Shared embedded tests include 1,709 checks and 1,000 clean
accessibility updates/composites without C++ allocations. This does not measure all OS-side allocation cost.

CI inspected at pause:

- 2ca8cc0: native 33988110749 and format 33988110763 passed.
- 5b366f2: native 33990523000 failed x64 Debug Menu owner-message-flood; x64 Release/both ARM64 passed. Format
  33990523073 passed. Failure retained in `.build/dxui-5b366f2-ci-failed.log`.
- 26459b4: native [33991448220](https://github.com/RedSalamanders/DxUi/actions/runs/33991448220) passed; format
  33991448221 passed. Another run of the **same commit**,
  [33991599173](https://github.com/RedSalamanders/DxUi/actions/runs/33991599173), failed x64 Release **split-button
  popup Refine-row hover** (hovered=-1, renders=4); validation, x64 Debug and both ARM64 passed. Format 33991599192
  passed. Failure retained in `.build/dxui-26459b4-ci-failed.log`. Preserve the failure and investigate separately;
  the owner-flood correction below is not evidence that this other fixture is fixed.

## Menu fixture and restored negative controls

Only `Tests/Controls/DxUiTests.Menu.cpp` is modified in the DxUi worktree: 97 additions/5 deletions at pause.
The coordinating task recommended the scoped current-thread WH_GETMESSAGE hook. It arms only for the fixture popup,
preserves the privately marked posted move while stripping its marker, filters unmarked popup motion to WM_NULL,
and always chains the hook. There is no cursor warp, forced paint or production-input change.

Pure tests cover disarmed, PM_NOREMOVE, unmarked, marked and other-message cases. The flood test records the render
baseline before queuing the owner backlog. Its unchanged 800-ms deadline starts immediately before posting the
marked move; success requires its dequeue, filtered interference, row-1 hover, highlight and a newer render.
The click/invocation deadline is unchanged. Debug Menu passed with six capability skips; Release passed with zero
skips before the final baseline-read relocation. Restored final Release Menu passed again.

`.build/validate-menu-isolation-negative2.py` has **finished**. Omitting the marked move and disabling production
modal priority each failed as intended (exit 1, starvation assertion). Both sources were restored; normal Release
rebuilt and full Menu passed (exit 0). Summary: `.build/menu-isolation-negative-summary2.log`.

Archive: `Z:/src/DxUi-worktrees/av-high-contrast/.build/menu-isolation-negative-20260905-02/` contains originals,
negative build/test logs, restored build/test logs and `results.json`. Restoration SHA256 was checked:

- `Tests/Controls/DxUiTests.Menu.cpp`: `AA0E1EBCCE72D0A8DCC9C2B1A51C26F8AF8616D59E0B8B81C1413ABCFC6B4607`
- `src/Controls/DxUi.Menu.cpp`: `BF12C886A39B381F51474FA3DB9873E4D912D505F2177E0ED81A902877F7F812`

Production Menu has no Git diff; no temporary negative edits remain. Do not rerun either one-shot negative helper.
Helper 01 failed before its variants due to CRLF matching, restored sources and passed Menu; retain its `-01` archive.
Final formatting, full matrix and exact-commit CI of the fixture remain to do.

## Performance: OPEN, not waived

Declared **EmbeddedUia-20260905-01** finished A1/B1/B2/A2 in Debug then Release: ad88833 baseline versus 26459b4,
matching v2 fixture, 83 controls/1,000 model rows, 20 warmup frames, five 40-frame rounds, owned-process affinity 0xFFFF.
All four nearest pairs flagged different timing/memory metrics. Same-source controls also vary: unchanged Debug
baseline flags eight bands; Debug candidate clean p95 varies; Release baseline is within bands; Release candidate
clean FPS/p95/compose CPU varies. Results are inconclusive, not passing and not an established causal regression.

Uncommitted DxUi archive `Measurements/TextInput/EmbeddedUia-2026-09-05/` has 24 JSON reports, run.log and a detailed
README/table. Older committed `Measurements/TextInput/HostServices-2026-09-05/` and `NativeStore-2026-09-05/` preserve
their failed comparisons. Original input evidence is in `Measurements/TextInput/2026-09-05/`. Never compare differing
fixture hashes or replace failed evidence. Do not rerun fixed-path `.build/measure-embedded-uia-abba.ps1` or its archive
helper over these results.

Proposed next experiment, **not implemented or run**: separate acceptance profile with 1,000 warmup frames and five
1,000-frame rounds, applied identically to the retained baseline and candidate. Keep 5% timing, 2% memory and zero
allocation/surface regression thresholds. Update driver, harness, comparator schema/tests and performance contract
together. Compare only matching long-profile runs; retain all short receipts. DxUi files:
`Tests/Embedded/{BenchmarkMain.h,ComplexUiBenchmark.h}`, `performance.ps1`, `Tools/compare_performance.py`,
`Tools/tests/test_performance.py`, `Specs/Core/Core_PerformanceAndResources.md`.

Current benchmarks keep UIA inactive. Active UIA, AV visible/hidden idle, presented-frame and camera resource
acceptance are separate. A five-minute visible plus five-minute hidden AV soak was discussed only; **no soak option
was implemented**. Measure CPU/private memory/handles/GDI objects, frame/work counts and event-blocked waiting.

## Synchronization and recovery

RxAv began as a 169-file snapshot including unrelated work. Guarded AV synchronization saved originals/incoming
bytes under `.build/av-uia-sync-20260905-01/`. Its manifest now covers 27 files; later updates have unique subfolders
with `before/`, `incoming/` and `changes.json`.

Read `.build/update-verified-av-uia-files.ps1` before using it for enrolled files. It checks the last synchronized
target hash, refuses independent edits, backs up both states and verifies copies. The original
`.build/apply-verified-av-uia-sync.ps1` was already executed; **do not rerun**. The older
`.build/av-text-validation-checkout.json` describes the initial snapshot, not the current lock override.

An initial unguarded copy was rejected by automatic approval review. The backup/hash-guarded retry succeeded; no
approval block remains. Sibling writes/builds still require normal tool escalation.

Additional recovery checkpoint: `.build/handoffs/av-control-20260905-2115/`. Its manifest records HEADs, branches,
statuses, source paths and file hashes. The verified ZIP retains modified/untracked source from RedXe, RxAv and our
DxUi branch, staged/unstaged patches, scripts/logs, synchronization backups, negative-test evidence and AV captures.
This is **local saving, not a commit/push**. It also preserves unrelated edits: inspect per-file diffs before restoring.
Build binaries/dependency caches are reproducible and excluded. Old `.build` editing helpers are historical one-shot
scripts, not a resume workflow; do not blindly replay them.

Independent sample patch: `Z:/src/DxUI/.build/handoffs/independent-samples-20260905/independent-samples.patch`,
base 56574a4, SHA256 `6099a66e236e59f876b9e3107390034313ea8ba666ea7da222aaefba70f6b957`.

## Remaining acceptance

1. Finalize isolated Menu fixtures and exact-source CI/relocated-consumer validation with the coordinating task.
2. Resolve matched library resource acceptance without relaxed thresholds or a new baseline. Measure active UIA,
   AV idle/hidden/presented behavior and pinned-module cost.
3. Refresh RxAv at the accepted final library revision with latest guide/root changes. Run full x64 Debug/Release,
   ARM64 builds and validators. Then adopt the accepted canonical pin and validate canonical RedXe.
4. Finish real IME, keyboard, touch, screen-reader, DPI/high-contrast, focus and device-loss acceptance.
5. Complete G1 audio-policy support/rollback with real endpoints and G2 camera install/register/uninstall/rollback,
   supported Windows platforms and calling-app interoperability. No real audio defaults, camera state or installer
   registration was changed by the synthetic validation. The guide is not installer automation.
6. Reconcile the mockup setup flow, WIP checklists and normative contracts. Move plans to Done only when every
   required implementation and validation gate is complete. AV Control is not done yet.
