---
name: plugin-development
description: Implement or revise RedXe native plugin interfaces, factory exports, PluginManager loading, bundled plugin DLLs, or generic widget instances. Do not use for renderer-only D3D pipeline work.
---

# RedXe plugin development

Read `Specs/Plugins/Plugins_API.md` before changing shipped plugin behavior. Read the active plugin architecture RFC
only when work concerns interfaces that the normative contract has not frozen yet, such as data providers, advanced
D3D11 widgets, or the experimental native-window prototype. Read `Specs/Core/Core_PerformanceAndResources.md` for every plugin ABI or hot
path change. Apply `spec-workflow` when behavior or ABI changes.

Preserve these boundaries:

- Keep stable public ABI declarations under `Common/PlugInterfaces/`; host-only code belongs under `RedXe/`, and
  bundled DLL projects belong under `Plugins/`.
- Never change a published COM vtable or IID in place. Add a new interface and let the host fall back only after
  `E_NOINTERFACE` when compatibility is required.
- Public ABI records use a leading `sizeBytes` and do not expose STL containers, exceptions, WIL owners, allocators,
  or yyjson document pointers.
- Use `FactoryImpl.h` for bundled plugin factory behavior. Clear outputs before validation, return the contract's
  exact HRESULTs, and return one caller-owned reference on success.
- Resolve exports from an absolute DLL path with restricted `LoadLibraryExW` flags. Do not search the working
  directory or hot-unload a v1 module.
- Release widget and provider COM objects before optional shutdown. Keep the module mapped until process teardown.
- Keep `Widget.h` rendering-neutral. Generic widgets expose the frozen GPU or future mechanisms through distinct IIDs;
  never add a plugin's geometry, shader, command, or HWND concept to the generic root. `WindowWidget.h` remains
  experimental until its host container and validation fixture are implemented.
- GPU widgets receive a borrowed D3D11 device during setup and immediate context during rendering. They never receive
  the HWND, swap chain, or back buffer. Share immutable device resources across compatible instances.
- When the native-window RFC is implemented, window widgets receive only a borrowed host-owned child container and own
  all children they create.
- Keep `Render`, resize, and visibility callbacks allocation-free and non-blocking.
- Keep each `CreateWidget` result isolated so multiple instance IDs can animate and configure independently.
- Do not allow exceptions across exports, COM methods, callbacks, `wWinMain`, or Win32 procedures.

When a plugin project is added, include all four solution configurations, place its DLL under the host output
`Plugins` directory, and add a build-only project reference from RedXe without static import-library linkage.

Run `./format.ps1`, `./test.ps1 -Configuration Debug -Platform x64 -Rebuild`,
`./test.ps1 -Configuration Release -Platform x64 -Rebuild`, an affected ARM64 build, and `./validate-skills.ps1`.
