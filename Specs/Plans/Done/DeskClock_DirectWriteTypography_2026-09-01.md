# Desk Clock DirectWrite typography refinement

Status: COMPLETE
Created: 2026-09-01
Completed: 2026-09-01
Owning specs: `Specs/Plugins/Plugins_API.md`, `Specs/Core/Core_PerformanceAndResources.md`

## Objective

Replace the fixed offline signed-distance glyph atlas with professional DirectWrite-rasterized typography while
preserving the Desk Clock's frozen `IRedXeGpuWidget` and `IRedXeScheduledWidget` interfaces, low-cadence scheduling,
bounded split-flap rendering, and settings contract.

## Interface decision

- Add no public interface, IID, host service, or rendering callback.
- Keep DirectWrite and all font resources private to `Plugins/DeskClock`.
- Use DirectWrite only during transactional device-resource initialization to build one bounded grayscale atlas and
  immutable glyph metrics. Rendering continues through the existing D3D11 pipeline.
- Keep every steady render allocation-free and preserve the three-draw static and four-draw animated bounds.

## Implementation and validation checklist

- [x] Update the normative Desk Clock contract to permit and define its private DirectWrite use.
- [x] Replace generated SDF assets with runtime DirectWrite glyph rasterization from in-box Windows font families.
- [x] Preserve native font proportions and use real glyph advances for the title-case `ddd D MMM` date.
- [x] Keep glyph creation transactional, bounded, shared per provider/device, and idempotent across device loss.
- [x] Preserve changed-tile-only split-flap behavior and refine easing/shading for clean motion at every phase.
- [x] Add deterministic WARP evidence for reference-size typography, single-digit date spacing, transition phases,
      DirectWrite loading, resource bounds, allocation-free rendering, and complete teardown.
- [x] Review the complete Desk Clock code path for correctness, ownership, architecture, and brittle tests.
- [x] Run formatting, Debug and Release x64 full tests, Release ARM64 build, skill validation, and whitespace checks.
- [x] Reconcile lasting behavior into the owning specs, mark this plan complete, move it to `Specs/Plans/Done/`, and
      remove its active-index row.

## Resource decision to measure

The completed implementation replaces the 256 KiB SDF texture with one 1 MiB grayscale DirectWrite atlas and expands
the shared visual constant buffer from 304 to 400 bytes for old/target date pen positions. The bounded 768 KiB texture
and 96-byte upload increases are accepted because reference-size and 2560×720 WARP inspection show materially cleaner
native contours and spacing. Focused Release validation measured 3.00/4.00 static/animation draws, 0.0454/0.0448 ms
CPU submission per frame, a 0.8711 ms active WARP timestamp, zero steady render allocations, and unchanged scheduling
and instance bounds.

## Validation evidence

- Repository formatting completed over all 48 C++ source/header files.
- Clean Debug x64 and clean Release x64 rebuilds passed the complete `test.ps1` matrix; the current post-review sources
  also passed Debug and Release full tests.
- Release ARM64 clean rebuild passed, followed by a successful current-source ARM64 build.
- The five-minute Release production-host soak rendered 8,502 frames, observed 8,202 active and 301 idle delays,
  retained exactly one provider, widget, and device-resource set, submitted 33,708 bounded draws, performed zero
  hidden clock work, and released the device set at shutdown.
- x64 and ARM64 import audits found no DirectWrite, WIC, or runtime shader-compiler import. Focused diagnostics prove
  that the isolated DirectWrite path built the bounded atlas and left no persistent module or COM resource.
- `validate-skills.ps1`, project XML parsing, and `git diff --check` passed.
- Complete code, shader, project, test, ownership, error-path, resource, and brittle-test review has no unresolved Desk
  Clock finding.
