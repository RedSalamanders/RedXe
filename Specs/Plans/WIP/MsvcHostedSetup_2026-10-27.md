# Hosted MSVC setup followup

Status: ACTIVE validation and merge
Date: 2026-10-27

- [x] Confirm the retained MSVC 14.52 work is already integrated; preserve its original worktree.
- [x] Port explicit GUI process waiting and diagnostic retention without changing compiler or target requirements.
- [x] Add delayed-GUI success/failure and required-tool/refusal controls to the ordinary tooling entrypoint.
- [x] Pass affected tooling, source validation and changelog folding.
- [ ] Verify available hosted checks and merge the qualified followup; preserve any external CI restriction explicitly.

This change owns CI provisioning only. Existing native, performance, physical-input and release gates retain their owners. RedTilde's separate Release keyboard-helper repair and historical qualification evidence remain in its rendering completion plan.
Local validation: the complete independent tooling runner, required-family and six-profile Release-policy checks, 12 delayed-GUI/refusal controls, ten skills and four validator regressions passed. The original setup fails the same delayed-GUI success case. PowerShell syntax, diff checks and changelog fold/check passed. No first-party native source or compiler policy changes; native/performance qualification is not relabeled by these tooling results.
