# Review hardening and local Zoom

Status: DONE (2026-09-21)

Owners: `Specs/Plugins/Plugins_API.md`, `Specs/Plugins/Plugins_Actions.md`, `Specs/Plugins/Plugins_Logicon.md`, `Specs/Plugins/Plugins_Zoom.md`, `Specs/Core/Core_PerformanceAndResources.md`, `Specs/Core/Core_Settings.md`, `Specs/UI/UI_XeneonDisplayWindowing.md`, and `Specs/Build/Build_Packaging.md`.

## Goal

Resolve the September 10–20 review findings, preserve bounded resource and idle behavior, and keep only Zoom actions that need no Marketplace application registration, Zoom installation, OAuth, or Plugin SDK. Zoom opens the web join page or an invite URL in the default browser.

## Work

| ID | Change | Evidence |
| --- | --- | --- |
| R1 | Keep a timed-out device lane, service, worker, events, and slot stable until `RunDeviceWork` returns; bound and safely drain HID I/O; cap Raw Input pumping and avoid unrelated mouse wake-ups where possible. | Stalled-lane fixture and Logicon protocol/device suite; HID and queue bounds reviewed at the call sites. |
| R2 | Refuse press-only Logicon hold actions and enforce a one-shot host hold deadline; preserve the originally pressed input across replacement, settings, page, disconnect, and shutdown. | Logicon and settings rejection tests; host replacement-hold and timer-with-no-later-action tests. |
| R3 | Make screenshot capture nonblocking on the UI thread, guard dock placement callbacks, preserve no-op reload interactions, apply dock siblings across a deferred window-kind change, and coalesce per-move GPU resize work without losing a responsive drag preview. | Dock geometry tests, application-generated WARP dock/screenshot smoke, and call-site review. |
| R4 | Preserve user settings comments and unrelated text when persisting dock thickness; show action notices without disabling the dashboard. | Settings regression tests and notice call-site review. |
| R5 | Move sky LUT drawing out of `Render` and restore the host target on all shader exits. | WARP target-state tests after size and render callbacks. |
| R6 | Keep one command-line switch catalog for the launcher and executable. | Catalog/launcher test covers every self-terminating mode. |
| R7 | Remove Zoom OAuth, Credential Manager, loopback listener, Marketplace app, SDK, and local Workplace accessibility; retain browser join actions only. | Zoom URL/contract tests and schema/templates/docs alignment. |
| R8 | Reconcile every changed domain spec and user guide, complete required Debug/Release/WARP/ARM64 validation, and close this plan. Evaluate the separate HostChrome WIP against its remaining PresentMon receipt rather than closing it on code status alone. | Build/test logs and plan closeout. |

## Constraints

- Keep the four-worker cap and zero idle polling. A timed-out lane cannot be safely abandoned by releasing only its COM object.
- The dock keeps a live resize preview; scale or throttle GPU work only when verified against appearance and latency requirements.
- Do not add a Zoom installation, Marketplace registration, client secret, OAuth token, or SDK dependency to the remaining mode.

## Closeout evidence

- A stalled device-lane fixture passed after the 3-second drain deadline and verified that Stop, COM release, and a
  replacement lane wait for `RunDeviceWork` to return. Logicon protocol/device tests and settings rejection tests
  passed; the HID completion wait is bounded and Raw Input message pumping is capped in the implementation.
- Host action tests passed for releasing replaced holds and for the one-shot 2-second deadline without a later action.
  Settings tests passed for source-preserving dock thickness edits, including escaped member names and commented
  examples. The no-op reload and sibling dock application changes were reviewed at their call sites.
- WARP contract tests passed for sky LUT target restoration after size changes and rendering. The screenshot
  command completed from the application and produced a PNG. A live bottom-dock WARP capture with a placed triangle
  produced a 3840 x 150 PNG and exited successfully. Dock geometry tests passed; drag resizes are coalesced to a
  16 ms timer and the final size is flushed on release. A physical drag was not part of this run.
- `ZoomTests` passed for the two browser actions, URL validation, and rejection of legacy settings. The shipped
  templates, schema, contracts, and user guide describe only the browser mode. The portable package contains no
  Zoom SDK.
- `test.ps1` passed in x64 Debug and x64 ASan Debug, including the sanitizer detection probe and hidden WARP smoke.
  x64 Debug, x64 Release, x64 ASan Debug, and ARM64 Debug builds completed with zero warnings and errors. ARM64
  execution was not available on the x64 validation host. `package.ps1 -Platform x64`, `validate-skills.ps1`, and
  `git diff --check` passed. The separate HostChrome plan keeps its PresentMon gate.
