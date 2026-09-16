# RFC: other forecast providers and regional early-warning providers for the Weather widget

Status: `DECISION`
Created: 2026-09-16
Owner: `Plugins/Weather`, `Tests/WeatherTests`, and the settings chain when a provider becomes user-selectable

## Purpose

The shipped widget uses one forecast provider (MET Norway) and two early-warning providers (MeteoAlarm in Europe,
the National Weather Service in the United States). Everywhere else the tile shows a forecast and no official
alerts. This document records how a further forecast provider or a further early-warning provider is wired into the
current plugin, which candidates exist per region, what each one requires, and the decisions that must be taken
before the first one ships. It is non-normative: [`../../Plugins/Plugins_Weather.md`](../../Plugins/Plugins_Weather.md)
owns the product behavior, including the network lane and the settled no-key policy that this document inherits
(decided in [`../Done/WeatherPlugin_2026-09-04.md`](../Done/WeatherPlugin_2026-09-04.md)).

Inherited rules that any candidate MUST satisfy (see the "Decisions" section of the weather plan):

- No account, token, or API key. No compiled secret. No IP-geolocation service.
- All I/O runs inside `RunNetworkWork` on the host network worker, through the plugin-owned curl, with the cancel
  event honored between requests. Never from `Render`, never on a plugin thread.
- Bounded storage only: `kWeatherMaximumUrlBytes` (2048), `kWeatherMaximumBodyBytes` (256 KiB heap body),
  `kWeatherHttpCacheEntries` (8), 10 s default / 30 s maximum timeout, and the `WeatherSnapshot` arrays
  (24 hourly, 9 daily, 8 alerts, UTF-16 strings of fixed capacity).
- Live endpoints are manual-only. Automated tests use fixtures through `WeatherTestContract.h`; `--self-test` and
  `HostPluginTests` disable the network lane before any widget exists.
- Attribution stays visible at Standard density and above, per provider actually shown.

## How the shipped pipeline is wired

Everything below lives in `Plugins/Weather/`. The order is the request order on the serial worker.

| Step | Code | Notes |
| --- | --- | --- |
| Resolve coordinates | `WeatherWidget::FetchLive` in `Weather.cpp`: `WeatherParseLatLon`, `WeatherBuildLocationSearchUrl` + `WeatherParseNominatim`, or `WeatherLocateWithHelper` | A configured city wins; an empty city runs the disposable helper once. |
| Reverse geocode | `nominatim.openstreetmap.org/reverse` → `WeatherParseNominatim` | Fills `locationName` and the ISO 3166-1 alpha-2 `countryCode`. **The country code is the only routing key today.** |
| Forecast | `api.met.no/weatherapi/locationforecast/2.0/compact` → `WeatherParseLocationForecast` in `WeatherModel.cpp` | Produces SI hourly/daily arrays and `currentCondition` from `symbol_code` via `WeatherConditionFromSymbol`. |
| Sun times | `api.met.no/weatherapi/sunrise/3.0/sun` → `WeatherParseSunrise` | Optional; failure leaves the header without sunrise/sunset. |
| Alerts | `WeatherAlertRegionFromCountry` (`kEuropeIso2`, US/PR/GU/VI) selects **one** document: `feeds.meteoalarm.org/api/v1/warnings/feeds-{cc}` → `WeatherParseMeteoAlarm`, or `api.weather.gov/alerts/active?point=lat,lon` → `WeatherParseNwsAlerts` | `WeatherAlertRegion::None` skips alerts. Each alert normalizes to title, description, instructions, authority, start, expiry, and a `WeatherAlertSeverity` of Yellow/Orange/Red. |
| Refresh | `WeatherHttpResponse::expiresDelayMilliseconds` per response, `WeatherClampRefreshMilliseconds` (5–60 min) | Providers without `Expires` fall back to the 15-minute default. `ETag`/`Last-Modified` revalidation is cached per URL. |
| Identity | `kWeatherUserAgent` in `WeatherHttp.h` | MET Norway and NWS both require an identifying User-Agent; the string names the RedXe product and MET's terms URL. |
| Test seams | `WeatherTestSnapshot` (`locationForecastJson`, `sunriseJson`, `nominatimJson`, `meteoAlarmJson`, `nwsJson`) applied by `RedXeWeatherApplyTestSnapshot`; `RedXeWeatherParseTestAlerts(json, unitedStates)`; `RedXeWeatherAlertRegionForCountry` | `Tests/WeatherTests/WeatherRegressionTests.cpp` drives parsers and WARP through these; no socket is opened. |
| Attribution and glyphs | `BuildScene` footer draws `MET Norway` (plus `Cached forecast` when stale); `RasterizeGlyphs` pre-rasterizes that string | A new attribution string MUST be added to the pre-rasterized text, or its glyphs are missing on the first frame after a snapshot. |

