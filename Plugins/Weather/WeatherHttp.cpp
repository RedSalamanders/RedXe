#include "WeatherHttp.h"

#include "WeatherModel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>

#include <curl/curl.h>

namespace
{
struct CacheEntry final
{
    std::array<char, kWeatherMaximumUrlBytes + 1> url{};
    std::array<char, 128> etag{};
    std::array<char, 128> lastModified{};
    std::unique_ptr<char[]> body;
    uint32_t bytes = 0;
    uint64_t expiresTick = 0;
    bool used = false;
};

std::once_flag g_curlOnce;
SRWLOCK g_cacheLock = SRWLOCK_INIT;
std::array<CacheEntry, kWeatherHttpCacheEntries> g_cache{};
bool g_curlReady = false;
std::atomic<uint32_t> g_httpGetCount{0};

struct FetchState final
{
    WeatherHttpResponse* response = nullptr;
    HANDLE cancelEvent = nullptr;
};

[[nodiscard]] HRESULT EnsureBodyBuffer(std::unique_ptr<char[]>& body, uint32_t& capacity) noexcept
{
    if (body && capacity >= kWeatherMaximumBodyBytes)
    {
        return S_OK;
    }
    body.reset(new (std::nothrow) char[kWeatherMaximumBodyBytes + 1]);
    if (!body)
    {
        capacity = 0;
        return E_OUTOFMEMORY;
    }
    capacity = kWeatherMaximumBodyBytes;
    body[0] = '\0';
    return S_OK;
}

[[nodiscard]] HRESULT PrepareResponse(WeatherHttpResponse& response) noexcept
{
    const HRESULT result = EnsureBodyBuffer(response.body, response.capacity);
    if (FAILED(result))
    {
        return result;
    }
    response.bytes = 0;
    response.status = 0;
    response.expiresDelayMilliseconds = 0;
    response.notModified = false;
    response.body[0] = '\0';
    return S_OK;
}

size_t WriteBody(char* pointer, size_t size, size_t count, void* user) noexcept
{
    auto* state = static_cast<FetchState*>(user);
    if (!state || !state->response || !state->response->body)
    {
        return 0;
    }
    const size_t bytes = size * count;
    if (state->response->bytes + bytes > state->response->capacity)
    {
        return 0;
    }
    std::memcpy(state->response->body.get() + state->response->bytes, pointer, bytes);
    state->response->bytes += static_cast<uint32_t>(bytes);
    state->response->body[state->response->bytes] = '\0';
    return bytes;
}

int Progress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept
{
    auto* state = static_cast<FetchState*>(user);
    if (state && state->cancelEvent && WaitForSingleObject(state->cancelEvent, 0) == WAIT_OBJECT_0)
    {
        return 1;
    }
    return 0;
}

size_t HeaderCallback(char* buffer, size_t size, size_t count, void* user) noexcept
{
    auto* headers = static_cast<std::array<char, 2048>*>(user);
    const size_t bytes = size * count;
    if (!headers || bytes == 0)
    {
        return bytes;
    }
    const size_t used = std::strlen(headers->data());
    if (used + bytes + 1 >= headers->size())
    {
        return bytes;
    }
    std::memcpy(headers->data() + used, buffer, bytes);
    (*headers)[used + bytes] = '\0';
    return bytes;
}

[[nodiscard]] std::string_view HeaderValue(std::string_view headers, std::string_view name) noexcept
{
    size_t cursor = 0;
    while (cursor < headers.size())
    {
        const size_t lineEnd = headers.find("\r\n", cursor);
        const std::string_view line =
            headers.substr(cursor, lineEnd == std::string_view::npos ? headers.size() - cursor : lineEnd - cursor);
        cursor = lineEnd == std::string_view::npos ? headers.size() : lineEnd + 2;
        if (line.size() <= name.size() + 1)
        {
            continue;
        }
        bool match = true;
        for (size_t index = 0; index < name.size(); ++index)
        {
            const unsigned char left = static_cast<unsigned char>(line[index]);
            const unsigned char right = static_cast<unsigned char>(name[index]);
            if (std::tolower(left) != std::tolower(right))
            {
                match = false;
                break;
            }
        }
        if (!match || line[name.size()] != ':')
        {
            continue;
        }
        size_t start = name.size() + 1;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
        {
            ++start;
        }
        return line.substr(start);
    }
    return {};
}

[[nodiscard]] uint32_t ExpiresDelayFromHeaders(std::string_view headers, uint64_t now) noexcept
{
    const std::string_view cacheControl = HeaderValue(headers, "cache-control");
    const size_t maxAge = cacheControl.find("max-age=");
    if (maxAge != std::string_view::npos)
    {
        uint32_t seconds = 0;
        const std::string_view number = cacheControl.substr(maxAge + 8);
        for (char value : number)
        {
            if (value < '0' || value > '9')
            {
                break;
            }
            seconds = seconds * 10U + static_cast<uint32_t>(value - '0');
        }
        return WeatherClampRefreshMilliseconds(seconds * 1000U);
    }
    (void)now;
    return kWeatherDefaultRefreshMilliseconds;
}

[[nodiscard]] CacheEntry* FindCache(std::string_view url) noexcept
{
    for (CacheEntry& entry : g_cache)
    {
        if (entry.used && std::string_view(entry.url.data()) == url)
        {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] HRESULT CopyBody(const char* source, uint32_t bytes, std::unique_ptr<char[]>& destination,
                               uint32_t& destinationBytes, uint32_t destinationCapacity) noexcept
{
    if (!source || !destination || destinationCapacity < bytes)
    {
        return E_INVALIDARG;
    }
    if (bytes != 0)
    {
        std::memcpy(destination.get(), source, bytes);
    }
    destination[bytes] = '\0';
    destinationBytes = bytes;
    return S_OK;
}

[[nodiscard]] CacheEntry* AllocateCache(std::string_view url) noexcept
{
    CacheEntry* empty = nullptr;
    for (CacheEntry& entry : g_cache)
    {
        if (!entry.used)
        {
            empty = &entry;
            break;
        }
    }
    if (!empty)
    {
        empty = &g_cache[0];
    }
    uint32_t capacity = empty->body ? kWeatherMaximumBodyBytes : 0;
    if (FAILED(EnsureBodyBuffer(empty->body, capacity)))
    {
        return nullptr;
    }
    empty->url = {};
    empty->etag = {};
    empty->lastModified = {};
    empty->bytes = 0;
    empty->expiresTick = 0;
    empty->used = true;
    empty->body[0] = '\0';
    WeatherCopyNarrow(url, empty->url.data(), empty->url.size());
    return empty;
}
} // namespace

HRESULT WeatherHttpInitialize() noexcept
{
    std::call_once(g_curlOnce,
                   []() noexcept
                   {
                       if (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK)
                       {
                           g_curlReady = true;
                       }
                   });
    return g_curlReady ? S_OK : E_FAIL;
}

HRESULT WeatherBuildLocationSearchUrl(std::string_view location, char* url, uint32_t capacity) noexcept
{
    if (!url || capacity == 0)
    {
        return E_INVALIDARG;
    }
    url[0] = '\0';
    if (location.empty() || location.size() > 128 || location.find('\0') != std::string_view::npos)
    {
        return E_INVALIDARG;
    }
    constexpr std::string_view prefix = "https://nominatim.openstreetmap.org/search?q=";
    constexpr std::string_view suffix = "&format=json&limit=1";
    const auto unreserved = [](unsigned char value) noexcept
    {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
               value == '-' || value == '.' || value == '_' || value == '~';
    };
    size_t bytes = prefix.size() + suffix.size();
    for (unsigned char value : location)
    {
        bytes += unreserved(value) ? 1U : 3U;
    }
    if (bytes >= capacity)
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    size_t used = prefix.size();
    std::memcpy(url, prefix.data(), used);
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char value : location)
    {
        if (unreserved(value))
        {
            url[used++] = static_cast<char>(value);
        }
        else
        {
            url[used++] = '%';
            url[used++] = hex[value >> 4];
            url[used++] = hex[value & 15];
        }
    }
    std::memcpy(url + used, suffix.data(), suffix.size());
    url[bytes] = '\0';
    return S_OK;
}

void WeatherHttpShutdown() noexcept
{
    AcquireSRWLockExclusive(&g_cacheLock);
    for (CacheEntry& entry : g_cache)
    {
        entry.body.reset();
        entry.url = {};
        entry.etag = {};
        entry.lastModified = {};
        entry.bytes = 0;
        entry.expiresTick = 0;
        entry.used = false;
    }
    ReleaseSRWLockExclusive(&g_cacheLock);
}

uint32_t WeatherHttpGetCount() noexcept
{
    return g_httpGetCount.load(std::memory_order_relaxed);
}

HRESULT WeatherHttpGet(std::string_view url, HANDLE cancelEvent, WeatherHttpResponse& response) noexcept
{
    g_httpGetCount.fetch_add(1, std::memory_order_relaxed);
    if (url.empty() || url.size() >= kWeatherMaximumUrlBytes || !cancelEvent)
    {
        return E_INVALIDARG;
    }
    const HRESULT prepared = PrepareResponse(response);
    if (FAILED(prepared))
    {
        return prepared;
    }
    if (WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0)
    {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    const HRESULT init = WeatherHttpInitialize();
    if (FAILED(init))
    {
        return init;
    }

    AcquireSRWLockShared(&g_cacheLock);
    CacheEntry* cached = FindCache(url);
    std::array<char, 128> etag{};
    std::array<char, 128> lastModified{};
    if (cached && cached->body)
    {
        etag = cached->etag;
        lastModified = cached->lastModified;
        if (cached->expiresTick != 0 && GetTickCount64() < cached->expiresTick)
        {
            const HRESULT copied =
                CopyBody(cached->body.get(), cached->bytes, response.body, response.bytes, response.capacity);
            response.status = 200;
            response.expiresDelayMilliseconds =
                WeatherClampRefreshMilliseconds(static_cast<uint32_t>(cached->expiresTick - GetTickCount64()));
            ReleaseSRWLockShared(&g_cacheLock);
            return copied;
        }
    }
    ReleaseSRWLockShared(&g_cacheLock);

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        return E_OUTOFMEMORY;
    }
    FetchState state{&response, cancelEvent};
    std::array<char, 2048> headers{};
    curl_easy_setopt(curl, CURLOPT_URL, url.data());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kWeatherUserAgent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(kWeatherDefaultTimeoutMilliseconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, static_cast<long>(kWeatherMaximumBodyBytes));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_slist* requestHeaders = nullptr;
    std::array<char, 192> ifNone{};
    std::array<char, 192> ifModified{};
    if (etag[0] != '\0')
    {
        sprintf_s(ifNone.data(), ifNone.size(), "If-None-Match: %s", etag.data());
        requestHeaders = curl_slist_append(requestHeaders, ifNone.data());
    }
    if (lastModified[0] != '\0')
    {
        sprintf_s(ifModified.data(), ifModified.size(), "If-Modified-Since: %s", lastModified.data());
        requestHeaders = curl_slist_append(requestHeaders, ifModified.data());
    }
    if (requestHeaders)
    {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, requestHeaders);
    }
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (requestHeaders)
    {
        curl_slist_free_all(requestHeaders);
    }
    if (code == CURLE_ABORTED_BY_CALLBACK)
    {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    if (code != CURLE_OK)
    {
        return HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE);
    }
    response.status = static_cast<uint32_t>(status);
    const std::string_view headerText(headers.data());
    response.expiresDelayMilliseconds = ExpiresDelayFromHeaders(headerText, GetTickCount64());
    if (status == 304)
    {
        AcquireSRWLockExclusive(&g_cacheLock);
        cached = FindCache(url);
        HRESULT copied = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        if (cached && cached->body)
        {
            copied = CopyBody(cached->body.get(), cached->bytes, response.body, response.bytes, response.capacity);
            response.notModified = true;
            cached->expiresTick = GetTickCount64() + response.expiresDelayMilliseconds;
        }
        ReleaseSRWLockExclusive(&g_cacheLock);
        return copied;
    }
    if (status < 200 || status >= 300)
    {
        return HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE);
    }

    AcquireSRWLockExclusive(&g_cacheLock);
    cached = FindCache(url);
    if (!cached)
    {
        cached = AllocateCache(url);
    }
    HRESULT stored = E_OUTOFMEMORY;
    if (cached && cached->body)
    {
        uint32_t unusedCapacity = kWeatherMaximumBodyBytes;
        stored = CopyBody(response.body.get(), response.bytes, cached->body, cached->bytes, unusedCapacity);
        cached->expiresTick = GetTickCount64() + response.expiresDelayMilliseconds;
        const std::string_view newEtag = HeaderValue(headerText, "etag");
        const std::string_view newModified = HeaderValue(headerText, "last-modified");
        WeatherCopyNarrow(newEtag, cached->etag.data(), cached->etag.size());
        WeatherCopyNarrow(newModified, cached->lastModified.data(), cached->lastModified.size());
    }
    ReleaseSRWLockExclusive(&g_cacheLock);
    return stored;
}
