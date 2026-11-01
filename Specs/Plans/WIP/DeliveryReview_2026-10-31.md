# Delivery review 2026-10-31

Status: evidence annex. Do not execute this file.
Date: 2026-10-31
Reviewed against tip `3a8b3b1` on 2026-11-01: the menu and probe line numbers still match. The package
command below does not.

This plan is a non-normative evidence annex. It does not change product behavior. The executable ship
list is [`DeliveryRedTeam_2026-10-31.md`](DeliveryRedTeam_2026-10-31.md). Keep the DX-2 and P1 sections
here for line numbers and the catalog sentence. Tick boxes only on the red-team plan.

No RedXe product C++ High exists on `origin/main`. The menu defect is in the pinned DxUi library.
Two pipeline Highs (engine AND-gate, production-receipt leftovers) are on the red-team plan and must
close before this tip is called qualified.

## Baseline

Product judgments were made from `origin/main` blobs. This worktree is `ej/unified-ci-20261030` at
`a459ff9a7b7ae6398974805e4eb3e2f5b2e3ed65`, an ancestor of `8c882636`, and some of its copies differ.
Re-read each anchor on the tip before editing. The line numbers below were confirmed with `git show
origin/main` on 2026-10-31.

Reviewed product tip: `8c882636a0859782acb4108cffad252b9607a242` (2026-10-31 12:32 +0100, merge of PR #69,
`ej/ci-artifact-closeout-20261031`).

Local `origin/main` when this plan was written: `229689e298cfabf5efd5eb285e6b4a5589a8060a` (PR #70, the
changelog fold of that tip, plus `4676603`). `git diff --stat 8c882636 origin/main` excluding
`CHANGELOG.md` and `Changes/` is empty, so the product line numbers match the tip.

