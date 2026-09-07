# Weather widget

Status: current normative product contract
Last reviewed: 2026-09-07
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

## Forecast and composition

- Keep 24 genuine one-hour entries and nine daily entries. A six-hour period MUST NOT masquerade as hourly data.
  Missing precipitation amounts differ from zero. Amounts are mm over the following period, not probabilities or
  exact onset times. [MET ForecastJSON](https://api.met.no/doc/ForecastJSON) defines these period semantics.
- Group days and format clocks using the computer's local civil date/time, including across UTC midnight. The first
  day's min/max covers available samples. Show that range once in Today; daily rows start with a future local day.
- The location owns its line. The header shows large light-gray temperature, an outline condition icon, and optional
  sunrise/sunset only when all fit. Today and wind follow. Measured columns stay within the tile; long labels end in
  an ellipsis without cutting glyphs. Tiny tiles prioritize temperature/condition.
- Upcoming hours use available width for up to 12 chronological columns: time/Now, condition, temperature, and supplied
  positive precipitation. Only unexpired intervals starting within 24 hours qualify. Add the strip when at least
  210 px width and 132 px height remain. Future-day rows use remaining space, up to five normally/eight raised.
  Hours or days that do not fit draw a bottom-right `+N` count; one-finger swipe or wheel pages them. The widget
  exposes `IRedXeInteractiveWidget` for that paging and returns `S_FALSE` on Down so raise still works.
  Reserve attribution space and omit unavailable/non-fitting content without shrinking body type below its floor.
- A separate notice selects the earliest unexpired wet hourly interval starting within 12 hours. Preserve snow,
  sleet and thunder names. Use `Rain expected around HH:MM` or `Snow forecast this hour`, never precise nowcasting.
  Explicit zero amount suppresses a wet symbol; missing amounts may use the symbol. Stale snapshots/expired periods
  must not produce upcoming notices. Official authority alerts remain separate from forecast notices.
- Retain Weather Icons and light Segoe UI. Fit complete measured glyph ink inside atlas cells with filtering padding,
  including oversized icons and accents; reject clipped glyphs. Measurement includes overhang and rendering uses
  cached cropped rectangles. Clear reused atlas cells. Render creates no fonts, textures, heap allocations or I/O.

## Validation

WeatherTests MUST cover amount/symbol/missing/zero semantics, snow/sleet, expired/stale notices, the 12-hour horizon,
six-hour exclusion, local-day grouping, JSON escaping, coordinate fallback, and bounded serialization. Offline tests
must exercise configured-city precedence, empty-manual discovery, cached refresh, queued UI save/collect fallback,
failed discovery without relaunch, helper cancellation/timeout/isolation, and persisted-city reuse in a new widget.
Cover each fallback source succeeding, short-circuit ordering, access/timeout/API-unavailable errors, partial failed
outputs, absent/default positions, invalid coordinates, negative/missing/excessive accuracy, stale/future timestamps,
and inclusive freshness/accuracy bounds. Use deterministic helper processes for fallback-to-persistence integration.
WARP must render production tiny/compact/narrow/
standard/raised layouts, Fahrenheit and long accented names. Readback must contain lit pixels and submitted quads
must fit the tile. Debug steady renders allocate nothing. Save PNG previews under `.build/` for inspection. Run Debug
and Release x64 `test.ps1 -Rebuild`, an ARM64 build, formatting and skill validation before closeout.