Structural facts that shape every extension:

- The plugin parses **JSON only** (yyjson). Most national early-warning feeds are **CAP 1.2 XML inside Atom/RSS**.
  Adding an XML source needs a bounded CAP reader first (decision D1 below).
- `WeatherHttpGet` is HTTPS through Schannel. FTP-only sources (Australia's BOM anonymous FTP) are out unless the
  transport decision is reopened.
- `WeatherSnapshot` has no provider identity. Attribution is a literal today; per-provider attribution needs a
  small closed enum on the snapshot (decision D3).

## Recipe: adding a forecast provider

MET Norway is global, so a second forecast provider is about redundancy or a better national model, not coverage.

1. Add a `WeatherForecastProvider` closed enum to `WeatherModel.h` (`MetNorway` first, then the new one) and a
   selector on `WeatherConfiguration` defaulting to `Auto`, which keeps MET Norway.
2. Write `WeatherParse<Provider>(std::string_view json, WeatherSnapshot&)` in `WeatherModel.cpp`. It MUST fill
   the same SI fields (`temperatureCelsius`, `windMetersPerSecond`, `currentCondition`, 24 genuine one-hour
   `hourly` entries with `hasPrecipitation` semantics from `Plugins_Weather.md`, and up to 9 local-day `daily`
   entries). Map the provider's condition vocabulary to the closed `WeatherCondition` enum; unknown codes map to
   `Unknown`, never to a guessed icon.
3. Build its URL in `FetchLive` with `sprintf_s` into a `kWeatherMaximumUrlBytes` array. A provider that needs
   two round trips (NWS `/points` then `forecastHourly`) counts both against the cancel event and the timeout.
4. Extend `WeatherTestSnapshot` with a fixture slot and `RedXeWeatherApplyTestSnapshot` with its branch; add a
   fixture and parser test to `WeatherRegressionTests.cpp` covering units, missing precipitation, six-hour
   exclusion, and local-day grouping exactly as the MET fixture does.
5. Add the attribution string (decision D3) and pre-rasterize it in `RasterizeGlyphs`.
6. If the provider is user-selectable, run the settings chain: `kSettingsSchema`/`kSettingsDefaults` and
   `WeatherReadConfiguration` in the plugin, `Specs/Settings.schema.json`, both `Settings/` templates,
   `Core_Settings.md`, `SettingsTests`, and `docs/plugins/weather.md`. Unknown keys reject the document, so the
   key MUST be added everywhere in one change.
7. Update `Plugins_Weather.md` (provider table, attribution, validation) before closeout.

## Recipe: adding an early-warning provider for a region

1. Extend `WeatherAlertRegion` in `WeatherModel.h` with the new region and `WeatherAlertRegionFromCountry` with
   its ISO codes. Keep exactly one alert document per refresh; if a region needs a sub-national key (a JMA area
   code, a Canadian office, a BOM state), derive it from the Nominatim `address` fields already parsed by
   `WeatherParseNominatim` and store it beside `countryCode` in the resolved location (decision D2).
2. Write `WeatherParse<Provider>Alerts(std::string_view body, WeatherSnapshot&)` producing `WeatherAlert`
   entries. Severity MUST map into Yellow/Orange/Red (CAP `severity`: Minor→Yellow, Moderate→Orange,
   Severe/Extreme→Red; MeteoAlarm `awareness_level` colors map directly). Drop expired entries, keep at most
   `kWeatherAlertCount`, and copy bounded UTF-16 strings with `WeatherCopyWide`; never keep the body.
3. Add the fetch branch to `FetchLive` after the sunrise request, respecting `Expires` when present.
4. Add a fixture slot to `WeatherTestSnapshot`, a `RedXeWeatherParseTestAlerts` mode (or a new export), and
   tests for severity mapping, expiry, empty documents, and the region routing of every ISO code added.
5. Add the authority attribution and its pre-rasterized glyphs.
6. Record the provider, its terms, and its region in `Plugins_Weather.md`; add the user-visible coverage change
   to `docs/plugins/weather.md`.

## Candidate providers by region

`Key` is whether an account, token or key is needed (a hard exclusion). `Checked` is the date the row was
verified against the provider's own documentation or a live response; rows marked `verify` are from memory and
MUST be confirmed before any implementation starts.

### Forecast

| Region | Provider | Endpoint shape | Format | Key | Terms | Checked |
| --- | --- | --- | --- | --- | --- | --- |
| Global | MET Norway Locationforecast 2.0 (shipped) | `api.met.no/weatherapi/locationforecast/2.0/compact?lat&lon` | JSON | none; identifying User-Agent required | NLOD 2.0 / CC BY 4.0, honor `Expires` | 2026-09-04 plan |
| Global | Open-Meteo | `api.open-meteo.com/v1/forecast?latitude&longitude&hourly=...` | JSON | none on the free tier | **Free tier is non-commercial only** (600/min, 10 000/day); commercial use needs a paid key. Data CC BY 4.0. Excluded by the weather plan unless RedXe's distribution is confirmed non-commercial. | 2026-09-16 |
| United States | NWS API | `api.weather.gov/points/{lat},{lon}` → `forecastHourly` | GeoJSON | none; User-Agent required (an API key is announced for the future) | Public domain; undisclosed rate limit, retry after ~5 s | 2026-09-16 |
| Germany | Bright Sky (community JSON over DWD open data) | `api.brightsky.dev/weather?lat&lon` | JSON | none | Third-party mirror of DWD MOSMIX; DWD data DL-DE/BY-2.0 | verify |
| Sweden | SMHI open data | `opendata-download-metfcst.smhi.se/api/.../geotype/point/lon/{lon}/lat/{lat}/data.json` | JSON | none | CC BY 4.0 | verify |
| Japan | JMA bosai forecast | `www.jma.go.jp/bosai/forecast/data/forecast/{areaCode}.json` | JSON, Japanese labels | none | Endpoint used by JMA's own site; no published terms for automated use | verify |
| United Kingdom | Met Office DataHub | — | — | **key required** | Excluded | 2026-09-04 plan |
| France | Météo-France | — | — | **key required** | Excluded | 2026-09-04 plan |

### Early warning (official alerts)

| Region | Provider | Endpoint shape | Format | Key | Notes | Checked |
| --- | --- | --- | --- | --- | --- | --- |
| Europe (EUMETNET members, incl. UK, NO, CH, IS, UA, TR) | MeteoAlarm (shipped) | `feeds.meteoalarm.org/api/v1/warnings/feeds-{country}`; Atom per country `feeds.meteoalarm.org/feeds/meteoalarm-legacy-atom-{country}` | JSON (API) or Atom+CAP | none | CC BY 4.0, "Data provided by EUMETNET members". RSS feeds deprecated 2026-01-14. `api.meteoalarm.org` hosts the API portal. | 2026-09-16 |
| United States, PR, GU, VI | NWS alerts (shipped) | `api.weather.gov/alerts/active?point={lat},{lon}` | GeoJSON with CAP fields | none; User-Agent | Public domain | 2026-09-16 |
| Canada | ECCC MSC Datamart CAP | `dd.weather.gc.ca/today/alerts/cap/{YYYYMMDD}/{OFFICE}/{hh}/*.cap` (`LAND`/`WATR` for tornado and severe-thunderstorm); AMQP push exists | CAP 1.2 XML, directory listings | none | Needs directory traversal (date/office/hour) plus polygon or CLC-code matching against the coordinates. A per-region RSS on `weather.gc.ca` also exists (`verify` the URL form). | 2026-09-16 (layout), matching `verify` |
| Japan | JMA bosai warning | `www.jma.go.jp/bosai/warning/data/warning/{areaCode}.json` (`130000` = Tokyo) | JSON, Japanese text, numeric warning codes | none | Live response verified. Needs prefecture area code from the reverse-geocoded `state`; codes in `bosai/common/const/area.json`. No published terms for automated use. | 2026-09-16 |
| Hong Kong | HKO Open Data | `data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=warnsum&lang=en` | JSON (`{}` when nothing is active) | none | Documented open data; single territory, no coordinates needed | 2026-09-16 |
| Australia | Bureau of Meteorology | CAP-AU XML and warning products on anonymous FTP `ftp.bom.gov.au` (`/anon/gen/fwo/`) | CAP-AU XML | none (anonymous FTP; Registered User Services for SLAs) | **`www.bom.gov.au` blocks automated HTTP (WAF, 403)**; the HTTPS curl in `Weather.dll` cannot use FTP today. Candidate only if transport decision D4 is reopened. | 2026-09-16 |
| Brazil | INMET Alert-AS | `alertas2.inmet.gov.br` RSS/CAP; a JSON `apiprevmet3.inmet.gov.br/avisos/ativos` is reported | CAP XML / JSON | none | Verify both endpoints and terms | verify |
| South Africa | SAWS CAP | `caps.weathersa.co.za` | CAP XML | none | Verify | verify |
| New Zealand | MetService | No documented public API; CAP reaches aggregators | — | — | Prefer the global aggregator row | verify |
| India, Africa, rest of Asia-Pacific, Latin America | National services via the WMO Alert Hub / SWIC (Alert-Hub.Org "Filtered Alert Hub") | Aggregated CAP feeds of the WMO Register of Alerting Authorities; source list at `alert-hub.s3.amazonaws.com/cap-sources.html` (JavaScript-rendered) | Atom + CAP 1.2 | none stated | The consumer feed URL, filtering by area, and the terms are not published on the static pages fetched on 2026-09-16; confirm with WMO/Alert-Hub.Org before relying on it. One aggregator would cover every remaining region with a single CAP reader. | partially, feed URL `verify` |

Any region with no row keeps today's behavior: forecast only, no alert banner, status `Ok` rather than
`Degraded`.

## Decisions required before the first new provider

| ID | Decision | Options | Recommendation |
| --- | --- | --- | --- |
| D1 | CAP/Atom XML reading | (a) JSON-only sources forever; (b) a bounded forward-scanning CAP reader in `WeatherModel.cpp` (no DOM, no MSXML, extracts `event`, `headline`, `severity`, `onset`, `expires`, `areaDesc`, `senderName` from each `<info>`); (c) MSXML/XmlLite | (b). XmlLite is a pull parser and light, but a scanner over the 256 KiB bound with entity decoding for the handful of CAP fields keeps the DLL dependency-free and testable with fixtures. Canada, BOM, INMET, SAWS and the WMO hub all need it; MeteoAlarm JSON and NWS GeoJSON do not. |
| D2 | Sub-national routing key | (a) country code only; (b) also keep the reverse-geocoded `state`/`province` and a per-provider code table; (c) point-in-polygon against the alert geometry | (b) for JMA and Canadian offices; (c) only where the feed already returns a per-point query (NWS does). Nominatim `zoom=10` already returns the address block; store one extra bounded string. |
| D3 | Provider identity on the snapshot | (a) attribution literal per branch; (b) `WeatherForecastProvider` and `WeatherAlertProvider` enums on `WeatherSnapshot` with a static attribution table | (b). The footer composes `MET Norway  |  MeteoAlarm` style attribution from the table, and `RasterizeGlyphs` pre-rasterizes every table entry once. |
| D4 | Transport | (a) HTTPS only (current); (b) enable `CURLPROTO_FTP` for BOM | (a). FTP adds a second protocol surface and passive-mode firewall behavior to a dashboard widget for one country; wait for BOM's CAP over HTTPS or the WMO hub. |
| D5 | User selection | (a) automatic by region only; (b) `forecastProvider` and `alertProvider` settings keys with `auto` default | (a) first. Add (b) only when two providers cover the same place (for example NWS versus MET Norway in the US), and then through the full settings chain. |
| D6 | Open-Meteo eligibility | Depends on whether RedXe's distribution is non-commercial | Keep excluded until the answer is recorded here; the hosted free endpoint is otherwise the simplest global second source. |
| D7 | Per-provider refresh | Providers without `Expires` | Use the 15-minute default; never poll a warning feed faster than 5 minutes (`kWeatherMinimumRefreshMilliseconds`). |

## Workstreams once decided

| ID | Item | Pass condition |
| --- | --- | --- |
| P1 | D3 provider enums, attribution table, pre-rasterized glyphs | WeatherTests: footer text per provider combination; WARP first frame after a snapshot shows no missing glyph. |
| P2 | D1 bounded CAP reader with entity decoding and truncation | Fixture tests: MeteoAlarm Atom, NWS CAP, Canadian CAP, a 256 KiB truncated body, malformed markup; zero heap allocation beyond the response body. |
| P3 | First new early-warning region (recommended: Canada via Datamart CAP, or the WMO hub if its feed terms are confirmed) | Region routing tests for every ISO code; severity mapping; `hourly`/`daily` unchanged; live check manual-only. |
| P4 | First additional forecast provider only if D6 or a national need is recorded | Parser fixture tests mirroring the MET coverage list in `Plugins_Weather.md`. |
| P5 | Closeout: `Plugins_Weather.md` provider table, `docs/plugins/weather.md` coverage sentence, this RFC moved to Done | Spec names every provider, its terms, its region, and its attribution. |

## Validation

Same as the weather plan: `.\format.ps1`, `.\test.ps1 -Configuration Debug -Platform x64 -Rebuild`,
`.\test.ps1 -Configuration Release -Platform x64 -Rebuild`, `.\build.ps1 -Configuration Release -Platform ARM64`,
`.\validate-skills.ps1`. Automated runs MUST NOT contact any provider; every new parser is exercised through
`WeatherTestSnapshot` fixtures, and `HostPluginTests` MUST still observe zero sockets from the gallery Weather tile.

## Exit criteria

This RFC closes when D1–D7 are recorded as decisions, at least P1–P3 have shipped with their tests, the durable
provider rules live in `Plugins_Weather.md`, `docs/plugins/weather.md` names the covered regions, and this file is
moved to `Specs/Plans/Done/` with its index row removed.
