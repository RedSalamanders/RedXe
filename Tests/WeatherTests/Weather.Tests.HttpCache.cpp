#include "Weather.Tests.HttpCache.h"

#include "../../Plugins/Weather/Weather.Tests.Contract.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Widget.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr HRESULT kFailure = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
constexpr char kUrl[] = "https://example.invalid/forecast";
#define CACHE_CHECK(expression)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::printf("Weather cache assertion at line %d: %s\n", __LINE__, #expression);                            \
            return kFailure;                                                                                           \
        }                                                                                                              \
    } while (false)

template <typename Function> Function Resolve(HMODULE module, const char* name) noexcept
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

struct Transport final
{
    std::string body = "abc";
    std::string headers = "ETag: old-tag\r\nLast-Modified: old-date\r\nCache-Control: max-age=0\r\n";
    HRESULT result = S_OK;
    uint32_t status = 200;
    uint32_t calls = 0;
    uint32_t bodyBytesOverride = 0;
    uint32_t headersBytesOverride = 0;
    bool nullBody = false;
    bool nullHeaders = false;
    bool invalidRecord = false;
    bool cancelAfter = false;
    std::array<char, kWeatherMaximumUrlBytes + 1> seenUrl{};
    std::array<char, 128> seenEtag{};
    std::array<char, 128> seenModified{};
};
Transport* g_transport = nullptr;

HRESULT __stdcall Fetch(const char* url, uint32_t urlBytes, const char* etag, const char* modified, HANDLE cancel,
                        WeatherHttpTestTransportResponse* response) noexcept
{
    Transport& transport = *g_transport;
    ++transport.calls;
    const size_t count = (std::min)(static_cast<size_t>(urlBytes), transport.seenUrl.size() - 1);
    std::memcpy(transport.seenUrl.data(), url, count);
    transport.seenUrl[count] = '\0';
    strcpy_s(transport.seenEtag.data(), transport.seenEtag.size(), etag);
    strcpy_s(transport.seenModified.data(), transport.seenModified.size(), modified);
    *response = {transport.invalidRecord ? 0U : static_cast<uint32_t>(sizeof(*response)),
                 transport.status,
                 transport.nullBody ? nullptr : transport.body.data(),
                 transport.bodyBytesOverride ? transport.bodyBytesOverride
                                             : static_cast<uint32_t>(transport.body.size()),
                 transport.headersBytesOverride ? transport.headersBytesOverride
                                                : static_cast<uint32_t>(transport.headers.size()),
                 transport.nullHeaders ? nullptr : transport.headers.data()};
    if (transport.cancelAfter)
        SetEvent(cancel);
    return transport.result;
}

HRESULT __stdcall Legacy(const char*, uint32_t, const char** body, uint32_t* bytes) noexcept
{
    *body = "legacy";
    *bytes = 6;
    return S_OK;
}

struct Fixture final
{
    explicit Fixture(HMODULE module)
        : diagnostics(
              Resolve<decltype(&RedXeWeatherGetTestCacheDiagnostics)>(module, "RedXeWeatherGetTestCacheDiagnostics")),
          setTransport(
              Resolve<decltype(&RedXeWeatherSetTestCacheTransport)>(module, "RedXeWeatherSetTestCacheTransport")),
          failAllocation(
              Resolve<decltype(&RedXeWeatherFailTestCacheAllocation)>(module, "RedXeWeatherFailTestCacheAllocation")),
          get(Resolve<decltype(&RedXeWeatherTestCachedHttpGet)>(module, "RedXeWeatherTestCachedHttpGet")),
          setLegacy(Resolve<decltype(&RedXeWeatherSetTestServices)>(module, "RedXeWeatherSetTestServices")),
          shutdown(Resolve<RedXePluginShutdownFn>(module, kRedXePluginShutdownExport)),
          create(Resolve<RedXeCreateFn>(module, kRedXeCreateExport)),
          body(std::make_unique<char[]>(kWeatherMaximumBodyBytes + 1)),
          cancel(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
    }

