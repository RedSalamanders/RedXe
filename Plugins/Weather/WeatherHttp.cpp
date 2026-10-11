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
#include <utility>

#include <curl/curl.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

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
uint32_t g_cacheClients = 0;
std::atomic<bool> g_failNextCacheAllocation{false};
bool g_curlReady = false;
std::atomic<uint32_t> g_httpGetCount{0};
std::atomic<WeatherHttpTestTransportFn> g_testTransport{nullptr};
std::atomic<uint64_t> g_testCacheTick{0};

[[nodiscard]] uint64_t CacheTick() noexcept
{
    const uint64_t test = g_testCacheTick.load(std::memory_order_relaxed);
    return test ? test : GetTickCount64();
}

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
            const uint32_t digit = static_cast<uint32_t>(value - '0');
            constexpr uint32_t maximumSeconds = kWeatherMaximumRefreshMilliseconds / 1000;
            if (seconds > (maximumSeconds - digit) / 10)
                return kWeatherMaximumRefreshMilliseconds;
            seconds = seconds * 10U + digit;
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

// Called with the cache lock exclusively held. The fixed metadata is small; body allocation is exact and
// transactional. Equal-sized successful content reuses storage; a failed replacement cannot change the entry.
[[nodiscard]] HRESULT StoreCache(std::string_view url, const WeatherHttpResponse& response,
                                 std::string_view headers) noexcept
{
    CacheEntry* target = FindCache(url);
    if (!target)
    {
        for (CacheEntry& entry : g_cache)
        {
            if (!entry.used)
            {
                target = &entry;
                break;
            }
        }
    }
    if (!target)
        target = &g_cache[0];
    CacheEntry candidate;
    if (!target->body || target->bytes != response.bytes)
    {
        if (g_failNextCacheAllocation.exchange(false, std::memory_order_relaxed))
            return E_OUTOFMEMORY;
        candidate.body.reset(new (std::nothrow) char[static_cast<size_t>(response.bytes) + 1]);
        if (!candidate.body)
            return E_OUTOFMEMORY;
    }
    else
    {
        candidate.body = std::move(target->body);
    }
    // All remaining operations are bounded, non-allocating and infallible after response validation.
    if (response.bytes)
        std::memcpy(candidate.body.get(), response.body.get(), response.bytes);
    candidate.body[response.bytes] = '\0';
    candidate.bytes = response.bytes;
    candidate.used = true;
    candidate.expiresTick = CacheTick() + response.expiresDelayMilliseconds;
    WeatherCopyNarrow(url, candidate.url.data(), candidate.url.size());
    WeatherCopyNarrow(HeaderValue(headers, "etag"), candidate.etag.data(), candidate.etag.size());
    WeatherCopyNarrow(HeaderValue(headers, "last-modified"), candidate.lastModified.data(),
                      candidate.lastModified.size());
    *target = std::move(candidate);
    return S_OK;
}

void ClearCache() noexcept
{
    for (CacheEntry& entry : g_cache)
        entry = {};
}
[[nodiscard]] HRESULT FetchCurl(const char* url, HANDLE cancelEvent, WeatherHttpResponse& response,
                                const std::array<char, 128>& etag, const std::array<char, 128>& lastModified,
                                std::array<char, 2048>& headers) noexcept
{
    const HRESULT initialized = WeatherHttpInitialize();
    if (FAILED(initialized))
        return initialized;
    using CurlOwner = wil::unique_any<CURL*, decltype(&curl_easy_cleanup), curl_easy_cleanup>;
    CurlOwner owner{curl_easy_init()};
    CURL* curl = owner.get();
    if (!curl)
    {
        return E_OUTOFMEMORY;
    }
    FetchState state{&response, cancelEvent};
    curl_easy_setopt(curl, CURLOPT_URL, url);
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
    using HeadersOwner = wil::unique_any<curl_slist*, decltype(&curl_slist_free_all), curl_slist_free_all>;
    HeadersOwner requestHeaders;
    const auto append = [&](const char* text) noexcept -> HRESULT
    {
        curl_slist* head = curl_slist_append(requestHeaders.get(), text);
        if (!head)
            return E_OUTOFMEMORY;
        // Successful append transfers the owned chain to its returned head.
        (void)requestHeaders.release();
        requestHeaders.reset(head);
        return S_OK;
    };
    std::array<char, 192> ifNone{};
    std::array<char, 192> ifModified{};
    if (etag[0] != '\0')
    {
        sprintf_s(ifNone.data(), ifNone.size(), "If-None-Match: %s", etag.data());
        const HRESULT result = append(ifNone.data());
        if (FAILED(result))
            return result;
    }
    if (lastModified[0] != '\0')
    {
        sprintf_s(ifModified.data(), ifModified.size(), "If-Modified-Since: %s", lastModified.data());
        const HRESULT result = append(ifModified.data());
        if (FAILED(result))
            return result;
    }
    if (requestHeaders)
    {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, requestHeaders.get());
    }
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (code == CURLE_ABORTED_BY_CALLBACK)
    {
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    if (code != CURLE_OK)
    {
        return HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE);
    }
    response.status = static_cast<uint32_t>(status);
    return S_OK;
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
    constexpr std::string_view suffix = "&format=json&limit=1&addressdetails=1";
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
    const auto guard = wil::AcquireSRWLockExclusive(&g_cacheLock);
    ClearCache();
}