DxUi lock at that tip, `Dependencies/DxUi.lock.json`: commit
`616104f3b2b693a824a3a76b18b851055accf777`, API revision 4, target `["DxUi"]`. That commit is the tip of
`origin/ej/wall-compat-redxe-20261028` (2026-10-28, "Settle sent menu fixture activation and stale input
before popup creation"). It is not an ancestor of DxUi `origin/main`. Their merge-base is
`fa4da2cb112e029646835f81f14ee2e0185129a7`. Four commits are on the pin and on neither DxUi main tip
named below:

- `fa3de6c9` Preserve API 4 payload identity under Release ICF
- `8081a7c2` Backport popup readiness and native focus fixture guards
- `f4600c3a` Establish modal timer fixture foreground before arming timers
- `616104f3` Settle sent menu fixture activation and stale input before popup creation

A fix that lands only on DxUi main does not reach RedXe.

The review named DxUi main as `f2940cc265db79458b6615492779b839c5c1b94a` (2026-10-31, PR #152). When this
plan was written, that commit was an ancestor of DxUi `origin/main`
`07ff5741cc0cbcd10f7aa5e9e120d0c4b0c16d92` (PR #154). The keyboard-cancel commit
`762983a021c448e4c7334d347ba46f79e20ac817` (2026-10-28, "Prioritize keyboard navigation over pending menu
hover timers") is an ancestor of `f2940cc2` and of that later main, and is not an ancestor of the pin.
`f2940cc2` has 295 commits the pin lacks; current DxUi main has 299. The branches have diverged. Do not
retarget `Dependencies/DxUi.lock.json` to either main tip.

## Checklist

Do not tick these here. The red-team plan owns the boxes.

- DX-2 is red-team P0-4. The pinned menu lines in the next section were re-read on pin `616104f3` on 2026-11-01 and still match.
- P1 probe wording is red-team P1-3. `CommandLine.h` 122–124, `docs/usage.md` 176, and `UI_XeneonDisplayWindowing.md` 52 still say `2 no listener` only.
- P2, packaging `8c882636`, is superseded. Package `c61861c` as red-team P0-3. Do not run the command in the P2 section below.
- P3 is a standing limit on [`RedMonitor_Adoption_2026-10-23.md`](RedMonitor_Adoption_2026-10-23.md), not a task.

## DX-2. Menu hover timers in the pinned library

Medium. This is the product ship item. The defect is already in the DxUi commit RedXe ships. RedXe product
C++ does not change except the lock, after the library commit exists.

Confirmed on pin `616104f3`, `src/Controls/DxUi.Menu.cpp`:

- Line 81: `kCascadeHoverDelayMs` is 400. Lines 4362 and 4376 arm `kSubmenuHoverTimerId` with that delay.
- Lines 4265-4278: `CancelSubmenuHoverTimer` clears `hoverTimerId`, `hoverTimerKind`, and
  `hoverTimerItemIndex`, and calls `KillTimer` when a timer is armed. Its last call site on this pin is
  line 5245. `RouteMenuKeyboardEvent` does not call it.
- Lines 5384-5394: `RouteMenuKeyboardEvent` returns only when the menu is not running, then reads
  `focusedIndex`. Up, Down, Home, and End assign `keyboardIndex` at lines 5446, 5459, 5475, and 5497 and
  leave a pending timer armed. Arrow keys therefore do not cancel the 400 ms timer.
- Lines 4413-4436: `HandleSubmenuHoverTimer` still opens the stored hover row (`PendingOpen` calls
  `OpenSubmenu` with `hoverTimerItemIndex`) or closes the submenu chain (`PendingClose` calls
  `CloseSubmenuChainFrom`). Lines 4415-4418 return only when `hoverTimerId` is 0 or the kind is `None`,
  so a timer that keyboard input left armed still runs. `WM_TIMER` reaches it from line 5734.
- Lines 3502-3506: `MenuAccessibilityItem::OnFocusChanged`, when focused, sets `keyboardIndex` and clears
  `hoveredIndex`. It does not cancel timers. The same gap is still on DxUi `origin/main` at lines
  3512-3519. UI Automation focus is not covered by `762983a0`.
- Lines 2166-2167: `MenuPopupAccessibleInvoke` sets `keyboardIndex` and then sends `WM_KEYDOWN` / `VK_RETURN`.
  Line 5823 routes keyboard events through `RouteMenuKeyboardEvent`, so that synthesized key is covered
  once the keyboard cancel exists. The focus path above does not send a key.

`762983a0` adds the keyboard cancel. Confirmed on current DxUi `origin/main`,
`src/Controls/DxUi.Menu.cpp` lines 5450-5457, inside `RouteMenuKeyboardEvent` after the running-popup
check and before `focusedIndex`: AltGr (`MenuKeyboardKind::Alt`, extended key, Control down) returns
`Consumed` and leaves the pointer timer alone; every other keyboard event calls
`CancelSubmenuHoverTimer` on each popup in the chain. Clearing that state makes an already queued
`WM_TIMER` hit the early return in `HandleSubmenuHoverTimer`. The pin's plain Alt case at line 5538 still
dismisses the menu. Port the AltGr return with the cancel loop so AltGr neither cancels the timer nor
falls through into that dismiss.

DxUi `Specs/UI/UI_InputAndAccessibility.md` lines 430-432, on `762983a0` and on current DxUi main, say:
keyboard navigation cancels pending pointer cascade timers throughout the popup chain, and a previously
queued timer must not replace or close a keyboard-focused child. AltGr preserves the existing pointer
timer. `git grep` for that cascade-cancel sentence on `616104f3` finds nothing. Those sentences are not
in the pinned spec, and they are not in a RedXe spec. `Specs/Core/Core_DxUiIntegration.md` leaves library
behavior in DxUi's contracts. Port the sentences onto the pin branch with the code, and extend them so
UI Automation focus that sets `keyboardIndex` cancels the same chain of timers.

`TestMenuKeyboardInputCancelsPendingSubmenuHoverTimers` is `Tests/Controls/DxUi.Tests.Menu.cpp` line 5501
at `762983a0`, registered at line 12374. It already hovers, moves with the keyboard, and delivers a
synchronous `WM_TIMER` for a pending open and a pending close. Port it, and add the UI Automation focus
case. Re-read those tests on `762983a0` before copying; line numbers on later main will have moved.

Repair, in order:

1. On `ej/wall-compat-redxe-20261028`, starting from `616104f3`, port the AltGr guard and the chain-wide
   `CancelSubmenuHoverTimer` loop from `762983a0` into `RouteMenuKeyboardEvent`. Keep
   `CancelSubmenuHoverTimer`'s clearing of timer id, kind, and item index. In
   `MenuAccessibilityItem::OnFocusChanged`, when focus sets `keyboardIndex`, cancel every popup timer in
   that controller the same way.
2. Port the spec sentences onto that branch and state the UI Automation focus rule there. Extend the
   menu regression for UI Automation focus. Keep the AltGr case.
3. Re-pin RedXe by changing only the `commit` field of `Dependencies/DxUi.lock.json`. Keep
   `"apiRevision": 4`. `Core_DxUiIntegration.md` does not hardcode this hash. Update that spec only when
   a sentence there names the old commit. Do not run `Update-DxUi.ps1`: it selects a current DxUi main
   commit. The manual update loop in that spec is the route that fits: change the lock on a product
   branch, build, and run the product suite. Library tests on the new commit do not by themselves
   qualify RedXe.
4. Retargeting the lock to `f2940cc2` or later DxUi main is not this repair. That move drops the four
   pin-only commits above, and main's `OnFocusChanged` still does not cancel timers. Reconsider a main
   bump only after a recorded check that those four commits' behavior is on the candidate and that UI
   Automation focus cancels the chain. Until that check exists, the repair stays on
   `ej/wall-compat-redxe-20261028`.

Acceptance, on the patched pin, then again from a RedXe build that restores that pin:

- Hover a submenu so the 400 ms open timer is pending, move the selection with the keyboard, and deliver
  `WM_TIMER`. One popup remains, on the focused row. The stored hover row does not open.
- Repeat with UI Automation focus setting `keyboardIndex` instead of a key.
- Repeat for a pending close. The queued timer does not close the focused child.
- AltGr leaves a pending pointer timer armed.

P2's package of `8c882636` does not qualify this re-pin. A release of the repaired pin needs its own
production `package.ps1` of that later commit.

## P1. Probe exit 2 wording

Low. One documentation change. Do not change the exit code, `RunProbe`, or `DescribeProbeResult`.

Confirmed on `origin/main`, identical in product content to `8c882636`:

- `Common/MonitorDiagnostics.cpp` lines 95-101: `RunProbe` returns 2 when the listener is still disabled
  after the wait, and returns 2 again when either test `Write` is not `ERROR_SUCCESS`.
- Line 112: the printed line for that code is `no RedMonitor listener within 2 seconds, or ETW refused a
  test message.`
- `RedXe/Main.cpp` lines 240-244 print that line and return the code. The comment at line 225 is right:
  this happens before settings, plugins, or windows exist.
- `Specs/Core/Core_RedMonitorIntegration.md` line 35 already says exit 2 is no listener or a rejected
  write. `Common/MonitorDiagnostics.h` line 21 matches. Leave both.

These three still say `2 no listener` only. `AGENTS.md` requires the catalog, the command-line section of
`docs/usage.md`, and the mode table in `Specs/UI/UI_XeneonDisplayWindowing.md` to move together:

- `RedXe/CommandLine.h` lines 122-124. The summary ends `2 no listener. Use it alone.` It does not
  mention the printed line.
- `docs/usage.md` line 176. It already says the probe prints one line, and it still says `2 no listener`.
- `Specs/UI/UI_XeneonDisplayWindowing.md` line 52. The probe cell already says it prints one line naming
  the outcome, and it still says `2 no listener`. The earlier `command-line error 2` on that line is a
  different outcome (`Use <switch> by itself.`). Change only the probe outcome.

In that one change, all three say exit 2 is no listener or a rejected write, and the catalog summary
mentions the printed line. Keep the summary's first 24 characters contiguous. `SettingsTests`
(`Tests/SettingsTests/Settings.Tests.Runner.cpp` lines 2074-2075) compares that prefix of each catalog
summary with `--help`, and `test.ps1` lines 639-661 require `--help` to mention `--redmonitor-probe`.
Those existing checks are the lock. Do not add another help parser. `test.ps1` lines 714-715 also require
exit 2 and the printed no-listener prefix when no viewer is running. Leave that assertion: the printed
line and the exit code stay.

Catalog summary to land, wrapped as the existing two wide-string literals:

`Wait up to 2 seconds for a RedMonitor listener, send RedMonitor's test messages, print one line naming the outcome, and exit: 0 sent, 1 no provider, 2 no listener or a rejected write. Use it alone.`

Usage line 176 replaces `2 no listener` with `2 no listener or a rejected write` and keeps `print one line
saying what happened`. The mode-table probe outcome on line 52 replaces `2 no listener` the same way and
keeps `prints one line naming the outcome on the console or redirected output`.

## P2. Production package of the reviewed tip

Process. Do not call `8c882636` qualified until this run exists. Packaging, CRT selection, the symbols
ZIP, and DxUi provenance changed on the way to that tip and were not executed.

Run from a checkout of `8c882636` itself. This worktree is an ancestor, and the changelog-fold tip
`229689e2` would stamp a different commit-count build number. Command:

```powershell
.\package.ps1 -Platform x64
```

No `-SkipBuild` and no `-SkipExecution`. `package.ps1` builds Release, writes the portable ZIP and the
symbols ZIP, and writes a `.sha256` sidecar for each (`Specs/Build/Build_Packaging.md`).

Acceptance: both ZIPs exist from that production-only Release build. Recompute each file's SHA-256 and
match it to the sidecar that run wrote. Record the two paths, both hashes, and the `DxUi.provenance.json`
identity inside the portable ZIP. A mismatch or a packaging failure is a new defect. This plan does not
pre-describe a source fix, and it does not edit `Build_Packaging.md` unless the run shows that contract
is wrong.

The in-tree Weather cases under Dropped may run in the same session. They are not a hold on this tip.

## P3. RedMonitor gates that stay open

Process. `Specs/Plans/WIP/RedMonitor_Adoption_2026-10-23.md` keeps its physical input/accessibility,
paired-performance, and native ARM64 gates open until new receipts exist. This review supplies none.

Do not check boxes in that plan from this review. Its unchecked line 17 (physical input/accessibility,
controlled performance, and release gates) stays unchecked. The 2026-10-27 integration note that native
ARM64, interactive/manual, and paired-performance gates remain open stays true. A historical checked
ARM64 matrix box in that plan is not a receipt for the post-integration native ARM64 gate.

## Dropped

These are closed. Do not reopen them as work items.

- Weather month-loop and 2026 EU/Sydney daylight-saving defects. `SystemTimeToTzSpecificLocalTimeEx` at
  `Plugins/Weather/WeatherModel.cpp` line 1088 is the right implementation. The in-tree lock, when run, is
  the month loop at `Tests/WeatherTests/Weather.Tests.Runner.cpp` lines 214-238 and the fixed W. Europe
  and AUS Eastern cases at lines 272-307 (both minutes of each 2026 transition, and a Sydney local day
  that differs from the UTC day). Running them can ride with P2. They are not a logic hold.
- yyjson 0.12 to 0.13 as a call-site defect. The changelog adds buffer writers and changes `set_int`. On
  `origin/main`, `git grep` of `yyjson_mut_set_int`, `yyjson_set_int`, `incr_read`, `iter_remove`, and
  `max_depth` across `*.cpp` and `*.h` is empty. No read sets a depth limit. No concrete wrong outcome.
- Debug viewer auto-launch in Release. `QueueOptionalDebugViewerLaunch` is inside `#if defined(_DEBUG)`
  at `Common/MonitorDiagnostics.cpp` lines 115-117. `RedXe/Main.cpp` lines 366-369 call it only for a
  bare start, and Release compiles the body out.
- Crash-handler install order dropping dumps for normal startup. Dropped. Not a work item.
- The probe `Sleep` loop violating the idle rule. `RunProbe` lines 91-94 are the specified 2-second
  barrier (`Sleep(10)` until the listener is enabled or 2000 ms have elapsed). `Main.cpp` lines 225-244
  run it and return before a window exists. Ordinary startup does not take this wait.
- Symbols ZIP PDB skew as a known Release failure. `package.ps1` and `Build/Package.psm1` fail the
  package, naming the binary, when a required PDB is missing or older than its binary.
  `Specs/Build/Build_Packaging.md` line 123 states the two-second bound. It fails closed. Not a work
  item. P2 still requires a symbols ZIP whose hash matches its sidecar; a fail-closed refusal is a failed
  package run, not this dropped finding.
- The action-name `char` versus `unsigned char` change. Dropped. Not a work item.
- `kSoakWorkingSetGrowthBudget` unused. It is the working-set check at
  `Tests/HostPluginTests/HostPlugin.Tests.Runner.cpp` lines 8696 and 8739
  (`SoakGrowthWithinBudget`). The `[[maybe_unused]]` attribute does not make that check dead.

## Closeout

DX-2, P1, and P2 can finish independently. P3 stays a standing limit on the RedMonitor plan. When the
red-team P0 items and P1-3 are done, their durable wording is in the owning contracts, and the
RedMonitor gates are still open there, move this file and
[`DeliveryRedTeam_2026-10-31.md`](DeliveryRedTeam_2026-10-31.md) to `Specs/Plans/Done/` and remove
both index rows. Do not close this annex while the red-team ship list is still `ACTIVE`.

P1's usage sentence is the `docs/` edit. Do not copy the rest of this review into `docs/`. The
implementing RedXe change adds its own `Changes/` fragment under `Specs/Core/Core_Changes.md`. This
plan-only document does not add one, and it does not edit `CHANGELOG.md`. The DxUi library change uses
DxUi's own fragment.

## Contracts the editor changes

- DxUi, on `ej/wall-compat-redxe-20261028`: `src/Controls/DxUi.Menu.cpp`,
  `Tests/Controls/DxUi.Tests.Menu.cpp`, and `Specs/UI/UI_InputAndAccessibility.md`.
- RedXe, after that commit: `Dependencies/DxUi.lock.json`, API revision staying 4.
- RedXe, one change: `RedXe/CommandLine.h`, `docs/usage.md`, and
  `Specs/UI/UI_XeneonDisplayWindowing.md`.
- No edit to `Common/MonitorDiagnostics.cpp`, `Common/MonitorDiagnostics.h`, or
  `Specs/Core/Core_RedMonitorIntegration.md` for P1. They already describe exit 2.
- No edit to `Specs/Plans/WIP/RedMonitor_Adoption_2026-10-23.md` that checks the gates in P3.
