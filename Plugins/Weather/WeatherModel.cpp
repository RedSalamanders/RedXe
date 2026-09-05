#include "WeatherModel.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <winnls.h>

#include <yyjson.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
using unique_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;

constexpr char kEuropeIso2[][3] = {"AT", "BE", "BA", "BG", "HR", "CY", "CZ", "DK", "EE", "FI", "FR",
                                   "DE", "GR", "HU", "IS", "IE", "IT", "LV", "LI", "LT", "LU", "MT",
                                   "MD", "ME", "NL", "MK", "NO", "PL", "PT", "RO", "RS", "SK", "SI",
                                   "ES", "SE", "CH", "TR", "UA", "GB", "UK", "AL", "AD", "MC", "SM"};

[[nodiscard]] bool EqualsIgnoreCase(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (size_t index = 0; index < left.size(); ++index)
    {
        const unsigned char a = static_cast<unsigned char>(left[index]);
        const unsigned char b = static_cast<unsigned char>(right[index]);
        if (std::tolower(a) != std::tolower(b))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string_view JsonString(yyjson_val* value) noexcept
{
    return yyjson_is_str(value) ? std::string_view(yyjson_get_str(value), yyjson_get_len(value)) : std::string_view{};
}

[[nodiscard]] bool JsonNumber(yyjson_val* value, double& out) noexcept
{
    if (yyjson_is_num(value))
    {
        out = yyjson_get_num(value);
        return std::isfinite(out);
    }
    if (yyjson_is_str(value))
    {
        const char* text = yyjson_get_str(value);
        char* end = nullptr;
        out = std::strtod(text, &end);
        return end != text && std::isfinite(out);
    }
    return false;
}

[[nodiscard]] unique_doc ParseJsonCopy(std::string_view json) noexcept
{
    if (json.empty() || json.size() > 256U * 1024U)
    {
        return {};
    }
    yyjson_read_err error{};
    return unique_doc{
        yyjson_read_opts(const_cast<char*>(json.data()), json.size(), YYJSON_READ_NOFLAG, nullptr, &error)};
}

[[nodiscard]] WeatherAlertSeverity SeverityFromText(std::string_view text) noexcept
{
    if (EqualsIgnoreCase(text, "extreme") || EqualsIgnoreCase(text, "severe") || EqualsIgnoreCase(text, "red") ||
        text.find("4;") != std::string_view::npos)
    {
        return WeatherAlertSeverity::Red;
    }
    if (EqualsIgnoreCase(text, "moderate") || EqualsIgnoreCase(text, "orange") ||
        text.find("3;") != std::string_view::npos)
    {
        return WeatherAlertSeverity::Orange;
    }
    if (text.empty())
    {
        return WeatherAlertSeverity::None;
    }
    return WeatherAlertSeverity::Yellow;
}

void AddAlert(WeatherSnapshot& snapshot, std::string_view title, std::string_view description,
              std::string_view instructions, std::string_view authority, WeatherAlertSeverity severity, uint64_t start,
              uint64_t expiry) noexcept
{
    if (snapshot.alertCount >= snapshot.alerts.size() || severity == WeatherAlertSeverity::None)
    {
        return;
    }
    WeatherAlert& alert = snapshot.alerts[snapshot.alertCount++];
    alert = WeatherAlert{};
    WeatherCopyWide(std::wstring_view{}, alert.title.data(), alert.title.size());
    const int titleChars = MultiByteToWideChar(CP_UTF8, 0, title.data(), static_cast<int>(title.size()),
                                               alert.title.data(), static_cast<int>(alert.title.size()) - 1);
    if (titleChars > 0)
    {
        alert.title[static_cast<size_t>(titleChars)] = L'\0';
    }
    const int descriptionChars =
        MultiByteToWideChar(CP_UTF8, 0, description.data(), static_cast<int>(description.size()),
                            alert.description.data(), static_cast<int>(alert.description.size()) - 1);
    if (descriptionChars > 0)
    {
        alert.description[static_cast<size_t>(descriptionChars)] = L'\0';
    }
    const int instructionChars =
        MultiByteToWideChar(CP_UTF8, 0, instructions.data(), static_cast<int>(instructions.size()),
                            alert.instructions.data(), static_cast<int>(alert.instructions.size()) - 1);
    if (instructionChars > 0)
    {
        alert.instructions[static_cast<size_t>(instructionChars)] = L'\0';
    }
    const int authorityChars =
        MultiByteToWideChar(CP_UTF8, 0, authority.data(), static_cast<int>(authority.size()), alert.authority.data(),
                            static_cast<int>(alert.authority.size()) - 1);
    if (authorityChars > 0)
    {
        alert.authority[static_cast<size_t>(authorityChars)] = L'\0';
    }
    alert.severity = severity;
    alert.startFileTime100ns = start;
    alert.expiryFileTime100ns = expiry;
}
} // namespace

bool WeatherCopyWide(std::wstring_view source, wchar_t* destination, size_t capacity) noexcept
{
    if (!destination || capacity == 0)
    {
        return false;
    }
    const size_t copy = std::min(source.size(), capacity - 1);
    if (copy != 0)
    {
        std::memcpy(destination, source.data(), copy * sizeof(wchar_t));
    }
    destination[copy] = L'\0';
    return true;
}

bool WeatherCopyNarrow(std::string_view source, char* destination, size_t capacity) noexcept
{
    if (!destination || capacity == 0)
    {
        return false;
    }
    const size_t copy = std::min(source.size(), capacity - 1);
    if (copy != 0)
    {
        std::memcpy(destination, source.data(), copy);
    }
    destination[copy] = '\0';
    return true;
}

WeatherCondition WeatherConditionFromSymbol(std::string_view symbol) noexcept
{
    const size_t underscore = symbol.find('_');
    const std::string_view stem = underscore == std::string_view::npos ? symbol : symbol.substr(0, underscore);
    if (stem == "clearsky" || stem == "fair" || stem == "clear")
    {
        return WeatherCondition::Clear;
    }
    if (stem == "partlycloudy")
    {
        return WeatherCondition::PartlyCloudy;
    }
    if (stem == "cloudy")
    {
        return WeatherCondition::Cloudy;
    }
    if (stem.find("thunder") != std::string_view::npos)
    {
        return WeatherCondition::Thunder;
    }
    if (stem.find("snow") != std::string_view::npos)
    {
        return WeatherCondition::Snow;
    }
    if (stem.find("sleet") != std::string_view::npos)
    {
        return WeatherCondition::Sleet;
    }
    if (stem.find("rain") != std::string_view::npos || stem.find("shower") != std::string_view::npos)
    {
        return WeatherCondition::Rain;
    }
    if (stem == "fog" || stem == "mist")
    {
        return WeatherCondition::Fog;
    }
    if (stem.find("wind") != std::string_view::npos)
    {
        return WeatherCondition::Wind;
    }
    return WeatherCondition::Unknown;
}

WeatherRgb WeatherConditionColor(WeatherCondition condition) noexcept
{
    switch (condition)
    {
    case WeatherCondition::Clear:
        return {232.0f / 255.0f, 195.0f / 255.0f, 106.0f / 255.0f};
    case WeatherCondition::PartlyCloudy:
        return {200.0f / 255.0f, 196.0f / 255.0f, 184.0f / 255.0f};
    case WeatherCondition::Cloudy:
        return {168.0f / 255.0f, 176.0f / 255.0f, 184.0f / 255.0f};
    case WeatherCondition::Rain:
    case WeatherCondition::Sleet:
        return {90.0f / 255.0f, 160.0f / 255.0f, 212.0f / 255.0f};
    case WeatherCondition::Snow:
        return {208.0f / 255.0f, 228.0f / 255.0f, 240.0f / 255.0f};
    case WeatherCondition::Thunder:
        return {240.0f / 255.0f, 160.0f / 255.0f, 48.0f / 255.0f};
    case WeatherCondition::Fog:
    case WeatherCondition::Wind:
        return {138.0f / 255.0f, 148.0f / 255.0f, 156.0f / 255.0f};
    default:
        return {168.0f / 255.0f, 176.0f / 255.0f, 184.0f / 255.0f};
    }
}

WeatherRgb WeatherAlertColor(WeatherAlertSeverity severity) noexcept
{
    switch (severity)
    {
    case WeatherAlertSeverity::Red:
        return {224.0f / 255.0f, 56.0f / 255.0f, 56.0f / 255.0f};
    case WeatherAlertSeverity::Orange:
        return {240.0f / 255.0f, 122.0f / 255.0f, 32.0f / 255.0f};
    case WeatherAlertSeverity::Yellow:
        return {224.0f / 255.0f, 192.0f / 255.0f, 64.0f / 255.0f};
    default:
        return {0.92f, 0.92f, 0.94f};
    }
}

WeatherAlertSeverity WeatherHighestAlertSeverity(const WeatherSnapshot& snapshot) noexcept
{
    WeatherAlertSeverity highest = WeatherAlertSeverity::None;
    for (uint32_t index = 0; index < snapshot.alertCount; ++index)
    {
        highest = static_cast<WeatherAlertSeverity>(
            std::max(static_cast<uint32_t>(highest), static_cast<uint32_t>(snapshot.alerts[index].severity)));
    }
    return highest;
}

WeatherAlertRegion WeatherAlertRegionFromCountry(std::string_view countryCode) noexcept
{
    if (EqualsIgnoreCase(countryCode, "US") || EqualsIgnoreCase(countryCode, "PR") ||
        EqualsIgnoreCase(countryCode, "GU") || EqualsIgnoreCase(countryCode, "VI"))
    {
        return WeatherAlertRegion::UnitedStates;
    }
    for (const char (&code)[3] : kEuropeIso2)
    {
        if (EqualsIgnoreCase(countryCode, code))
        {
            return WeatherAlertRegion::Europe;
        }
    }
    return WeatherAlertRegion::None;
}

bool WeatherParseIso8601Utc(std::string_view text, uint64_t& fileTime100ns) noexcept
{
    fileTime100ns = 0;
    if (text.empty() || text.size() > 40)
    {
        return false;
    }
    char buffer[48]{};
    std::memcpy(buffer, text.data(), text.size());
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int consumed = 0;
    if (sscanf_s(buffer, "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day, &hour, &minute, &second, &consumed) != 6)
    {
        second = 0;
        consumed = 0;
        if (sscanf_s(buffer, "%4d-%2d-%2dT%2d:%2d%n", &year, &month, &day, &hour, &minute, &consumed) != 5)
        {
            return false;
        }
    }
    if (consumed <= 0 || consumed >= static_cast<int>(sizeof(buffer)))
    {
        return false;
    }
    const char* rest = buffer + consumed;
    if (*rest == '.')
    {
        ++rest;
        while (*rest >= '0' && *rest <= '9')
        {
            ++rest;
        }
    }
    int tzMinutes = 0;
    if (*rest == '+' || *rest == '-')
    {
        const int sign = *rest == '+' ? 1 : -1;
        int tzHour = 0;
        int tzMinute = 0;
        if (sscanf_s(rest + 1, "%2d:%2d", &tzHour, &tzMinute) != 2 &&
            sscanf_s(rest + 1, "%2d%2d", &tzHour, &tzMinute) != 2)
        {
            return false;
        }
        tzMinutes = sign * (tzHour * 60 + tzMinute);
    }
    else if (*rest != '\0' && *rest != 'Z' && *rest != 'z')
    {
        return false;
    }

    SYSTEMTIME time{};
    time.wYear = static_cast<WORD>(year);
    time.wMonth = static_cast<WORD>(month);
    time.wDay = static_cast<WORD>(day);
    time.wHour = static_cast<WORD>(hour);
    time.wMinute = static_cast<WORD>(minute);
    time.wSecond = static_cast<WORD>(second);
    FILETIME fileTime{};
    if (SystemTimeToFileTime(&time, &fileTime) == FALSE)
    {
        return false;
    }
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    const int64_t utc = static_cast<int64_t>(value.QuadPart) - static_cast<int64_t>(tzMinutes) * 60LL * 10000000LL;
    if (utc < 0)
    {
        return false;
    }
    fileTime100ns = static_cast<uint64_t>(utc);
    return true;
}

bool WeatherParseLatLon(std::string_view text, double& latitude, double& longitude) noexcept
{
    latitude = 0.0;
    longitude = 0.0;
    const size_t comma = text.find(',');
    if (comma == std::string_view::npos || comma == 0 || comma + 1 >= text.size())
    {
        return false;
    }
    const std::string_view latText = text.substr(0, comma);
    const std::string_view lonText = text.substr(comma + 1);
    char* latEnd = nullptr;
    char* lonEnd = nullptr;
    std::array<char, 64> latBuffer{};
    std::array<char, 64> lonBuffer{};
    if (latText.size() >= latBuffer.size() || lonText.size() >= lonBuffer.size())
    {
        return false;
    }
    std::memcpy(latBuffer.data(), latText.data(), latText.size());
    std::memcpy(lonBuffer.data(), lonText.data(), lonText.size());
    latitude = std::strtod(latBuffer.data(), &latEnd);
    longitude = std::strtod(lonBuffer.data(), &lonEnd);
    if (latEnd == latBuffer.data() || lonEnd == lonBuffer.data() || !std::isfinite(latitude) ||
        !std::isfinite(longitude) || latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0)
    {
        return false;
    }
    return true;
}

bool WeatherTryAutomaticLocation(wchar_t* name, size_t nameCapacity, char* countryCode, size_t countryCapacity,
                                 double& latitude, double& longitude) noexcept
{
    latitude = 0.0;
    longitude = 0.0;
    if (name && nameCapacity != 0)
    {
        name[0] = L'\0';
    }
    if (countryCode && countryCapacity != 0)
    {
        countryCode[0] = '\0';
    }
    const GEOID geoId = GetUserGeoID(GEOCLASS_NATION);
    if (geoId == GEOID_NOT_AVAILABLE)
    {
        return false;
    }
    std::array<wchar_t, 64> latText{};
    std::array<wchar_t, 64> lonText{};
    std::array<wchar_t, 8> iso2{};
    std::array<wchar_t, kWeatherMaximumNameCharacters> friendly{};
    if (GetGeoInfoW(geoId, GEO_LATITUDE, latText.data(), static_cast<int>(latText.size()), 0) <= 0 ||
        GetGeoInfoW(geoId, GEO_LONGITUDE, lonText.data(), static_cast<int>(lonText.size()), 0) <= 0)
    {
        return false;
    }
    latitude = wcstod(latText.data(), nullptr);
    longitude = wcstod(lonText.data(), nullptr);
    if (!std::isfinite(latitude) || !std::isfinite(longitude))
    {
        return false;
    }
    if (GetGeoInfoW(geoId, GEO_ISO2, iso2.data(), static_cast<int>(iso2.size()), 0) > 0 && countryCode &&
        countryCapacity > 1)
    {
        const int converted = WideCharToMultiByte(CP_UTF8, 0, iso2.data(), -1, countryCode,
                                                  static_cast<int>(countryCapacity), nullptr, nullptr);
        if (converted <= 0)
        {
            countryCode[0] = '\0';
        }
    }
    if (GetGeoInfoW(geoId, GEO_FRIENDLYNAME, friendly.data(), static_cast<int>(friendly.size()), 0) > 0)
    {
        WeatherCopyWide(friendly.data(), name, nameCapacity);
    }
    return true;
}

HRESULT WeatherParseLocationForecast(std::string_view json, WeatherSnapshot& snapshot) noexcept
{
    unique_doc document = ParseJsonCopy(json);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    yyjson_val* properties = yyjson_is_obj(root) ? yyjson_obj_get(root, "properties") : nullptr;
    yyjson_val* timeseries = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "timeseries") : nullptr;
    if (!yyjson_is_arr(timeseries) || yyjson_arr_size(timeseries) == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    snapshot.hourlyCount = 0;
    snapshot.dailyCount = 0;
    bool haveCurrent = false;
    double dayMin = 0.0;
    double dayMax = 0.0;
    uint64_t currentDay = 0;
    WeatherDailyForecast accumulating{};
    bool accumulatingDay = false;

    const size_t count = yyjson_arr_size(timeseries);
    for (size_t index = 0; index < count; ++index)
    {
        yyjson_val* entry = yyjson_arr_get(timeseries, index);
        yyjson_val* data = yyjson_is_obj(entry) ? yyjson_obj_get(entry, "data") : nullptr;
        yyjson_val* instant = yyjson_is_obj(data) ? yyjson_obj_get(data, "instant") : nullptr;
        yyjson_val* details = yyjson_is_obj(instant) ? yyjson_obj_get(instant, "details") : nullptr;
        double temperature = 0.0;
        double wind = 0.0;
        uint64_t time = 0;
        if (!WeatherParseIso8601Utc(JsonString(yyjson_obj_get(entry, "time")), time) ||
            !JsonNumber(yyjson_obj_get(details, "air_temperature"), temperature))
        {
            continue;
        }
        (void)JsonNumber(yyjson_obj_get(details, "wind_speed"), wind);
        yyjson_val* next1 = yyjson_is_obj(data) ? yyjson_obj_get(data, "next_1_hours") : nullptr;
        if (!next1)
        {
            next1 = yyjson_is_obj(data) ? yyjson_obj_get(data, "next_6_hours") : nullptr;
        }
        yyjson_val* summary = yyjson_is_obj(next1) ? yyjson_obj_get(next1, "summary") : nullptr;
        const WeatherCondition condition =
            WeatherConditionFromSymbol(JsonString(yyjson_obj_get(summary, "symbol_code")));
        if (!haveCurrent)
        {
            snapshot.observationTimeFileTime100ns = time;
            snapshot.temperatureCelsius = static_cast<float>(temperature);
            snapshot.windMetersPerSecond = static_cast<float>(wind);
            snapshot.currentCondition = condition;
            haveCurrent = true;
        }
        if (snapshot.hourlyCount < snapshot.hourly.size())
        {
            WeatherHourlyForecast& hour = snapshot.hourly[snapshot.hourlyCount++];
            hour.timeFileTime100ns = time;
            hour.temperatureCelsius = static_cast<float>(temperature);
            hour.condition = condition;
        }

        constexpr uint64_t kDay = 864000000000ULL;
        const uint64_t day = (time / kDay) * kDay;
        if (!accumulatingDay || day != currentDay)
        {
            if (accumulatingDay && snapshot.dailyCount < snapshot.daily.size())
            {
                accumulating.minimumCelsius = static_cast<float>(dayMin);
                accumulating.maximumCelsius = static_cast<float>(dayMax);
                snapshot.daily[snapshot.dailyCount++] = accumulating;
            }
            accumulating = WeatherDailyForecast{};
            accumulating.dayFileTime100ns = day;
            accumulating.condition = condition;
            dayMin = temperature;
            dayMax = temperature;
            currentDay = day;
            accumulatingDay = true;
        }
        else
        {
            if (temperature > dayMax)
            {
                dayMax = temperature;
                accumulating.condition = condition;
            }
            if (temperature < dayMin)
            {
                dayMin = temperature;
            }
        }
    }
    if (accumulatingDay && snapshot.dailyCount < snapshot.daily.size())
    {
        accumulating.minimumCelsius = static_cast<float>(dayMin);
        accumulating.maximumCelsius = static_cast<float>(dayMax);
        snapshot.daily[snapshot.dailyCount++] = accumulating;
    }
    if (snapshot.dailyCount > 0)
    {
        snapshot.todayMinimumCelsius = snapshot.daily[0].minimumCelsius;
        snapshot.todayMaximumCelsius = snapshot.daily[0].maximumCelsius;
    }
    return haveCurrent ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

HRESULT WeatherParseSunrise(std::string_view json, WeatherSnapshot& snapshot) noexcept
{
    unique_doc document = ParseJsonCopy(json);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    yyjson_val* properties = yyjson_is_obj(root) ? yyjson_obj_get(root, "properties") : nullptr;
    yyjson_val* sunrise = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "sunrise") : nullptr;
    yyjson_val* sunset = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "sunset") : nullptr;
    if (!sunrise)
    {
        yyjson_val* when = yyjson_is_obj(properties) ? yyjson_obj_get(properties, "when") : nullptr;
        if (yyjson_is_arr(when) && yyjson_arr_size(when) > 0)
        {
            yyjson_val* first = yyjson_arr_get(when, 0);
            sunrise = yyjson_is_obj(first) ? yyjson_obj_get(first, "sunrise") : nullptr;
            sunset = yyjson_is_obj(first) ? yyjson_obj_get(first, "sunset") : nullptr;
        }
    }
    uint64_t rise = 0;
    uint64_t set = 0;
    const bool haveRise =
        WeatherParseIso8601Utc(JsonString(yyjson_is_obj(sunrise) ? yyjson_obj_get(sunrise, "time") : sunrise), rise);
    const bool haveSet =
        WeatherParseIso8601Utc(JsonString(yyjson_is_obj(sunset) ? yyjson_obj_get(sunset, "time") : sunset), set);
    if (haveRise)
    {
        snapshot.sunriseFileTime100ns = rise;
    }
    if (haveSet)
    {
        snapshot.sunsetFileTime100ns = set;
    }
    return haveRise || haveSet ? S_OK : S_FALSE;
}

HRESULT WeatherParseNominatim(std::string_view json, WeatherSnapshot& snapshot) noexcept
{
    unique_doc document = ParseJsonCopy(json);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    yyjson_val* entry = root;
    if (yyjson_is_arr(root))
    {
        if (yyjson_arr_size(root) == 0)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        entry = yyjson_arr_get(root, 0);
    }
    if (!yyjson_is_obj(entry))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    double latitude = 0.0;
    double longitude = 0.0;
    if (!JsonNumber(yyjson_obj_get(entry, "lat"), latitude) || !JsonNumber(yyjson_obj_get(entry, "lon"), longitude))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    snapshot.latitude = latitude;
    snapshot.longitude = longitude;
    snapshot.hasCoordinates = true;
    yyjson_val* address = yyjson_obj_get(entry, "address");
    std::string_view name = JsonString(yyjson_obj_get(address, "city"));
    if (name.empty())
    {
        name = JsonString(yyjson_obj_get(address, "town"));
    }
    if (name.empty())
    {
        name = JsonString(yyjson_obj_get(address, "municipality"));
    }
    if (name.empty())
    {
        name = JsonString(yyjson_obj_get(address, "village"));
    }
    if (name.empty())
    {
        name = JsonString(yyjson_obj_get(address, "hamlet"));
    }
    if (name.empty())
    {
        name = JsonString(yyjson_obj_get(address, "county"));
    }
    if (name.empty())
    {
        name = JsonString(yyjson_obj_get(entry, "name"));
    }
    if (name.empty())
    {
        name = JsonString(yyjson_obj_get(entry, "display_name"));
        const size_t comma = name.find(',');
        if (comma != std::string_view::npos)
        {
            name = name.substr(0, comma);
        }
    }
    std::array<wchar_t, kWeatherMaximumNameCharacters> wide{};
    const int converted = MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wide.data(),
                                              static_cast<int>(wide.size()) - 1);
    if (converted > 0)
    {
        wide[static_cast<size_t>(converted)] = L'\0';
        WeatherCopyWide(wide.data(), snapshot.locationName.data(), snapshot.locationName.size());
    }
    std::string_view country = JsonString(yyjson_obj_get(address, "country_code"));
    if (!country.empty())
    {
        WeatherCopyNarrow(country, snapshot.countryCode.data(), snapshot.countryCode.size());
        for (char& value : snapshot.countryCode)
        {
            if (value >= 'a' && value <= 'z')
            {
                value = static_cast<char>(value - 'a' + 'A');
            }
        }
    }
    return S_OK;
}

HRESULT WeatherParseMeteoAlarm(std::string_view json, WeatherSnapshot& snapshot) noexcept
{
    unique_doc document = ParseJsonCopy(json);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    yyjson_val* warnings = nullptr;
    if (yyjson_is_arr(root))
    {
        warnings = root;
    }
    else if (yyjson_is_obj(root))
    {
        warnings = yyjson_obj_get(root, "warning");
        if (!warnings)
        {
            warnings = yyjson_obj_get(root, "warnings");
        }
        if (!warnings)
        {
            warnings = yyjson_obj_get(root, "features");
        }
    }
    if (!yyjson_is_arr(warnings))
    {
        return S_FALSE;
    }
    snapshot.alertCount = 0;
    const size_t count = yyjson_arr_size(warnings);
    for (size_t index = 0; index < count && snapshot.alertCount < snapshot.alerts.size(); ++index)
    {
        yyjson_val* item = yyjson_arr_get(warnings, index);
        yyjson_val* properties = yyjson_is_obj(item) ? yyjson_obj_get(item, "properties") : nullptr;
        if (yyjson_is_obj(properties))
        {
            item = properties;
        }
        if (!yyjson_is_obj(item))
        {
            continue;
        }
        std::string_view title = JsonString(yyjson_obj_get(item, "headline"));
        if (title.empty())
        {
            title = JsonString(yyjson_obj_get(item, "event"));
        }
        std::string_view description = JsonString(yyjson_obj_get(item, "description"));
        std::string_view instructions = JsonString(yyjson_obj_get(item, "instruction"));
        std::string_view authority = JsonString(yyjson_obj_get(item, "sender"));
        if (authority.empty())
        {
            authority = "MeteoAlarm";
        }
        std::string_view severityText = JsonString(yyjson_obj_get(item, "awareness_level"));
        if (severityText.empty())
        {
            severityText = JsonString(yyjson_obj_get(item, "severity"));
        }
        uint64_t start = 0;
        uint64_t expiry = 0;
        (void)WeatherParseIso8601Utc(JsonString(yyjson_obj_get(item, "onset")), start);
        (void)WeatherParseIso8601Utc(JsonString(yyjson_obj_get(item, "expires")), expiry);
        AddAlert(snapshot, title, description, instructions, authority, SeverityFromText(severityText), start, expiry);
    }
    return S_OK;
}

HRESULT WeatherParseNwsAlerts(std::string_view json, WeatherSnapshot& snapshot) noexcept
{
    unique_doc document = ParseJsonCopy(json);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    yyjson_val* features = yyjson_is_obj(root) ? yyjson_obj_get(root, "features") : nullptr;
    if (!yyjson_is_arr(features))
    {
        return S_FALSE;
    }
    snapshot.alertCount = 0;
    const size_t count = yyjson_arr_size(features);
    for (size_t index = 0; index < count && snapshot.alertCount < snapshot.alerts.size(); ++index)
    {
        yyjson_val* feature = yyjson_arr_get(features, index);
        yyjson_val* properties = yyjson_is_obj(feature) ? yyjson_obj_get(feature, "properties") : nullptr;
        if (!yyjson_is_obj(properties))
        {
            continue;
        }
        std::string_view title = JsonString(yyjson_obj_get(properties, "headline"));
        if (title.empty())
        {
            title = JsonString(yyjson_obj_get(properties, "event"));
        }
        std::string_view description = JsonString(yyjson_obj_get(properties, "description"));
        std::string_view instructions = JsonString(yyjson_obj_get(properties, "instruction"));
        std::string_view authority = JsonString(yyjson_obj_get(properties, "senderName"));
        if (authority.empty())
        {
            authority = "NWS";
        }
        uint64_t start = 0;
        uint64_t expiry = 0;
        (void)WeatherParseIso8601Utc(JsonString(yyjson_obj_get(properties, "onset")), start);
        (void)WeatherParseIso8601Utc(JsonString(yyjson_obj_get(properties, "expires")), expiry);
        AddAlert(snapshot, title, description, instructions, authority,
                 SeverityFromText(JsonString(yyjson_obj_get(properties, "severity"))), start, expiry);
    }
    return S_OK;
}

uint32_t WeatherFormatTemperature(float celsius, WeatherTemperatureUnit unit, wchar_t* text, uint32_t capacity) noexcept
{
    if (!text || capacity == 0)
    {
        return 0;
    }
    const float value = unit == WeatherTemperatureUnit::Fahrenheit ? (celsius * 9.0f / 5.0f + 32.0f) : celsius;
    const wchar_t* suffix = unit == WeatherTemperatureUnit::Fahrenheit ? L"\u00B0F" : L"\u00B0C";
    const int written = swprintf_s(text, capacity, L"%.0f%s", static_cast<double>(value), suffix);
    return written > 0 ? static_cast<uint32_t>(written) : 0;
}

uint32_t WeatherFormatTemperatureRange(float minimumCelsius, float maximumCelsius, WeatherTemperatureUnit unit,
                                       wchar_t* text, uint32_t capacity) noexcept
{
    if (!text || capacity == 0)
    {
        return 0;
    }
    wchar_t lo[16]{};
    wchar_t hi[16]{};
    if (WeatherFormatTemperature(minimumCelsius, unit, lo, 16) == 0 ||
        WeatherFormatTemperature(maximumCelsius, unit, hi, 16) == 0)
    {
        return 0;
    }
    const int written = swprintf_s(text, capacity, L"%s | %s", lo, hi);
    return written > 0 ? static_cast<uint32_t>(written) : 0;
}

uint32_t WeatherFormatWind(float metersPerSecond, WeatherWindUnit unit, wchar_t* text, uint32_t capacity) noexcept
{
    if (!text || capacity == 0)
    {
        return 0;
    }
    const float value = unit == WeatherWindUnit::MilesPerHour ? metersPerSecond * 2.2369363f : metersPerSecond * 3.6f;
    const wchar_t* suffix = unit == WeatherWindUnit::MilesPerHour ? L"mph" : L"km/h";
    const int written = swprintf_s(text, capacity, L"%.0f%s", static_cast<double>(value), suffix);
    return written > 0 ? static_cast<uint32_t>(written) : 0;
}

namespace
{
[[nodiscard]] bool FileTimeToLocalSystem(uint64_t fileTime100ns, SYSTEMTIME& local) noexcept
{
    if (fileTime100ns == 0)
    {
        return false;
    }
    const FILETIME utc{static_cast<DWORD>(fileTime100ns), static_cast<DWORD>(fileTime100ns >> 32)};
    FILETIME localFileTime{};
    if (FileTimeToLocalFileTime(&utc, &localFileTime) == FALSE)
    {
        return false;
    }
    return FileTimeToSystemTime(&localFileTime, &local) != FALSE;
}
[[nodiscard]] uint32_t CivilDayNumber(const SYSTEMTIME& local) noexcept
{
    SYSTEMTIME noon = local;
    noon.wHour = 12;
    noon.wMinute = 0;
    noon.wSecond = 0;
    noon.wMilliseconds = 0;
    FILETIME fileTime{};
    if (SystemTimeToFileTime(&noon, &fileTime) == FALSE)
    {
        return 0;
    }
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return static_cast<uint32_t>(value.QuadPart / 864000000000ULL);
}

[[nodiscard]] uint64_t CurrentUtcFileTime() noexcept
{
    FILETIME fileTime{};
    GetSystemTimeAsFileTime(&fileTime);
    return (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | static_cast<uint64_t>(fileTime.dwLowDateTime);
}
} // namespace

uint32_t WeatherFormatClock(uint64_t fileTime100ns, wchar_t* text, uint32_t capacity) noexcept
{
    if (!text || capacity == 0)
    {
        return 0;
    }
    SYSTEMTIME local{};
    if (!FileTimeToLocalSystem(fileTime100ns, local))
    {
        text[0] = L'\0';
        return 0;
    }
    const int written = swprintf_s(text, capacity, L"%02u:%02u", local.wHour, local.wMinute);
    return written > 0 ? static_cast<uint32_t>(written) : 0;
}

uint32_t WeatherFormatForecastDay(uint64_t fileTime100ns, uint64_t nowFileTime100ns, uint32_t fallbackIndex,
                                  wchar_t* text, uint32_t capacity) noexcept
{
    if (!text || capacity == 0)
    {
        return 0;
    }
    constexpr const wchar_t* weekdays[] = {L"Sunday",   L"Monday", L"Tuesday", L"Wednesday",
                                           L"Thursday", L"Friday", L"Saturday"};
    const uint64_t nowUtc = nowFileTime100ns == 0 ? CurrentUtcFileTime() : nowFileTime100ns;
    SYSTEMTIME nowLocal{};
    SYSTEMTIME dayLocal{};
    const bool haveNow = FileTimeToLocalSystem(nowUtc, nowLocal);
    bool haveDay = FileTimeToLocalSystem(fileTime100ns, dayLocal);
    if (!haveDay && haveNow)
    {
        haveDay = FileTimeToLocalSystem(nowUtc + static_cast<uint64_t>(fallbackIndex) * 864000000000ULL, dayLocal);
    }
    if (haveDay && haveNow)
    {
        const uint32_t dayNumber = CivilDayNumber(dayLocal);
        const uint32_t nowNumber = CivilDayNumber(nowLocal);
        if (dayNumber == nowNumber)
        {
            const int written = swprintf_s(text, capacity, L"Today");
            return written > 0 ? static_cast<uint32_t>(written) : 0;
        }
        if (dayNumber == nowNumber + 1)
        {
            const int written = swprintf_s(text, capacity, L"Tomorrow");
            return written > 0 ? static_cast<uint32_t>(written) : 0;
        }
        if (dayLocal.wDayOfWeek < 7)
        {
            const int written = swprintf_s(text, capacity, L"%s, %u", weekdays[dayLocal.wDayOfWeek], dayLocal.wDay);
            return written > 0 ? static_cast<uint32_t>(written) : 0;
        }
    }
    if (fallbackIndex == 0)
    {
        const int written = swprintf_s(text, capacity, L"Today");
        return written > 0 ? static_cast<uint32_t>(written) : 0;
    }
    if (fallbackIndex == 1)
    {
        const int written = swprintf_s(text, capacity, L"Tomorrow");
        return written > 0 ? static_cast<uint32_t>(written) : 0;
    }
    const int written = swprintf_s(text, capacity, L"%s, %u", weekdays[fallbackIndex % 7], fallbackIndex + 1);
    return written > 0 ? static_cast<uint32_t>(written) : 0;
}

uint32_t WeatherClampRefreshMilliseconds(uint32_t milliseconds) noexcept
{
    if (milliseconds < kWeatherMinimumRefreshMilliseconds)
    {
        return kWeatherMinimumRefreshMilliseconds;
    }
    if (milliseconds > kWeatherMaximumRefreshMilliseconds)
    {
        return kWeatherMaximumRefreshMilliseconds;
    }
    return milliseconds;
}

HRESULT WeatherReadConfiguration(std::string_view json, WeatherConfiguration& configuration) noexcept
{
    configuration = WeatherConfiguration{};
    if (json.empty())
    {
        return S_OK;
    }
    unique_doc document = ParseJsonCopy(json);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (!yyjson_is_obj(root))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const std::string_view mode = JsonString(yyjson_obj_get(root, "locationMode"));
    if (mode == "manual")
    {
        configuration.locationMode = WeatherLocationMode::Manual;
    }
    else if (!mode.empty() && mode != "automatic")
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const std::string_view location = JsonString(yyjson_obj_get(root, "location"));
    if (location.size() > 128)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    WeatherCopyNarrow(location, configuration.location.data(), configuration.location.size());
    const std::string_view temperature = JsonString(yyjson_obj_get(root, "temperatureUnit"));
    if (temperature == "fahrenheit")
    {
        configuration.temperatureUnit = WeatherTemperatureUnit::Fahrenheit;
    }
    else if (!temperature.empty() && temperature != "celsius")
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const std::string_view wind = JsonString(yyjson_obj_get(root, "windUnit"));
    if (wind == "mph")
    {
        configuration.windUnit = WeatherWindUnit::MilesPerHour;
    }
    else if (!wind.empty() && wind != "kmh")
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}
