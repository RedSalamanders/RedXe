# RedXe source layout

Status: current normative repository map
Last reviewed: 2026-11-18

Where each part of RedXe lives and what each directory and top-level file owns. [`AGENTS.md`](../../AGENTS.md) keeps
the ownership boundaries between these parts (which class owns the HWND, the process plugin runtime, the device
resources, and so on), and the domain contracts listed in [`Specs/README.md`](../README.md) own behavior. Update this
map in the same change that adds, moves, or retires a directory or a file it lists.

```text
Common/PlugInterfaces/
  Factory.*        Current factory ABI, shared factory implementation, and the RedXeComObject mixin
  Host.h           Host-service COM root: data providers, frame requests, widget status, settings persist, JSONL log, and named actions (request, execute, validate)
  Widget.h         Complete generic, GPU, scheduled, child-window, raised-overlay, interactive, and network widget ABI
  Data.h           Complete source, provider, snapshot, sink, and subscription ABI
  Service.h        Headless service ABI: start/apply/host-state/stop and the host-owned device lane worker
  Action.h         Action publication ABI: descriptors, namespaces, RedXeGetActionContract, IRedXeActionPack, name grammar
Common/Actions/
  ActionTargets.*  Shared target grammars (paths, chords, points, monitors, windows, meetings) compiled into the host and every publisher
  WindowSelector.* Top-level window selection
  FluentGlyphNames.h, GlyphIcon.*  Segoe Fluent Icons name table and DirectWrite glyph rasterization shared by Logicon faces and Launcher tiles
Common/
  MonitorDiagnostics.* RedXe's only RedMonitor adapter: ETW provider, log-level mapping, --redmonitor launch and --redmonitor-probe
ThirdParty/RedMonitor/ Hash-pinned RedMonitor client header and licence (Dependencies/RedMonitor.lock.json; Verify-/Update-RedMonitorClient.ps1)
Plugins/
  RotatingTriangle/ First bundled widget-provider DLL
  GdiOrbit/         Double-buffered GDI window-widget DLL
  MatrixRain/       Production low-resource Direct3D digital-rain DLL
  5H4D3R5/          Build-time HLSL shader and demo catalog (twelve Shadertoy ports, the RedXe cosmic orb, Hillaire's sky atmosphere): single, random, or fading slideshow; ShadersSettings.h is the shared catalog/settings model; LICENSES.md the notices
  ProcessViewer/    System Data GPU viewers sharing one Direct3D DLL
  Launcher/         GPU shortcut launcher with jumbo icons, glyph tiles, taskbar-pin fallback, and action bindings
  Weather/          GPU weather widget with host-owned network lane
  AVControl/        DxUi retained controls, isolated audio/camera helper, profiles and virtual-camera source
  Logicon/          Headless MX Creative Console keypad and dialpad service (HID++ and Raw Input over the device lane, key faces, the `logicon` action namespace) plus the Debug monitor tile and the Probe tool
  Actions/Zoom/     zoom.action.dll: browser-only dedicated action DLL publishing `zoom.open` and `zoom.join` (no service or settings); no Zoom installation, SDK, OAuth, or Marketplace application registration
RedXeLauncher/
  Main.cpp          Dependency-free shim behind the winget `RedXe` alias: resolves its final path, starts the package-root RedXe.exe
Installer/
  Install-RedXe.ps1 In-package installer (copy, Start Menu, Apps entry, start at sign-in, remove); install.cmd / uninstall.cmd wrap it
  winget/templates/ RedSalamanders.RedXe manifest templates (schema 1.12.0, zip + nested portable launcher)
RedXe/
  Main.cpp          Process setup and command-line modes
  CommandLine.h     Command-line switch catalog: --help text, names Main.cpp parses through, unknown-token scanner
  Application.*     Win32 window and message-loop lifetime
  DisplayEnumeration.* The one bounded walk of the active displays: rectangles, work areas, effective DPI
  DockPlacement.h   Screen-edge dock as pure functions: app-bar plan, monitor selection, first-run edge, autohide
  NoticeWindow.h    Settings-error and action-notice window placement inside the monitor's work area
  CrashHandler.*    Fatal-process front door, local minidumps/call stacks, and prior-crash notice
  PluginHost.*      Process plugin runtime: module store, data providers, workers, JSONL log, and settings persist
  PluginManager.*   Widget providers and instance lifetime
  HostActionCatalog.* Hardcoded default action namespaces (page, widget, redxe, system, keys, mouse) and name checks
  HostActions.*     Win32 execution of the system, keys, and mouse namespaces; counters for automated hosts
  LaunchWorker.*    Host-owned launch thread: ShellExecuteExW and CreateProcessW for launches, never on the UI thread
  BundledPlugins.h  Compile-time module, widget, service, and action-namespace catalogs
  DashboardHost.*   Widget placement, frame-scheduling policy, and collect-on-exit
  Renderer.*        Direct3D 11 host resources, widget callbacks, placeholder tiles, and frames
  Settings.*        Typed yyjson persistence, paths, recovery, stamps, and persist merge
  SettingsWatcher.* Event-blocked directory notification; posts to the UI thread only
  TrayIcon.*        Notification-area icon: hidden owner window, double-click edits the settings, Edit settings/Exit menu
  FluentIcons.h     Segoe Fluent Icons glyphs and font selection for all host chrome
  app.manifest      Per-monitor-v2 DPI and Windows compatibility metadata
Tests/
  PluginContractTests/ Factory, COM identity, and rendering-IID tests
  HostPluginTests/     Hidden WARP production host/plugin integration and soak tests; embeds a Windows 8+ compatibility manifest so layered native containers are legal
  SettingsTests/       Settings, schema, stamp, watcher, and log-retention tests
  LauncherTests/       Launcher factory, pin fallback, WARP, launch, and drop tests
  WeatherTests/        Weather HTTP heap-body and small-stack overflow regression
  AVControlTests/      Synthetic AV/IPC/MF faults, native controls, camera packaging and bounded control work
  LogiconTests/        HID++ framing, image stream, settings model, faces, synthetic keypad and dialpad sessions, raw-input helpers, and the shipped service DLL
  ZoomTests/           Meeting-link grammar, the retired services entry model, and the shipped Zoom action DLL
  BuildProcessTests/   Build preflight and the bounded process helper, DxUi provenance/restore/update, the scoped-test planner and receipts, and packaging (version, ZIP rules, winget manifest, in-package installer round-trip)
Build/
  Versioning.psm1   major.minor from Common/Version.h plus the caller's build number
  Package.psm1      Portable ZIP and release symbols ZIP staging rules, CRT bundling from the build's own toolset, and the clean-extraction smoke
  CameraPackage.psm1 PE machine probe and the AV Control camera package helpers shared with the packaging
  Winget.psm1       Manifest generation from Installer/winget/templates and `winget validate`
  DxUiRestore.psm1  The DxUi pin: lock, verified sparse restore, leases, the leased and locked output root, superseded-restore cleanup, and the MSBuild the build runs
  DxUiPaths.psm1    16-hex DxUi source and output folder locators with full identity markers (shared with RedPrism and RedTilde)
  DxUiProvenance.psm1, DxUiUpdate.psm1 The DxUi module provenance record the build writes, and Update-DxUi.ps1's pin update
  RedXe.DxUi.props, RedXe.DxUi.targets, PinnedDxUiPolicy.targets The consumer import of the restored DxUi headers and archive, and the policy its archive is built with
  NativeBuildPolicy.targets The shared /Wall without /WX, debug-format and Release ICF policy (five repositories)
  BuildConsole.psm1 The shared zero-warning gate grammar and the green result with terminal links (five repositories)
  BuildPresentation.psm1, BuildOutputProcess.psm1 build.ps1's banner, log, streamed colors, diagnostic summary and warned-project reset; the running-output check
  ScopedTesting.psm1 Scope planner behind Test-Changes.ps1: Git change discovery, scope rules, evidence identity and receipts, PR delegation
  StreamingProcess.psm1 Child-process runner of build.ps1, test.ps1 and the package smoke: one argument quoter, streamed log, budget in a kill-on-close job
  TestRunEvidence.psm1 Per-run evidence directories: pinned handles, 128-bit file identities, create-only records (Build_TestRunEvidence.md)
  SkillValidation.psm1, SkillPackageIntegrity.psm1, SkillText.psm1 Skill metadata grammar, package/reference checks, and their shared strict reads and Markdown fence/code-span grammar (validate-skills.ps1)
  RedMonitor.props  Pinned RedMonitor client include path and the RequirePinnedRedMonitorClient build gate
Tools/
  Test-Changelog.ps1, Fold-Changelog.ps1, Submit-ChangelogFold.ps1 Shared Changes workflow (Core_Changes.md): validate, fold, and fold main through a pull request
  Test-ChangelogWorkflow.ps1 That workflow's shared behaviour suite
.github/actions/setup-msvc/ Hosted verification and bounded provisioning of the required MSVC 14.52 tools
Settings/
  RedXe-debug.settings.json  Shipped Debug default
  RedXe.settings.json        Shipped Release default
Specs/
  README.md         Specification authority and plan workflow
  Core/             Normative cross-cutting performance and resource behavior
  Plugins/          Normative native plugin and widget behavior
  UI/               Normative display and windowing behavior
  Plans/WIP/        Non-normative active plans
  Plans/Done/       Historical completed plans
docs/
  README.md         End-user guide index
  usage.md          Global usage
  plugins/          One page per settings-visible bundled widget
```
