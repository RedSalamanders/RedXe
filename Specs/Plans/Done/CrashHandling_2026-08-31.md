# Done: Crash handling implementation

Status: COMPLETE
Date: 2026-08-31
Owning domain spec: `Specs/Core/Core_CrashHandling.md`

## Goal

Add a RedSalamander-style fatal-process front door to RedXe with local minidumps, sibling text call stacks, a one-shot
next-launch notice, and an automated end-to-end crash harness that never writes to the user's normal crash directory.

## Checklist

- [x] Persist the normative crash-handling, privacy, resource, and validation contract.
- [x] Install SEH, C++ terminate, purecall, and invalid-parameter handlers before normal application startup.
- [x] Write one guarded minidump, sibling UTF-16 call-stack report, and marker under
  `%LOCALAPPDATA%\RedXe\Crashes`.
- [x] Show and consume the previous-crash marker only after a normal main window is ready.
- [x] Add an explicit crash-test path with an isolated output-directory override.
- [x] Validate dump signature, stream directory, report metadata and frames, marker contents, normal WARP smoke
  behavior, x64 Debug and Release, and ARM64 build.
- [x] Move this plan to `Specs/Plans/Done/` after the normative contract, code, tests, and validation are complete.

## Implementation outcome

- `CrashHandler` prepares the per-user path once, registers all fatal front doors, and serializes capture through an
  atomic first-writer gate.
- The fatal path dynamically loads System32 `dbghelp.dll`, writes a bounded minidump and sibling text call stack,
  then commits a UTF-16 marker. Normal startup has no static DbgHelp import and creates no crash-handling thread,
  timer, or directory.
- The top-level SEH scope is isolated from C++ object lifetime. The explicit test exception exits with code 127 and
  runs before settings, plugins, windows, or graphics initialization.
- The next normal launch consumes the marker after its main window is ready and offers only the known crash directory;
  marker content is displayed but never executed. The settings watcher starts after this modal prompt, then performs
  its normal catch-up check so settings reload cannot re-enter through the prompt's nested message loop.
- `test.ps1` rejects relative output overrides, launches the production crash path into a unique `.build` directory,
  validates the minidump header and stream directory, sibling report metadata/call stack, and exact marker path, then
  removes the artifacts.

## Validation evidence

- `.\format.ps1`: passed; 31 C++ files formatted.
- `.\test.ps1 -Configuration Debug -Platform x64`: passed all existing tests, hidden WARP smoke, invalid override,
  isolated crash exit, minidump structure, sibling call-stack report, and marker checks.
- `.\test.ps1 -Configuration Release -Platform x64`: passed the same suite using Release binaries.
- `.\build.ps1 -Configuration Release -Platform ARM64`: passed all application, plugin, and test targets.
- `.\validate-skills.ps1`: passed all 10 repository-local skills.
- Visual Studio import inspection confirmed `RedXe.exe` has no static `DBGHELP.dll` import.
- No desktop automation or interactive computer control was used.
