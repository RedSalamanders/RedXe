# Verified hosted MSBuild selection

Status: DONE for implementation and local tooling qualification
Date: 2026-10-27

- [x] Preserve RedXe run 40510017762: setup verified 14.52.37025 but product restore selected 14.51.36231 and correctly refused.
- [x] Carry the verified installation's MSBuild into later hosted build/restore steps; preserve local caller discovery and the required compiler family.
- [x] Add the older ambient MSBuild counterfactual, actual product discovery handoff, missing MSBuild refusal and read-only no-write controls.
- [x] Pass focused and ordinary tooling plus documentation/changelog validation.
- [x] Record final delivery gates and archive the completed implementation plan.

RedPrism shares RedXe's environment/PATH-first discovery and receives the same handoff. RedTilde already discovers the same latest installation as its setup action. DxUi uses its canonical library discovery and has successful six-profile hosted evidence; RedSalamander's current hosted native matrix is also green. Existing performance, physical-input, platform and release holds remain with their owners.
The full independent tooling runner passed on the corrected source, including 192 native profiles, six pinned-archive profiles, 14 GUI/discovery/refusal cases, skills and validator regressions. The setup without a context handoff fails the new actual-discovery control. The final plan archival changes prose/indexes only. RedXe PR 51 must still pass every named candidate check before merge; prior run 40510017762 remains failed and preserved. Compiler-family guards and all native/runtime/performance requirements remain unchanged.
