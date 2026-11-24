# Crash handling specification

Status: current normative product contract
Last reviewed: 2026-10-07

## Scope

This contract owns fatal-process capture, local diagnostic artifacts, the previous-crash user notice, and the
deterministic crash harness for `RedXe.exe`. It does not define recoverable runtime errors or plugin-specific
quarantine policy.

## Fatal-process front door

`RedXe.exe` MUST install its crash handler before entering normal application startup. The following fatal entry
points MUST converge on the same best-effort artifact writer:

- the process unhandled-exception filter;
- the top-level `wWinMain` structured-exception boundary;
- `std::terminate`;
- the CRT purecall handler;
- the CRT invalid-parameter handler; and
- SIGABRT.

The top-level structured-exception scope MUST contain no C++ object whose destruction depends on stack unwinding.
Normal C++ application lifetime remains inside a separate function below that boundary. Fatal handlers MUST NOT let
exceptions escape, attempt to resume execution, or present interactive UI from the compromised process.

No fatal path returns into normal shutdown. The top-level boundary, `std::terminate`, and the CRT handlers end the
process with `TerminateProcess` and the crash exit code once the writer returns; the unhandled-exception filter
returns `EXCEPTION_EXECUTE_HANDLER` and the system ends the process. In particular the top-level boundary MUST NOT
return from `wWinMain`: CRT exit would then run static destruction, which shuts the process plugin runtime down
(service drains, worker joins, `RedXePluginShutdown`) inside the compromised process, with its locks, windows, and
widgets in whatever state the fault left them.

Before the top-level boundary runs, RedXe MUST request a 128 KiB main-thread stack guarantee with
`SetThreadStackGuarantee` so a real `EXCEPTION_STACK_OVERFLOW` retains room for bounded capture. Fatal-path dump-name
scratch MUST live in fixed process storage, not as a large automatic array on the failing thread. Prepared crash,
marker, and dump path buffers are bounded to 1,024 UTF-16 characters; an overlong override fails before crashing.

The artifact writer MUST use a process-wide atomic first-writer gate. Recursive faults return to termination.
Another faulting thread can wait on the prepared completion event for up to ten seconds without starting another
dump. Failure to locate storage, create a directory, load the system dump library, create a file, write a dump, or
flush a marker remains best-effort.

## Diagnostic artifacts

The default crash directory is `%LOCALAPPDATA%\RedXe\Crashes`. A successful capture writes:

- one timestamped `RedXe-report-*.dmp` minidump; and
- one sibling `RedXe-report-*.txt` call-stack report with the same basename as the minidump; and
- `last_crash.txt`, a UTF-16 marker containing the absolute minidump path.

The marker MUST be flushed and atomically replaced only after `MiniDumpWriteDump` and the dump flush succeed. An incomplete dump MUST be removed when that
best-effort cleanup succeeds. Dump names MUST include the process identifier so independent RedXe processes do not
select the same normal path.

The handler MUST load `dbghelp.dll` from System32 on the fatal path instead of adding normal-startup DLL search or
module-load work. Captures MUST be bounded to `MiniDumpNormal`, thread information, and unloaded-module information;
they MUST NOT deliberately include full process memory or handle data.

The UTF-16 call-stack report MUST record the exception code and address, process and thread identifiers, target
architecture, and a bounded call stack of at most 64 current-thread frames. Every frame MUST include its instruction
address. Module, symbol, source-file, and line details are best-effort and MUST be included when the System32 DbgHelp
library can resolve them from the executable directory. Symbol handling MUST NOT use a network symbol path, prompt,
or retry. Failure to create the text report MUST NOT prevent minidump marker commit or process termination.
The portable package ships no PDBs, so a user's report names modules and addresses only. Each GitHub release
publishes `RedXe-<version>-<platform>-Symbols.zip` with the exact PDBs of its shipped binaries
([Build_Packaging.md](../Build/Build_Packaging.md)); a developer symbolizes a returned minidump with it.

Crash artifacts remain local. On a healthy interactive start, retain the newest ten post-adoption dumps by write
time and their matching call-stack reports. Keep pre-adoption dumps, their reports and unrelated diagnostic files.
RedXe MUST NOT upload or transmit crash artifacts. A minidump can contain fragments of process memory, while a report contains executable addresses and local
source paths when symbols are available; both MUST be treated as potentially sensitive diagnostic data.

## Previous-crash notice

After the main window is ready on the next normal launch, RedXe MUST check for `last_crash.txt`. Self-tests, an
unattended `--screenshot` run, and the deliberate crash process MUST NOT show or consume the user's marker; it waits
for the next interactive start.