    HRESULT Reset()
    {
        shutdown();
        setLegacy(nullptr, 0);
        if (failAllocation)
            (void)failAllocation(FALSE);
        transport = Transport{};
        g_transport = &transport;
        ResetEvent(cancel.get());
        return setTransport(Fetch, 1000);
    }
    HRESULT At(uint64_t tick) const noexcept
    {
        return setTransport(Fetch, tick);
    }
    HRESULT Get(std::string_view url = kUrl) noexcept
    {
        response = {sizeof(response)};
        return get(url.data(), static_cast<uint32_t>(url.size()), cancel.get(), body.get(),
                   kWeatherMaximumBodyBytes + 1, &response);
    }
    bool HasBody(std::string_view expected) const noexcept
    {
        return response.bodyBytes == expected.size() && body[expected.size()] == '\0' &&
               std::memcmp(body.get(), expected.data(), expected.size()) == 0;
    }
    bool Metrics(uint32_t entries, uint32_t bytes, uint32_t clients = 0) const noexcept
    {
        WeatherHttpCacheTestDiagnostics value{sizeof(value)};
        return diagnostics(&value) == S_OK && value.entries == entries && value.bodyCapacityBytes == bytes &&
               value.liveClients == clients;
    }
    decltype(&RedXeWeatherGetTestCacheDiagnostics) diagnostics;
    decltype(&RedXeWeatherSetTestCacheTransport) setTransport;
    decltype(&RedXeWeatherFailTestCacheAllocation) failAllocation;
    decltype(&RedXeWeatherTestCachedHttpGet) get;
    decltype(&RedXeWeatherSetTestServices) setLegacy;
    RedXePluginShutdownFn shutdown;
    RedXeCreateFn create;
    Transport transport;
    WeatherHttpTestGetResult response{sizeof(response)};
    std::unique_ptr<char[]> body;
    wil::unique_handle cancel;
};

HRESULT BoundsAndExpiry(Fixture& fixture)
{
    CACHE_CHECK(fixture.Reset() == S_OK && fixture.Metrics(0, 0));
    CACHE_CHECK(fixture.Get() == S_OK && fixture.HasBody("abc"));
    // Keep this first storage assertion before resolving the optional fault hook: the old fixed-body DLL
    // must fail on actual retained bytes, rather than on a test-only export added with the new implementation.
    CACHE_CHECK(fixture.Metrics(1, 4));
    CACHE_CHECK(fixture.failAllocation != nullptr);
    CACHE_CHECK(fixture.response.expiresDelayMilliseconds == 300000);
    fixture.transport.body = "changed";
    CACHE_CHECK(fixture.Get() == S_OK && fixture.HasBody("abc") && fixture.transport.calls == 1);
    CACHE_CHECK(fixture.At(400000) == S_OK);
    fixture.transport.status = 304;
    fixture.transport.headers = "Cache-Control: max-age=9999999999999999999999999999999\r\n";
    CACHE_CHECK(fixture.Get() == S_OK && fixture.HasBody("abc") && fixture.response.notModified == TRUE);
    CACHE_CHECK(fixture.response.status == 304 && fixture.response.expiresDelayMilliseconds == 3600000);
    CACHE_CHECK(std::strcmp(fixture.transport.seenEtag.data(), "old-tag") == 0);
    CACHE_CHECK(std::strcmp(fixture.transport.seenModified.data(), "old-date") == 0);
    CACHE_CHECK(fixture.Get("https://example.invalid/missing") == HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
    CACHE_CHECK(fixture.Metrics(1, 4));
    CACHE_CHECK(fixture.failAllocation(TRUE) == S_OK && fixture.At(4100000) == S_OK);
    CACHE_CHECK(fixture.Get() == S_OK && fixture.HasBody("abc"));
    fixture.transport.status = 200;
    CACHE_CHECK(fixture.Get("https://example.invalid/new") == E_OUTOFMEMORY && fixture.Metrics(1, 4));

    CACHE_CHECK(fixture.Reset() == S_OK);
    fixture.transport.headers.clear();
    CACHE_CHECK(fixture.Get() == S_OK && fixture.response.expiresDelayMilliseconds == 900000);
    CACHE_CHECK(fixture.Get("https://example.invalid/second") == S_OK && fixture.transport.calls == 2);
    CACHE_CHECK(fixture.Get("https://example.invalid/second") == S_OK && fixture.transport.calls == 2);
    CACHE_CHECK(fixture.Metrics(2, 8));

    CACHE_CHECK(fixture.Reset() == S_OK);
    WeatherHttpCacheTestDiagnostics diagnostics{0};
    CACHE_CHECK(fixture.diagnostics(nullptr) == E_POINTER && fixture.diagnostics(&diagnostics) == E_INVALIDARG);
    CACHE_CHECK(fixture.get(nullptr, 0, fixture.cancel.get(), fixture.body.get(), 10, &fixture.response) == E_POINTER);
    CACHE_CHECK(fixture.get(kUrl, sizeof(kUrl) - 1, fixture.cancel.get(), nullptr, 10, &fixture.response) == E_POINTER);
    CACHE_CHECK(fixture.get(kUrl, sizeof(kUrl) - 1, fixture.cancel.get(), fixture.body.get(), 10, nullptr) ==
                E_POINTER);
    fixture.response.sizeBytes = 0;
    CACHE_CHECK(fixture.get(kUrl, sizeof(kUrl) - 1, fixture.cancel.get(), fixture.body.get(), 10, &fixture.response) ==
                E_INVALIDARG);
    fixture.response.sizeBytes = sizeof(fixture.response);
    CACHE_CHECK(fixture.get(kUrl, sizeof(kUrl) - 1, fixture.cancel.get(), fixture.body.get(), 0, &fixture.response) ==
                E_INVALIDARG);
    CACHE_CHECK(fixture.get(kUrl, sizeof(kUrl) - 1, nullptr, fixture.body.get(), 10, &fixture.response) ==
                E_INVALIDARG);
    CACHE_CHECK(fixture.Get("") == E_INVALIDARG);
    const std::string longUrl(kWeatherMaximumUrlBytes + 1, 'u');
    CACHE_CHECK(fixture.Get(longUrl) == E_INVALIDARG);
    CACHE_CHECK(fixture.Get(std::string_view("a\0b", 3)) == E_INVALIDARG && fixture.transport.calls == 0);
    const std::string maximumUrl(kWeatherMaximumUrlBytes, 'u');
    CACHE_CHECK(fixture.Get(maximumUrl) == S_OK &&
                std::strlen(fixture.transport.seenUrl.data()) == kWeatherMaximumUrlBytes);
    CACHE_CHECK(fixture.get(maximumUrl.data(), kWeatherMaximumUrlBytes, fixture.cancel.get(), fixture.body.get(), 3,
                            &fixture.response) == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));

    CACHE_CHECK(fixture.Reset() == S_OK);
    fixture.transport.body.clear();
    fixture.transport.nullBody = true;
    CACHE_CHECK(fixture.Get() == S_OK && fixture.HasBody("") && fixture.Metrics(1, 1));
    CACHE_CHECK(fixture.Get() == S_OK && fixture.transport.calls == 1);
    CACHE_CHECK(fixture.Reset() == S_OK);
    fixture.transport.body.assign(kWeatherMaximumBodyBytes, 'm');
    CACHE_CHECK(fixture.Get() == S_OK && fixture.HasBody(fixture.transport.body) &&
                fixture.Metrics(1, kWeatherMaximumBodyBytes + 1));
    return S_OK;
}

