# RFC: Human-first RedXe settings schema

Status: `DECISION` — non-normative product-design work
Created: 2026-08-31
Owner: RedXe settings, dashboard configuration, and future settings UI

## Purpose

Design a settings model whose structure matches the RedXe product mental model, is clear and practical for direct
JSON editing, and can later be represented faithfully by a RedXe settings UI. This RFC records product decisions;
the current version 3 behavior remains authoritative in `Specs/Core/Core_Settings.md`,
`Specs/UI/UI_Dashboard.md`, `Specs/Plugins/Plugins_API.md`, and `Specs/Settings.schema.json` until the redesign is
approved, implemented, validated, and closed out.

## Design method

Resolve one product question at a time with the product owner. After the product model is settled, this RFC will
define the proposed document shape, annotated examples, validation and error behavior, compatibility and migration,
resource bounds, implementation impact, and acceptance criteria before any normative contract or parser changes.

## Settled decisions

### 1. Authoring audience

- Direct user editing of JSON is the primary authoring experience.
- A future RedXe settings UI is the next authoring surface and must be able to represent the same model without
  changing its meaning.
- Field names, nesting, defaults, diagnostics, and examples therefore prioritize human comprehension while remaining
  deterministic and strictly validatable.

### 2. Document scope

- One settings document represents one physical XENEON display and the dashboard configured for that display.
- The document is not an installation-wide container for multiple displays.
- A settings document is portable between compatible XENEON displays; it is not permanently bound to a hardware
  serial number or Windows device-instance identity.
- One RedXe process configures one XENEON display. Simultaneous multi-display configuration is outside this schema's
  scope.
- An application launch parameter may select a dedicated settings file for that process. The selected document still
  represents the one display targeted by the process.
- Behavior when no dedicated file is supplied remains to be decided.

## Open decision sequence

1. Decide default-file selection when no dedicated launch path is supplied.
2. Define the user's top-level mental model and common editing tasks.
3. Decide identity, references, ordering, and reuse semantics.
4. Define plugin discovery, enablement, widget types, and plugin-owned configuration.
5. Define dashboards, pages, layouts, placements, and active selection.
6. Define defaults, optional fields, comments, validation diagnostics, recovery, compatibility, and migration.
7. Establish explicit capacity and resource bounds without exposing implementation-shaped complexity to users.
8. Validate the design with representative hand-authored documents and future-UI round trips.

## Closeout condition

This RFC remains non-normative while decisions are unresolved. An approved design requires an implementation plan
that updates every affected normative contract, the canonical schema, templates, parser, runtime consumers, and
tests. Once no decision remains here, repository workflow requires this RFC to move to `Specs/Plans/Done/`.
