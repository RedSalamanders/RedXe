# Done: Launcher plugin

Status: `COMPLETE`
Created: 2026-09-04
Completed: 2026-09-04
Owner: bundled Direct3D launcher widget, jumbo shell-icon extraction, host pointer/drop forwarding, OLE
drop-to-settings, and settings coverage

## Goal

Ship a settings-visible **GPU** launcher tile that shows the configured shortcut collection as large icons, fills the
tile with an adaptive grid, plays a bounded 3D launch animation, then opens the target through the shell: files and
folders with their default association, and full URLs with their registered protocol. The tile accepts drag-and-drop to
append a shortcut, and a settings PNG path may replace the extracted icon.

Owning contracts (updated at closeout, not by this file alone):
[`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md),
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md),
[`../../Core/Core_Settings.md`](../../Core/Core_Settings.md),
[`../../UI/UI_Dashboard.md`](../../UI/UI_Dashboard.md),
[`../../UI/UI_XeneonDisplayWindowing.md`](../../UI/UI_XeneonDisplayWindowing.md).

Prior art for icon extraction lives in RedSalamander (`IconCache`, `IShellItemImageFactory`, `SHIL_JUMBO`). Copy the
extraction rules, not the D2D cache or folder-view architecture.

## Scope

### Included

- Bundled `Launcher.dll` GPU widget with a bounded shortcut list in widget settings.
- Host sibling `IRedXeInteractiveWidget` so GPU tiles can receive pointer and drop without a child HWND.
- Jumbo-preferring shell icon extraction, optional PNG override, GPU textures, adaptive icon grid.
- Click/tap launches with one API, `ShellExecuteExW`: files and folders use the default association; full URLs use
  the registered protocol. `CreateProcess` and `ShellExecuteW` are not used.
- Bounded 3D launch animation on the selected icon via `RequestFrame` (not a continuous-animation widget).
- Host OLE initialization and top-level `IDropTarget`; drop appends persist through `IRedXeHost::PersistWidgetSettings`.
- Host plugin-schema subset extended with bounded arrays of closed objects.
- When the authored shortcut list is empty, show up to eight apps pinned to the current user's taskbar so the tile is
  not blank. Shipped templates stay `{"shortcuts":[]}`.
- Catalog, canonical schema, both shipped templates, and focused tests.

### Excluded

- In-tile delete, reorder, labels, folders-of-shortcuts, or a settings UI.
- Native-window rendering, a plugin-owned drop HWND, or a transparent hit-test overlay.
- `IRedXeDataSource`, HTTP, or any acquisition worker.
- Raising the 4096-byte private-configuration cap or the 8192-byte factory envelope.
- Extracting from Explorer's on-disk `iconcache_*.db`.
- `CreateProcess`, `ShellExecuteW`, `runas`, elevation, `SEE_MASK_NOZONECHECKS`, or waiting for the launched process.
- Launching a bare host name with no URI scheme (`example.com` is invalid; `https://example.com` is valid).

## Decisions

These choices are settled for this plan. Implementation follows them rather than reopening them.

### 1. GPU widget, with host-forwarded input

A later 3D launch motion has to live in the same Direct3D tile that draws the icons. A GDI child cannot host that
path. Click, tap, and OLE drop therefore move onto the host: the top-level HWND already owns pointer hit-testing for
raise and page pan.

- Module: `Plugins/Launcher/Launcher.dll`
- Plugin ID: `builtin.launcher`
- Type ID: `launcher`
- Mechanisms: `IRedXeWidget` + `IRedXeGpuWidget` + `IRedXeInteractiveWidget` + `IRedXeRaisedWidget`
- Raised extent: half (`RedXeRaisedExtentHalf`)
- Flags: not continuous. No `IRedXeScheduledWidget`. Launch motion uses `IRedXeHost::RequestFrame` after each
  presented frame, the same pattern as Desk Clock flips.
- Default size 480×480, minimum 160×160.
- The widget rejects `IRedXeWindowWidget`. `GdiOrbit` remains the shipped native-window example.

Shared device resources live once per provider: embedded Shader Model 5.0 blobs, one textured-quad pipeline, one
linear sampler, immutable blend/rasterizer/depth state. Each instance owns at most eight 256×256 icon textures, one
small dynamic instance buffer, and one constant buffer. `Render` is allocation-free. `OnTargetSizeChanged` recomputes
the cell grid only. Device loss keeps CPU BGRA sources and re-uploads; it does not re-query the shell.

A swipe viewport keeps the widget's full size and may have a negative origin; `Render` must still draw.

### 2. `IRedXeInteractiveWidget` is a generic sibling

`IRedXeHost` grows no vtable. The new interface is a direct `IUnknown` child in `Widget.h`, discovered with
`QueryInterface`, with the same controlling identity as `IRedXeWidget`. It is not launcher-specific.

```text
RedXePointerEvent  (sizeBytes, pointerId, kind, phase, x, y)
  kind:  mouse / touch / pen
  phase: down / move / up / cancel
  x,y:   widget-local pixels, origin at the tile's top-left, including during raise

IRedXeInteractiveWidget
  OnPointer(event) -> S_OK consumed, S_FALSE miss
  OnDragOver(x, y) -> S_OK accept, S_FALSE refuse
  OnDragLeave()
  OnDrop(event)    -> S_OK accepted at least one item
```

`RedXeDropEvent` carries widget-local `x,y`, `itemCount` 1 through 8, and borrowed host-owned UTF-16 `target` strings
for this call only. The plugin copies what it keeps before returning. The host parses OLE formats; the plugin never
sees `IDataObject`.

Threading matches the other widget interfaces: UI thread, synchronous, non-reentrant, allocation-free. The widget MAY
call `RequestFrame` and `IRedXeHost::PersistWidgetSettings` from `OnDrop` and from `OnPointer` on a committed
click. It MUST NOT call them from `Render`, `OnTargetSizeChanged`, `SetVisible`, or `SetRaised`. `RequestFrame` from
`Render` during an in-flight launch animation is the documented exception.

Host routing:

- Hit-test uses the same topmost-widget bounds as raise.
- Mouse and pointer are forwarded in widget-local pixels. Pointer-synthesized mouse is ignored when the pointer path
  already delivered that contact.
- `S_OK` on Down/Up over an icon consumes the contact for launch and MUST NOT count toward double-activate raise.
- `S_FALSE` (empty cell, padding, empty tile) leaves raise and edge-click navigation unchanged.
- Touch/pen page pan still wins once the existing swipe threshold locks horizontal: the host sends `cancel` to the
  widget and does not launch.
- Edge-band clicks never reach the widget.
- While a widget is raised, pointer events use the overlay content rectangle as the local origin.

`Application` implements `IDropTarget` on the top-level HWND after `OleInitialize`. `DragOver` hit-tests a GPU
interactive widget and calls `OnDragOver`. Drops over a native-window child that is not a drop target are ignored
(GdiOrbit does not steal launcher drops on GPU tiles). `Drop` copies `CF_HDROP` paths and Unicode text that is a full
URL into the bounded `RedXeDropEvent` and calls `OnDrop`.

### 3. Settings own the collection

The shortcut list is the widget's closed settings object. It is not a data source and not a sidecar file.

```json
{
  "shortcuts": [
    { "target": "C:\\Docs\\report.xlsx", "iconPng": "D:\\Icons\\excel.png" },
    { "target": "https://example.com/path?q=1" }
  ]
}
```

| Member | Required after merge | Contract |
| --- | --- | --- |
| `shortcuts` | yes | Array of 0 through 8 closed objects |
| `shortcuts[].target` | yes | UTF-8 string, 1 through 512 bytes. See target kinds below |
| `shortcuts[].iconPng` | no | UTF-8 string, 0 through 260 bytes. Absolute path to a PNG. Empty or omitted means extract |

Defaults are `{"shortcuts":[]}`. The host merges omitted members from these defaults before validation. Unknown
members, non-arrays, extra item members, empty `target`, overlong strings, and more than 8 items reject the complete
candidate. Duplicate `target` values (case-insensitive Win32 path compare, exact URL compare) reject the document.
Compact settings remain ≤ 4096 bytes; a drop that would exceed that bound is refused and does not mutate the list.

Relative paths are invalid. The plugin copies strings during provider or in-place patch apply and retains no borrowed
JSON.

### 4. Launch API: `ShellExecuteExW` only

The launcher opens the same kinds of targets Explorer does: documents, folders, `.lnk`, `.url`, `.exe`, and full
URLs. The one Windows API that does all of that is **`ShellExecuteExW`**.

| API | Why it is not the launcher |
| --- | --- |
| `CreateProcessW` | Starts a PE image only. It cannot apply a file association, open a folder, follow a `.lnk` / `.url`, or dispatch a protocol URL. A `CreateProcess` path for `.exe` plus a shell path for everything else is two implementations and still misses shim, App execution alias, and “Run with” metadata that the shell applies. |
| `ShellExecuteW` | Same shell, weaker contract: `HINSTANCE` error codes instead of `GetLastError`, no `fMask`, no owner HWND flags. |
| `IApplicationActivationManager` | Store-package AUMIDs only. `ShellExecuteExW` already reaches those when the association or protocol points there. |

Classification happens once when applying settings or accepting a drop. Launch MUST NOT probe `CreateProcess` first.

Fill `SHELLEXECUTEINFOW` as follows, then call `ShellExecuteExW` on the UI thread (OLE is already initialized):

| Field | Value |
| --- | --- |
| `lpFile` | Filesystem path or full URL string |
| `lpVerb` | `nullptr` (the **default** verb, not a hard-coded `"open"`) |
| `lpDirectory` | Parent directory of a filesystem file; `nullptr` for directories and URLs |
| `lpParameters` | `nullptr` (arguments are not a settings field in this plan) |
| `nShow` | `SW_SHOWNORMAL` |
| `hwnd` | `nullptr` (GPU widgets never receive the dashboard HWND; `SEE_MASK_FLAG_NO_UI` already suppresses shell dialogs) |
| `fMask` | `SEE_MASK_FLAG_NO_UI` only |

`SEE_MASK_FLAG_NO_UI` keeps a missing-association dialog off the XENEON surface; a failed launch reports `Degraded`
with a bounded reason and does not block the UI thread. Do not set `SEE_MASK_NOCLOSEPROCESS` (no handle to wait on or
close). Do not set `SEE_MASK_NOASYNC` (do not wait for DDE). Do not set `SEE_MASK_NOZONECHECKS`. Do not request
elevation.

**Filesystem.** An absolute Win32 path (`C:\...` or `\\server\share\...`), including directories, documents, `.exe`,
`.lnk`, and `.url` files. The shell applies the default association: a `.xlsx` opens in the workbook handler, a folder
opens in Explorer, a `.lnk` is resolved, a `.url` file opens as an internet shortcut. The widget MUST NOT treat a
document as an executable.

**Full URL.** A URI with an alphabetic scheme of at least two characters followed by `:` (examples: `https://...`,
`http://...`, `file:///...`, `mailto:...`, `steam://...`). `lpFile` is the URL string so the **registered protocol**
handler runs. A string without a scheme is not a URL. The widget MUST NOT rewrite a URL into a browser executable
path.

Drop classification matches: `CF_HDROP` entries become filesystem targets; Unicode text that is a full URL becomes a
URL target. Other text is ignored.

Hidden, minimized, `--self-test`, and `HostPluginTests` MUST NOT call `ShellExecuteExW`. Tests count launches through
the plugin test contract, including kind (filesystem vs URL).

### 5. Host schema subset gains bounded arrays

The published plugin schema must stay inside the host-enforced subset. Numbered `target1`…`target8` keys are rejected
as unusable for drop append.

Extend the subset in `PluginManager` validation, `Plugins_API.md`, and tests with:

- `"array"` with `items`, `minItems`, and `maxItems`
- `items` is one closed `"object"` (same object rules as today)
- Nested arrays are forbidden

Strings stay unconstrained at schema level (no `maxLength` keyword). The plugin and `SettingsV4` still reject overlong
values. Canonical `Specs/Settings.schema.json` MAY document `maxLength` / `maxItems` because that file is the full
Draft 2020-12 host schema, not the plugin-published subset.

### 6. Drop appends; the host persists

The plugin MUST NOT write the settings file. RedXe is pre-production, so **`IRedXeHost` may grow**. Settings persist is
a host service on that vtable, not a sibling `QueryInterface`. Do not reintroduce `IRedXeSettingsEditor` /
`SettingsEdit.h`. The weather host work lands this ABI; launcher consumes it.

```text
IRedXeHost
  PersistWidgetSettings(instanceId, settingsJsonUtf8, settingsBytes)

IRedXeWidget
  CollectPersistentSettings(jsonUtf8, capacity, writtenBytes)
    S_OK   wrote a complete settings object or a mergeable subset
    S_FALSE nothing to save
```

`PersistWidgetSettings` is the anytime path: a plugin may persist **all** of its instance settings or **part** of them.
A complete object replaces the instance settings. A partial object merges the supplied members into the current
instance settings, then the host validates the **complete** object against the plugin schema and the 4096-byte compact
cap. Unknown members, overlong strings, and a merge that would exceed the cap reject the call and leave the document
unchanged.

`CollectPersistentSettings` is the exit path. The host calls it on the UI thread after `SetVisible(FALSE)` and before
`Detach`, for every live widget. `S_FALSE` is a valid answer and MUST NOT write. `S_OK` hands the same complete-or-
partial JSON to `PersistWidgetSettings`. A widget that never dirty-saves (pins-only launcher, empty triangle) returns
`S_FALSE`. The widget MUST NOT persist from inside `CollectPersistentSettings`; the host writes.

- UI thread only, synchronous, non-reentrant.
- `settingsJsonUtf8` is the plugin settings object, not the factory envelope.
- The host writes the instance's `privateConfiguration` and may atomically save the current user document.
- Round-trip of comments and insignificant whitespace is not required (same as a future settings UI).
- The watcher stamp is marked applied so this write does not rebuild the dashboard.
- The call MUST NOT destroy or detach the calling widget. The plugin updates its in-memory list first, requests one
  frame, then persists. Failure rolls back the in-memory list.
- Forbidden from device, size, visibility, raise, and `Render`.
- `--self-test` and `HostPluginTests` MUST NOT write `%LocalAppData%`; they persist only the in-memory document used by
  that process.

Document mapping uses the same appearance order as `widget.N` instance IDs. A leaf that is a declaration name is
rewritten to `{ "use": <name>, "override": { "settings": { "shortcuts": [...] } } }` so other uses of the declaration
do not change. Arrays replace in full, matching existing merge rules.

A drop of several files appends each until the 8-item or 4096-byte cap; remaining items are skipped. A duplicate
`target` is ignored for that item. `OnDragOver` may request one frame for a highlight; the widget owns no timer.
Taskbar pins shown for an empty authored list are display-only and MUST NOT be included in persist JSON.

### 7. OLE on the UI thread

RedXe currently never calls `OleInitialize` / `CoInitialize`. Shell item image factories and `RegisterDragDrop`
require OLE STA.

`Application` calls `OleInitialize` before the first plugin attach, registers the top-level HWND as the drop target,
revokes it before destroying the HWND, and calls `OleUninitialize` after dashboard teardown. Plugins MUST NOT
initialize or uninitialize COM/OLE and MUST NOT call `RegisterDragDrop`.

### 8. Extract the largest useful source, upload once

Extraction is off the render path: settings apply, drop, PNG change, and DPI does not re-extract. `Render` samples
GPU textures only.

For each shortcut, when `iconPng` is non-empty:

1. Decode the PNG with WIC (PNG container only).
2. Cap the retained CPU source on the long edge at 256 px. Larger sources are downscaled once with WIC high-quality
   scaling and the original decode is released.
3. Failure leaves that cell on the extracted shell icon when possible, otherwise a generic shell icon, and reports
   `Degraded`. Siblings still draw.

When `iconPng` is omitted, extract in this order and keep the first success:

1. `IExtractIconW::Extract` asking for 256×256 (shortcut or file parsing name).
2. `IShellItemImageFactory::GetImage` at 256×256 with `SIIGBF_BIGGERSIZEOK`. Do not use `SIIGBF_THUMBNAILONLY` as the
   primary path; executables have no thumbnail.
3. `SHGetFileInfoW` with `SHGFI_SYSICONINDEX` (not `SHGFI_ICON`) plus `IImageList::GetIcon` from `SHIL_JUMBO` (256),
   then `SHIL_EXTRALARGE`, `SHIL_LARGE`, `SHIL_SMALL`.
4. Generic associated-icon index.

Never call `SHGetFileInfo` with `SHGFI_ICON` on this path. Never read Explorer's on-disk icon cache. After a jumbo
extract, trim fully transparent padding so a 32/48 px glyph centered on a 256 canvas is not displayed as a tiny icon
in a large cell. Keep one CPU 32-bit premultiplied BGRA source per shortcut (≤ 8×256×256×4) and upload it to a
`B8G8R8A8_UNORM` (or `R8G8B8A8_UNORM`) texture. The sampler fills the cell; do not CPU-scale to the display size on
every resize.

`.lnk` extraction uses the shortcut item so the overlay and custom icon match Explorer. Launch still opens that
shortcut through the shell.

### 9. Adaptive grid fills the tile

Icons are the content. There is no label, caption, or FluentIcons chrome inside a populated cell.

```text
n = shortcut count
if n == 0: draw an empty drop target (plugin-owned hint, not host FluentIcons)
else:
  aspect = width / height
  columns = clamp(round(sqrt(n * aspect)), 1, n)
  rows = ceil(n / columns)
  cell = tile / (columns, rows), with DPI-scaled inset
  iconPx = min(cellWidth, cellHeight) - 2 * padding
  center the used cells in the tile
```

Layout is cached until size, DPI, or count changes. `Render` writes at most eight instanced quads from that cache.

### 9a. Empty list uses taskbar pins

Authored `shortcuts: []` is a live fallback, not a blank tile. On `SetVisible(TRUE)` and after settings apply, the
widget enumerates `.lnk` files in the current user's pinned-taskbar folder
(`FOLDERID_UserPinned` + `\TaskBar`), up to eight, sorted by file name. Those shortcuts are **display-only**: they are
not written to settings. `--self-test`, `HostPluginTests`, and other automated hosts set `REDXE_AUTOMATED_HOST=1` and
MUST NOT read the live taskbar; tests inject a pin directory through the launcher test contract.

A drop while the tile is showing the fallback persists only the dropped items (the pin snapshot is not copied into the
document). After that the authored list is non-empty and pins are no longer used.

If the pin folder is missing or empty, draw the empty drop hint. `Initializing` is not used for an empty authored
list. `Unavailable` is reserved for a failed GPU setup, which remains a host placeholder.

An empty or pin-filled tile still accepts drops.

### 10. Click launches with a 3D animation

Hit-test the pointer against the cached cell rectangles. A committed click or tap on a cell (up in the same cell,
not cancelled by page pan, not the second click of a double-activate) starts the launch:

1. Begin a bounded 3D animation of the selected icon (perspective tilt / rotate toward the camera and ease forward;
   siblings may dim). Duration is at most 400 ms. Exact art may be iterated; the GPU 3D transform path, eight-instance
   budget, and `RequestFrame` cadence are the contract.
2. Call `ShellExecuteExW` on the UI thread at commit with the record in decision 4, **not** delayed until the
   animation ends, and **not** from `Render`. Association and protocol start while the animation plays.
3. Each presented animation frame calls `RequestFrame`. When the ease finishes, stop requesting. Idle with a static
   grid owns no wake-up.
4. `SetVisible(FALSE)` or detach aborts the animation. If `ShellExecuteExW` already ran, it is not undone.

One launch at a time. A second click while an animation is running is ignored.

### 11. Catalog and templates

Add `builtin.launcher` / `Launcher.dll` to `kRedXeBundledPlugins` and `kRedXeBundledWidgets`. Place a real instance in
both shipped templates **in the same change that ships `Launcher.dll`**:

- Debug first page: replace one of the two Rotating Triangle leaves with Launcher so page-1 widget count stays 4.
  Until that DLL exists, keep both triangles on the Debug first page so cold start is a real composition, not a
  placeholder.
- Debug and Release galleries: add one Launcher leaf; page-2 widget count becomes 7.
- Do not place Launcher on the System page.

Shipped examples use `{"shortcuts":[]}`. Sample third-party absolute paths do not belong in templates.

Cataloguing `builtin.launcher` without `Launcher.dll` used to abort startup after the settings template was installed.
The host now treats an unmapped catalogued module as a placeholder. W1 still MUST ship the DLL before putting Launcher
on the Debug first page.

### 12. Resource bounds

| Bound | Value |
| --- | --- |
| Shortcuts per instance | 8 |
| `target` UTF-8 bytes | 512 |
| `iconPng` UTF-8 bytes | 260 |
| Source icon edge | 256 px |
| Icon textures | 8, uploaded on settings/drop/device create |
| Draws per frame | 2 (background + instanced icons) |
| Submitted icon instances | 8 |
| Launch animation | ≤ 400 ms, `RequestFrame` only |
| Timers / workers | 0 |
| OLE drop formats | `CF_HDROP` + Unicode URL text |
| Launch | `ShellExecuteExW` only; default verb; `SEE_MASK_FLAG_NO_UI`; no wait |

Steady `Render` is allocation-free. Extraction, PNG decode, and texture upload run only on settings, drop, attach, and
device create. Idle with a static list blocks in the host message loop.

## Workstreams

| ID | Item | Pass condition |
| --- | --- | --- |
| H1 | UI-thread `OleInitialize` / `OleUninitialize`; top-level `RegisterDragDrop` / revoke. | HostPluginTests: plugins never call OLE init; shutdown revokes and uninitializes. |
| H2 | Consume host persist ABI: `IRedXeHost::PersistWidgetSettings` (vtable may grow) and `IRedXeWidget::CollectPersistentSettings` (`S_FALSE` = nothing to save). No sibling editor IID. | PluginContractTests: persist is on `IRedXeHost`, not a base of `IRedXeWidget`; widgets with nothing to save return `S_FALSE`. Landed with the weather host work; launcher must not reintroduce `SettingsEdit.h`. |
| H3 | Host persist maps instance JSON into the user document: schema validate, `widget.N` leaf rewrite, atomic save, watcher stamp, no widget destroy. | SettingsTests: in-memory persist of an inline launcher leaf and of a declaration-name leaf (becomes use/override); a drop-sized object over 4096 bytes is rejected; file stamp does not retrigger apply. Partial merge of a subset of members leaves unspecified members unchanged. |
| H4 | Plugin schema subset accepts bounded arrays of closed objects; rejects nested arrays and unknown keywords. | SettingsTests / PluginManager tests: launcher schema validates; a Matrix-style object schema is unchanged; an array-of-array schema is rejected. |
| H5 | `IRedXeInteractiveWidget` plus host pointer/drop forwarding, pan-cancel, raise-exclusion on consumed clicks. | HostPluginTests: click on a launcher icon does not raise; empty padding still double-activate-raises; a horizontal swipe over the tile pans and does not launch; drop of a file and of an `https://` URL reach `OnDrop`. |
| W1 | `Launcher.dll` GPU factory, settings contract, catalog, schema `oneOf`, both templates, solution/project references. | SettingsTests catalog coverage; Debug page-1 count 4; both galleries 7; schema accepts `builtin.launcher`; widget rejects the window IID. |
| W2 | Icon extraction pipeline, PNG override, padding trim, texture upload, adaptive grid, taskbar-pin fallback, empty hint, device-loss re-upload. | LauncherTests WARP: jumbo-or-better source; PNG overrides shell; empty automated list presents; injected pin directory shows those `.lnk` files; `Render` allocates nothing; textures survive device loss without a second shell extract. |
| W3 | Target classification, `ShellExecuteExW` launch (no `CreateProcess` / `ShellExecuteW`), 3D launch animation `RequestFrame`, harness suppression. | LauncherTests: filesystem vs URL kinds; `.xlsx`-style association uses default verb not `CreateProcess`; `https://` is URL; `example.com` rejected; launch counter with zero real `ShellExecuteExW` in host tests; animation requests frames then returns to idle. |
| W4 | `OnDrop` append, cap/duplicate refusal, in-place list update then `PersistWidgetSettings`. `CollectPersistentSettings` returns `S_FALSE` when the authored list is unchanged. | LauncherTests simulated drop of a file and a URL; 9th item refused; duplicate ignored; settings object after persist round-trips; pins are not written. |
| W5 | Closeout: merge durable rules into `Plugins_API.md`, `Core_Settings.md`, `Core_PerformanceAndResources.md`, `UI_Dashboard.md` (pointer/drop routing), plugin-development / settings-store / performance / win32-windowing / direct3d11-rendering skills, AGENTS.md catalog row. | Domain specs name the GPU launcher, interactive IID, array subset, OLE host drop target, host persist (full or partial; collect-on-exit with `S_FALSE`), extraction order, association/URL launch, and idle/render rules. |

## Validation

```powershell
.\format.ps1
.\test.ps1 -Configuration Debug -Platform x64 -Rebuild
.\test.ps1 -Configuration Release -Platform x64 -Rebuild
.\build.ps1 -Configuration Release -Platform ARM64
.\validate-skills.ps1
```

LauncherTests MUST cover factory/settings rejection (unknown members, 9 items, empty target, relative path, schemeless
host name, overlong string, non-PNG override), extraction fixtures, grid layout at 160² / 480² / a wide 960×160 strip,
WARP present with two draws and eight instances, zero steady render allocations, launch counting without creating a
process, filesystem-vs-URL classification, `SHELLEXECUTEINFOW` default-verb / `SEE_MASK_FLAG_NO_UI` / no process wait, and drop
append/cap. HostPluginTests MUST cover gallery Launcher with no
`ShellExecuteExW`, interactive persist on `IRedXeHost` (not a sibling editor IID), consumed-click vs raise, swipe-over-
tile pan, and existing Matrix / System / clock soaks unchanged. SettingsTests MUST keep three-page templates, catalog
iteration, Debug page-1 count 4, and gallery count 7. Contract tests MUST prove `d3dcompiler_47.dll` is not loaded.

Interactive checks (not CI): drop a document (not an `.exe`), a `.lnk`, and a full `https://` URL onto the Debug
first-page tile; confirm jumbo rendering; tap launches with the association or protocol; the 3D motion plays; PNG
override in the JSON live-reloads the icon; a valid save from another editor still live-applies.

## Exit criteria

This plan is complete when H1–H5 and W1–W5 pass, the listed validation is green, every durable requirement lives in
the owning domain specs, this file is moved to `Specs/Plans/Done/`, and its WIP index row is removed.