HRESULT FailedReplacement(Fixture& fixture)
{
    for (const std::string replacement : {std::string("x"), std::string(32, 'g')})
    {
        CACHE_CHECK(fixture.Reset() == S_OK && fixture.Get() == S_OK);
        CACHE_CHECK(fixture.At(400000) == S_OK && fixture.failAllocation(TRUE) == S_OK);
        fixture.transport.body = replacement;
        fixture.transport.headers = "ETag: new-tag\r\nLast-Modified: new-date\r\nCache-Control: max-age=3600\r\n";
        CACHE_CHECK(fixture.Get() == E_OUTOFMEMORY && fixture.Metrics(1, 4));
        CACHE_CHECK(fixture.At(1000) == S_OK && fixture.Get() == S_OK && fixture.HasBody("abc"));
        CACHE_CHECK(fixture.transport.calls == 2); // Failure did not replace the prior expiry or body.
        CACHE_CHECK(fixture.At(400000) == S_OK);
        fixture.transport.status = 304;
        CACHE_CHECK(fixture.Get() == S_OK && fixture.HasBody("abc"));
        CACHE_CHECK(std::strcmp(fixture.transport.seenEtag.data(), "old-tag") == 0);
        CACHE_CHECK(std::strcmp(fixture.transport.seenModified.data(), "old-date") == 0);
    }
    CACHE_CHECK(fixture.Reset() == S_OK && fixture.failAllocation(TRUE) == S_OK);
    CACHE_CHECK(fixture.Get() == E_OUTOFMEMORY && fixture.Metrics(0, 0));
    CACHE_CHECK(fixture.Get() == S_OK && fixture.Metrics(1, 4));
    CACHE_CHECK(fixture.At(400000) == S_OK && fixture.failAllocation(TRUE) == S_OK);
    fixture.transport.body = "xyz";
    CACHE_CHECK(fixture.Get() == S_OK && fixture.HasBody("xyz") && fixture.Metrics(1, 4));
    // Equal-sized publication did not allocate or consume the armed allocation fault.
    CACHE_CHECK(fixture.At(800000) == S_OK);
    fixture.transport.body = "growth";
    CACHE_CHECK(fixture.Get() == E_OUTOFMEMORY && fixture.Metrics(1, 4));
    CACHE_CHECK(fixture.At(400000) == S_OK && fixture.Get() == S_OK && fixture.HasBody("xyz"));

    CACHE_CHECK(fixture.Reset() == S_OK && fixture.Get() == S_OK && fixture.At(400000) == S_OK);
    fixture.transport.body.assign(32, 'g');
    CACHE_CHECK(fixture.Get() == S_OK && fixture.HasBody(fixture.transport.body) && fixture.Metrics(1, 33));
    CACHE_CHECK(fixture.At(800000) == S_OK);
    fixture.transport.body = "x";
    CACHE_CHECK(fixture.Get() == S_OK && fixture.HasBody("x") && fixture.Metrics(1, 2));

    for (uint32_t mode = 0; mode < 9; ++mode)
    {
        CACHE_CHECK(fixture.Reset() == S_OK && fixture.Get() == S_OK && fixture.At(400000) == S_OK);
        HRESULT expected = E_INVALIDARG;
        switch (mode)
        {
        case 0:
            fixture.transport.bodyBytesOverride = kWeatherMaximumBodyBytes + 1;
            break;
        case 1:
            fixture.transport.headersBytesOverride = 2048;
            break;
        case 2:
            fixture.transport.nullBody = true;
            break;
        case 3:
            fixture.transport.nullHeaders = true;
            break;
        case 4:
            fixture.transport.invalidRecord = true;
            break;
        case 5:
            fixture.transport.result = E_FAIL;
            expected = E_FAIL;
            break;
        case 6:
            fixture.transport.status = 500;
            expected = HRESULT_FROM_WIN32(ERROR_NETWORK_UNREACHABLE);
            break;
        case 7:
            fixture.transport.cancelAfter = true;
            expected = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            break;
        case 8:
            SetEvent(fixture.cancel.get());
            expected = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            break;
        }
        CACHE_CHECK(fixture.Get() == expected && fixture.Metrics(1, 4));
        CACHE_CHECK(fixture.transport.calls == (mode == 8 ? 1U : 2U));
        ResetEvent(fixture.cancel.get());
        CACHE_CHECK(fixture.At(1000) == S_OK && fixture.Get() == S_OK && fixture.HasBody("abc"));
    }
    CACHE_CHECK(fixture.Reset() == S_OK);
    fixture.setLegacy(Legacy, 0);
    CACHE_CHECK(fixture.Get() == S_OK && fixture.HasBody("legacy") && fixture.transport.calls == 0 &&
                fixture.Metrics(0, 0));
    fixture.setLegacy(nullptr, 0);
    return S_OK;
}

