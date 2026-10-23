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

The viewer is a separately packaged production Release application. Install its standalone ZIP with
Install-RedMonitor.ps1. Discovery checks only the per-user LocalAppData/Programs/RedMonitor/RedMonitor.exe,
then ProgramFiles/RedMonitor/RedMonitor.exe, and rejects debug/test-special-build or foreign metadata.
There is no PATH, sibling, generated-output or adjacent executable fallback.

A bare Debug startup may queue an asynchronous optional launch. Release startup and all nonempty command
lines (including help, tests and probes) never auto-launch the helper. Explicit --redmonitor launches an
installed helper and returns failure when absent, without creating application state. Existing viewer
lifetime and single-instance ownership remain independent; no consumer kills, waits for or upgrades it.

The hidden --redmonitor-probe command emits synthetic Unicode diagnostics through a bounded test-only
readiness barrier without document/settings/window state. Offline verification controls exercise exact,
missing, tampered, unsafe and reparse inputs with Git/network unavailable. Real cross-process transport
tests use the actual application executable. App packages carry the client MIT notice and no viewer
payload. Native platform, physical input/accessibility and paired performance gates remain separate.
ARM64 build tooling uses the same default x64 compiler host for dependency identity/restore and compilation on an x64 process, respects explicit overrides and restores the caller environment. Project evaluation carries the same default for direct MSBuild.
