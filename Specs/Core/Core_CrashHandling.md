# Crash handling specification

Status: current normative product contract
Last reviewed: 2026-08-31

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
- the CRT purecall handler; and
- the CRT invalid-parameter handler.

The top-level structured-exception scope MUST contain no C++ object whose destruction depends on stack unwinding.
Normal C++ application lifetime remains inside a separate function below that boundary. Fatal handlers MUST NOT let
exceptions escape, attempt to resume execution, or present interactive UI from the compromised process.

Before the top-level boundary runs, RedXe MUST request a 128 KiB main-thread stack guarantee with
`SetThreadStackGuarantee` so a real `EXCEPTION_STACK_OVERFLOW` retains room for bounded capture. Fatal-path dump-name
scratch MUST live in fixed process storage, not as a large automatic array on the failing thread. Prepared crash,
marker, and dump path buffers are bounded to 1,024 UTF-16 characters; an overlong override fails before crashing.

The artifact writer MUST use a process-wide atomic first-writer gate. Recursive or concurrent fatal paths MUST return
to termination without starting another dump. Failure to locate storage, create a directory, load the system dump
library, create a file, write a dump, or flush a marker MUST remain best-effort and MUST NOT delay process termination
with retries.

## Diagnostic artifacts

The default crash directory is `%LOCALAPPDATA%\RedXe\Crashes`. A successful capture writes:

- one timestamped `RedXe-*.dmp` minidump; and
- one sibling `RedXe-*.txt` call-stack report with the same basename as the minidump; and
- `last_crash.txt`, a UTF-16 marker containing the absolute minidump path.

The marker MUST be committed only after `MiniDumpWriteDump` succeeds. An incomplete dump MUST be removed when that
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

Crash artifacts remain local. RedXe MUST NOT upload, transmit, or automatically delete minidumps or call-stack
reports. A minidump can contain fragments of process memory, while a report contains executable addresses and local
source paths when symbols are available; both MUST be treated as potentially sensitive diagnostic data.

## Previous-crash notice

After the main window is ready on the next normal launch, RedXe MUST check for `last_crash.txt`. Self-tests and the
deliberate crash process MUST NOT show or consume the user's marker.

When a marker exists, RedXe MUST remove it before presenting a one-shot prompt so another interrupted launch does not
repeat the notice indefinitely. The prompt MUST identify the saved dump when the bounded marker can be read and MUST
offer to open the crash directory. Opening the directory requires an explicit user choice; RedXe MUST NOT execute or
open a path read from the marker itself.

The settings directory watcher MUST start after this prompt returns. The watcher startup catch-up notification MUST
reconcile settings edits made while the prompt was open without permitting live reload to run inside the prompt's
nested message loop.

## Deterministic crash harness

`--crash-test` MUST raise a non-continuable application exception after handler installation and before application
or plugin initialization. `--crash-test-stack-overflow` MUST exhaust the main-thread stack to produce
`0xC00000FD` through that same production boundary. `--crash-test-directory=<absolute-path>` MAY override the artifact
directory only for either deliberate crash invocation. An invalid override MUST fail without deliberately crashing
or falling back to the user's normal crash directory.

The repository test entrypoint MUST launch each crash mode into its own unique directory beneath `.build`, then verify
all of the following for both modes:

- the process exits through the top-level fatal boundary with the documented nonzero crash exit code;
- exactly one non-empty `.dmp` exists, starts with the `MDMP` signature, and has a non-empty in-bounds stream
  directory;
- exactly one sibling UTF-16 `.txt` report exists and contains the deliberate exception code, process/thread
  metadata, a `Callstack` section, at least one numbered address frame, and a positive bounded frame count;
- `last_crash.txt` exists and identifies that dump; and
- the isolated crash directory is removed after validation.

The normal mode report MUST contain `0xE000CAFE`; the stack-exhaustion report MUST contain `0xC00000FD`. The harness
MUST use process launch and filesystem inspection only. It MUST NOT require interactive desktop control, touch the
normal `%LOCALAPPDATA%\RedXe\Crashes` directory, or retain generated crash artifacts.

## Performance and resources

Normal execution incurs only fixed handler registration, three fixed 1,024-character prepared path buffers, the
main-thread stack guarantee, and atomic state. Crash
handling MUST create no worker thread, timer, polling loop, periodic wake-up, or steady-state rendering work. Directory
creation, `dbghelp.dll` loading, symbol walking, dump/report I/O, and marker I/O occur only on a fatal path. The report
uses fixed formatting buffers, performs at most 64 frame iterations, and restricts symbol lookup to a local directory.
Previous-marker I/O occurs once after a normal main window becomes ready.

These requirements supplement `Core_PerformanceAndResources.md`.

## Implementation anchors

- `RedXe/CrashHandler.h` and `RedXe/CrashHandler.cpp`
- `RedXe/Main.cpp`
- `RedXe/Application.cpp`
- `test.ps1`

## Required validation

Before this contract is complete:

```powershell
.\format.ps1
.\test.ps1 -Configuration Debug -Platform x64
.\test.ps1 -Configuration Release -Platform x64
.\build.ps1 -Configuration Release -Platform ARM64
.\validate-skills.ps1
```

The two x64 test runs MUST include the isolated real-process crash harness as well as the existing hidden WARP smoke
test. ARM64 cross-build validation is sufficient on an x64 host; running ARM64 binaries requires ARM64 Windows.