HRESULT CapacityAndEviction(Fixture& fixture)
{
    CACHE_CHECK(fixture.Reset() == S_OK);
    for (uint32_t index = 0; index < kWeatherHttpCacheEntries; ++index)
        CACHE_CHECK(fixture.Get(std::string("https://example.invalid/") + std::to_string(index)) == S_OK);
    CACHE_CHECK(fixture.Metrics(kWeatherHttpCacheEntries, 32) && fixture.transport.calls == 8);
    CACHE_CHECK(fixture.failAllocation(TRUE) == S_OK);
    fixture.transport.body = "zz";
    CACHE_CHECK(fixture.Get("https://example.invalid/ninth") == E_OUTOFMEMORY && fixture.Metrics(8, 32));
    CACHE_CHECK(fixture.Get("https://example.invalid/0") == S_OK && fixture.HasBody("abc") &&
                fixture.transport.calls == 9);
    CACHE_CHECK(fixture.Get("https://example.invalid/ninth") == S_OK && fixture.Metrics(8, 31));
    CACHE_CHECK(fixture.Get("https://example.invalid/1") == S_OK && fixture.HasBody("abc") &&
                fixture.transport.calls == 10);
    CACHE_CHECK(fixture.Get("https://example.invalid/0") == S_OK && fixture.HasBody("zz") &&
                fixture.transport.calls == 11);
    CACHE_CHECK(fixture.Reset() == S_OK);
    fixture.transport.body.assign(kWeatherMaximumBodyBytes, 'm');
    for (uint32_t index = 0; index < kWeatherHttpCacheEntries; ++index)
        CACHE_CHECK(fixture.Get(std::string("https://example.invalid/") + std::to_string(index)) == S_OK);
    CACHE_CHECK(fixture.Metrics(8, 8 * (kWeatherMaximumBodyBytes + 1)));
    fixture.shutdown();
    CACHE_CHECK(fixture.Metrics(0, 0));
    return S_OK;
}

