# Review fixes for PRs #21 to #32

Status: `ACTIVE`, with open product decisions (see [Decisions](#decisions)).
Date: 2026-10-06
Owner: the domain specs listed under [Contracts expected to change](#contracts-expected-to-change).

This plan is non-normative. Each batch MUST land its durable behavior in the owning domain spec in the same change,
and the plan moves to `Specs/Plans/Done/` once every batch is closed.

## Origin

A production-readiness review of the eleven PRs merged into `main` from 2026-09-26 to 2026-10-06 (#21 to #32, range
`3973fd8..25433ae`, 198 files). The review read each change with the code around it, looking for bugs, side effects
on code the change did not touch, gaps in resilience and user experience, architecture misfits, and possible
simplifications.

Method:

- Eleven review units: live window-kind switch, autohide slide and tray icon, #21 host hardening, settings,
  Logicon and Zoom, Studio Clock, the streaming child-process helper, DxUi restore, scoped testing, contract alignment,
  and test quality.
- Each subsystem was reviewed from four angles (correctness, side effects, production resilience and UX, architecture
  and simplification), followed by a fresh-eyes pass and a completeness check. The completeness check opened a second
  round on five gaps: the 5H4D3R5 lookup-table bake, the DxUi API revision 3 bump, session end and crash lifecycle,
  modal-loop re-entrancy, and UI-thread `ShellExecuteExW`.
- Every finding went to one to three independent verifiers, scaled by severity, who tried to disprove it. The lead
  reviewer re-checked every high finding and the medium findings that shape this plan against the code, the
  `v1.0.102` tag, and the GitHub release, CI and ruleset state.

Result: 227 open findings (13 high, 66 medium, 121 low, 27 nit; 2 still disputed) and 9 refuted. None is critical.
Several predate the range but are listed because #23 and #27 make them much easier to hit; the register marks the
PR where known. Line numbers are at `25433ae` and will drift: re-read each anchor before fixing it.

Process finding: PRs #23, #24, #25, #27 and #28 were merged while their only PR check (`native (x64, Release)`)
was failing, and the `main` push run stayed red from `5d62503` to `a4671df` until #30 repaired the DxUi long-path
restore. HEAD `25433ae` is green on every job. See decision D6.

## Release blockers

Fix both before the next release dispatch.

### B1. Settings written by v1.0.102 must still load

The only public release, `v1.0.102`, installed a Release template whose `services.Zoom` holds `clientId`,
`redirectPort` and `autoConnect`. #21 made `Zoom::ParseSettings` accept only `{}`, and `ParseServiceEntry` copies
every authored member except `plugin` into that check. The first start after an upgrade therefore fails validation,
and cold recovery backs up and replaces the user's whole file. Without a XENEON the replacement also installs the
first-run autohide bar. A Logicon `keys.down` or `mouse.down` binding, which `v1.0.102` accepted, has the same
effect through #21's new refusal.

Fix:

- Accept the seven retired Zoom members (`clientId`, `redirectPort`, `domain`, `displayName`, `autoConnect`, `mode`,
  `labels`) with any value and ignore them, in `Zoom::ParseSettings`, with one warning log entry. Keep them in the
  schema as deprecated, ignored properties. Correct `Plugins_Zoom.md`, which currently says they are rejected.
- Turn `keys.down` and `mouse.down` on a Logicon key, dial button or turn into an invalid binding (red `!`, never
  dispatched) instead of a document error, with a precise diagnostic. Align the schema, `docs/actions.md` and
  `docs/plugins/logicon.md`, and decide whether Launcher taps get the same rule.
- Add SettingsTests that load the exact `v1.0.102` Release and Debug templates and a document with a held binding.

### B2. Release workflow runs the Python tooling suite

`.github/workflows/release.yml` calls `test.ps1 -BuildNumber <n>`. A bound `-BuildNumber` skips the hand-off to
`Test-Changes.ps1`, so the default suites run, including `BuildProcess`, which since #31 runs `validate-skills.ps1`
and the Python unit tests. The release build job never installs `Build/requirements-validation.txt`, so the next
release fails before packaging.

Fix: call `./test.ps1 -Full -SkipTooling -Configuration Release -Platform <P> -BuildNumber <n>`. The version job
already requires a successful `ci.yml` push run for the same commit, which includes the `tooling` job. Extend the
reviewed-CI assertion in `ScopedTesting.Tests.ps1` to every workflow that calls `test.ps1`. Update
`Build_Packaging.md`, which documents the release command.

## Batches

One PR per batch, in this order after B1 and B2. Each batch updates its owning specs and adds the tests named in its
register rows.

### P1. Settings integrity

- `SettingsStore::PersistPatchedDocument` writes without checking the file on disk. A page swipe away from a page
  that collects settings on shutdown, an exit, a dock drag or a Launcher import replaces a just-rejected edit, a
  deleted file, or a `--settings` file that fell back to the template. Write only when the current stamp equals
  `_lastAppliedStamp`; otherwise keep the patch in memory and log `settings-persist-deferred` once.
- A widget persist rewrites and reformats the whole file through yyjson even when nothing changed, stripping every
  comment, including the #23 first-run guidance. Skip the write when the merged settings equal the previous ones.
- A comment-only reload made during a page swipe is undone when the swipe commits. Copy only the active page fields
  in `PromoteTransitionPage`.
- A UTF-8 BOM makes the file invalid and gets it reset at the next start. Read with `YYJSON_READ_ALLOW_BOM`
  everywhere the user document is read.
- Smaller: the stale typed minor after `PatchDockThickness`, CR-terminated comments in the text patcher, no flush
  before the atomic rename, and the first-run file keeping the template's "Uncomment and edit" dock example next to a
  live `dock` member.

### P2. RedXe exits, freezes, or re-enters at runtime

- A full reload while the titled window is minimized closes RedXe (zero-size canvas; predates the range). Defer the
  reload until the window is restored, or size from the last non-zero canvas.
- A failed kind switch on the dock-only reload path exits RedXe and still marks the file applied; a rolled-back
  switch logs `window-kind-changed`. Roll back like the full path and log `window-kind-switch-failed`.
- The `redxe.screenshot` action quits RedXe after the capture, drops a second press while reporting success, and
  logs nothing on failure. Add a per-request close flag set only by `--screenshot`. `--screenshot` must not exit 0
  when the window closes before the PNG is written.
- A Launcher tile bound to `redxe.settings.reload` can free its own widget during `OnPointer` (high). Defer the
  reload to a posted message.
- `system.launch`, `zoom.open` / `zoom.join` and tray "Edit settings" call `ShellExecuteExW` on the UI thread; a
  target on an offline share froze the dashboard for about 42 s (high). Move launches to a worker with a bounded
  queue. Queued key injections need an age limit so they do not replay into another window after a stall.
- A kind switch dispatched inside the titled window's move/size loop is undone when the loop ends; the startup
  failure message box is dismissed at once by a pending `WM_QUIT`.
- Held input: key-ups injected twice after 2 s, and a failed release that stops tracking a held modifier.

### P3. Dock, tray, Explorer, and session lifecycle

- Explorer restart: the dock never re-registers its app bar (only the tray handles `TaskbarCreated`), a refused
  tray add is never retried, and a `WM_CLOSE` to the tray's hidden owner leaves a ghost icon.
- Session end: no `WM_QUERYENDSESSION` / `WM_ENDSESSION` handling, so sign-out skips app-bar removal, tray removal,
  settings collection and Logicon device restore. After a crash, CRT exit still runs plugin shutdown in the crashed
  process.
- `PlaceDock`'s re-entrancy guard drops a nested request instead of placing again.
- The settings-error and action-notice windows are centered on a 4 px collapsed bar and open off-screen. Use one
  helper that places them inside the monitor's work area.
- Host actions, pointer, accessibility and IME geometry on a collapsed or sliding bar use the peek strip instead of
  the full canvas; top and left slides move the window one composition before the matching frame.
- First-run placement questions: see D2.

### P4. Logicon, Zoom, Studio Clock, and 5H4D3R5

- The Logicon device lane hot-spins after a dialpad disconnect: `Pump()` returns without draining while the lane
  waits with `QS_ALLINPUT | MWMO_INPUTAVAILABLE` (raised to high; reproduced on Windows 11). Drain unconditionally or
  wait with an empty wake mask when the listener is stopped.
- Dial turns now depend on a successful HID++ connect although Raw Input never needed it; `SetFeature` is an
  unbounded blocking IOCTL on the lane; restore keeps sending after a timeout; retired I/O blocks are unbounded.
- `zoom.join` declares a free-text target, so an invalid link passes validation and silently does nothing. Publish
  it with the meeting target kind narrowed to the browser rules, and add the host-spoofing test vectors.
- Studio Clock date halos are clipped at the tile edge; the Sky Atmosphere lookup-table bake (#21) is never retried
  after a failed first size notification and draws a black sky.

### P5. Build and test tooling

- Streaming helper: the budget is never enforced while the child keeps writing; disposal order can skip the
  kill-on-close job; Ctrl+C leaves MSBuild running; a session keeps a stale compiled launcher type; output buffered
  at a budget kill is lost.
- DxUi restore: a failed or interrupted restore folder counts as restored and code runs from it. Clone into a
  sibling temporary folder, verify, then rename. Also the `vswhere -latest` choice in `restore-dxui.ps1`, narrow-locale
  failure reports, empty self-test log, unbounded `Start-Process -Wait` runs in `test.ps1`, uncollected old roots, and
  Update-DxUi accepting another API revision.
- Scoped testing: the build number sits outside the evidence identity (every commit invalidates receipts and
  `-SkipBuild` then fails); Git output is decoded with the console code page; a passing run fails when any file
  changes during it, which is common with several sessions on one worktree; the test-file detector matches "test",
  "mock" and "fake" case-insensitively; scope rules are hand-maintained without a check against project inputs.
- `PluginContractTests` exits with `result & 0xFF`, so a failing HRESULT whose low byte is zero passes (disputed).

### P6. Behavior-preserving simplifications

- Replace `Application::ApplySettings`'s hand-written `sameRuntime` list with `RuntimeSettingsEqual()` in
  `Settings.*`, beside `ActiveDashboardRuntimeEquals`, and test it member by member.
- Merge `RebuildPresentation` into `InitializeDashboardRuntime`.
- One monitor-enumeration helper for `MakeFirstRunDock`, `ResolveDockMonitor` and `PlaceStandardWindow`; run
  `MakeFirstRunDock` only when a template is actually installed.
- In `Settings.cpp`, share the install-target probe and atomic commit, and drop the duplicate JSON text scanner and
  second parse.
- One shared Studio Clock settings header, following `ShadersSettings.h`.
- Derive slide and kind-switch state instead of storing it.
- Delete leftovers: the unreferenced `zoomSettings` schema definition, Zoom SDK `.gitignore` entries,
  `RedXeActions::BringToForeground`, the dead HID cancel path, the second argument quoter and dead
  `ContainedProcess` members, and text copied from other repositories into the scoped-testing tooling and
  `Build_Process.md`.

### P7. Spec, user-guide, and test-coverage drift

- Bring README, `--help`, the mode table in `UI_XeneonDisplayWindowing.md`, `docs/actions.md`, `Plugins_API.md`,
  the `AGENTS.md` layout, and the scoped-testing WIP plan in line with the code.
- Add the missing tests: kind switch with rollback, `TrayIcon`, held-key release identity, action monitor
  resolution including `secondary`, and Logicon raw-input ownership.
- Run the live checks `UI_XeneonDisplayWindowing.md` requires and #27 left unchecked: a real Explorer restart with
  the tray icon and an autohide bar up, and a real first-run install on two displays without a XENEON. Correct the
  spec sentence that dates the slide checks to the 2026-09-19 closeout.

## Decisions

Recommended defaults are in bold. Batches that depend on a decision wait for it.

- D1, upgrade policy (B1): **accept and ignore retired members with a warning**, or migrate the file on first load.
  Is a rollback from a newer build to `v1.0.102` supported? Today it resets the file in that direction too.
- D2, first-run bar (P3):
  - The top edge of the second screen covers maximized windows' caption buttons: **prefer the free bottom edge**, or
    keep the top edge and inset the strip's corners?
  - `secondary` can later resolve to a XENEON connected afterwards: **skip a XENEON in `secondary`**, or write the
    chosen display's name at install time?
  - Should recovery of an invalid file install the bar, or **only a missing file**?
- D3, failed kind switch (P2): **roll back and keep running**, or exit as today.
- D4, `redxe.screenshot` (P2): **keep RedXe running after an action capture**; only `--screenshot` exits.
- D5, settings comments (P1): **no write when nothing changed plus the stamp check now**; patch source text so
  comments survive a real widget change later.
- D6, CI gate: **require `native (x64, Release)` and `tooling` in ruleset 23723903** (a repository setting the owner
  changes), so a red PR cannot merge and PrePush delegation rests on a required check.
- D7, Zoom (P4): Zoom no longer has state. **Move it to the dedicated action-DLL path**, or keep the service wiring.
- D8, `Application.cpp` (6,338 lines): extracting a `DockController` that owns app bar, placement, reveal and slide
  was judged mostly code movement. **Do only the P6 de-duplication now** and revisit after P3.

## Contracts expected to change

`Specs/Core/Core_Settings.md`, `Specs/Settings.schema.json`, `Specs/Plugins/Plugins_Zoom.md`,
`Specs/Plugins/Plugins_Logicon.md`, `Specs/Plugins/Plugins_Actions.md`, `Specs/Plugins/Plugins_API.md`,
`Specs/UI/UI_XeneonDisplayWindowing.md`, `Specs/Core/Core_PerformanceAndResources.md`,
`Specs/Core/Core_CrashHandling.md`, `Specs/Core/Core_DxUiIntegration.md`, `Specs/Build/Build_Process.md`, and
`Specs/Build/Build_Packaging.md`; user guide pages `docs/usage.md`, `docs/actions.md`, `docs/plugins/logicon.md` and
`docs/plugins/zoom.md` where an end-user scenario changes.

## Validation

- Every batch: the affected build, `Test-Changes.ps1` for the changed scopes, and the validation its owning specs
  name. P1 to P4 also run `test.ps1 -Full` on x64 Debug and Release before their PR.
- B1: a SettingsTests case per `v1.0.102` template and a held Logicon binding.
- B2: the release workflow command in the reviewed-CI assertion, plus the same command run locally on x64 Release.
  Never dispatch `release.yml` as a test: every dispatch creates a GitHub release and, with the default inputs,
  submits it to winget. The next intentional release is the end-to-end check, unless a non-publishing dry-run input
  is added first.
- P3: the live Explorer-restart and first-run checks above, recorded with date and topology.
- Spec-only changes: `validate-skills.ps1`.

## Refuted during review

No action: `host-hardening#4` (publisher retry), `tests#6` (empty affected plan; intended, the doc drift is tracked
as `scoped-testing#4`), `dock-switch#18` (app-bar owner extraction; see D8), `slide-tray#8`, `slide-tray#14`,
`studioclock#3`, `studioclock#8`, `scoped-testing#17` (superseded by `tests#4`), `tests#9`.

## Closeout

When every register row is checked, its durable behavior is in the owning spec, required validation has passed, and
any user-visible change is in `docs/`, move this plan to `Specs/Plans/Done/` and remove its row from the WIP index.

## Findings register

Each row is one finding: review ID, severity, anchor at `25433ae`, and the problem. The ID prefix names the review
unit; `shaders-lut`, `dxui-rev3`, `session-end`, `modal-reentrancy` and `launch-ui-thread` come from the second round.
Check a row when its fix lands, or note why it was dropped.

### B1. Settings written by v1.0.102 must still load (6)

- [ ] `alignment#0` (high) `Plugins/Actions/Zoom/ZoomSettings.cpp:43`: Removing the legacy Zoom settings breaks every v1.0.102 Release settings file, and cold recovery then backs up and replaces the user's whole file
- [ ] `logicon-zoom#0` (high) `Plugins/Actions/Zoom/ZoomSettings.cpp:43`: Upgrading from v1.0.102 resets the user's whole settings file because the shipped template's Zoom clientId/redirectPort/autoConnect members are now a document error
- [ ] `settings#0` (high) `Plugins/Actions/Zoom/ZoomSettings.cpp:43`: Upgrading from the public v1.0.102 resets every user's default settings file: Zoom::ParseSettings now rejects the Zoom members that release's templates shipped
- [ ] `alignment#3` (medium) `Plugins/Logicon/LogiconSettings.cpp:125`: Logicon's keys.down / mouse.down refusal is enforced only in the Logicon parser: it invalidates the whole file with a wrong diagnostic, schema and user docs disagree, and Launcher still accepts holds
- [ ] `logicon-zoom#3` (medium) `Plugins/Logicon/LogiconSettings.cpp:125`: Logicon rejects keys.down/mouse.down as a whole-document error with a misleading 'is not an action name' diagnostic, and the docs, schema and Launcher disagree
- [ ] `studioclock#9` (low) `Specs/Core/Core_Settings.md:89`: A document using glowPercent is treated as invalid by the published v1.0.102 build, which then backs up and replaces the user's shared settings file

### B2. Release workflow runs the Python tooling suite (3)

- [ ] `alignment#1` (high) `.github/workflows/release.yml:124`: The release workflow's test.ps1 call now runs the Python/PyYAML tooling suite without installing Python
- [ ] `scoped-testing#0` (high) `.github/workflows/release.yml:124`: Release workflow now runs the Python/PyYAML tooling suite in every build job without installing Python dependencies
- [ ] `tests#16` (high) `.github/workflows/release.yml:124`: Release workflow now runs the Python/PyYAML tooling suite on runners that never install it

### P1. Settings integrity (12)

- [ ] `settings#1` (high) `RedXe/Settings.cpp:2833`: PersistPatchedDocument writes the in-memory document over a rejected, unprocessed, deleted or failed --settings file with no stamp check (page swipe, exit, dock drag, Launcher import)
- [ ] `settings#2` (medium) `RedXe/Settings.cpp:1850`: Widget persist rewrites the whole file through yyjson even when nothing changed: it strips every comment (including the first-run dock guidance) on the first page swipe and does UI-thread write-through I/O
- [ ] `settings#3` (medium) `RedXe/Application.cpp:2565`: A comment-only reload during a page swipe is reverted when the page commits, and the next persist writes the stale text back to disk
- [ ] `settings#4` (medium) `RedXe/SettingsV4.cpp:1878`: A UTF-8 BOM makes the whole settings file invalid; on the next start cold recovery resets the user's file
- [ ] `alignment#6` (low) `Settings/RedXe.settings.json:10`: First-run dock file keeps the template's 'Uncomment and edit' dock example, so following it makes a duplicate `dock` that rejects the save
- [ ] `dock-switch#22` (low) `RedXe/Settings.cpp:2310`: First-run settings file holds a live `dock` and the template's commented example whose instruction ('Uncomment and edit') produces a duplicate-member rejection
- [ ] `host-hardening#19` (low) `RedXe/Application.cpp:2565`: A comment-only reload during a page swipe is overwritten by the staged page copy when the swipe commits; the next dock-edge drag then writes the old text back to disk
- [ ] `settings#10` (low) `RedXe/Settings.cpp:2327`: First-run file holds a live dock next to the template's 'Uncomment and edit' dock example; following that instruction makes a duplicate member and invalidates the file
- [ ] `settings#15` (low) `RedXe/Settings.cpp:2194`: PatchDockThickness appends a new dock object as a compact fragment on the root's closing-brace line
- [ ] `settings#7` (low) `RedXe/Settings.cpp:2207`: PatchDockThickness raises version.minor in the source but leaves the typed settings.versionMinor stale, so the next comment-only edit takes the full reload path
- [ ] `settings#8` (low) `RedXe/Settings.cpp:1887`: Text patcher ends // comments only at LF while yyjson also ends them at CR; PatchDockThickness persists its output without re-validating
- [ ] `settings#9` (low) `RedXe/Settings.cpp:1212`: Atomic settings writes do not flush the temporary file before the rename; a short write can report S_OK

### P2. RedXe exits, freezes, or re-enters at runtime (23)

- [x] `dock-switch#0` (high) `RedXe/Application.cpp:2058`: A live reload that rebuilds the page while the titled window is minimized closes RedXe
- [x] `host-hardening#0` (high) `RedXe/Application.cpp:936`: The redxe.screenshot action quits RedXe after the capture, a second press during a capture is dropped but reports S_OK, and a failed capture is not logged
- [x] `launch-ui-thread#0` (high) `RedXe/HostActions.cpp:314`: system.launch runs GetFileAttributesW and ShellExecuteExW on the UI thread; a target on an offline share freezes the dashboard for about 42 s
- [x] `modal-reentrancy#2` (high) `RedXe/Application.cpp:3597`: A Launcher tile bound to redxe.settings.reload can free the Launcher widget while its own OnPointer is still running
- [x] `dock-switch#1` (medium) `RedXe/Application.cpp:1784`: A kind switch that fails on the fast (dock-only) reload path exits RedXe and marks the file applied, while the same failure in a full reload rolls back
- [x] `dock-switch#3` (medium) `RedXe/Application.cpp:2625`: A rolled-back kind switch logs window-kind-changed as if it succeeded, never logs window-kind-switch-failed, and restores the titled window on another monitor
- [x] `host-hardening#1` (medium) `RedXe/Main.cpp:325`: --screenshot exits 0 with no PNG when the run ends before the async capture completes, and the worker's real HRESULT is read too late
- [ ] `host-hardening#2` (medium) `RedXeLauncher/Main.cpp:152`: The launcher does not wait when an argument is unknown, so exit code 2 is lost and the error prints after the prompt; test.ps1 masks it
- [x] `launch-ui-thread#1` (medium) `RedXe/PluginHost.cpp:2047`: Queued actions have no age bound, so key injections requested during a UI stall replay into whatever window is foreground afterwards
- [x] `modal-reentrancy#0` (medium) `RedXe/Application.cpp:5862`: A window-kind switch dispatched inside the titled window's move/size loop is undone when the loop ends: the dock is left at the titled window's rectangle
- [x] `modal-reentrancy#1` (medium) `RedXe/Main.cpp:363`: The startup and runtime failure message box is dismissed at once by the WM_QUIT that destroying the main window leaves in the queue
- [x] `settings#6` (medium) `RedXe/Application.cpp:1784`: An edge-only live dock reload that fails the window-kind switch quits RedXe and is reported as a successful reload
- [x] `dock-switch#11` (low, disputed) `RedXe/Application.cpp:5862`: A kind switch can run inside the titled window's own modal move/size loop
- [ ] `host-hardening#10` (low) `RedXeLauncher/Main.cpp:259`: The launcher reports an NTSTATUS crash exit code (above INT_MAX) as 1, the documented settings-failure code
- [ ] `host-hardening#11` (low) `RedXe/PluginHost.cpp:523`: Exiting with a stuck device lane skips the log drain, so the drain-timeout line can be lost
- [ ] `host-hardening#12` (low) `RedXe/PluginHost.cpp:2372`: A service re-added while its old device lane is still stuck fails with ERROR_BUSY, the error is discarded, and nothing retries when the lane returns
- [ ] `host-hardening#13` (low) `RedXe/HostActions.cpp:189`: A held key or button stops being tracked even when its release injection fails, so a modifier can stay down with no retry
- [x] `host-hardening#6` (low) `RedXe/Application.cpp:3052`: If the screenshot worker thread cannot start, an idle --screenshot run can hang instead of exiting 8; the capture flags and atomic are redundant
- [ ] `host-hardening#7` (low) `RedXe/HostActions.cpp:1088`: Every hold longer than 2 s injects its key-ups twice, and the Execute comment describes an exemption that no longer exists
- [x] `host-hardening#8` (low) `RedXe/Application.cpp:3435`: An open action-notice window is not closed when its notices are cleared, so it keeps showing stale failures
- [x] `launch-ui-thread#2` (low) `Plugins/Actions/Zoom/Zoom.cpp:133`: zoom.open/zoom.join are flagged Deferred but defer only to the UI-thread ring, not a host-owned lane, so the flag gives no relief
- [x] `launch-ui-thread#3` (low) `RedXe/Application.cpp:2704`: Tray Edit settings calls ShellExecuteExW with shell UI on the UI thread; an error box or a share-hosted --settings freezes the dashboard
- [x] `slide-tray#12` (low) `RedXe/Application.cpp:2704`: Tray 'Edit settings' runs ShellExecuteExW with shell UI on the render thread; an error box freezes the dashboard

### P3. Dock, tray, Explorer, and session lifecycle (27)

- [ ] `dock-switch#2` (medium) `RedXe/Application.cpp:1538`: After an Explorer restart (or a start before the taskbar exists) the dock's app bar is never registered again; only the tray icon handles TaskbarCreated
- [ ] `dock-switch#21` (medium) `RedXe/Application.cpp:5030`: Page and widget host actions on a collapsed autohide bar lay out against the peek strip; #27's SettleDockSlide guard covers only a sliding bar
- [ ] `dock-switch#6` (medium) `RedXe/DockPlacement.h:190`: First-run edge ignores displays stacked above or below: the autohide strip can land on an inter-monitor boundary
- [ ] `dock-switch#7` (medium) `RedXe/DockPlacement.h:197`: First-run autohide strip on the top edge sits over maximized windows' caption buttons and tab strips; a click there steals focus and reveals a half-screen bar
- [ ] `dock-switch#8` (medium) `RedXe/DockPlacement.h:177`: `secondary`, written into the first-run file, can later move the bar onto a XENEON connected later or a display nobody is looking at
- [ ] `dock-switch#9` (medium) `RedXe/Application.cpp:5361`: Settings-error dialog is centered on a collapsed autohide strip and opens partly off-screen; with the first-run top bar its caption and the error location are hidden
- [ ] `host-hardening#3` (medium) `RedXe/Application.cpp:3448`: Action-notice (and settings-error) window can open off-screen or unreachable on a dock, especially a collapsed autohide strip; creation code is duplicated between ShowActionNotices and ShowSettingsError
- [ ] `host-hardening#5` (medium) `RedXe/Application.cpp:1617`: PlaceDock re-entrancy guard drops a nested placement request, so a display or work-area change that arrives during a placement leaves the dock at stale geometry
- [ ] `session-end#0` (medium) `RedXe/Application.cpp:6113`: Sign-out, restart and shutdown terminate RedXe without any teardown: no WM_QUERYENDSESSION / WM_ENDSESSION handling, so Logicon never restores the devices
- [ ] `session-end#1` (medium) `RedXe/Main.cpp:377`: After a crash on the main thread, wWinMain returns normally, so CRT exit runs PluginHost::Shutdown and plugin shutdown exports in the crashed process
- [ ] `settings#19` (medium) `RedXe/Application.cpp:5361`: Settings-error and action-notice dialogs are centred on the dock window, which for the collapsed first-run autohide bar is a 4 px strip, so the dialog lands half off-screen
- [ ] `settings#5` (medium) `RedXe/Application.cpp:751`: The first-run dock is persisted from a single discovery result, so a XENEON that is briefly absent at install or recovery turns RedXe into a dock permanently
- [ ] `slide-tray#0` (medium) `RedXe/TrayIcon.cpp:94`: A refused tray icon add is never retried while the taskbar already exists (no timer, reload skips Show, misleading log; NIM_SETVERSION not retried)
- [ ] `slide-tray#1` (medium) `RedXe/Application.cpp:1538`: Dock app-bar and autohide registrations are lost when Explorer restarts; PR #27 handles TaskbarCreated only in the tray icon
- [ ] `slide-tray#2` (medium) `RedXe/Application.cpp:4604`: During a reveal slide, pointer hover/up, accessibility and IME bounds are computed against the partly open window instead of the full dashboard canvas
- [ ] `slide-tray#3` (medium) `RedXe/Application.cpp:932`: Top/left bar slides: SetWindowPos and native-container moves land one composition before the matching Present (a background band at the leading edge, native tiles drift)
- [ ] `slide-tray#6` (medium) `RedXe/TrayIcon.cpp:243`: A WM_CLOSE to the hidden tray owner (Alt+F4 after dismissing the menu, or a graceful taskkill) destroys it without NIM_DELETE: ghost icon, and the running app loses its tray icon
- [ ] `slide-tray#7` (medium) `RedXe/DockPlacement.h:187`: First-run top-edge strip covers the top 4 px of maximized windows on the second screen (caption buttons, tabs)
- [ ] `dock-switch#10` (low) `RedXe/Application.cpp:752`: First-run bar is decided from a transient topology: a first start or invalid-file recovery over RDP (or with the XENEON off) permanently writes a bar for a XENEON user
- [ ] `dock-switch#12` (low) `RedXe/Application.cpp:1607`: DockYieldsToFullscreen counts a maximized window's frame overhang as covering the monitor; the pure rule sits untested in Application
- [ ] `session-end#2` (low) `RedXe/Application.cpp:407`: AutohideBarOnEdge counts a dead app bar as a taken edge, so a first-run install after a RedXe crash or kill can pick the wrong edge
- [ ] `session-end#3` (low) `RedXe/Application.cpp:871`: The previous-crash prompt is owned by the unpainted dock and then activates it, which breaks show-without-focus and pins an autohide bar open
- [ ] `slide-tray#11` (low) `RedXe/TrayIcon.cpp:161`: Keyboard-opened tray menu never returns focus to the notification area (no NIM_SETFOCUS)
- [ ] `slide-tray#19` (low) `RedXe/Application.cpp:5878`: A press, tap or wheel during a reveal slide goes to the tile that the settle moves under it, not the tile the user saw
- [ ] `slide-tray#4` (low) `RedXe/Application.cpp:5928`: Mouse wheel on the hidden strip or on a bar sliding out snaps the bar shut and scrolls or pages invisible widgets
- [ ] `slide-tray#5` (low) `RedXe/Application.cpp:1710`: The peek strip is computed two ways: clamped by the slide (DockPeekPixels) but raw in PlaceDock, WM_GETMINMAXINFO and the grip
- [ ] `slide-tray#15` (nit) `RedXe/DashboardHost.cpp:432`: SetSlideOffset records the new offset before moving the native containers, so a failed final reset is never retried

### P4. Logicon, Zoom, Studio Clock, and 5H4D3R5 (21)

- [ ] `alignment#2` (medium) `Plugins/Actions/Zoom/Zoom.cpp:36`: zoom.join publishes a free-text target, so an invalid invite never shows the promised red ! or warning tile and fails silently
- [ ] `logicon-zoom#1` (medium) `Plugins/Actions/Zoom/Zoom.cpp:35`: zoom.join declares a plain Text target, so an invalid invite passes validation (no red '!') and silently does nothing when pressed; the shared Meeting kind/ParseMeeting is now dead and contradicts the Zoom allowlist
- [ ] `logicon-zoom#12` (medium) `Plugins/Actions/Zoom/Zoom.cpp:28`: Zoom is now stateless but is still wired as a headless service, so zoom.* silently fails without a services entry and the dedicated-action-DLL path stays unused
- [ ] `logicon-zoom#13` (high, raised) `Plugins/Logicon/LogiconRawInput.cpp:240`: With the raw-input window now destroyed mid-lane, Pump() returns without draining the queue while the lane still waits with QS_ALLINPUT/MWMO_INPUTAVAILABLE, so it can hot-spin
- [ ] `logicon-zoom#15` (medium) `Plugins/Logicon/LogiconHid.cpp:441`: WindowsHidPort::SetFeature is an unbounded blocking IOCTL on the device lane, contrary to the bounded-I/O contract
- [ ] `logicon-zoom#4` (medium) `Plugins/Logicon/LogiconService.cpp:834`: Dial/roller turn bindings now depend on a successful HID++ dialpad connect, though Raw Input never needed it
- [ ] `logicon-zoom#5` (medium) `Tests/ZoomTests/Zoom.Tests.Runner.cpp:136`: ZoomTests never exercise the authority delimiters (@ : # ? and backslash) that block host spoofing in IsMeetingUrl
- [ ] `shaders-lut#0` (medium) `Plugins/5H4D3R5/Shaders.cpp:1290`: A failed first lookup-table bake is never retried: Sky Atmosphere draws a black sky until the next resize or DPI change
- [ ] `studioclock#0` (medium) `Plugins/StudioClock/StudioClockDotVertex.hlsl:240`: Date LED halos extend past the 10:9 dated composition and are hard-clipped at the tile's bottom edge
- [ ] `tests#19` (medium) `Tests/HostPluginTests/HostPlugin.Tests.Runner.cpp:4146`: zoom.join binding validation dropped: a mistyped meeting link is accepted and its key silently does nothing
- [ ] `logicon-zoom#14` (low) `Plugins/Logicon/LogiconDevice.cpp:542`: Restore keeps sending HID++ commands after a timeout, so an unresponsive device can push lane shutdown past the 3 s drain budget
- [ ] `logicon-zoom#16` (low) `Plugins/Logicon/LogiconHid.cpp:408`: Retired HID IoState blocks are unbounded: a stuck image write triggers an immediate reconnect that retires another open handle on every lane wake
- [ ] `logicon-zoom#17` (low) `Plugins/Logicon/LogiconMonitor.cpp:877`: Debug monitor now shows an amber 'wheels: raw input unavailable' warning whenever no dialpad is connected
- [ ] `logicon-zoom#19` (low) `Plugins/Actions/Zoom/ZoomSettings.cpp:96`: zoom.join rejects Zoom's direct browser-join link, so every browser-only join goes through the desktop-app launch prompt
- [ ] `logicon-zoom#2` (low) `Plugins/Actions/Zoom/Zoom.cpp:116`: zoom.open fails any authored target even though the TargetNone contract says an authored target is ignored
- [ ] `logicon-zoom#7` (low) `Plugins/Logicon/LogiconService.cpp:1201`: Every unrelated system mouse packet runs a full Logicon lane turn while the dialpad is connected, and in Debug forces a dashboard frame
- [ ] `logicon-zoom#9` (low) `Plugins/Actions/Zoom/Zoom.cpp:133`: zoom.open/zoom.join carry RedXeActionFlagDeferred but Execute returns S_OK, so Launcher reports a queued launch as completed and never surfaces a later failure
- [ ] `shaders-lut#1` (low) `Plugins/5H4D3R5/Shaders.cpp:1544`: BakeLookupTables runs on every size notification and for configurations that can never show Sky Atmosphere
- [ ] `studioclock#10` (low) `Tests/StudioClockTests/StudioClock.Tests.Runner.cpp:899`: Glow readback never checks secondsColor halos or the 0.55 date weight, and its out-of-extent probe cannot detect a halo extent up to about 5.5 radii
- [ ] `studioclock#4` (low) `Specs/Core/Core_PerformanceAndResources.md:291`: Glow cost was accepted as a once-per-second cost, but a raised clock next to a continuous widget redraws (and re-uploads constants) twice per frame
- [ ] `studioclock#5` (low) `Specs/Plugins/Plugins_API.md:1126`: Spec says dimmed ring LEDs keep their brightness under glow, but the five-second companion's halo brightens them; the test only probes the dot center

### P5. Build and test tooling (62)

- [ ] `dxui-restore#0` (high) `Build/DxUiRestore.psm1:72`: A failed, interrupted or leftover DxUi source folder counts as restored: it is never repaired, and vcpkg-install.ps1 runs code from it before anything checks it
- [ ] `alignment#17` (medium) `Test-Changes.ps1:81`: A commit invalidates every native receipt, and -SkipBuild then fails test.ps1's version check, because the commit-count version stamp is part of the artifact identity
- [ ] `dxui-restore#1` (medium) `test.ps1:349`: RedXe.self-test.log is empty for every self-test failure except a CRT report, and exit code 3 now means two different things
- [ ] `dxui-restore#2` (medium) `restore-dxui.ps1:28`: restore-dxui.ps1 runs the pinned DxUi vcpkg-install.ps1, which picks Visual Studio through vswhere -latest instead of the build's MSBuild, contrary to the spec
- [ ] `dxui-restore#4` (medium) `test.ps1:405`: Several RedXe.exe and launcher runs, including the packaged --self-test and the crash tests, stay unbounded under Start-Process -Wait, and the crash tests do not suppress Windows Error Reporting
- [ ] `dxui-restore#7` (medium) `Common/FailureReports.h:24`: ReportAndEnd writes through fputws on a narrow "C"-locale stderr, so a report whose path contains a character above U+00FF is cut off and the routing check fails
- [ ] `dxui-restore#8` (medium, disputed) `Tests/PluginContractTests/PluginContract.Tests.Runner.cpp:2566`: PluginContractTests passes on any failing HRESULT whose low byte is 0x00
- [ ] `dxui-rev3#0` (medium) `Build/DxUiRestore.psm1:72`: Pin restore treats any existing source/<commit> folder as restored, and vcpkg-install runs DxUi modules from it before anything checks it
- [ ] `process-containment#0` (medium) `Build/BuildPresentation.psm1:806`: Timeout budget is never enforced while a bounded child keeps writing output (1 ms floor on the remaining wait)
- [ ] `process-containment#12` (medium) `Common/FailureReports.h:32`: A budget kill drops the hung test's own buffered stdout, so the log cannot say which case hung (10 of 12 test executables)
- [ ] `process-containment#2` (medium) `Build/BuildPresentation.psm1:861`: If the log writer's Dispose throws in the finally block, the job is never disposed, so kill-on-close never runs and an orphaned child can block
- [ ] `process-containment#5` (medium) `Build/BuildPresentation.psm1:776`: Pressing Ctrl+C during build.ps1 in Windows Terminal leaves MSBuild building silently in the background
- [ ] `process-containment#7` (medium) `Build/BuildPresentation.psm1:501`: Nothing enforces the rename-on-change rule for the compiled launcher type: a session that already compiled it keeps stale code, and Test-Changes can save a passing receipt for code that never ran
- [ ] `scoped-testing#1` (medium) `Test-Changes.ps1:81`: Commit-count build number is outside the evidence identity: every commit invalidates all native receipts, and SkipBuild after a commit passes attestation and then fails with a misleading version error (spec claims the opposite)
- [ ] `scoped-testing#2` (medium) `Build/ScopedTesting.psm1:7`: Git output is decoded with the console code page, so non-ASCII paths silently drop out of the source identity, the build attestation and the test inventory
- [ ] `scoped-testing#4` (medium) `test.ps1:47`: Plain test.ps1 exits 0 without building or testing when there is no merge-base diff, docs still call it full validation, and it now fails without Git
- [ ] `scoped-testing#5` (medium) `Build/ScopedTesting.psm1:245`: PrePush delegates all x64 Release obligations to a CI workflow that main does not require
- [ ] `scoped-testing#6` (medium) `Tests/BuildProcessTests/ScopedTesting.Tests.ps1:73`: Rule completeness is hand-maintained; a cheap project-closure check would have caught the four Settings edges #31 missed
- [ ] `tests#17` (medium) `Test-Changes.ps1:79`: Commit-count build number is outside the scoped-test identity: SkipBuild breaks after a commit, and commits defeat reuse
- [ ] `tests#18` (medium) `Build/ScopedTesting.psm1:245`: PrePush delegates every Release x64 obligation to a PR check that the main ruleset does not require
- [ ] `tests#4` (medium) `Test-Changes.ps1:125`: Test-Changes turns 'not reusable' into a failed run when any repository file changes while the tests run
- [ ] `tests#5` (medium) `Build/DxUiRestore.psm1:72`: In-place DxUi clone: an interrupted or concurrent restore poisons every later tooling-test and build run
- [ ] `alignment#18` (low) `Build/ScopedTesting.psm1:67`: The live test-source detector matches case-insensitively, so any product file whose name contains "test", "mock" or "fake" blocks every Test-Changes run
- [ ] `alignment#19` (low) `Build/DxUiRestore.psm1:98`: A partially restored DxUi source directory is never repaired, and the new vcpkg-install.ps1 caller then fails with an unrelated module-load error
- [ ] `alignment#20` (low) `Test-Changes.ps1:74`: The ordinary `./test.ps1` now refuses the default x64 platform on an ARM64 host, which the Full path still allows
- [ ] `dxui-restore#10` (low) `restore-dxui.ps1:27`: The DxUi restore never removes superseded output folders or source clones: 3.4 GB has built up, most of it unused
- [ ] `dxui-restore#11` (low) `Build/DxUiRestore.psm1:77`: The sparse restore still clones DxUi's full history, so each network restore downloads about 43 MB of packs for a 6 MB working tree
- [ ] `dxui-restore#14` (low) `Tests/BuildProcessTests/DxUiRestoreTests.ps1:57`: The DxUi restore test's fixture commits inherit the developer's global Git signing, hook and line-ending settings
- [ ] `dxui-restore#17` (low) `Common/FailureReports.h:34`: Clearing _WRITE_ABORT_MSG makes every abort that is not a CRT report end with exit 3 and print nothing
- [ ] `dxui-restore#18` (low) `Build/DxUiUpdate.psm1:52`: Update-DxUi accepts a DxUi main commit at another API revision, so -UpdateOnly writes a lock that can never restore
- [ ] `dxui-restore#3` (low) `vcpkg-install.ps1:66`: Standalone vcpkg-install.ps1 and restore-dxui.ps1 discover MSBuild / Visual Studio differently from build.ps1, so overlay triplets and DxUi identity files flip between installations
- [ ] `dxui-restore#5` (low) `Common/FailureReports.h:32`: RouteAwayFromDialogs leaves the standard C assert() message box in place for the GUI-subsystem RedXe.exe
- [ ] `dxui-restore#9` (low) `Plugins/AVControl/BrokerMain.cpp:353`: AVControlBroker.exe, which AVControlTests starts, does not route failed checks, so a Debug check in it still opens a dialog
- [ ] `dxui-rev3#1` (low) `Build/DxUiUpdate.psm1:26`: Update-DxUi.ps1 picks a DxUi main commit without checking its API revision, so a revision bump produces a lock every build rejects
- [ ] `process-containment#1` (low) `Build/BuildPresentation.psm1:773`: The budget uses the wall clock (DateTime.UtcNow), so a clock step can cut it short or stretch it
- [ ] `process-containment#13` (low) `Build/BuildPresentation.psm1:749`: On timeout the helper stops reading before it terminates the job, so output already in the pipes and a trailing partial line are dropped
- [ ] `process-containment#14` (low) `Tests/HostPluginTests/HostPlugin.Tests.Runner.cpp:3498`: The FlushLog early return cannot bound a hung writer: ~PluginHost then joins the log worker with no limit
- [ ] `process-containment#15` (low) `test.ps1:327`: A HostPlugin or HostSmoke timeout shows none of the child's output on the console, not even the tails the PR relies on
- [ ] `process-containment#3` (low) `Build/BuildPresentation.psm1:747`: Kill-on-close job is created outside the try/finally that disposes it and leaks if creating the log directory fails
- [ ] `process-containment#4` (low) `Build/BuildPresentation.psm1:807`: Ctrl+C has no effect while a bounded test executable is silent, for up to its 15-minute budget
- [ ] `process-containment#6` (low) `Build/BuildPresentation.psm1:751`: When the child has exited but a descendant still holds its pipe, the helper waits out the whole budget and then reports the child as hung
- [ ] `scoped-testing#10` (low) `Build/ScopedTesting.psm1:242`: PR delegation does not check the base/merge-tree workflow equivalence the accepted plan requires
- [ ] `scoped-testing#11` (low) `Test-Changes.ps1:44`: Test-Changes -Scopes does not split comma lists under pwsh -File, unlike test.ps1 -Suites
- [ ] `scoped-testing#12` (low) `Tests/test-scopes.json:266`: Rules for test-project folders send a test .vcxproj edit only to its own suite, though all projects build into one shared output folder
- [ ] `scoped-testing#13` (low) `Tests/BuildProcessTests/ScopedTesting.Tests.ps1:248`: The stale-workflow-digest delegation test cannot fail in CI
- [ ] `scoped-testing#14` (low) `Build/ScopedTesting.psm1:18`: Git failures in scope discovery give no diagnostic (empty message for unrelated or shallow history; failing command not named)
- [ ] `scoped-testing#15` (low) `test.ps1:54`: Suite list is maintained in three hand-kept copies; a scope with no test.ps1 block earns a reusable PASSED receipt, and a test.ps1-only suite is never selected
- [ ] `scoped-testing#16` (low) `Tests/test-scopes.json:336`: Skill metadata and validator-script edits fall through to the full native fallback
- [ ] `scoped-testing#23` (low) `Build/ScopedTesting.psm1:162`: Evidence environment identity misses OS servicing level, D2D/WIC/DXGI runtimes, and the Python/PyYAML/git that tooling runs under
- [ ] `scoped-testing#24` (low) `Build/ScopedTesting.psm1:236`: PrePush ties delegation of the profile-independent BuildProcess scope to the x64 Release profile, so the default Debug PrePush repeats the PR tooling job locally
- [ ] `scoped-testing#3` (low) `Build/ScopedTesting.psm1:67`: Live test-file detector matches 'test'/'mock'/'fake' as a case-insensitive substring, so ordinary production names block every run and cannot be registered
- [ ] `scoped-testing#7` (low) `Test-Changes.ps1:69`: PrePush with full CI delegation never prints the CI_PENDING verdict the spec requires
- [ ] `scoped-testing#8` (low) `Build/ScopedTesting.psm1:241`: PR delegation refusals are silent; PrePush Release falls back to a full local Release run with no reason given
- [ ] `scoped-testing#9` (low) `Test-Changes.ps1:74`: Default test.ps1 on an ARM64 host now throws instead of running x64 tests; the two entrypoints disagree about the same profile
- [ ] `studioclock#1` (low) `Build/DxUiProvenance.psm1:18`: DxUi provenance no longer binds the recorded build identity to the archive it hashes, and repeats path logic owned elsewhere
- [ ] `studioclock#2` (low) `restore-dxui.ps1:27`: DxUi restore never removes old output roots; #22's switch to 16-digit folder names orphaned every existing 64-digit root
- [ ] `tests#10` (low) `Build/BuildPresentation.psm1:752`: Streaming helper timeout path neither waits for the job's processes to exit nor drains buffered output; the stall fixture's immediate survivor scan and log check can fail spuriously
- [ ] `tests#14` (low) `test.ps1:405`: test.ps1 still runs six RedXe/launcher processes with unbounded Start-Process -Wait despite the bounded-process policy
- [ ] `tests#15` (low) `Tests/BuildProcessTests/ScopedTesting.Tests.ps1:19`: Tooling-test fixture repositories inherit the developer's global Git config (commit signing, hooks)
- [ ] `tests#20` (low) `test.ps1:349`: HostSmoke self-test log stays empty on failure, and its exit codes collide with the failure-report code 3
- [ ] `tests#8` (low) `Tests/test-scopes.json:217`: Scope manifest rules are hand-maintained and never checked against the test projects' real inputs; four rule patterns match nothing
- [ ] `tests#22` (nit) `Build/ScopedTesting.psm1:67`: Native test-inventory discovery uses a case-insensitive 'Test|Mock|Fake' match that catches ordinary product names

### P6. Behavior-preserving simplifications (41)

- [ ] `tests#0` (medium) `Tests/SettingsTests/Settings.Tests.Runner.cpp:1408`: Tray-icon test claims 'a toggle is a runtime change the host applies live' but only checks AppSettings::operator==, which the live-reload path never uses
- [ ] `alignment#10` (low) `Specs/Build/Build_Process.md:117`: Build_Process.md states normative requirements for RedSalamander and DxUi, which RedXe neither owns nor implements
- [ ] `alignment#13` (low) `Tests/test-scopes.json:182`: The scoped-testing manifest, module and help carry rules and examples copied from other repositories
- [ ] `alignment#8` (low) `RedXe/Application.cpp:751`: MakeFirstRunDock measures displays on every launch without a XENEON, though the spec says only at install
- [ ] `dock-switch#15` (low) `RedXe/Application.cpp:433`: Monitor enumeration and effective-DPI lookup duplicated across MakeFirstRunDock, ResolveDockMonitor and PlaceStandardWindow; the `secondary` order invariant depends on the copies matching
- [ ] `dock-switch#16` (low) `RedXe/Settings.cpp:2477`: Duplicated install-target probe and atomic-commit code in Settings.cpp introduced by the first-run path
- [ ] `dock-switch#5` (low) `RedXe/Application.cpp:751`: MakeFirstRunDock measures displays and queries Explorer on every start without a XENEON, though its result is used only when a template is installed
- [ ] `dxui-restore#12` (low) `Build/DxUiProvenance.psm1:12`: Lock reading, source-path building and MSBuild discovery are still duplicated after the PR's 'each fact lives once' consolidation, including a pass-through Read-RedXeDxUiUpdateLock
- [ ] `host-hardening#9` (low) `RedXe/Application.cpp:2558`: The sameRuntime field list duplicates AppSettings::operator== by hand and silently treats any new member as source-only
- [ ] `logicon-zoom#10` (low) `Common/Actions/WindowSelector.cpp:153`: RedXeActions::BringToForeground is dead after zoom.focus was removed; it is the only synthetic-Alt focus stealer and its docs still name Zoom
- [ ] `logicon-zoom#6` (low) `Plugins/Logicon/LogiconHid.h:90`: Dead HID cancel and redundant stop paths after the IoState rewrite: HidPort::Cancel has no caller and races Close, Write's ERROR_BUSY branch is unreachable, and lane exit stops the wheels twice
- [ ] `process-containment#11` (low) `Build/BuildPresentation.psm1:720`: The process-containment launcher lives in the presentation module; test.ps1 loads BuildPresentation.psm1 only for it
- [ ] `process-containment#8` (low) `Build/BuildPresentation.psm1:475`: Two argument-quoting implementations for the two launch paths; ConvertTo-RedXeProcessCommandLine matches .NET ArgumentList exactly, so Set-RedXeProcessArguments can be deleted
- [ ] `scoped-testing#18` (low) `Build/ScopedTesting.psm1:224`: Code, help text and normative spec text copied from other repositories (DxUi, RedSalamander) is dead or wrong in RedXe
- [ ] `settings#11` (low) `RedXe/Settings.cpp:2137`: PrepareDockSourcePatch parses the whole document a second time for a check ParseAppSettingsJsonV5 already guarantees
- [ ] `settings#12` (low) `RedXe/Application.cpp:751`: MakeFirstRunDock queries Explorer on every launch without a XENEON although it is used only when the default file is installed, and it duplicates ResolveDockMonitor's monitor enumeration
- [ ] `settings#13` (low) `RedXe/Settings.cpp:1872`: The new source-text JSON scanner duplicates JsonTextCursor already in SettingsV4.cpp
- [ ] `settings#14` (low) `RedXe/Settings.cpp:2506`: InstallTemplateWithDock checks only 4 dock fields where DockSettings::operator== is available, and repeats the install and commit plumbing
- [ ] `settings#16` (low) `RedXe/Application.cpp:2261`: Application::_persistSettingsToDisk duplicates the store's self-test write gate
- [ ] `slide-tray#10` (low) `RedXe/Application.cpp:427`: MakeFirstRunDock duplicates ResolveDockMonitor's display enumeration and round-trips its own constant selector through text
- [ ] `slide-tray#13` (low) `RedXe/Application.cpp:2462`: Two slide fields can be derived from existing state, and the end of TickDockSlide repeats SettleDockSlide
- [ ] `slide-tray#9` (low) `RedXe/Application.cpp:752`: MakeFirstRunDock runs on every launch without a XENEON, including three synchronous Explorer round trips, although only a template install uses its result
- [ ] `studioclock#6` (low) `RedXe/SettingsV4.cpp:54`: Studio Clock settings are spelled out in four places; follow the shared ShadersSettings.h pattern
- [ ] `studioclock#7` (low) `Plugins/StudioClock/StudioClock.cpp:219`: #22 added a fourth identical copy of JsonCursor::ReadUnsigned; the settings cursor is copied across four plugin DLLs
- [ ] `tests#12` (low) `RedXe/SettingsV4.cpp:55`: Studio Clock defaults and glowPercent range exist in four copies, each tested only against its own literal
- [ ] `alignment#14` (nit) `Specs/Settings.schema.json:323`: Dead leftovers of the removed Zoom SDK path: unreferenced schema def and stale guidance that ActionTargets is compiled into every publisher
- [ ] `alignment#15` (nit) `.gitignore:26`: Leftover Zoom SDK ignore entries point at a deleted import script
- [ ] `dock-switch#17` (nit) `RedXe/Application.cpp:2079`: RebuildPresentation duplicates the renderer start sequence of InitializeDashboardRuntime
- [ ] `dock-switch#19` (nit) `RedXe/Application.h:373`: Two long-lived members carry the kind-switch placement from RestyleWindowKind to FinishWindowKindSwitch
- [ ] `dock-switch#20` (nit) `RedXe/Application.h:378`: _dockWorkRect is write-only state, and RestyleWindowKind keeps a WS_VISIBLE bit that is always clear
- [ ] `dxui-restore#15` (nit) `Tests/PluginContractTests/PluginContract.Tests.Runner.cpp:2552`: The --asan-probe SetErrorMode call is now redundant
- [ ] `host-hardening#15` (nit) `RedXe/HostActions.cpp:1066`: HostActions::ReleaseHeld ignores its deviceAccess parameter (and the Execute comment describes removed behavior)
- [ ] `host-hardening#16` (nit) `RedXe/PluginHost.cpp:2328`: Simplification: the stopPending retry block is copied in StartServices and ApplyServiceSettings
- [ ] `host-hardening#17` (nit) `RedXe/Application.h:407`: _dockDashboardResizeTimerArmed duplicates _dockDashboardResizePending
- [ ] `logicon-zoom#11` (nit) `Specs/Settings.schema.json:323`: Leftovers from the removed Zoom SDK/client path: unreferenced $defs/zoomSettings, a stale .gitignore script reference, and ZoomSettings compiled into the host
- [ ] `process-containment#10` (nit) `Build/BuildPresentation.psm1:686`: ContainedProcess carries dead members after #26 removed the Kill fallback, and the read/wait logic is duplicated per path; one wait with Timeout.Infinite removes both
- [ ] `process-containment#16` (nit) `test.ps1:142`: Two assignments of the test budget; the effective value for later suites depends on whether PluginContract was selected
- [ ] `scoped-testing#19` (nit) `Tests/test-scopes.json:182`: Four scope rules match test folders that do not exist (dead rules)
- [ ] `scoped-testing#20` (nit) `Build/ScopedTesting.psm1:101`: Top-level Mockups/ is treated as code: it triggers a full run and invalidates native results
- [ ] `scoped-testing#21` (nit) `test.ps1:142`: Duplicate $testTimeoutSeconds assignment left inside the PluginContract block
- [ ] `settings#18` (nit) `Specs/Settings.schema.json:323`: Dead `zoomSettings` definition left in the schema

### P7. Spec, user-guide, and test-coverage drift (32)

- [ ] `alignment#4` (medium) `test.ps1:47`: test.ps1 now runs only affected scopes, or nothing, but the specs, README and skills still treat it as the full gate
- [ ] `tests#1` (medium) `RedXe/Application.cpp:2625`: Live window-kind switch and its rollback have no automated coverage (self-test pins edge none; manual checklist has success paths only)
- [ ] `tests#2` (medium) `RedXe/TrayIcon.cpp:214`: TrayIcon class (Show/Hide, TaskbarCreated re-add, class unregistration) is untested; only the pure callback table is covered
- [ ] `tests#3` (medium) `Tests/HostPluginTests/HostPlugin.Tests.Runner.cpp:4206`: Held-input test checks only release counts: it cannot tell which chord or button is released, nor catch an early or busy timer
- [ ] `alignment#11` (low) `Specs/Plugins/Plugins_API.md:184`: Plugins_API.md still says test exports go in `*TestContract.h`; the new naming check rejects that name
- [ ] `alignment#12` (low) `AGENTS.md:115`: AGENTS.md and README layout entries are stale after the Zoom removal and the scoped-testing tooling
- [ ] `alignment#21` (low) `RedXe/CommandLine.h:189`: The --help text and README still describe startup without a XENEON as the prompt or plain titled window, which #27's first-run bar replaced
- [ ] `alignment#5` (low) `Specs/Core/Core_Settings.md:335`: Minor-raise rules and dock patch comments disagree with the code and with each other
- [ ] `alignment#7` (low) `Specs/UI/UI_XeneonDisplayWindowing.md:53`: Mode table's Debug-without-XENEON row omits the first-run dock that replaces it
- [ ] `alignment#9` (low) `Specs/Plans/WIP/ScopedTesting_2026-10-05.md:3`: The ScopedTesting WIP plan is still ACTIVE with unchecked items, although its work and qualification are recorded as done
- [ ] `dock-switch#13` (low) `Build/Package.psm1:107`: User-facing startup docs (README, packaged README, usage.md) still describe the prompt or titled window for a machine without a XENEON
- [ ] `dock-switch#4` (low) `RedXe/Application.cpp:2625`: Window-kind switch orchestration and the combined-reload rollback have no automated or manual coverage
- [ ] `dxui-restore#13` (low) `README.md:88`: The README says vcpkg-install.ps1 is enough before a direct Visual Studio build, but the build also needs restore-dxui.ps1
- [ ] `dxui-restore#6` (low) `Tests/PluginContractTests/PluginContract.Tests.Runner.cpp:2546`: No test covers RedXe.exe's own failure-report routing, any other runner's call, or the Release abort path
- [ ] `host-hardening#14` (low) `test.ps1:366`: No automated end-to-end coverage for the new asynchronous --screenshot pipeline
- [ ] `logicon-zoom#18` (low) `Tests/LogiconTests/Logicon.Tests.Runner.cpp:718`: The new raw-input ownership rules (do not replace a prior owner, do not remove a later one) have no test
- [ ] `logicon-zoom#8` (low) `Specs/Plugins/Plugins_Zoom.md:34`: Plugins_Zoom.md says removed zoom.* bindings fail settings validation, but documents accept them and they show a red '!'
- [ ] `process-containment#9` (low) `Tests/BuildProcessTests/BuildProcessTests.ps1:240`: The committed test only checks quoting for one argument with spaces, though every bounded test.ps1 call uses the custom quoter
- [ ] `scoped-testing#22` (low) `Common/AddressSanitizerProbe.h:5`: A test-only ASan fixture header in Common/ escaped the #31 naming migration and native test inventory
- [ ] `tests#11` (low) `Tests/HostPluginTests/HostPlugin.Tests.Runner.cpp:963`: Slide-offset test covers one GPU tile only; native widget containers moving with a top or left dock slide are untested
- [ ] `tests#13` (low) `RedXe/HostActions.cpp:698`: Monitor resolution for actions (including the new `secondary` kind) never runs under test: deviceAccess=false swaps in a fixed rectangle
- [ ] `tests#21` (low) `Tests/LogiconTests/Logicon.Tests.Runner.cpp:718`: Logicon raw-input ownership rules added in #21 have no test; only plain Start/Stop/restart is exercised
- [ ] `tests#7` (low) `test.ps1:116`: Launcher wait-policy tests pin only the self-terminating switches; an unknown argument through the RedXe alias exits 0, and test.ps1 hides this by adding --self-test
- [ ] `alignment#16` (nit) `Specs/Plans/Done/DxUiFollowUps_2026-10-01.md:52`: Plan hygiene: an unindexed WIP checkpoint edited in the range, and a Done plan naming a header that does not exist
- [ ] `dock-switch#14` (nit) `RedXe/Settings.cpp:2637`: Stale comment: the first-run bar is no longer always on the primary display
- [ ] `dxui-restore#16` (nit) `Specs/Plans/Done/DxUiFollowUps_2026-10-01.md:52`: The Done plan names Tests/Support/FailureReports.h, which no longer exists
- [ ] `host-hardening#18` (nit) `Plugins/5H4D3R5/Shaders.cpp:850`: Stale comment: lookup tables are no longer drawn by the first widget frame
- [ ] `settings#17` (nit) `RedXe/Settings.h:381`: Stale or contradictory persistence contract text for the dock source edit and the first-run minor raise
- [ ] `shaders-lut#2` (nit) `Plugins/5H4D3R5/Shaders.cpp:850`: Stale comment says lookup tables are drawn by the first widget frame
- [ ] `slide-tray#16` (nit) `Common/PlugInterfaces/Action.h:57`: Plugin ABI comment for RedXeActionTargetMonitor omits the new `secondary` selector
- [ ] `slide-tray#17` (nit) `RedXe/Main.cpp:268`: `--dock` error text omits the new `@secondary` selector
- [ ] `slide-tray#18` (nit) `RedXe/DockPlacement.h:529`: Comments and docs still describe autohide as it worked before the slide