void WeatherHttpAcquireCacheClient() noexcept
{
    const auto guard = wil::AcquireSRWLockExclusive(&g_cacheLock);
    ++g_cacheClients;
}
void WeatherHttpReleaseCacheClient() noexcept
{
    const auto guard = wil::AcquireSRWLockExclusive(&g_cacheLock);
    if (g_cacheClients != 0 && --g_cacheClients == 0)
        ClearCache();
}
void WeatherHttpFailNextCacheAllocationForTest(bool fail) noexcept
{
    g_failNextCacheAllocation.store(fail, std::memory_order_relaxed);
}

uint32_t WeatherHttpGetCount() noexcept
{
    return g_httpGetCount.load(std::memory_order_relaxed);
}

namespace
{
std::atomic<WeatherHttpTestResponseFn> g_testResponse{nullptr};
}

void WeatherHttpSetTestResponse(WeatherHttpTestResponseFn response) noexcept
{
    g_testResponse.store(response, std::memory_order_release);
}

HRESULT WeatherHttpGet(std::string_view url, HANDLE cancelEvent, WeatherHttpResponse& response) noexcept
{
    g_httpGetCount.fetch_add(1, std::memory_order_relaxed);
    if (url.empty() || url.size() > kWeatherMaximumUrlBytes || url.find('\0') != std::string_view::npos || !cancelEvent)
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
    if (const auto test = g_testResponse.load(std::memory_order_acquire))
    {
        const char* body = nullptr;
        uint32_t bytes = 0;
        const HRESULT result = test(url.data(), static_cast<uint32_t>(url.size()), &body, &bytes);
        if (FAILED(result))
            return result;
        if (!body || bytes > kWeatherMaximumBodyBytes)
            return E_INVALIDARG;
        response.status = 200;
        return CopyBody(body, bytes, response.body, response.bytes, response.capacity);
    }
    AcquireSRWLockShared(&g_cacheLock);
    CacheEntry* cached = FindCache(url);
    std::array<char, 128> etag{};
    std::array<char, 128> lastModified{};
    if (cached && cached->body)
    {
        etag = cached->etag;
        lastModified = cached->lastModified;
        const uint64_t now = CacheTick();
        if (cached->expiresTick != 0 && now < cached->expiresTick)
        {
            const HRESULT copied =
                CopyBody(cached->body.get(), cached->bytes, response.body, response.bytes, response.capacity);
            response.status = 200;
            response.expiresDelayMilliseconds =
                WeatherClampRefreshMilliseconds(static_cast<uint32_t>(cached->expiresTick - now));
            ReleaseSRWLockShared(&g_cacheLock);
            return copied;
        }
    }
    ReleaseSRWLockShared(&g_cacheLock);

    std::array<char, 2048> headers{};
    const auto transport = g_testTransport.load(std::memory_order_acquire);
    HRESULT fetched = S_OK;
    if (transport)
    {
        WeatherHttpTestTransportResponse result{sizeof(WeatherHttpTestTransportResponse)};
        fetched = transport(url.data(), static_cast<uint32_t>(url.size()), etag.data(), lastModified.data(),
                            cancelEvent, &result);
        if (SUCCEEDED(fetched))
        {
            if (result.sizeBytes != sizeof(result) || result.bodyBytes > kWeatherMaximumBodyBytes ||
                result.headersBytes >= headers.size() || (!result.body && result.bodyBytes) ||
                (!result.headers && result.headersBytes))
                return E_INVALIDARG;
            response.status = result.status;
            if (result.bodyBytes)
                std::memcpy(response.body.get(), result.body, result.bodyBytes);
            response.bytes = result.bodyBytes;
            response.body[response.bytes] = '\0';
            if (result.headersBytes)
                std::memcpy(headers.data(), result.headers, result.headersBytes);
        }
    }
    else
    {
        std::array<char, kWeatherMaximumUrlBytes + 1> terminated{};
        WeatherCopyNarrow(url, terminated.data(), terminated.size());
        fetched = FetchCurl(terminated.data(), cancelEvent, response, etag, lastModified, headers);
    }
    if (WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0)
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if (FAILED(fetched))
        return fetched;
    const uint32_t status = response.status;
    const std::string_view headerText(headers.data());
    response.expiresDelayMilliseconds = ExpiresDelayFromHeaders(headerText, CacheTick());
    if (status == 304)
    {
        AcquireSRWLockExclusive(&g_cacheLock);
        cached = FindCache(url);
        HRESULT copied = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        if (cached && cached->body)
        {
            copied = CopyBody(cached->body.get(), cached->bytes, response.body, response.bytes, response.capacity);
            response.notModified = true;
            cached->expiresTick = CacheTick() + response.expiresDelayMilliseconds;
        }
        ReleaseSRWLockExclusive(&g_cacheLock);
        return copied;
    }
    if (status < 200 || status >= 300)
    {
        return HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE);
    }

    const auto guard = wil::AcquireSRWLockExclusive(&g_cacheLock);
    return StoreCache(url, response, headerText);
}

void WeatherHttpSetTestTransport(WeatherHttpTestTransportFn transport) noexcept
{
    g_testTransport.store(transport, std::memory_order_release);
}
void WeatherHttpSetTestCacheTick(uint64_t tick) noexcept
{
    g_testCacheTick.store(tick, std::memory_order_relaxed);
}
void WeatherHttpCacheMetrics(uint32_t& clients, uint32_t& entries, uint32_t& bodyCapacityBytes) noexcept
{
    const auto guard = wil::AcquireSRWLockShared(&g_cacheLock);
    clients = g_cacheClients;
    entries = 0;
    bodyCapacityBytes = 0;
    for (const CacheEntry& entry : g_cache)
    {
        if (entry.used)
            ++entries;
        if (entry.body)
            bodyCapacityBytes += entry.bytes + 1;
    }
}
