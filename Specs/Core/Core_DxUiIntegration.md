# DxUi integration

Status: current normative consumer contract
Last reviewed: 2026-09-07

This contract owns how RedXe consumes the standalone DxUi library: the exact source pin, restore/build isolation,
which process modules link `DxUi.lib`, and the COM/POD boundary that keeps DxUi C++ objects inside those modules.
Library behavior remains in DxUi's own contracts. AV audio/camera backends, real IME/touch/screen-reader acceptance,
and matched text/UIA performance remain in [`Plugins_AVControl.md`](../Plugins/Plugins_AVControl.md) and the active
[AV plan](../Plans/WIP/RFC_Plugins_AVControl.md). RedSalamander migration is a separate DxUi HOLD plan.

## Pin and restore

`Dependencies/DxUi.lock.json` identifies `https://github.com/RedSalamanders/DxUi`, one 40-character commit, API
revision 2, and lock target `["DxUi"]`. `build.ps1` runs `restore-dxui.ps1` for the selected platform. Restore clones
that exact commit under `.build/dependencies/DxUi/source/<commit>` and isolates vcpkg/library outputs under a
fingerprint that includes commit, API revision, toolset, SDK and CRT. It never checks out, resets, or edits a sibling
`DxUi` working tree. A mismatched or dirty pin fails the consumer restore.

The pinned library (commit `2d5691fe…`) releases the cached surface
of a hidden or zero-extent `EmbeddedHost`, marks a view dirty only through control invalidation, and bounds its
solid-brush and configured-text-format caches (256 and 96 entries, reported through `EmbeddedStatistics`). Consumers
rely on `SetVisible(false)` alone to drop a hidden tile's or an unraised overlay's surface; the next sized `Prepare`
reallocates exactly one surface. The resource consequences for RedXe are owned by
[`Core_PerformanceAndResources.md`](Core_PerformanceAndResources.md).

`RedXe`, `AVControl`, and `AVControlTests` import `Build/RedXe.DxUi.props` / `.targets`, which reference
`src/DxUi.vcxproj` once. Project references keep Configuration/Platform; they do not enumerate library `.cpp` files.
Debug uses `/MDd`, Release `/MD`, matching DxUi. There is no `DxUi.dll` to stage.

## Module ownership

`AVControl.dll` owns retained control trees and the shared supplied-device `GraphicsDevice` pool for its instances.
The RedXe host links the same archive for application-side `TextInputServices` and the accessibility root. DxUi C++,
STL, exceptions, and callbacks never cross the plugin ABI. Plugins that do not import DxUi must not depend on its
headers.

## Host bridges

The host owns the HWND, swap chain, presentation, OS focus, TSF/IME association, clipboard owner window, and screen
origin. `WidgetTextClient` adapts `IRedXeTextInputWidget` to `DxUi::TextInputClient`. `AccessibilityHost` owns
`WM_GETOBJECT`, generation-bound sites, and prepared publication. AV adapts `IRedXeAccessibilitySite` to
`DxUi::EmbeddedAccessibilitySite` inside the plugin module.

Text and accessibility ABI records stay in `Widget.h`: a 9,312-byte text snapshot and a 56-byte physical placement
record. `Common/DxUiTextTransport.h` converts between those records and DxUi snapshots inside each module.

## Supported versus remaining

Synthetic host/plugin tests, WARP composition, and the relocated DxUi consumer check establish the pin and adapters.
They do not establish a real IME session, a screen-reader pass, physical touch, displayed-frame resource acceptance,
or AV hardware backends. Those remain AV release gates. Library matched-performance archives that flagged investigation
bands stay open in the AV plan; they are not waived by this contract.
