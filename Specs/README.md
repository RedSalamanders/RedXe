# RedXe specification authority and workflow

Status: current normative repository policy
Last reviewed: 2026-08-31

## Purpose

This catalog defines where RedXe requirements live and how specifications, plans, implementation, and validation stay
consistent. Location alone does not make a document authoritative.

## Authority classes

| Class | Location | Meaning |
| --- | --- | --- |
| Normative domain contract | `Specs/<Domain>/*.md` | Current intended product behavior, ownership boundaries, and required validation. |
| Normative repository policy | `AGENTS.md` and this file | Cross-domain engineering and closeout workflow. |
| Machine contract | Manifests, project files, schemas, resources, and dependency lock data | Exact data consumed by the build or application. |
| Active plan | `Specs/Plans/WIP/*.md`, excluding its `README.md` | Non-normative proposal or execution record for unfinished work. |
| Historical plan | `Specs/Plans/Done/*.md`, excluding its `README.md` | Completed sequencing and rationale; never the authority for current behavior. |
| Implementation and tests | `RedXe/`, `Plugins/`, `Tests/`, root scripts, and test entrypoints | Executable behavior and evidence that must agree with the owning normative contract. |

## Reconciliation rule

Neither prose nor implementation wins only because of its file type. When a spec, implementation, test, manifest, or
developer document disagrees:

1. Identify the owning normative domain contract and every affected consumer.
2. Inspect current runtime behavior, implementation anchors, tests, and relevant historical plans.
3. Correct the stale side when intent is mechanically clear.
4. Preserve the safe current behavior and create a `DECISION` WIP plan when product intent is unresolved.
5. Update the normative contract, implementation, tests, and developer guidance together when behavior changes.

## Change workflow

1. Read this catalog and the owning domain spec before changing durable behavior.
2. For a small, well-defined change, update the contract, implementation, and validation directly in one change.
3. For multi-step, risky, or undecided work, create one plan under `Specs/Plans/WIP/` and add it to the WIP index.
4. Treat the WIP plan as an execution aid only; it cannot override the current normative contract.
5. Run the validation required by the owning spec and the relevant repo skill.
6. Before closeout, merge every durable requirement discovered during implementation into the authoritative spec.
7. When code and tests are complete, required validation passes, and every durable requirement is persisted in the
   normative domain contracts, the plan MUST move to `Specs/Plans/Done/` and its active-index row MUST be removed. A
   completed plan MUST NOT remain under WIP.

If a behavior has no owning domain spec yet, create that spec as part of the first change that makes the behavior
durable.

## Naming

- Domain specs use `Domain_Title.md` without spaces.
- Implementation plans use a descriptive name with a date suffix: `Title_YYYY-MM-DD.md`.
- Undecided design documents use `RFC_Domain_Title.md` under `Specs/Plans/WIP/`.

## Domain entry points

| Domain | Start with |
| --- | --- |
| Build output and running-target preflight | [`Build/Build_Process.md`](Build/Build_Process.md) |
| Performance and resource consumption | [`Core/Core_PerformanceAndResources.md`](Core/Core_PerformanceAndResources.md) |
| Fatal-process capture and previous-crash diagnostics | [`Core/Core_CrashHandling.md`](Core/Core_CrashHandling.md) |
| User settings, schema, recovery, and live reload | [`Core/Core_Settings.md`](Core/Core_Settings.md) |
| Dashboard pages, grid placement, and active composition | [`UI/UI_Dashboard.md`](UI/UI_Dashboard.md) |
| UI, display, windowing, and DPI | [`UI/UI_XeneonDisplayWindowing.md`](UI/UI_XeneonDisplayWindowing.md) |
| Native plugins and generic widgets | [`Plugins/Plugins_API.md`](Plugins/Plugins_API.md) |

The canonical user JSON schema is [`Settings.schema.json`](Settings.schema.json). The build copies it beside deployed
and per-user settings files as `RedXe.settings.schema.json`.

## Specification validation

Before closing specification or skill changes:

```powershell
.\validate-skills.ps1
```

Also run the build and runtime checks named by each modified domain spec. Generated reports and build products belong
under `.build/`, never under `Specs/`.
