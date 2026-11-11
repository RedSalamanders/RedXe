# SDK-aware AV measurement qualification

Hosted campaign 42884674473 passed its native scopes on both architectures, then
failed because measurement assumed Source restore files after an SDK build. Its
failed aggregate and skipped production remain original evidence.

The repair reopens the actual test build's SDK descriptor, checks module/role,
key/manifest and linked archive identity, and retains its live reader through all
measurement rounds. Source restore behavior remains supported. Wrapper provenance
is published only after successful final verification. Packaging continues to
require its explicit production descriptor.

Local x64 Release Source and SDK builds, all 15 native scopes for each recipe,
and six measurement scenarios with five rounds each passed. Complete independent
tooling, focused refusal/lifetime controls and static checks pass. Current SDK
production and separate package/extraction smoke pass; original hashes and local
paths are recorded in qualification.json. The Source measurement preceded only
the final wrapper-provenance publication deferral; the final SDK run includes it.

Only the remote SDK-origin lookup used a controlled local producer (6111102).
Actual readers, ABI guards, compiler, native suites, measurements and packaging
executed. This is not live GitHub-origin or ARM64 runtime acceptance. Concurrent
background work makes these functional runs unsuitable for a controlled speed or
CPU comparison. Hosted acceptance remains pending. Earlier failed fixture logs
are retained beside their corrected results.
Final PrePush is **failed and held**: the Desk Clock resource soak recorded peak
private growth 4,718,592 bytes against the unchanged 4,194,304-byte limit. Stable
resource counts, draw/scheduling checks and teardown passed. Earlier Source/SDK
runs recorded approximately 1.1 MiB and remain those original narrower results.
Attribution is unresolved; do not replace this failure with a passing retry or
claim final merge acceptance. Its original run/log and values are retained in
qualification.json. The measurement-routing repair is ready for review as a draft.

The follow-up final wrapper freezes its complete local tooling before import and
rechecks all inputs after SDK close/restoration. Cold Source and SDK helper
visibility and each helper-mutation refusal pass. Both final Source and local-origin
SDK measurements passed six scenarios with five rounds; original identities are in
[the follow-up record](final-tooling-closure.json). Complete independent tooling,
static and canonical formatting pass. A separate unchanged-budget canonical
HostPlugin sequence passed with about2.47MiB peak private growth; it remains an
attribution diagnostic and does not supersede the original failed PrePush.
