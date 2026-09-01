# Native plugin interface consolidation

Status: `COMPLETE`
Created: 2026-09-01
Completed: 2026-09-01
Owner: native plugin ABI and public interface layout

## Goal

Consolidate widget declarations into `Widget.h` and data declarations into `Data.h`, then remove native-plugin
backward/forward compatibility behavior while RedXe and every plugin remain pre-production and source-coordinated.

## Selected policy

- The repository defines one current native plugin contract; host and bundled plugins are rebuilt together.
- Public records retain `sizeBytes` only as an exact stale-binary/malformed-input guard. A size mismatch is rejected;
  prefixes and future tails are not accepted.
- Factory and settings contracts have one current shape with no V1/V2 or major/minor fallback.
- Widget mechanism discovery through `QueryInterface` remains runtime capability negotiation, not version fallback.
- Public COM declarations remain sibling interfaces with controlling-`IUnknown` identity.

## Completed work

- [x] Merged GPU, scheduled, and native-window widget declarations into `Common/PlugInterfaces/Widget.h`.
- [x] Merged provider, snapshot, sink, subscription, and broker declarations into `Common/PlugInterfaces/Data.h`.
- [x] Removed obsolete split headers and every current source/project reference to them.
- [x] Replaced prefix/tail acceptance and plugin-contract version fallback with exact current-record validation.
- [x] Removed legacy module enumeration and null-plugin-ID factory paths.
- [x] Updated tests and current normative/developer guidance.
- [x] Ran formatting, Debug/Release x64 tests, Release ARM64 compilation, and skill validation.

## Validation

- `./format.ps1` — passed; 44 files formatted.
- `./test.ps1 -Configuration Debug -Platform x64 -Rebuild` — passed.
- `./test.ps1 -Configuration Release -Platform x64 -Rebuild` — passed.
- `./build.ps1 -Configuration Release -Platform ARM64 -Rebuild` — passed.
- `./validate-skills.ps1` — passed; all 10 repository skills valid.
- Final scans found no current references to the deleted public headers, native V1/V2 constants, relational
  `sizeBytes` checks, plugin-contract version fields, or direct effective-settings compatibility input.

## Result

The public header surface contains `Factory.h`, `Host.h`, `Widget.h`, and `Data.h` plus the shared factory helper.
Current records reject both smaller and larger sizes, required module enumeration has no legacy fallback, and current
provider configuration accepts only the normalized factory envelope.
