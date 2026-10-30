# RedMonitor diagnostics

RedXe uses only the immutable client header and MIT notice recorded in
[the lock](../../Dependencies/RedMonitor.lock.json). The files live under ThirdParty/RedMonitor.
Normal builds verify their SHA-256 values through Verify-RedMonitorClient.ps1. They perform no
RedMonitor clone, download, compilation or executable staging. Direct MSBuild uses the same verifier.
Only an explicit Update-RedMonitorClient.ps1 -Commit FULL_SHA updates the two files and lock together.

The EXE owns one provider before workers start and through their teardown. Messages carry the RedXe
label, mapped severity, counted Unicode text, copied PID/TID and FILETIME through Client.h. The wire
GUID, event name and original field types remain compatible; Application is an additive field.
Disabled writes return before conversion, formatting or heap allocation. The 16000 UTF-16-unit bound
includes the label. Existing local/debug/crash and dedicated performance-provider policy remains intact.
No SDK owner crosses a plugin ABI; host-forwarded plugin text is copied before publication.
Client.h, its provider definition and its link libraries stay in Common/MonitorDiagnostics.cpp; other
translation units pass RedXeLogLevel values, which that adapter maps to RedMonitor severities.

The viewer is a separately packaged production Release application. Install its standalone ZIP with
Install-RedMonitor.ps1. Discovery checks only the per-user LocalAppData/Programs/RedMonitor/RedMonitor.exe,
then ProgramFiles/RedMonitor/RedMonitor.exe, and rejects debug/test-special-build or foreign metadata.
There is no PATH, sibling, generated-output or adjacent executable fallback.

A bare Debug startup (no arguments) may queue an asynchronous optional launch. Release startup and every
nonempty command line (including help, tests and probes) never auto-launch the helper.
`--redmonitor` and `--redmonitor-probe` are RedXe/CommandLine.h catalog switches. Main.cpp reads them from
argv after the help and unknown-argument checks and before settings, plugins or windows exist. Either one
beside any other argument is command-line error 2 (`Use <switch> by itself.`). Explicit --redmonitor
launches an installed helper and exits 0, also when one already runs. Otherwise it exits 1 with text that
names both searched folders and RedMonitor's releases page, or the Win32 reason for a failed launch, through the
command-line text route (console, redirected output, else a message box unless the run is unattended). Existing viewer lifetime and
single-instance ownership remain independent; no consumer kills, waits for or upgrades it.

`--redmonitor-probe` emits RedMonitor's synthetic Unicode Warning and copied UTF-8 Info diagnostics from the
main thread after a bounded 2-second test-only readiness barrier. It creates no document/settings/window
state and never shows a message box: exit 0 sent, 1 provider unavailable, 2 no listener or a rejected write.
It prints one line naming that outcome to the console or redirected output (else the debugger output).
RedMonitor's real cross-process transport test runs it against the actual application executable.
test.ps1 covers both mixed-argument refusals, an absent viewer (skipped when either install folder holds
RedMonitor.exe) and the probe's no-listener exit and line (skipped while a RedMonitor instance runs).
Offline verification controls exercise exact, missing, tampered, unsafe and reparse inputs with Git/network
unavailable. App packages carry the client MIT notice and no viewer payload. Native platform, physical
input/accessibility and paired performance gates remain separate.
Build tooling uses the native OS compiler host (x64 on x64 Windows, arm64 on ARM64 Windows) for dependency identity/restore and compilation, including when PowerShell runs under emulation. Explicit overrides are preserved and the caller environment is restored. Direct MSBuild carries the same defaults, checking the native WOW64 architecture before the process architecture.
