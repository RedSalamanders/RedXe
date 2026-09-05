# Weather plugin and host-owned network lane

Status: `ACTIVE`
Created: 2026-09-04
Owner: plugin host network lane, bundled Direct3D weather widget, and settings coverage

## Goal

Ship a settings-visible Direct3D weather widget that shows current conditions, a multi-day forecast, and official
alerts when they exist, with automatic or manual location and display-unit choices. Before that widget exists, this
plan decides host-owned network **scheduling**, cancellation, and shutdown so the weather DLL never owns a polling
thread and never performs network I/O from `Render`.

This is the dated implementation plan required by architecture gate 1 in
[`PluginDashboardRemainingCloseout_2026-09-02.md`](PluginDashboardRemainingCloseout_2026-09-02.md) for **outbound HTTP
used by bundled widgets**. It does not close that gate for push providers, schema-selected sources, or a general
network `IRedXeDataSource`.

Owning contracts (updated at closeout, not by this file alone):
[`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md),
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md),
[`../../Core/Core_Settings.md`](../../Core/Core_Settings.md).

Historical context:

- [`../Done/RFC_Plugins_XeneonDashboardArchitecture.md`](../Done/RFC_Plugins_XeneonDashboardArchitecture.md) — local
  pull is the shipped data mechanism; network policy was deferred.
- [`../Done/SystemDataPlugin_2026-08-31.md`](../Done/SystemDataPlugin_2026-08-31.md) — host-owned acquisition worker,
  no plugin threads, `CollectSnapshots` is synchronous and local.
- [`../Done/SystemDataViewers_2026-09-02.md`](../Done/SystemDataViewers_2026-09-02.md) — GPU scheduled widgets, adaptive
  density, sample-driven `RequestFrame`, condition color by intent.

## Scope

### Included

- Host-owned **network lane**: one serial worker that runs plugin-owned fetch, with cancellation and shutdown drain.
- Bundled GPU weather widget with location, units, forecast, alerts, adaptive layout, and condition/alert color.
- vcpkg `curl` with `ssl` only, linked by `Weather.dll` and copied beside that DLL.
- Parser, layout, WARP, settings, and host integration tests that never call live weather endpoints.

### Excluded

- `IRedXeHttpService` or any host URL/body ABI. Curl is not linked into `RedXe.exe`.
- WinRT, C++/WinRT, `windowsapp.lib`, `RoGetActivationFactory`, and the Windows Location COM stack (`locationapi.lib`
  / `CLSID_Location`). None of these may be linked or loaded for weather.
- API keys, tokens, or accounts: no WeatherAPI, OpenWeather, Météo-France Vigilance API, or compiled secrets.
- Open-Meteo hosted free endpoint (non-commercial only).
- IP geolocation (discloses the user IP to a third party and typical free endpoints are non-commercial).
- Push/`IRedXeDataSource` network datasets, WebView, or interactive networked child windows.
- Host primitive batching, ABI freeze, or settings migration beyond adding this plugin's closed object.

## Decisions

These choices are settled for this plan. Implementation follows them rather than reopening them.

### 1. Why not `IRedXeHttpService` — plugin-owned curl, host-owned lane

The non-negotiable host job is **when** HTTP runs, **that it can be cancelled**, and **that it does not share the
local System Data worker**. `CollectSnapshots` on that worker already has a five-second hang budget. HTTP there would
stall every local dataset.

A public `IRedXeHttpService` (host Submit/cache/User-Agent) was rejected for the first consumer:

| Host HTTP ABI | Plugin-owned curl on a host lane |
| --- | --- |
| `libcurl` maps with every `RedXe.exe`, including Matrix-only pages | `libcurl` maps only when `Weather.dll` is loaded |
| New IID, records, and tests before any widget exists | One thin sibling IID: the host calls into the widget |
| Shared cache/UA across future HTTP plugins | Cache/UA live in the weather module until a second consumer exists |

`Core_PerformanceAndResources.md` requires lazy creation of expensive resources. Curl, Schannel, and a response cache
are expensive. They MUST NOT become process-wide because one gallery tile might use them.

**If a second HTTP plugin ships**, the reusable host piece is already this plan's network lane (one serial worker,
cancel, shutdown, offline switch). That is the factorization that pays immediately. Extracting a shared HTTP client
and moving curl into the host is a later plan, and only after two consumers and a measurement that one mapping beats
two delay-loaded plugin mappings. Two plugins both delay-load the same `libcurl.dll` still share one image via the
Windows loader; `curl_global_init` must then be process-once (host `PluginHost` may call it lazily when the first
network widget appears, without linking curl itself — or the first weather fetch uses a plugin-local `call_once`
documented as unsafe if a second curl plugin inits independently; the follow-on extraction owns that).

Weather is **not** an `IRedXeDataSource`. Location and units are per widget instance. The process-global one-source-
per-provider-ID model cannot carry two cities without encoding settings into dataset IDs.

The widget exposes sibling **`IRedXeNetworkWidget`**. The host calls `RunNetworkWork(cancelEvent)` only on the
network worker, only while the widget is visible, and only when network access is enabled. Inside that call the
plugin MAY use curl, parse JSON, and copy a bounded snapshot. It MUST return promptly when `cancelEvent` is signaled.
It MUST NOT touch Direct3D. It MAY call `IRedXeHost::RequestFrame` before returning. It MUST NOT `Submit` into the
host, create a thread, or wait on the UI thread.

`IRedXeHost`'s vtable MAY grow while RedXe is pre-production. Do not add a sibling `QueryInterface` only to avoid
growing it. `IRedXeNetworkWidget` is a direct `IUnknown` child, not a base of `IRedXeWidget`, because it is a widget
work mechanism rather than a host service. Widget settings persist is `IRedXeHost::PersistWidgetSettings` plus
`IRedXeWidget::CollectPersistentSettings`; do not reintroduce `IRedXeSettingsEditor`.

### 2. One serial network worker

`PluginHost` owns exactly one additional `std::jthread` for `IRedXeNetworkWidget` work. It is created lazily when a
visible network widget first needs a run, and joined on runtime shutdown before optional `RedXePluginShutdown`.

| Bound | Value |
| --- | --- |
| In-flight plugin `RunNetworkWork` calls | 1 |
| Pending widget slots | 8 |
| URL bytes (plugin-side) | 2048 |
| Response body | 256 KiB heap, never an automatic array on the network-worker stack |
| Plugin HTTP cache entries | 8 |
| Default timeout | 10 s |
| Maximum timeout | 30 s |

Queue exhaustion skips that wake without dropping the in-flight call. Idle, hidden, and shutdown states block on
events; the worker does not poll. This worker is distinct from the local acquisition worker and from the optional
device-I/O lane, which still MUST NOT ship.

`SetVisible(FALSE)` signals cancel and drains `RunNetworkWork` before returning. Shutdown cancels every queued widget,
waits for the in-flight call, then joins. After Cancel/hide returns, that call MUST NOT still be inside curl.

### 3. libcurl lives in `Weather.dll`

`vcpkg.json` adds curl with `ssl` only (`default-features: false`). `builtin-baseline` MUST stay the same commit as
`vcpkg-tool.json`; a newer baseline asks for zlib port-versions the pinned versions database does not contain.

Windows curl uses Schannel. `Weather.dll` links `libcurl`. The build copies every vcpkg runtime DLL from the installed
triplet `bin` (or `debug\bin`) into the host `Plugins` directory next to `Weather.dll`: `libcurl`, `yyjson`, and zlib.
This baseline names zlib `z.dll` / `zd.dll`, not `zlib1.dll`. The plugin load flags do not search the executable
directory, so a missing zlib DLL makes `LoadLibraryExW` fail with `ERROR_MOD_NOT_FOUND` (`0x8007007E`). `RedXe.exe`
MUST NOT link curl. The plugin sets `CURLOPT_USERAGENT` to
`RedXe/0.1.0 (XENEON EDGE dashboard weather; +https://api.met.no/doc/TermsOfService)` and honors `Expires` /
`Last-Modified` / `ETag` in its own bounded cache. Automated tests MUST NOT require a hardware GPU or a live weather
account.

### 4. Automated runs stay offline

`--self-test` and `HostPluginTests` disable the network lane **before** any widget is created. The host then never
calls `RunNetworkWork`. Interactive RedXe leaves the lane enabled. WeatherTests drive parsers and WARP through
test-contract fixtures, never through curl. WeatherTests also run a cancelled `WeatherHttpGet` on a 192 KiB stack so a
256 KiB automatic response body cannot return.

### 5. Provider strategy — no keys

Internal model, SI units only. Convert °C/°F and km/h/mph when preparing display strings.

```text
WeatherSnapshot
  locationName, countryCode, latitude, longitude
  observationTimeFileTime100ns
  temperatureCelsius, windMetersPerSecond, currentCondition
  sunriseFileTime100ns, sunsetFileTime100ns
  hourlyForecast[24]
  dailyForecast[9]
  alerts[8]
```

Each alert normalizes to title, severity, start, expiry, description, instructions, and issuing authority. Strings are
bounded UTF-16 copies. Daily rows carry min/max °C and a condition code.

| Role | Service | When |
| --- | --- | --- |
| Forecast | [MET Norway Locationforecast 2.0 compact](https://api.met.no/weatherapi/locationforecast/2.0/documentation) | Always, after coordinates exist |
| Sun times | [MET Norway Sunrise 3.0](https://api.met.no/weatherapi/sunrise/3.0/documentation) | After coordinates exist |
| Geocode / reverse | [Nominatim](https://nominatim.org/release-docs/latest/api/Overview/) | Manual city/postcode, or reverse after region coordinates |
| Europe alerts | [MeteoAlarm](https://api.meteoalarm.org/) public feeds | Coordinates in Europe, including France |
| US alerts | [National Weather Service API](https://www.weather.gov/documentation/services-web-api) | Coordinates in the United States |

No adapter that requires an account, token, or API key ships. Météo-France Vigilance, WeatherAPI.com, and OpenWeather
are out. Outside Europe and the United States, the widget shows forecast without alerts.

Request order on the serial worker, once the widget is visible and has coordinates: forecast, sunrise, then at most
one alert document. Geocoding, if needed, runs first. Honor each response `Expires` (clamped 5–60 minutes) for the
next `RunNetworkWork`. Inactive widgets are not queued.

### 6. Location — no WinRT

Settings:

| Member | Type | Default |
| --- | --- | --- |
| `locationMode` | `"automatic"` or `"manual"` | `"automatic"` |
| `location` | string, at most 128 UTF-8 bytes | `""` |
| `temperatureUnit` | `"celsius"` or `"fahrenheit"` | `"celsius"` |
| `windUnit` | `"kmh"` or `"mph"` | `"kmh"` |

The published plugin schema stays inside the host's bounded subset (closed object, enums, unconstrained strings, no
`maxLength` keyword). The plugin and `SettingsV4` still reject overlong strings. There is no key or token member.

WinRT geolocation would pull the WinRT runtime, consent UI stacks, and additional in-memory modules. That is
forbidden here: no extra link library and no extra loaded image beyond `Weather.dll` + `libcurl` + code the process
already uses.

**Automatic:** `GetUserGeoID(GEOCLASS_NATION)` and `GetGeoInfo` (`GEO_LATITUDE`, `GEO_LONGITUDE`, `GEO_ISO2`,
`GEO_FRIENDLYNAME`) from the already-linked NLS APIs. This is the Windows **region**, not GPS: a nation centroid.
The widget reports `Degraded` with a reason that a city can be entered for a local forecast. Never from `Render`.

**Manual:** parse `location` as `lat,lon` when both numbers are finite and in range; otherwise Nominatim search
(limit 1). Reverse-geocode when only coordinates are known so the tile can show a place name and ISO country code.

If automatic region lookup fails and `location` is non-empty, use the manual path. If both fail, `Initializing` or
`Unavailable` with a reason to enter a location.

### 7. Widget identity and visuals

- Module: `Plugins/Weather/Weather.dll`
- Plugin ID: `builtin.weather`
- Type ID: `weather`
- Mechanisms: `IRedXeWidget` + `IRedXeGpuWidget` + `IRedXeScheduledWidget` + `IRedXeRaisedWidget` +
  `IRedXeNetworkWidget`
- Raised extent: half (`RedXeRaisedExtentHalf`)
- Flags: not continuous. `GetNextFrameDelayMilliseconds` returns the delay until the next network run, or `S_FALSE`
  while work is in flight or the widget is hidden.
- Default size 960×540, minimum 160×72, matching other information tiles.

Layout is density-driven from the widget rectangle, following Process Viewer floors rather than stretching one
composition. Standard density matches the three-zone outline mockup:

```text
[ 27°C              ☁ outline              ☀ 07:11 ]
[                                          ☀ 20:27 ]
[ Paris             20°C | 28°C         🌬 17km/h  ]
[ Friday, 4         20°C | 28°C                 ☁ ]
[ Saturday, 5       16°C | 24°C                 ☁ ]
```

| Density | Shown |
| --- | --- |
| Tiny | Temperature left, outline condition icon right |
| Compact | Hero temperature, centered outline icon, location, today's min `\|` max |
| Standard | Compact plus stacked sunrise/sunset, wind, and as many weekday forecast rows as fit |
| Raised / wide | Standard plus alert banner and more forecast days, up to 9 |

Omit rows that do not fit. Convert units only in the display formatter. Daily labels are `Today`, `Tomorrow`, or the
local weekday and day number (`Friday, 4`), never `Day N`. Temperature ranges use ` | `. Wind is `17km/h` / `11mph`
with a leading wind glyph. Sunrise and sunset are local `HH:MM` beside sunrise/sunset glyphs. Condition, wind, and
sun-time marks come from Erik Flowers [Weather Icons](https://erikflowers.github.io/weather-icons/) (`weathericons-regular-webfont.ttf`,
SIL OFL 1.1), copied beside `Weather.dll` and atlas-rasterized through DirectWrite. Geometry stroke marks remain only
as a fallback if that font file is missing. Do not use host `FluentIcons.h` chrome for weather symbols. Type is
light-weight Segoe UI (Bahnschrift, then Arial), not bold condensed. The widget copies the Process Viewer GPU pattern:
shared device resources, DirectWrite atlas rasterized from snapshot delivery and `OnTargetSizeChanged`, allocation-free
`Render`.

Visual language: opaque near-black panel, light-gray type, thin monochrome outline weather marks. MET Norway
`symbol_code` maps to a closed condition enum (clear, partly cloudy, cloudy, rain, snow, thunder, fog, sleet, wind).

**Color follows condition and alert severity**, same intent pattern as System Data healthy/warn/hot, cached in the
display snapshot so `Render` does not recompute palettes. Hero temperature, location, ranges, wind, and forecast type
stay light gray. Condition color tints Weather Icons glyphs (or fallback strokes) only:

| Intent | Use | Color |
| --- | --- | --- |
| Clear / sun | Outline current icon | Warm gold `#E8C36A` |
| Partly cloudy | Icon | Light gray-gold `#C8C4B8` |
| Cloudy | Icon | Neutral gray `#A8B0B8` |
| Rain / sleet | Icon, precipitation cue | Steel blue `#5AA0D4` |
| Snow | Icon | Ice `#D0E4F0` |
| Thunder | Icon | Alert orange `#F0A030` |
| Fog / wind | Icon | Muted slate `#8A949C` |
| Alert informational / yellow | Banner, row mark | `#E0C040` |
| Alert orange | Banner, row mark | `#F07A20` |
| Alert red | Banner, row mark | `#E03838` |

An active alert paints the banner and a thin leading edge on the current block in the highest present severity. Type
stays light gray for readability; color is the icon stroke and alert chrome, not a full-tile wash or gold temperature.
Attribution `MET Norway` (and MeteoAlarm or NWS when those bodies are shown) is visible at Standard density and
above.

Status: `Initializing` until the first usable snapshot, `Degraded` when showing Windows-region coordinates, stale
cache, or missing alerts, `Unavailable` only when the widget cannot draw (no location and no cached snapshot).
`Unavailable` hands the tile to the host placeholder.

## Workstreams

| ID | Item | Pass condition |
| --- | --- | --- |
| H1 | Add `IRedXeNetworkWidget` with `sizeBytes`-pinned records if any, documented thread/cancel rules, in `Widget.h`. | PluginContractTests: direct `IUnknown` child, not a base of `IRedXeWidget`; QueryInterface identity with the weather widget. |
| H2 | `PluginHost` network lane: lazy serial worker, queue, cancel drain, shutdown join, offline switch. | HostPluginTests: offline mode never calls `RunNetworkWork` and starts no extra thread; hide drains an in-flight stub; shutdown joins. |
| H3 | vcpkg curl+ssl, Weather.vcxproj links curl, copy curl/yyjson/zlib runtime DLLs and `weathericons-regular-webfont.ttf` into `Plugins\`. | x64 and ARM64 install; Weather.dll maps with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`; RedXe.exe does not list libcurl as a link dependency. HostPluginTests: `TestWeatherPluginConstructs`. WeatherTests: font file sits beside `Weather.dll`. |
| H4 | `--self-test` and HostPluginTests disable the network lane before widget create. | Hidden WARP and self-test make zero sockets to weather hosts. |
| W1 | `Weather.dll` factory, settings contract, catalog, schema, both templates. Replace the gallery Process Viewer tile with Weather so page-1 widget counts stay 6; `builtin.process-viewer` remains on the System page. | SettingsTests catalog coverage and page counts stay green; schema `oneOf` accepts `builtin.weather`; no key fields. |
| W2 | Snapshot parsers for MET compact, sunrise, Nominatim, MeteoAlarm, NWS, plus unit and intent-color formatters. | WeatherTests apply fixtures with no network and assert SI storage, display conversion, alert severity colors, and Europe/US routing. |
| W3 | Location: NLS region for automatic, `lat,lon` parse, Nominatim for manual city. | Automatic uses `GetGeoInfo` only; tests inject coordinates; no WinRT/LocationApi module load. |
| W4 | GPU widget: adaptive layout, condition/alert color, scheduled refresh, raise, `SetVisible(FALSE)` cancels network work. | WeatherTests WARP at tiny/compact/standard/raised sizes; negative swipe origin still draws; `Render` allocates nothing. |
| W5 | Closeout: merge durable rules into `Plugins_API.md`, `Core_PerformanceAndResources.md`, `Core_Settings.md`, plugin-development and performance skills; AGENTS.md catalog row. | Domain specs name the network lane, weather plugin, curl lifetime, no-key adapters, and offline-test rule. |

## Validation

```powershell
.\vcpkg-install.ps1 -Platform All
.\format.ps1
.\test.ps1 -Configuration Debug -Platform x64 -Rebuild
.\test.ps1 -Configuration Release -Platform x64 -Rebuild
.\build.ps1 -Configuration Release -Platform ARM64
.\validate-skills.ps1
```

WeatherTests MUST cover factory/settings rejection, fixture parsers, unit conversion, density layout, condition and
alert colors, WARP present, device loss, and `d3dcompiler_47.dll` not loaded. They MUST also fail if the weather module
loads `windowsapp.dll`, `locationapi.dll`, or a WinRT `Windows.Devices.Geolocation` activation, and if
`WeatherHttpResponse` is large enough to overflow a 192 KiB network-worker stack. HostPluginTests MUST
cover the offline network lane, gallery Weather with no live sockets, and existing Matrix/System/clock soaks
unchanged. SettingsTests MUST keep three-page templates, catalog iteration, and the page-1 count of 6.

Live MET Norway / MeteoAlarm / Nominatim / NWS calls are manual-only and MUST NOT be a CI pass condition.

## Exit criteria

This plan is complete when H1–H4 and W1–W5 pass, the listed validation is green, every durable requirement lives in
the owning domain specs, this file is moved to `Specs/Plans/Done/`, and its WIP index row is removed. Architecture
gate 1 remains open for push providers and network `IRedXeDataSource` datasets.
