# DxUi: independent shared control library

Status: COMPLETE (2026-09-06) — library extraction and synthetic RedXe pin/adapters
Date: 2026-09-05
First consumer: RedXe, through AV Control
Later consumer: RedSalamander

Normative consumer behavior now lives in [`Specs/Core/Core_DxUiIntegration.md`](../../Core/Core_DxUiIntegration.md).
Real IME/touch/screen-reader, matched text/UIA performance, and AV hardware backends remain on
[`RFC_Plugins_AVControl.md`](../WIP/RFC_Plugins_AVControl.md). RedSalamander stays on DxUi's HOLD migration plan.

This file is historical sequencing. Do not treat remaining AV release gates as unfinished library extraction.

Latest pause checkpoint (2026-09-05 21:15 UTC):
[AVControl-Continuation.md](../WIP/AVControl-Continuation.md). It records the separate canonical/validation
pins, coordinated task, completed UIA integration, failed CI/performance evidence and remaining adoption gates.
The canonical DxUi task may continue independently; this pause applies to this RedXe integration task.

Working checkout: `Z:\src\DxUi`, beside `Z:\src\RedXe` and `Z:\src\RedSalamander`
Repository: [RedSalamanders/DxUi](https://github.com/RedSalamanders/DxUi), private, default branch `main`

## Purpose and authority

Create an independently buildable, tested, documented DxUi repository from RedSalamander's existing
`Common/DxUi`. It owns reusable controls, rendering, input behavior, accessibility, and host adapters. RedXe is
the first application to consume the extracted library. RedSalamander keeps its current implementation until a
separate migration is executed later.

The user approved this proposal and repository creation on 2026-09-05. The private sibling repository now owns its
library contracts and adoption plan. The user subsequently chose one `DxUi.lib`, superseding the split-target
proposal. It contains Foundation, all 26 public controls, native Win32 hosting/text/accessibility, and EmbeddedHost
with a supplied-device graphics pool. A public toggle/slider consumer and all-control gallery exercise the library.
DxUi owns source under src, public headers under include/DxUi and tests under Tests; historical attribution stays in
provenance and Git. This is the canonical home of shared development.
RedXe pins an exact DxUi commit and ships synthetic preparation/input/text/UIA adapters. Real IME/AT and AV
backends remain unfinished consumer gates. Library test evidence does not establish those product backends.

Current RedXe authority remains in [spec policy](../../README.md), [plugin API](../../Plugins/Plugins_API.md),
[dashboard](../../UI/UI_Dashboard.md), [performance](../../Core/Core_PerformanceAndResources.md),
[build process](../../Build/Build_Process.md), and [DxUi integration](../../Core/Core_DxUiIntegration.md).
The [AV Control RFC](../WIP/RFC_Plugins_AVControl.md) owns remaining product gates. DxUi's own normative contracts are listed below. Do not describe an unfinished adapter
as currently supported merely because its intended contract has been written.

## Accepted decision: one static library from a pinned source revision

Use one independent source repository and the single `src/DxUi.vcxproj` target, producing `DxUi.lib`.
Link it into `AVControl.dll`, which owns the control trees and shared graphics pool. If RedXe's OS bridge needs shared
utilities, it references this same target; it does not introduce a separately shipped service library or a second
renderer owner. Match the supported compiler, CRT, architecture and API revision 2. The lock target is `["DxUi"]`.

| Integration | Benefit | Cost / constraint | Decision |
| --- | --- | --- | --- |
| Pinned source, compiled into one `DxUi.lib` | Shared implementation and tests; ordinary C++ internally; no additional DxUi runtime DLL to deploy. | Consumer rebuild on updates; code/state can be duplicated if many DLLs link the same archive. | **Use for V1.** Reference one project once. |
| Shared `DxUi.dll` runtime | Can centralize code and resources across modules within one process. | Export/ownership ABI, version negotiation, loader/staging policy, and coordinated lifetime become a separate product surface. | Defer until measurements justify it. A future DLL needs its own reviewed interface contract. |
| Include the sibling `.cpp` files in each application project | Small initial project-file change. | Consumer flags and source inventories drift; no independent build boundary. | Reject. Consume a project/library target. |
| Copy DxUi source into each application | Each checkout is self-contained. | Permanent parallel implementations and fixes; defeats the requested shared ownership. | Reject as the ongoing integration model. A documented initial extraction is the only copy. |

Static linking does not by itself share runtime memory between executables or between independently linked DLLs.
V1 has one control runtime/cache owner inside `AVControl.dll`, shared by all its instances and staged pages. Before
a second RedXe DLL links the renderer, establish process-wide resource reuse or measure and explicitly justify
duplication under the performance contract. Do not silently introduce one device or atlas per widget.

The library is source-coordinated, with no promise of a stable exported C++ binary ABI. Public headers, static
libraries, compiler/STL, CRT mode, architecture, configuration, and feature switches must match. `/MDd` is the Debug
baseline and `/MD` the Release baseline. DxUi C++ objects, STL containers, exceptions, allocation ownership, and
callbacks containing C++ state never cross RedXe's plugin ABI. Use its size-pinned COM/POD contracts at that boundary.
This avoids cross-module ownership assumptions of the kind described in Microsoft's
[CRT boundary guidance](https://learn.microsoft.com/en-us/cpp/c-runtime-library/potential-errors-passing-crt-objects-across-dll-boundaries?view=msvc-170).

## Extraction evidence and dependency cleanup

Source inspected on 2026-09-05:

| Source anchor in RedSalamander | Existing capability / coupling | Extraction requirement |
| --- | --- | --- |
| `Common/DxUi/DxUi.h` | Button, Toggle, Slider, ComboBox, TextField, panels, menus, grid/tree, accessibility metadata. Public header imports `PlugInterfaces/Viewer.h`; namespace is `RedSalamander::DxUi`. | Move to application-independent `DxUi` namespace and public headers. Replace viewer/theme dependencies with library-owned types; translate in the application adapter. |
| `DxUi.Controls.cpp`, `DxUi.ComboBox.cpp`, text and accessibility sources | Controls paint and receive input through concrete `WindowHost&`. | Introduce a host-services/rendering abstraction; both embedded and window hosts use the same controls and behavior. Do not fork each control for RedXe. |
| `DxUi.WindowHost.cpp` | D3D11 creation, D2D context, HWND/composition swap chains, `Present`, Win32 messages, text services and UIA. | Split reusable services from window presentation. Keep the existing window-host mode available in the new repository, but RedXe uses embedded mode. |
| `Helpers.h`, `WindowMessages.h`, `Ui/AnimationDispatcher.h`, `WindowSizing.h`, `Win32CallbackHelpers.h` includes | Logging, dispatch, scheduling and window utilities reach into RedSalamander. | Inventory transitive symbols. Inject callbacks or extract only the generic helper with provenance and tests. No dependency on either application's Common library. |
| `DxUi.FrameRuntime.*` | Event/animation frame stages and timing; diagnostics use application helpers. | Retain timing semantics with injected diagnostics and externally driven embedded scheduling. |
| `DxUi.WindowHost.cpp::GetSolidBrush` and text-layout helpers | Lazy brush/cache population can allocate during painting. | Preparation must populate bounded caches before the RedXe composite callback. Existing performance is evidence to assess, not proof of RedXe compliance. |
| `Common/DxUi/DxUi.vcxproj` | Already a static library with x64/ARM64 configurations, but relies on solution-root paths and RedSalamander properties. | Make targets independent of `SolutionDir` and application property names. |
| `Tests/DxUiTests/*` and `Baselines/*` | Existing control, rendering, input, accessibility, theme and window-host regression coverage. | Extract generic tests and their bounded support dependencies; preserve baseline provenance. Application-specific tests stay in their application. |
| `LICENSE.txt` | MIT notice present in the source repository. | Carry the actual notices and inventory included code/fonts/assets in `THIRD-PARTY-NOTICES.md`; do not invent new ownership statements. |

Before copying, record the exact source commit and file inventory with hashes. If required source includes local
changes, archive a named patch/hash and record it explicitly; a branch name or dirty checkout is not a reproducible
origin. Preserve history where practical and always preserve source attribution. This inspection is not a full
transitive-dependency or license audit; both inventories are bootstrap acceptance items.

During the transition, standalone DxUi owns extracted-library development. RedSalamander's old copy remains the
current implementation for that application. Record relevant fixes and explicit backports in a migration ledger
until it ports; do not periodically copy directories in either direction. No RedSalamander source deletion, include
rewrite, project-reference change, or application rebuild is part of RedXe adoption.

## Required repository bootstrap

The sibling path is a convenient checkout layout, not a hard-coded build dependency. The repository must also build
from a different drive or a path containing spaces. Delivered library structure:

```text
DxUi/
  AGENTS.md
  README.md
  CONTRIBUTING.md
  CHANGELOG.md
  LICENSE.txt
  THIRD-PARTY-NOTICES.md
  .gitignore
  .gitattributes
  .editorconfig
  .clang-format
  .github/workflows/ci.yml
  .agents/skills/
    build-dxui/SKILL.md
    control-development/SKILL.md
    embedded-rendering/SKILL.md
    win32-host/SKILL.md
    input-accessibility/SKILL.md
    performance-resources/SKILL.md
    modern-cpp-wil/SKILL.md
    spec-workflow/SKILL.md
    consumer-integration/SKILL.md
  DxUi.sln
  Directory.Build.props
  Directory.Build.targets
  vcpkg.json
  vcpkg-configuration.json
  build.ps1
  test.ps1
  test-consumer.ps1
  gallery.ps1
  vcpkg-install.ps1
  format.ps1
  validate-skills.ps1
  validate-specs.ps1
  validate-dependencies.ps1
  Tools/README.md
  Tools/requirements-validation.txt
  Tools/validate_skills.py
  Build/DxUi.Consumer.props
  Build/DxUi.Consumer.targets
  include/DxUi/
  src/Controls/
  src/DxUi.vcxproj
  src/Foundation/
  src/Rendering/
  src/Support/
  Tests/Controls/
  Tests/Foundation/
  Tests/Embedded/
  Tests/Support/
  Tests/Controls/Baselines/
  Samples/EmbeddedControls/
  Specs/README.md
  Specs/Core/Core_Architecture.md
  Specs/Core/Core_PerformanceAndResources.md
  Specs/Build/Build_ToolchainAndConsumption.md
  Specs/UI/UI_ControlsAndLayout.md
  Specs/UI/UI_ThemeAndTypography.md
  Specs/UI/UI_InputAndAccessibility.md
  Specs/Rendering/Rendering_EmbeddedD3D11.md
  Specs/Rendering/Rendering_Win32Host.md
  Specs/Testing/Testing_Validation.md
  Specs/Plans/WIP/README.md
  Specs/Plans/WIP/BootstrapAndRedXeAdoption_2026-09-05.md
  Specs/Plans/WIP/RedSalamanderMigration.md
  Specs/Plans/Done/README.md
  provenance/source-origin.json
  provenance/migration-ledger.md
  .build/                           # ignored: outputs, reports, test scratch
```

Files must contain usable instructions and contracts, not empty placeholders. The standalone WindowHost sample
preserves and tests the future hosting path without making RedSalamander a consumer. No new application framework,
package manager or CMake build is required for this MSBuild-first extraction.

### Agent and skill contract

`AGENTS.md` is the single repository instruction source. It defines the architecture, commands, supported matrix,
ownership/error handling, hot-path rules, safe output locations, source/spec reconciliation, and exact completion
checks. Consumer repositories keep their own guidance; DxUi instructions do not silently overwrite application
policy. Optional editor/agent entrypoints only link to this file.

| Skill | Required practical content |
| --- | --- |
| `build-dxui` | Standalone and consumer builds, configurations, output paths, deterministic diagnostics, exact running-output checks. |
| `control-development` | Retained state, events, layout, minimum hit targets, disabled/pressed/pending states, source and test anchors. |
| `embedded-rendering` | Borrowed D3D device/context, offscreen targets, preparation/composition split, state isolation and device loss. |
| `win32-host` | HWND/message ownership, DPI, presentation, occlusion, teardown; unavailable responsibilities in embedded mode. |
| `input-accessibility` | Pointer capture, keyboard/focus, text/IME, UIA lifetime and screen coordinates, relevant automated/manual tests. |
| `performance-resources` | Budgets, dirty state, bounded caches, allocation counters, event-driven idle, measurement evidence. |
| `modern-cpp-wil` | Unicode, HRESULTs, WIL ownership, named exception boundaries, ABI-safe types, compiler warning policy. |
| `spec-workflow` | Authority/index rules, normative updates, WIP/Done closeout, dependency and consumer coordination. |
| `consumer-integration` | Pinned source restoration, MSBuild imports, version/CRT matching, adapters, rollback, provenance and migration ledger. |

Each skill has valid YAML front matter, focused triggers and exclusions, relative links to real repo files, and
concrete validation commands. Do not copy RedXe's AV rules or RedSalamander's file-manager rules into library skills.
The validator and its pinned Python dependencies are repository-owned, runnable in a clean CI checkout, and do not
require a user's Codex installation. Missing tooling fails with a setup command, never a false pass. The current
RedXe PyYAML problem must not be inherited as a hidden prerequisite.

### Normative contracts and plan lifecycle

| Contract | Owns |
| --- | --- |
| `Core_Architecture` | Dependency direction, public/internal API, runtime/module ownership, host services, threading and error boundaries. |
| `Core_PerformanceAndResources` | Bounded caches and queues, lazy resources, preparation/composition, idle/hidden behavior and required measurements. |
| `Build_ToolchainAndConsumption` | Library targets, source pin, CRT/SDK/toolset matrix, supported OS capabilities, output isolation and reproducible packages. |
| `UI_ControlsAndLayout` | Control semantics, layout/measurement, visible vs hidden state, hit geometry, interaction contracts and extension points. |
| `UI_ThemeAndTypography` | Tokens, fonts/glyph fallback, high contrast, reduced motion, DPI, Unicode and text clipping. |
| `UI_InputAndAccessibility` | Focus, capture, navigation, TSF/IME, UIA patterns, provider lifetimes and host bridges. |
| `Rendering_EmbeddedD3D11` | Host-owned graphics, offscreen texture lifetime, preparation, composition, device loss, no HWND/swap chain/present. |
| `Rendering_Win32Host` | The separate window-owned presentation mode, including its existing controls' behavior. |
| `Testing_Validation` | Unit/contract/image/performance suites, WARP, manual checks, evidence format, baselines and supported configurations. |

`Specs/README.md` identifies authority and labels each supported or planned capability. Bootstrap creates the initial
contracts from reviewed intended behavior and marks unimplemented embedded support explicitly. Tests and source must
then satisfy those contracts before support is advertised. Plans describe sequencing and evidence, not a second copy
of enduring requirements. Every direct WIP plan is indexed exactly once. Completed plans move to Done only after
implementation, validation and normative closeout; an application migration on HOLD is not a blocker for completing
the separate bootstrap/RedXe plan.

### Library and hosting boundaries

One archive contains controls and both hosting modes. `ControlHost` owns the retained tree services; native
WindowHost is a compatibility alias. `EmbeddedHost` configures those services for a caller-created D3D11 device and
never attaches a renderer HWND, creates a swap chain or starts a timer. Win32 mode is retained for the later
RedSalamander port. Inclusion of both modes in one archive is not a claim that no native object code will be linked.

`GraphicsDevice::Create` shares D2D/DWrite and composition state per supplied device generation. Each EmbeddedHost
owns one tree and cached surface. AV uses distinct tile/raised views bound to the same application model; this avoids
reusing hit rectangles from a different density. Its module admits their summed surface cost.

Library controls implement UI semantics, not AV operations. DxUi never enumerates audio/camera devices, switches
profiles, stores RedXe settings, or embeds XENEON-specific dimensions. The consumer supplies labels, application
state, style tokens and layout policy. Host chrome keeps its own icon source; AV uses the shared control/glyph system
with the agreed Fluent-font fallback, and does not copy browser Lucide assets into the native product.

## Reproducible consumption and builds

The initial matrix is Windows 10/11 for the embedded baseline, x64 and ARM64, Debug and Release, Unicode, the current
shared VS 2026/v145/Windows SDK baseline and `stdcpplatest`. Exact SDK, dependency versions and supported capabilities
are recorded once in machine-owned build/lock files. Windows-11-only backdrop features remain optional WindowHost
capabilities; they must not introduce unconditional imports that break the RedXe Windows 10 baseline. OS-specific
text and accessibility paths require actual verification. Preserve existing ASan configurations where supported as
additional tests, not replacements for the four required builds.

Each consumer adds a machine-readable `Dependencies/DxUi.lock.json` with source repository identity, exact commit,
required API revision, enabled targets and dependency/toolchain fingerprint. The repository is
`https://github.com/RedSalamanders/DxUi.git`; bootstrap revision
`316e39cbdfc20eea619020b5d4e31e4f395b34e5` passed all five hosted checks, including native x64/ARM64 Debug/Release.
That Foundation-only revision is provenance, not an AV-ready dependency pin. RedXe pins a tested revision that
provides its required Controls/Embedded capabilities when those gates pass. No floating `main`, branch name,
`latest`, or silently accepted dirty sibling checkout is a release dependency.

`DxUiRoot` defaults to a sibling checkout and supports an explicit absolute override. A consumer restore entrypoint
can populate an isolated pinned checkout for CI using its configured repository identity. It never resets, checks
out, cleans or overwrites the developer's existing sibling checkout. A missing root or mismatched revision fails
with an actionable message. An explicit development override may use edited source, but must record its fingerprint
and mark the result non-release; clean release validation requires the lock match.

Consumers import `Build/DxUi.Consumer.props` / `.targets`, which reference the single `src/DxUi.vcxproj` target. They do
not maintain a second list of library `.cpp` files. Public header paths and output paths resolve from the imported
file/project, never an assumed application `SolutionDir`. Standalone `.build` outputs and consumer dependency outputs
are separate. Consumer outputs use `.build/dependencies/DxUi/<fingerprint>/<platform>/<configuration>/` beneath that
consumer; the fingerprint includes source, toolchain, CRT and build flags. Parallel builds of both applications must
not share writable intermediates or vcpkg work trees.

Pin WIL and any other extracted dependency with the library; no implicit reuse of whatever headers are on the
developer's machine. Build validation detects conflicting public-header dependencies and incompatible CRT/STL flags.
Shipping the static library means there is no `DxUi.dll` to stage; the existing `AVControl.dll` remains dynamically
loaded by RedXe as a plugin. Windows/system runtime dependencies and notices still require normal packaging checks.

Library CI builds and tests without either application checkout. RedXe CI restores its pinned revision and runs
its integration tests. A library update changes the consumer lock in a reviewed change with test evidence and a
rollback to the previous pin. RedSalamander may stay on the old in-tree implementation and later its own pin; shared
source ownership does not require simultaneous releases of the two applications.

## RedXe embedded integration contract

### Graphics and lifetime

RedXe keeps ownership of the D3D11 device, immediate context, swap chain, presentation, frame pacing and device-loss
sequence. Embedded DxUi receives a borrowed device during setup, derives its D2D/DWrite resources, and owns only its
offscreen textures and cached control resources. No second D3D device, swap chain, rendering HWND or independent
render thread is allowed. RedXe already creates its device with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`.

Render D2D content into reusable BGRA textures on that same device, then composite those textures in the plugin's
D3D11 callback. This use of a D3D-backed Direct2D target is supported by the
[device-context model](https://learn.microsoft.com/en-us/windows/win32/direct2d/devices-and-device-contexts) and
[DXGI interoperability](https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-and-direct3d-interoperation-overview).
No CPU readback/upload round trip is permitted in normal UI rendering. Premultiplied alpha, target color format,
text antialiasing on transparent targets and D2D/D3D ordering must be tested; D2D and D3D must not concurrently bind
the same texture for incompatible use. End preparation before host scene composition and rebind the host target
and every required widget pipeline state.

One explicit, module-owned resource pool is keyed by D3D device identity and device generation, and shared by all
AV instances. Per-instance state includes the control tree, focus/capture, draft values and tile/raised surfaces.
Destroy instances before the pool; release all old-generation D2D/D3D references on device loss. Never infer audio
or camera state from graphics lifetime, and never reapply a profile during recovery.

### Preparation must be a supported host phase

The current GPU contract only sanctions resource/raster work on size changes. That is insufficient for changing
device names, focus, text entry and live values at an unchanged size. Do not call `Paint(WindowHost&)` from `Render`
and declare it allocation-free.

Propose a generic `RequestWidgetPreparation(instanceId)` host service plus `IRedXeGpuWidget::PrepareContent(...)`.
These names describe the intended ABI change, not existing methods. Keep declarations in current host/widget headers,
use exact `sizeBytes` and offset assertions, and rebuild all consumers. Non-DxUi GPU implementations provide a cheap
no-work implementation; this mechanism must carry no control types or AV-specific commands.

1. Input, model completion, theme/DPI or relevant geometry changes mark bounded dirty state and request preparation.
   The host coalesces requests per live instance/generation in bounded storage. Unknown/stale instances are rejected
   or discarded without waking a dead owner. Any-thread request copies only bounded identity/flags, never a tree.
2. On its UI thread, before frame construction and outside widget `Render`, the host services dirty visible instances.
   The preparation record supplies borrowed graphics context and at most two final tile/raised raster extents,
   DPI and layout/device generations. No HWND or back buffer is supplied. New requests raised during preparation
   remain pending for a later bounded dispatch; no recursive preparation loop.
3. Apply model changes, allocate/rebuild bounded size resources when needed, measure/shape changed text and paint
   dirty textures here. Stage a coherent visual/input snapshot; never publish new hit rectangles with old visuals.
   The owning contracts must explicitly permit this bounded event-driven work, including content refresh at fixed
   size, while retaining allocation-free frame construction and composition.
4. `Render` selects an already prepared layout variant and composites it. No control-tree traversal for layout,
   text shaping, resource creation, endpoint calls, logging or blocking work is allowed there. Clean textures are
   not repainted when another widget animates. Hidden/occluded surfaces defer preparation until needed again.
5. Allocation/preparation failure retains the last coherent surface or produces an explicit unavailable state; it
   must not leave stale interactive targets active. Capacity failure is bounded and does not start automatic retries.

Keep separate tile and raised density variants when both are drawn in one frame. An identical extent may share one
surface. Raise animation scales the final raised surface for its presentation only; it does not rebuild resources
at every intermediate extent. Hit testing uses the displayed transform and active interaction surface. Page swipes
with negative viewport origins do not rerasterize or reflow content. Host notifications must cover both extents,
even when the existing largest-extent notification alone would not change.

### Input, text and accessibility

RedXe's adapter supplies widget-local physical coordinates; EmbeddedHost maps them to DIPs exactly once. RedXe extends the generic input
contract to distinguish capture by a control from page-pan arbitration, and forwards Move/Up/Cancel for the captured
pointer. One accepted slider Down owns the gesture; edge navigation retains its reserved region. Cancel on hide,
capture loss, detach and invalidating geometry changes; no volume setter on cancellation.

Keyboard, focus, text and UIA are implementation gates, not optional polish. Add generic keyboard/focus/text events
to the interactive mechanism, including composition and cancellation semantics. The host owns OS focus, message
routing and any TSF/IME HWND association. The adoption work must expose reusable text services from the same
`DxUi.lib` for the host side; bounded
COM/POD transport connects it to the plugin's control tree. No top-level HWND, `DxUi::Control*`, `std::function` or
STL string crosses the RedXe ABI. Clipboard and text-service behavior use explicit host requests.

An optional accessibility mechanism exposes virtual-child providers/immutable snapshots to RedXe's UIA root.
Record interfaces, ownership, UI-thread marshalling, generation invalidation, screen-coordinate transforms, and
provider behavior after detach. A surviving assistive-technology reference must never dereference a destroyed
control or unloaded owner. Hidden controls and background modal contents leave navigation; labels, Toggle,
RangeValue, selection, text/value and live-state patterns must agree with visible confirmed state. The exact bridge
records are designed and tested in the RedXe adoption plan before AV is declared complete.

DxUi must support preview-versus-commit slider events, keyboard steps, cancellation, and externally acknowledged
values. The old `SetOnValueChanged` alone does not establish AV's commit-on-release behavior. AV commands remain in
the plugin; visual changes do not themselves mutate Windows devices. At least one real touch/IME/screen-reader
verification supplements synthetic tests.

## Performance acceptance and resource accounting

The offscreen approach has a real memory and dirty-paint cost compared with the AV RFC's original custom-instancing
idea. The choice is a reuse proposal subject to measurement, not an already approved resource regression. Record
both preparation and final composition; a single composite draw does not make the entire UI a one-draw renderer.

| Resource / scenario | Proposed acceptance rule |
| --- | --- |
| Static visible, hidden, minimized, occluded, display-off | No embedded-owned periodic timer, worker, polling, preparation, repaint or present. Host visibility rules control all work. |
| Clean composition | Zero application heap allocations, text shaping, target creation, D2D repaint or texture upload; at most one composite draw per layout viewport and one bounded geometry upload only when its transform changes. |
| Dirty updates | Coalesce to the latest bounded snapshot; measure CPU time, D2D/GPU submissions, allocations and bytes separately. No allocation growth over repeated changes. |
| Texture count | At most two resident layout surfaces per AV instance; identical layouts share. Replacement may temporarily retain one old/new pair per changed surface until publication, then releases the old one. |
| Surface memory | Compute from actual physical extents and format. One 1280×720 BGRA surface is 3,686,400 bytes (3.52 MiB); two are 7.03 MiB before driver overhead. At 2× raster dimensions, two are 28.13 MiB. Include replacement peaks rather than hiding them. |
| Provisional safety caps | 64 MiB resident surface payload and 128 MiB replacement peak per instance; 256 KiB composite dynamic buffers per instance. These are ceilings, not preallocations or normal targets. Checked arithmetic, device dimension limits and host admission must reject excess explicitly. |
| Shared resources | One device-generation pool in the AV module; initial shared cache target 8 MiB, with D2D/DWrite/driver retained overhead reported separately. Cache residency and multi-instance totals must be measured. |
| Responsive input | Pending visual acknowledgement within AV's 100 ms target; measure p50/p95 preparation/frame latency during drag and text entry on a named hardware fixture and WARP. Report results, not invented timings. |
| Multiple widgets / recovery | Test the host-supported instance bound, staged pages, repeated raise/dismiss, DPI and device loss; no proportional growth in devices, workers or immutable caches. |

At native resolution, record the total active-page plus overlay texture footprint as well as per-instance numbers.
Test 96/144/192 DPI and all AV sizes, including portrait; do not mistake logical pixels for texture pixels. Allocation
counters distinguish library-controlled work from OS/driver internals, which remain measured even when not directly
bounded by the application allocator. If the prototype misses acceptable resource or latency targets, optimize the
dirty/caching path or revise the rendering adapter with measured justification; keep the shared control model and
do not silently fork controls into AV.

## Implementation plan and exit gates

Bootstrap/RedXe and RedSalamander migration are separate plans. This proposal remains indexed in RedXe until its
own adopted scope is implemented, verified and reconciled. After bootstrap, DxUi's authoritative contracts and its
execution plan own library work; the RedXe integration plan owns application/ABI work, with explicit cross-links
and no competing checklists for the same deliverable.

| Phase | Deliverables | Exit gate |
| --- | --- | --- |
| D0 — establish repository | Sibling checkout, actual origin/pin, provenance and notices, all bootstrap files, usable AGENTS/skills/specs/plans, isolated outputs. | Skill/spec/link/dependency validation runs from a clean checkout without either application or personal Codex paths. |
| D1 — extract shared library | Dependency cleanup, public namespace/API, one library target, retained controls, Win32 sample and extracted generic tests. | Four required configurations build; control and existing WindowHost regression tests pass; no application includes/imports remain. |
| D2 — prove embedded host | Minimal independent sample renders toggle, slider, combo and editable text with synthetic state; borrowed-device lifecycle and prepared surfaces. | WARP, state isolation, alpha/text, DPI, input, dirty/clean/hidden behavior, allocation and surface accounting pass. |
| D3 — first application: RedXe | Pinned projects/imports, preparation/input/text/UIA bridges and current ABI/spec updates; a synthetic RedXe integration fixture. | Host and every bundled plugin rebuilt; WARP and real input/accessibility gates pass; no hardware AV changes in automated tests. |
| D4 — AV Control adoption | Shared DxUi controls with the reviewed responsive layout; AV model/backend gates stay in its own RFC. | AV native UX/instance/lifecycle tests and measured resource acceptance pass. DxUi completion does not falsely close audio/camera gates. |
| D5 — later RedSalamander migration | Separate HOLD plan: inventory consumers, adapt themes/services, choose a tested pin, port window hosts and plugin users incrementally, then remove the old implementation. | Run RedSalamander's required full validation and update its normative contracts; source and dependency duplication are removed only when its migration passes. |

Bootstrap acceptance checklist:

- [x] All required repository guidance and skill content exists and passes clean-checkout validators.
- [ ] Source/test/license provenance and transitive dependency inventory are complete.
- [ ] Public headers compile independently of both applications; standalone and relocated/path-with-spaces builds work.
- [ ] MSBuild source pin, overrides, wrong-revision failure, header/library mismatch and simultaneous consumer output
  isolation are tested. A development override cannot produce a clean release receipt.
- [ ] Unit/contract suites cover toggle, slider preview/commit/cancel, combo selection, text/IME, focus, UIA lifetime,
  themes, clipping, hidden state and bounded failure behavior.
- [ ] WARP fixtures cover dirty preparation vs clean composition, hostile prior state, two layout variants, negative
  origins, DPI, recovery, detach and failed resource replacement. Hardware fixtures cover relevant driver behavior.
- [ ] Release tests record five-minute idle/hidden intervals, repeated interaction/resize cycles, multi-instance
  totals and recovery memory peaks; no benchmark changes real audio/camera settings.
- [ ] x64 Debug/Release and ARM64 Debug/Release builds pass; native ARM64 behavior is tested on a recorded ARM64
  runner or explicitly remains blocked. A cross-build alone is not an ARM64 runtime pass.
- [ ] CI validates skills, spec indexes/links, dependencies, formats, unit/contract tests, WARP, smoke samples and
  performance budgets; generated evidence goes under `.build`, with commit/configuration/fixture provenance.
- [ ] RedXe current contracts, declarations, tests and build tooling reflect the supported adapter; library and
  application documentation point to each other's owners.
- [ ] Archive the completed bootstrap/RedXe plan after normative closeout. Leave the separate later migration on
  HOLD until undertaken; do not mark the library unready solely because RedSalamander has not ported yet.

## Proposal validation record

The initial proposal was documentation only. Inspected the existing DxUi control API, rendering/window ownership, project
dependencies, test inventory and source notice, and RedXe's device flags, GPU/input ABI and resource requirements.
The subsequent approved bootstrap builds the Foundation target in the new repository; neither application runtime
was changed. The proposal deliberately identifies
the preparation, touch, text/UIA and resource measurements that still need implementation evidence.

Validation on 2026-09-05:

- Both RFCs have valid relative links, no trailing whitespace and exactly one entry each in the active index.
- `git diff --check` passed for the index edit. Existing unrelated workspace changes were left in place.
- The initial sandbox validation was blocked by its Python launcher/PyYAML environment. Retrying in the normal
  user context on 2026-09-05 passed all ten RedXe skills. The tooling block is resolved.
- DxUi's separate bootstrap passed x64 Debug/Release tests and all four x64/ARM64 builds locally.
  [Hosted run 33958577659](https://github.com/RedSalamanders/DxUi/actions/runs/33958577659) passed Linux validation
  and all four native Foundation test configurations, including ARM64. Its adoption plan records the evidence.
  Existing AV mockup screenshots validate interaction design, not the proposed native adapter.


### Approved single-library implementation update

DxUi now exposes DxUi.h, Embedded.h, ControlCatalog.h, neutral ThemeColors, diagnostics and FrameRuntime headers.
Slider::SetOnChange distinguishes Preview/Commit/Cancel; AV calls Windows setters only on Commit. EmbeddedHost
converts supplied widget-local pixels to DIPs and separates Prepare from allocation-free Composite. The standalone
consumer creates its own WARP device and renders a working toggle/slider through the public API. Gallery tooling
generates every concrete control in five themes. Inherited cases have an explicit test-port inventory.

This replaces split Controls/Embedded/Win32Services/Win32Host delivery. RedXe's ABI additions, release pin, real
touch/IME/UIA acceptance and AV backend integration remain in this active plan; RedSalamander migration is later.

### Coordinated source pin, 2026-09-05

RedXe now restores `4544d3492c33c95c061894646f6a20d28e37cb4c` from DxUi `main`. The shared checkout's
documentation/workflow task published the coordinated library changes; RedXe never builds in or resets that checkout.
[Native CI 33971459949](https://github.com/RedSalamanders/DxUi/actions/runs/33971459949) passed validation and all four
x64/ARM64 Debug/Release native jobs at this exact commit, including consumer and gallery steps. The earlier Menu
owner-message-flood failure is resolved at this revision, without changing its two 800-ms assertions.
[Formatting CI 33971460024](https://github.com/RedSalamanders/DxUi/actions/runs/33971460024) also passed.

The full RedXe Debug build passed against this pin. Device and role selectors opt into the public 48-DIP popup-row
minimum. This closes the dependency's failed-native-CI blocker; it does not close RedXe IME/UIA, physical touch,
camera/audio compatibility, or application resource acceptance. Those remain in the AV/host implementation work.

### Contrast pin and text-service continuation, 2026-09-05

The lock then selected `1947a5b91beb029e9b99d71e0893c6075bbb29ca`. Its high-contrast primary buttons preserve the
exact opaque system selection pair. [Native CI 33980767827](https://github.com/RedSalamanders/DxUi/actions/runs/33980767827)
passed all four x64/ARM64 Debug/Release configurations; [formatting CI 33980767838](https://github.com/RedSalamanders/DxUi/actions/runs/33980767838)
passed. RedXe's full Debug rebuild and regression entrypoint passed with zero compiler warnings/errors against that
pin.

Further work continued on `codex/av-input-services` and later `main`: revision-checked embedded text snapshots,
composition imports, application-owned TSF/IME services, and embedded UIA. Those APIs landed in RedXe source before
the consumer lock moved.

### Text/UIA pin, 2026-09-06

The current lock selects `3208083836a89d2c3348e4389b105cf3c2b453fc` on DxUi `main`. Public headers are identical to
`26459b4b25c8573fccf049d4947a1d221f64b182`: `CancelTextInput`, `TextInputServices`, and `EmbeddedAccessibility.h`.
RedXe's in-tree AV and host adapters already called those APIs; `1947a5b` cannot compile them. This restores a
buildable consumer. DxUi closed library text/UIA APIs and synthetic tests in
`Specs/Plans/Done/EmbeddedTextServices_2026-09-05.md`; that does not close matched performance, real IME/AT,
hardware, or AV resource gates.
