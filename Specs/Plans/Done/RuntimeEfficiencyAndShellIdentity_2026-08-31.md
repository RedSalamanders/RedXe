# Done: Runtime efficiency and Windows shell identity

Status: COMPLETE
Date: 2026-08-31
Owning specs: performance, settings, crash handling, plugins, dashboard, and XENEON windowing

## Goal

Close the concrete resource and packaging gaps found after the Matrix Rain/dashboard implementation without adding an
unused rendering ABI or weakening transactional settings.

## Completed checklist

1. [x] Replaced the scheduler's any-message redraw proxy with explicit coalesced frame invalidation. Clean static
   pages wait through unrelated input and native-child timer dispatch.
2. [x] Kept one authoritative heap-owned typed settings document, reduced reload to one candidate, adopted successful
   candidates without a full-document copy, and applied inactive-only edits without rebuilding the active dashboard.
3. [x] Moved dump-name scratch off the failing thread, bounded prepared path storage, requested a 128 KiB main-thread
   stack guarantee, and added a real `0xC00000FD` process harness beside the existing deliberate exception harness.
4. [x] Removed the three-module capacity landmine by sizing slots to the settings registry cap with a bundled-table
   compile-time check, and cached Matrix color conversion outside the frame callback.
5. [x] Embedded the conventional primary executable icon and stable version identity, then validated shell extraction
   and version fields from Debug and Release executables.
6. [x] Reduced the architecture RFC to the unresolved future data-provider, host-batching, settings-UI, migration,
   and interactive-window decisions. No speculative command-buffer IID was added.
7. [x] Reconciled every owning normative contract and the plugin-development skill.
8. [x] Completed automated validation without desktop control and moved this plan to `Done`.

## Implementation outcome

- `Application` renders static GPU content only while its explicit dirty bit is set; unrelated message dispatch no
  longer reaches the scheduler as a redraw signal. Device/occlusion recovery retains the dirty frame until a normal
  present succeeds.
- `SettingsStore` parses directly into one heap candidate. `Application` compares the active runtime projection,
  adopts unchanged-runtime documents without resource teardown, and moves successfully applied candidates into the
  authoritative slot.
- The fatal boundary now has guaranteed emergency stack and uses process storage for dump paths. Both the synthetic
  exception and true stack exhaustion write one minidump, sibling text call stack, and marker before exiting 127.
- Matrix Rain converts configured RGB values once, and plugin module storage can accept the complete bounded settings
  registry rather than today's three DLLs only.
- `RedXe.exe` uses icon group ID 1 and embeds `RedXe XENEON dashboard`, `RedXe.exe`, and `RedXe` version identity. The
  hidden app test extracts both icon sizes from the executable.
- The remaining RFC is explicitly `DECISION` and contains no duplicate shipped contract.

## Validation evidence

- `.\format.ps1`: passed; 31 C++ files formatted.
- `.\test.ps1 -Configuration Debug -Platform x64`: passed plugin, settings, host, hidden WARP, normal crash, and real
  stack-overflow crash checks.
- `.\test.ps1 -Configuration Release -Platform x64`: passed the same complete suite.
- `.\build.ps1 -Configuration Release -Platform ARM64`: passed every application, plugin, and test target.
- `.\validate-skills.ps1`: passed all 10 repository-local skills.
- `git diff --check`: passed; only repository line-ending conversion notices were reported.
- Debug x64, Release x64, and Release ARM64 executables report `RedXe XENEON dashboard`, `RedXe.exe`, `RedXe`, and
  version `1.0.0.0` through Windows version APIs.
- No desktop automation or interactive computer control was used.
