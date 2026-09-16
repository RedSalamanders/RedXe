# Done: one dashboard background, host-owned color, per-widget override

Status: `COMPLETE`
Created: 2026-09-16
Completed: 2026-09-16
Owner: `SettingsStore` document contract, `PluginManager` provider construction, `Renderer` clear and tile fills,
every bundled widget that paints an opaque background

## Problem

Every widget paints whatever background its author picked, so tiles on one page do not match: the host clears the
canvas with `#060913`, Studio Clock defaults to `#111111`, Desk Clock to `#000000`, Matrix Rain to `#010502`,
System Data panels to `#111111`, Weather to `#0A0A0A`, Launcher to `#121214`, GdiOrbit to `#070A12`, and AV Control
takes the DxUi dark window color `#1A1A1C`. Only three plugins expose a `backgroundColor` setting, each with its own
default and its own schema entry.

## Goal

1. One document-level `backgroundColor` (default `#000000`) is the background of the whole dashboard: the host clear
   color and the background every bundled widget paints.
2. `backgroundColor` is a host-reserved key on any widget object (declare entry, flattened plugin object, use-object),
   valid for every plugin. It overrides the document color for that instance only. Plugins no longer own a
   `backgroundColor` member; the three plugin schemas that had one drop it.
3. Plugins receive the resolved color through `RedXeFactoryOptions::backgroundColor` and MUST use it wherever they
   paint an opaque background. The host additionally fills an overridden tile before `Render`, so the override is
   uniform even for widgets that paint nothing behind their content.

## Design

- `RedXeFactoryOptions` gains `uint32_t backgroundColor` (opaque ARGB, same convention as `RedXeAppearance`) at
  offset 20; the record stays 24 bytes. Host and bundled plugins rebuild together (pre-production ABI rule).
- `AppSettings::backgroundColor` holds the document color. `WidgetInstanceSettings` records an optional override.
  `EffectiveWidgetBackgroundColor` resolves override-or-document. The parser extracts `backgroundColor` from the
  merged flattened keys before plugin defaults and validators run, so declare/use merging applies unchanged.
- Persist (`ApplyFlattenedSettings`) preserves the widget object's `backgroundColor` alongside `plugin` / `use`.
  Plugin private configurations never contain the key, so collect-on-exit and interactive persist cannot freeze the
  document color into an instance.
- `PluginManager` passes the effective color in the factory options and keys the per-pass provider cache on it.
  `DashboardHost` exposes the document color and each tile's effective color; `Renderer` clears with the document
  color and `ClearView`s only the tiles whose color differs.
- A document `backgroundColor` change is a runtime change (`ActiveDashboardRuntimeEquals`) and rebuilds the page.

## Work items

All landed on 2026-09-16. Weather tests were failing at the time from unrelated in-progress Weather atlas work in the
same working tree; every other suite passed on Debug and Release x64, Release ARM64 compiled, and the Debug/Release
WARP self-tests passed with the shipped templates.

- [x] ABI field, host settings model, parser, typed validation, persist preservation, runtime equality.
- [x] PluginManager options and cache key; DashboardHost accessors; Renderer clear and override fills.
- [x] Studio Clock, Desk Clock, Matrix Rain: drop the setting, take the host color.
- [x] System Data viewers, Weather, Launcher, GdiOrbit, AV Control: take the host color.
- [x] Schema, both templates, `Core_Settings.md`, `Plugins_API.md`, `UI_Dashboard.md`, `docs/`.
- [x] Tests: SettingsTests, PluginContractTests, StudioClockTests, DeskClockTests, HostPluginTests.
- [x] `format.ps1`, Debug/Release x64 `test.ps1`, ARM64 build, `validate-skills.ps1`.

Owning contracts changed at closeout: `Specs/Core/Core_Settings.md`, `Specs/Plugins/Plugins_API.md`,
`Specs/UI/UI_Dashboard.md`, `docs/usage.md`, `docs/plugins/studio-clock.md`, `docs/plugins/desk-clock.md`,
`docs/plugins/matrix-rain.md`.
