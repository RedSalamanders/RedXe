# RFC: other forecast providers and regional early-warning providers for the Weather widget

Status: `DONE` (decided and shipped 2026-09-16; see "Decisions" and "Outcome")
Created: 2026-09-16
Owner: `Plugins/Weather`, `Tests/WeatherTests`, and the settings chain when a provider becomes user-selectable

Sections up to "Candidate providers" describe the pipeline as it was when the RFC was written; the current
contract, including the Canada and Hong Kong routes and the composed attribution, is `Plugins_Weather.md`.

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
5. Add the attribution string (decision D3). ASCII names need no pre-rasterization: the static atlas already
   holds every printable ASCII glyph; only a non-ASCII name would need `RasterizeGlyphs`.
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
5. Add the body to `WeatherAlertProvider` and `WeatherAlertProviderName` (ASCII, see above).
6. Record the provider, its terms, and its region in `Plugins_Weather.md`; add the user-visible coverage change
   to `docs/plugins/weather.md`.

As shipped, steps 1–3 are `WeatherAlertRegionFromCountry`, `WeatherBuildAlertUrl`, and `WeatherParseAlerts` in
`WeatherModel.cpp`; `FetchLive` calls exactly those three, so a new region never touches `Weather.cpp`.

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
| Canada (**shipped**) | ECCC MSC GeoMet, layer `Current-Alerts` (titled "[experimental]") | WMS 1.3.0 `GetFeatureInfo` at `geo.weather.gc.ca/geomet`, `INFO_FORMAT=application/json`, `CRS=EPSG:4326` (latitude first), a 0.02° box with `WIDTH=HEIGHT=101`, `I=J=50`, `FEATURE_COUNT=6` | GeoJSON FeatureCollection; properties `alert_type` (warning/watch/advisory/statement), `alert_name_en`, `alert_text_en`, `publication/expiration/validity/event_end_datetime`, `status_en`, `display_status`, `risk_colour_en`, `feature_name_en`, `province`, plus the polygon (~4 KB per feature) | none | Live query verified at St. John's NL (one special weather statement); empty `features` elsewhere. The Datamart CAP directory tree (`dd.weather.gc.ca/today/alerts/cap/{YYYYMMDD}/{OFFICE}/{hh}/`) was rejected: it needs date/office/hour traversal and polygon matching, several documents per refresh. | 2026-09-16 |
| Japan | JMA bosai warning | `www.jma.go.jp/bosai/warning/data/warning/{areaCode}.json` (`130000` = Tokyo, 14 KB) | JSON, Japanese text, numeric warning codes with `status` 発表/継続/解除 | none | Live response verified. Deferred: warnings are per sub-prefectural area (Tokyo's Izu islands share `130000` with the city), so a correct route needs the JMA class-20 municipality code from the reverse-geocoded Japanese name and a code→English table; no published terms for automated use. | 2026-09-16 |
| Hong Kong (**shipped**) | HKO Open Data | `data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=warnsum&lang=en` | JSON object keyed by statement (`WFIRE`, `WFROST`, `WHOT`, `WCOLD`, `WMSGNL`, `WRAIN`, `WFNTSA`, `WL`, `WTCSGNL`, `WTMW`, `WTS`) with `name`, `code`, `actionCode` (ISSUE/REISSUE/CANCEL/EXTEND/UPDATE), `issueTime`, `updateTime`, optional `expireTime`/`type`; `{}` when nothing is in force | none | Documented in the HKO Open Data API PDF (v1.12/1.13); live `{}` verified; single territory, no coordinates needed | 2026-09-16 |
| Australia | Bureau of Meteorology | CAP-AU XML and warning products on anonymous FTP `ftp.bom.gov.au` (`/anon/gen/fwo/`) | CAP-AU XML | none (anonymous FTP; Registered User Services for SLAs) | **`www.bom.gov.au` blocks automated HTTP (WAF, 403)**; the HTTPS curl in `Weather.dll` cannot use FTP (D4). Rejected. | 2026-09-16 |
| Brazil | INMET | `apiprevmet3.inmet.gov.br/avisos/ativos` (JSON `hoje`/`futuro`/`passado` with `descricao`, `severidade`, `aviso_cor`, `estados`, `geocodes`, polygon, base64 `icone`); `alertas2.inmet.gov.br` RSS/CAP | JSON / CAP XML | none | Rejected for now: one fetch returned the full document, the next two reset the connection (`ECONNRESET`, curl `000`), and every alert embeds a polygon and a base64 icon, so the body does not fit 256 KiB reliably. Municipality matching would use Nominatim `ISO3166-2-lvl4` (`BR-SP`). | 2026-09-16 |
| South Africa | SAWS | `caps.weathersa.co.za/Home/RssFeed` (RSS 2.0, `Public Domain`) linking one CAP XML per item | RSS + CAP XML | none | Rejected for now: the feed is a rolling list of 435 items and 298 KiB (over the 256 KiB bound), carries no expiry or severity in the RSS, and needs one CAP fetch per item; province is only the file-name prefix (`WC_`, `KZN_`). | 2026-09-16 |
| New Zealand | MetService | No documented public API; CAP reaches aggregators | — | — | Prefer the global aggregator row | verify |
| India, Africa, rest of Asia-Pacific, Latin America | National services via the WMO Alert Hub / SWIC (Alert-Hub.Org "Filtered Alert Hub") | Aggregated CAP feeds of the WMO Register of Alerting Authorities; source list at `alert-hub.s3.amazonaws.com/cap-sources.html` (JavaScript-rendered) | Atom + CAP 1.2 | none stated | The consumer feed URL, filtering by area, and the terms are not published on the static pages fetched on 2026-09-16 (`severeweather.wmo.int/feeds.html` renders an empty table without JavaScript; a guessed `v2/json` path is 404). Confirm with WMO/Alert-Hub.Org before relying on it. One aggregator would cover every remaining region with a single CAP reader. | partially, feed URL `verify` |

Any region with no row keeps today's behavior: forecast only, no alert banner, status `Ok` rather than
`Degraded`.

## Decisions (recorded 2026-09-16)

| ID | Decision | Outcome |
| --- | --- | --- |
| D1 | CAP/Atom XML reading | **Deferred with the consumer.** No CAP source verified today fits the one-document, 256 KiB model (SAWS 298 KiB rolling RSS plus one CAP per item; Datamart directory traversal; BOM FTP; WMO hub feed unpublished). When one does, ship a bounded forward-scanning reader (no DOM, no MSXML) in `WeatherModel.cpp` in the same change, with truncated-body and malformed-markup fixtures. Not built speculatively. |
| D2 | Sub-national routing key | **Country code only**, plus per-point queries where the feed answers per point (NWS `point=`, GeoMet `GetFeatureInfo`). A state/province key is added only with the first provider that needs it (JMA, INMET). |
| D3 | Provider identity on the snapshot | **`WeatherAlertProvider` on `WeatherSnapshot`**, set by every parser; `WeatherAlertProviderName` composes the footer. A forecast-provider enum is not added while MET Norway is the only forecast source. Names are ASCII, so no pre-rasterization was needed. |
| D4 | Transport | **HTTPS only.** BOM stays out. |
| D5 | User selection | **Automatic by region only.** No new settings keys; the settings chain is untouched. |
| D6 | Open-Meteo eligibility | **Excluded** until RedXe's distribution is recorded as non-commercial; nothing else needs a second forecast source. |
| D7 | Per-provider refresh | Providers without `Expires` (HKO, GeoMet) use the 15-minute default; the 5-minute floor stands. |

## Outcome

Shipped on 2026-09-16 in `Plugins/Weather` and `Tests/WeatherTests`:

- `WeatherAlertRegion::Canada` and `::HongKong`; `WeatherBuildAlertUrl` and `WeatherParseAlerts` centralize the
  one-document-per-region rule, and `FetchLive` calls only those.
- `WeatherParseEnvironmentCanadaAlerts` (GeoMet `Current-Alerts` GeoJSON; risk colour, then warning/watch/advisory/
  statement tiers, tornado warnings red, ended/hidden skipped, capitalised names) and `WeatherParseHongKongWarnings`
  (`warnsum`; tier and title from the statement code, cancellations skipped).
- `WeatherAlertProvider` recorded by all four parsers; the footer composes `MET Norway  |  <body>` while that body's
  alerts are on screen, then `  |  Cached forecast`.
- Offline coverage: routing for every region and unrouted countries, all four parsers on trimmed live fixtures,
  severity tiers, URL builder bounds, fixture slots for the new documents, and the footer attribution asserted
  through the WARP diagnostics. Live GeoMet and HKO calls were made by hand on 2026-09-16 only.

Not shipped, by decision: a CAP reader (D1), JMA (needs sub-prefectural routing), INMET (unstable, oversized), SAWS
(oversized rolling feed), BOM (FTP), the WMO hub (feed terms unpublished), and any second forecast provider. Each is
recorded above with the evidence, so the next region starts from the recipe rather than from research.

## Validation performed

`WeatherTests` Debug and Release x64 (parsers, routing, URLs, WARP attribution), `Weather.dll` and `WeatherTests`
built for ARM64 Release, per-file clang-format, and `validate-skills.ps1`. `test.ps1` as a whole was blocked at
the time by another in-flight change to `IRedXeHost` outside this plugin; the weather executables were run directly.