When a marker exists, RedXe MUST claim it under a delete-access handle that excludes a competing claim, then remove it
before presenting a one-shot prompt. The prompt MUST identify the saved dump when the bounded marker can be read and MUST
offer to open the crash directory. Opening the directory requires an explicit user choice; RedXe MUST NOT execute or
open a path read from the marker itself.

The titled and fullscreen windows own the prompt, which follows their show. A dock MUST NOT own it: the bar is shown
without activation and presents nothing until the frame loop runs, so an owned prompt would stand over a topmost bar
with no frame and, when it closes, activate the bar, which keeps an autohide bar revealed with the keyboard focus. For
a dock the prompt has no owner and is answered before the bar is shown (`Specs/UI/UI_Windowing.md`
"Window").

The settings directory watcher MUST start after this prompt returns. The watcher startup catch-up notification MUST
reconcile settings edits made while the prompt was open without permitting live reload to run inside the prompt's
nested message loop.

## Deterministic crash harness

`--crash-probe <directory>` MUST raise a non-continuable application exception after handler installation and before
application or plugin initialization. `--crash-test` remains available for existing callers.
`--crash-test-stack-overflow` MUST exhaust the main-thread stack to produce `0xC00000FD` through that same production
boundary. Both legacy switches require `--crash-test-directory=<absolute-path>`. Use one probe form per invocation.
The supplied path MUST name an existing isolated directory within the build root. Missing, invalid, out-of-scope and
reparse destinations MUST fail before the deliberate fault. Help, probes and unattended self-tests and captures
never enable the interactive crash store.

The repository test entrypoint MUST launch each crash mode, and the invalid-override case, through its bounded child
runner (`Invoke-RedXeStreamingProcess`, [`Build_Process.md`](../Build/Build_Process.md)) with a time budget and a log
under `.build/<Platform>/<Configuration>/`. The runner's job keeps Windows Error Reporting's dialog away, so a crash
path that stops handling its exception ends the run at once instead of holding it. Each crash mode writes into its own
unique directory beneath `.build`. The entry point verifies these results for the shared, legacy and stack-overflow forms.

- the process exits through the top-level fatal boundary with the documented nonzero crash exit code (127);
- exactly one non-empty `.dmp` exists, starts with the `MDMP` signature, and has a non-empty in-bounds stream
  directory;
- exactly one sibling UTF-16 `.txt` report exists and contains the deliberate exception code, the identifier of the
  process the runner started, the thread identifier, a `Callstack` section, at least one numbered address frame, and a
  positive bounded frame count;
- `last_crash.txt` exists and identifies that dump.

The normal mode report MUST contain `0xE000CAFE`. The stack-exhaustion report MUST contain `0xC00000FD`. The harness
MUST use process launch and filesystem inspection only. It MUST NOT require interactive desktop control or touch the
normal `%LOCALAPPDATA%\RedXe\Crashes` directory. The dump, report and marker remain in local build evidence outside runner artifact uploads.

The console store control exercises retention at zero, ten and eleven owned dumps with matching call-stack reports,
preserves legacy and unrelated files, and verifies a headless check followed by two ordinary notice claims.

## Performance and resources

Normal execution incurs fixed handler registration, four fixed 1,024-character path buffers, the
main-thread stack guarantee, and atomic state. Crash
handling MUST create no worker thread, timer, polling loop, periodic wake-up, or steady-state rendering work. Directory
creation, `dbghelp.dll` loading, symbol walking, dump/report I/O, and marker I/O occur only on a fatal path. The report
uses fixed formatting buffers, performs at most 64 frame iterations, and restricts symbol lookup to a local directory.
Previous-marker I/O and bounded retention run once per healthy interactive start.

These requirements supplement `Core_PerformanceAndResources.md`.

## Implementation anchors

- `RedXe/CrashHandler.h` and `RedXe/CrashHandler.cpp`
- `RedXe/Main.cpp`
- `RedXe/Application.cpp`
- `test.ps1`

## Required validation

Before this contract is complete:

```powershell
.\format.ps1 -Check
.\test.ps1 -Full -Configuration Debug -Platform x64
.\test.ps1 -Full -Configuration Release -Platform x64
.\build.ps1 -Configuration Release -Platform ARM64
.\validate-skills.ps1
```

The two x64 test runs MUST include the isolated real-process crash harness as well as the existing hidden WARP smoke
test. ARM64 cross-build validation is sufficient on an x64 host; running ARM64 binaries requires ARM64 Windows.

The previous-crash notice has no automated check, because self-tests MUST NOT show or consume the user's marker and
the prompt takes the foreground. A change to it requires a manual launch with a marker in the normal crash
directory: for a dock, the prompt appears with no bar on screen, and after it closes the bar appears without taking
the focus and an autohide bar collapses after its hide delay.