HRESULT WidgetLeases(Fixture& fixture)
{
    CACHE_CHECK(fixture.Reset() == S_OK);
    constexpr char json[] = R"({"location":"Paris"})";
    const RedXeFactoryOptions options{sizeof(options), 0, json, sizeof(json) - 1};
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    CACHE_CHECK(fixture.create(__uuidof(IRedXeWidgetProvider), &options, nullptr, "builtin.weather",
                               provider.put_void()) == S_OK);
    wil::com_ptr_nothrow<IRedXeWidget> first;
    wil::com_ptr_nothrow<IRedXeWidget> second;
    CACHE_CHECK(provider->CreateWidget("weather", "cache.first", first.put()) == S_OK);
    CACHE_CHECK(provider->CreateWidget("weather", "cache.second", second.put()) == S_OK);
    CACHE_CHECK(fixture.Metrics(0, 0, 2) && fixture.Get() == S_OK && fixture.Metrics(1, 4, 2));
    CACHE_CHECK(first->SetVisible(FALSE) == S_OK && fixture.Metrics(1, 4, 2));
    first.reset();
    CACHE_CHECK(fixture.Metrics(1, 4, 1) && fixture.Get() == S_OK && fixture.transport.calls == 1);
    wil::com_ptr_nothrow<IRedXeNetworkWidget> callbackLease;
    CACHE_CHECK(second.query_to(callbackLease.put()) == S_OK);
    second.reset();
    CACHE_CHECK(fixture.Metrics(1, 4, 1));
    callbackLease.reset();
    CACHE_CHECK(fixture.Metrics(0, 0));
    provider.reset();
    CACHE_CHECK(fixture.Metrics(0, 0));
    return S_OK;
}

HRESULT Run(HMODULE module)
{
    Fixture fixture(module);
    CACHE_CHECK(fixture.diagnostics && fixture.setTransport && fixture.get && fixture.setLegacy && fixture.shutdown &&
                fixture.create && fixture.cancel);
    const DWORD required = GetEnvironmentVariableW(L"REDXE_AUTOMATED_HOST", nullptr, 0);
    std::wstring previous(required, L'\0');
    if (required)
        CACHE_CHECK(GetEnvironmentVariableW(L"REDXE_AUTOMATED_HOST", previous.data(), required) == required - 1);
    const auto restore = wil::scope_exit(
        [&]
        {
            SetEnvironmentVariableW(L"REDXE_AUTOMATED_HOST", L"1");
            (void)fixture.setTransport(nullptr, 0);
            if (fixture.failAllocation)
                (void)fixture.failAllocation(FALSE);
            fixture.setLegacy(nullptr, 0);
            fixture.shutdown();
            g_transport = nullptr;
            SetEnvironmentVariableW(L"REDXE_AUTOMATED_HOST", required ? previous.c_str() : nullptr);
        });
    CACHE_CHECK(SetEnvironmentVariableW(L"REDXE_AUTOMATED_HOST", nullptr));
    CACHE_CHECK(fixture.setTransport(Fetch, 1000) == E_ACCESSDENIED);
    CACHE_CHECK(fixture.Get() == E_ACCESSDENIED);
    if (fixture.failAllocation)
        CACHE_CHECK(fixture.failAllocation(TRUE) == E_ACCESSDENIED);
    CACHE_CHECK(SetEnvironmentVariableW(L"REDXE_AUTOMATED_HOST", L"11"));
    CACHE_CHECK(fixture.setTransport(Fetch, 1000) == E_ACCESSDENIED);
    CACHE_CHECK(SetEnvironmentVariableW(L"REDXE_AUTOMATED_HOST", L"1"));
    CACHE_CHECK(BoundsAndExpiry(fixture) == S_OK);
    CACHE_CHECK(FailedReplacement(fixture) == S_OK);
    CACHE_CHECK(CapacityAndEviction(fixture) == S_OK);
    CACHE_CHECK(WidgetLeases(fixture) == S_OK);
    std::printf(
        "Weather production cache bounds, expiry, transactional failures, eviction and last-widget leases passed.\n");
    return S_OK;
}
} // namespace

HRESULT RunWeatherHttpCacheTests(HMODULE module)
{
    try
    {
        return Run(module);
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}
