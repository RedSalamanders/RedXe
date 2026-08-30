---
name: spec-workflow
description: Create, update, or reconcile RedXe normative domain specs and manage the WIP-to-Done plan lifecycle. Use when durable product behavior, validation requirements, specifications, plans, or repository workflow change.
---

# RedXe specification workflow

Read `Specs/README.md` completely before changing specification authority or plan state. Then read the owning domain
spec completely before changing its behavior. For display, window, fullscreen, fallback, or DPI work, the owner is
`Specs/UI/UI_XeneonDisplayWindowing.md`.

## Authority

- `Specs/<Domain>/*.md` states current normative behavior.
- `AGENTS.md` and `Specs/README.md` state repository-wide policy.
- `Specs/Plans/WIP/` contains non-normative active work; index every direct plan in its `README.md`.
- `Specs/Plans/Done/` is historical and does not override a current domain spec.
- Implementation and tests are evidence and consumers. Reconcile disagreements; do not silently declare either side
  authoritative.

## Workflow

1. Identify the owning domain contract and its implementation and validation anchors.
2. Use a direct spec-plus-code change for small, settled work. Create an indexed WIP plan for multi-step, risky, or
   undecided work.
3. Keep requirements in the domain spec, not only in commentary, a WIP checklist, code, or tests.
4. Update all affected contracts and consumers in the same change when behavior changes.
5. Run the domain spec's validation plus the relevant repo skill's checks.
6. Before closeout, move every lasting discovery into the authoritative spec. Move a completed WIP plan to Done and
   remove its active index row.

Use normative language only for observable requirements, ownership boundaries, compatibility constraints, and
required validation. Keep rationale concise and avoid copying exact schemas, dependency versions, or generated data
when a machine-owned artifact can be linked instead.

For specification-only or skill-only changes, run `.\validate-skills.ps1`; it applies the bundled
`quick_validate.py` to every directory under `.agents/skills/`. Code changes still require the build and runtime checks
owned by their domain spec.
