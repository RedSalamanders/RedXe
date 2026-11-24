# RedMonitor diagnostics

RedMonitor's [consumer contract](https://github.com/RedSalamanders/RedMonitor/blob/main/Specs/Build/Build_Consumption.md#consumer-contract)
owns the rules every application that adopts its client follows: the pinned client files and their verification, the
provider and its message format, viewer discovery and launch, the `--redmonitor-probe` command, and the validation
gates. This file keeps only RedXe's adapter facts.

## Adapter facts

- The client header and its MIT notice live under `ThirdParty/RedMonitor/`, recorded in
  [the lock](../../Dependencies/RedMonitor.lock.json). The root `Verify-RedMonitorClient.ps1` verifies them and
  `Update-RedMonitorClient.ps1 -Commit FULL_SHA` updates them with the lock.
  The root commands verify the pinned bundle under `Dependencies/RedMonitorClientTools/` before invoking it.
  The lock records the reviewed tooling commit separately from the native client commit. A client refresh preserves
  the tooling pin.
- Messages carry the `RedXe` label. Client.h, its provider definition and its link libraries stay in
  Common/MonitorDiagnostics.cpp; other translation units pass RedXeLogLevel values, which that adapter maps to
  RedMonitor severities.
- `--redmonitor` and `--redmonitor-probe` are RedXe/CommandLine.h catalog switches. Main.cpp reads them from
  argv after the help and unknown-argument checks and before settings, plugins or windows exist. Either one
  beside any other argument is command-line error 2 (`Use <switch> by itself.`). Explicit --redmonitor
  launches an installed helper and exits 0, also when one already runs. Otherwise it exits 1 with text that
  names both searched folders and RedMonitor's releases page, or the Win32 reason for a failed launch, through the
  command-line text route (console, redirected output, else a message box unless the run is unattended).
- `--redmonitor-probe` emits RedMonitor's synthetic Unicode Warning and copied UTF-8 Info diagnostics from the
  main thread after a bounded 2-second test-only readiness barrier. It creates no document/settings/window state
  and never shows a message box: exit 0 sent, 1 provider unavailable, 2 no listener or a rejected write. It prints
  one line naming that outcome to the console or redirected output (else the debugger output).
- test.ps1 covers both mixed-argument refusals, an absent viewer (skipped when either install folder holds
  RedMonitor.exe) and the probe's no-listener exit and line (skipped while a RedMonitor instance runs).
- Build tooling uses the native OS compiler host (x64 on x64 Windows, arm64 on ARM64 Windows) for dependency
  identity/restore and compilation, including when PowerShell runs under emulation. Explicit overrides are preserved
  and the caller environment is restored. Direct MSBuild carries the same defaults, checking the native WOW64
  architecture before the process architecture.
