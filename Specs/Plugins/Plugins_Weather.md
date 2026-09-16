# Weather widget

Status: current normative product contract
Last reviewed: 2026-09-08
Owner: `Plugins/Weather`, `Plugins/WeatherLocation`, and `Tests/WeatherTests`

The native ABI and host services are owned by `Plugins_API.md`. Resource requirements remain owned by
`../Core/Core_PerformanceAndResources.md`; settings storage and merging by `../Core/Core_Settings.md`.

## Location and persistence

- A non-empty `location` is authoritative regardless of the legacy `locationMode`. Parse finite, in-range `lat,lon`
  directly; otherwise search Nominatim once, percent-encoding UTF-8 and requesting address details for city/country.
  A failed configured-city lookup MUST NOT switch to the computer's location.
- Only an empty `location` permits Windows discovery. The network worker launches the sibling `WeatherLocation.exe`
  hidden with an explicit executable path and only its output pipe inherited. This helper alone uses C++/WinRT.
  Try these sources in order, stopping at the first usable result: normal `GetGeopositionAsync` (eight seconds),
  a new Geolocator with Windows coarse fallback enabled (four seconds), the most recent legacy `ILocation` latitude/
  longitude report, then `Geolocator.DefaultGeoposition` (the position manually saved in Windows). Catch errors per
  attempt, including activation failure, access denial, timeout and absent data, so later sources still run. Cancel
  an asynchronous acquisition that exceeds its attempt deadline before trying the next source.
  [Windows coarse fallback](https://learn.microsoft.com/en-us/uwp/api/windows.devices.geolocation.geolocator.allowfallbacktoconsentlesspositions)
  permits approximate location when precise app access is disabled; it still requires the system location switch.
- All discovered coordinates must be finite and in range. Observed fixes require finite, non-negative accuracy no
  worse than 50 km and a timestamp within five minutes in the past or one minute in the future (clock tolerance).
  Reject missing/stale/invalid fixes and try the next source. The manually saved Windows default has no observation
  timestamp or accuracy; validate its coordinates. Never request permission UI, alter Windows settings or enable a
  disabled service. The legacy report API is best-effort and must tolerate unavailability; it respects report access
  denial and registers no subscription. If every source fails, require an explicit city and persist nothing.
- Weather.dll and RedXe.exe MUST NOT link or activate WinRT geolocation or load LocationAPI. The helper exits after
  one result. The parent blocks on completion/cancellation with a 20-second timeout, terminating and joining only its
  owned helper when cancelled/timed out. No location thread, timer, or continuous subscription remains.
- Cache only coordinates/city/country per widget; reuse them on forecast refresh. Failed discovery is not repeatedly
  relaunched during that widget's lifetime. Cancellation permits another visible attempt. Never use a Windows country
  centroid as a city or infer a city from locale/timezone. Do not call an external IP-geolocation service. Reverse-
  geocode coordinates; if that fails, retain coordinates rather than inventing a city.
- Persist `location` (city plus country, or coordinates when reverse lookup fails) and `locationMode: "manual"` through
  the optional host settings queue. The UI thread commits the copy; `CollectPersistentSettings` retains fallback for
  page teardown/exit. The plugin never writes settings files. Configured locations and units are never overwritten.
  Defaults remain empty/automatic, Celsius and km/h; machine-specific Paris belongs only in the user's settings.

## Providers and transport

- Every adapter is keyless. Forecast comes from MET Norway Locationforecast 2.0 `compact`, sun times from MET Norway
  Sunrise 3.0, geocoding and reverse geocoding from Nominatim, European alerts from MeteoAlarm public feeds, and
  United States alerts from the National Weather Service API. No adapter that needs an account, token, or API key
  ships (Météo-France Vigilance, WeatherAPI.com, OpenWeather, and the hosted Open-Meteo endpoint are out), and no
  IP-geolocation service is called. Outside Europe and the United States the widget shows the forecast without
  alerts. Further providers are a `DECISION` in `Specs/Plans/WIP/RFC_Plugins_WeatherProvidersAndRegionalAlerts.md`.
- One `RunNetworkWork` cycle on the host network lane (`Plugins_API.md`) performs geocoding if still needed, then
  forecast, then sunrise, then at most one alert document, in that order. The next run waits for the longest
  `Cache-Control: max-age` among those responses, clamped to 5–60 minutes with a 15-minute default. Inactive widgets
  are never queued.
- The plugin owns its HTTP client: `Weather.dll` links libcurl with Schannel and sends the user agent
  `RedXe/0.1.0 (XENEON EDGE dashboard weather; +https://api.met.no/doc/TermsOfService)`. It keeps a bounded cache of
  eight entries that revalidates with `If-None-Match` / `If-Modified-Since` and treats `304` as a cache hit. URLs are
  at most 2048 bytes, response bodies are 256 KiB heap buffers, and requests time out after 10 s (30 s maximum). A
  signaled cancel event returns promptly with `ERROR_CANCELLED`; the call never blocks inside curl after cancel.
- Internal storage is SI (°C, m/s, mm, FILETIME); °F, km/h, and mph exist only in display strings. Strings are
  bounded UTF-16 copies: 24 hourly entries, nine daily entries, eight alerts with title, severity, start, expiry,
  description, instructions, and issuing authority.

## Forecast and composition

- Keep 24 genuine one-hour entries and nine daily entries. A six-hour period MUST NOT masquerade as hourly data.
  Missing precipitation amounts differ from zero. Amounts are mm over the following period, not probabilities or
  exact onset times. [MET ForecastJSON](https://api.met.no/doc/ForecastJSON) defines these period semantics.
- Group days and format clocks using the computer's local civil date/time, including across UTC midnight. The first
  day's min/max covers available samples. Show that range once in Today; daily rows start with a future local day.
- The location owns its line. The header shows large light-gray temperature, an outline condition icon centered in
  the remaining space between that temperature and optional sunrise/sunset, and sunrise/sunset only when all fit. The
  current-condition icon is a sharp ~80 px mark from a 96 px atlas cell, not stretched across the leftover band.
  Today and wind follow. Measured columns stay within the tile; long labels end in an ellipsis without cutting glyphs.
  Tiny tiles prioritize temperature/condition.
- Upcoming hours use available width for up to 12 chronological columns: time/Now, condition, temperature, and supplied
  positive precipitation. Only unexpired intervals starting within 24 hours qualify. Add the strip when at least
  210 px width and 132 px height remain. Future-day rows use remaining space, up to five normally/eight raised.
  Hours or days that do not fit draw a bottom-right `+N` count; one-finger swipe or wheel pages them. The widget
  exposes `IRedXeInteractiveWidget` for that paging and returns `S_FALSE` on Down so raise still works.
  Reserve attribution space and omit unavailable/non-fitting content without shrinking body type below its floor.
- A separate notice selects the earliest unexpired wet hourly interval starting within 12 hours. Preserve snow,
  sleet and thunder names. Use `Rain expected around HH:MM` or `Snow forecast this hour`, never precise nowcasting.
  Explicit zero amount suppresses a wet symbol; missing amounts may use the symbol. Stale snapshots/expired periods
  must not produce upcoming notices. Official authority alerts remain separate from forecast notices. Each notice or
  alert that fits paints a tinted banner with a solid leading edge and a leading colored icon badge (severity mark for
  authority alerts, wet-condition glyph on a yellow warning badge for forecast notices). The banner uses at least 56 px
  and grows up to 64 px when leftover height cannot fit another complete future-day row.
- Retain Weather Icons and light Segoe UI. Fit complete measured glyph ink inside atlas cells with filtering padding,
  including oversized icons and accents; reject clipped glyphs. Measurement includes overhang and rendering uses
  cached cropped rectangles. Clear reused atlas cells. Render creates no fonts, textures, heap allocations or I/O.
- Text rasterizes once into 48 px cells. Condition icons and the header temperature glyphs (digits, sign, degree,
  `C`, `F`) also own a 96 px twin cell in the atlas's reserved bottom rows; drawing switches to the twin above ~1.1x
  the small cell so the hero temperature is as sharp as the icon beside it. A text twin is an exact 2x raster that
  reuses the small cell's normalized advance and ink, so measurement never depends on which cell draws; a twin whose
  ink does not fit that rectangle is skipped rather than clipped. WeatherTests verify every hero glyph links its twin.

## Status and color

- The widget reports `Initializing` until its first usable snapshot, `Ok` after a complete refresh, and `Degraded`
  when a refresh fails after a snapshot was drawn: the last snapshot stays on screen with the attribution
  `MET Norway  |  Cached forecast`, and the next successful run clears it. `Unavailable` is reported only while
  nothing has ever been drawn (no coordinates, failed city lookup, or failed first forecast); it hands the tile to
  the host placeholder per `Plugins_API.md`.
- Type stays light gray: hero temperature, location, ranges, wind, and forecast labels never take condition color.
  Condition color tints only Weather Icons strokes and precipitation cues, by intent: clear `#E8C36A`, partly cloudy
  `#C8C4B8`, cloudy `#A8B0B8`, rain and sleet `#5AA0D4`, snow `#D0E4F0`, thunder `#F0A030`, fog and wind `#8A949C`.
  Alert severity paints the banner, its leading edge, and the warning mark: yellow `#E0C040`, orange `#F07A20`,
  red `#E03838`; a wet-hour notice uses the yellow badge. The footer attribution `MET Norway` is drawn at Standard
  density and above; alert banners carry the alert title, and the issuing authority is retained in the snapshot.

## Validation

WeatherTests MUST cover amount/symbol/missing/zero semantics, snow/sleet, expired/stale notices, the 12-hour horizon,
six-hour exclusion, local-day grouping, JSON escaping, coordinate fallback, and bounded serialization. Offline tests
must exercise configured-city precedence, empty-manual discovery, cached refresh, queued UI save/collect fallback,
failed discovery without relaunch, helper cancellation/timeout/isolation, and persisted-city reuse in a new widget.
Cover each fallback source succeeding, short-circuit ordering, access/timeout/API-unavailable errors, partial failed
outputs, absent/default positions, invalid coordinates, negative/missing/excessive accuracy, stale/future timestamps,
and inclusive freshness/accuracy bounds. Use deterministic helper processes for fallback-to-persistence integration.
Every automated run is offline: fixtures answer through the test response hook, never curl, and live MET Norway,
Nominatim, MeteoAlarm, or NWS calls are manual-only and MUST NOT be a CI pass condition. WeatherTests MUST fail if
`Weather.dll` loads `Windows.Devices.Geolocation.dll` or `locationapi.dll`, and MUST prove a cancelled
`WeatherHttpGet` on a 192 KiB stack returns `ERROR_CANCELLED`. They MUST prove a failed refresh after a drawn
snapshot reports `Degraded` and keeps the temperature, while a widget that never drew reports `Unavailable`.
HostPluginTests MUST prove the network lane: offline activation never calls `RunNetworkWork` and starts no thread,
deactivation cancels and drains an in-flight call, and disabling access joins the worker.
WARP must render production tiny/compact/narrow/
standard/raised layouts, Fahrenheit and long accented names. Readback must contain lit pixels and submitted quads
must fit the tile. Debug steady renders allocate nothing. Save PNG previews under `.build/` for inspection. Run Debug
and Release x64 `test.ps1 -Rebuild`, an ARM64 build, formatting and skill validation before closeout.
