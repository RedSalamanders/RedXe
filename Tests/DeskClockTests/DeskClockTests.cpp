#include "../../Plugins/DeskClock/DeskClockTestContract.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Widget.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <psapi.h>
#include <string>
#include <string_view>
#include <vector>

#if defined(_DEBUG)
#include <crtdbg.h>
#endif

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr char kPluginId[] = "builtin.desk-clock";
constexpr char kWidgetTypeId[] = "desk-clock";
constexpr std::string_view kDefaultConfiguration =
    R"json({"flipDurationMilliseconds":420,"backgroundColor":"#000000","cardColor":"#FF3B43","digitColor":"#FFFFFF","dateColor":"#D8D8D8"})json";
constexpr HRESULT kTestFailure = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

std::atomic<std::uint64_t> gRenderAllocations{0};
std::atomic<DWORD> gRenderThread{0};

#if defined(_DEBUG)
int __cdecl CountRenderAllocation(int allocationType, void*, std::size_t, int, long, const unsigned char*, int)
{
    if (allocationType != _HOOK_FREE && GetCurrentThreadId() == gRenderThread.load(std::memory_order_relaxed))
    {
        gRenderAllocations.fetch_add(1, std::memory_order_relaxed);
    }
    return TRUE;
}
#endif

template <typename Function> [[nodiscard]] Function Resolve(HMODULE module, const char* name) noexcept
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

[[nodiscard]] HRESULT BuildSiblingPath(const wchar_t* relativePath, std::array<wchar_t, 1024>& path) noexcept
{
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return HRESULT_FROM_WIN32(length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);
    }
    wchar_t* separator = std::wcsrchr(path.data(), L'\\');
    if (!separator)
    {
        return E_UNEXPECTED;
    }
    ++separator;
    const std::size_t prefix = static_cast<std::size_t>(separator - path.data());
    const std::size_t suffix = std::wcslen(relativePath);
    if (prefix + suffix + 1 > path.size())
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    std::memcpy(separator, relativePath, (suffix + 1) * sizeof(wchar_t));
    return S_OK;
}

struct RenderTarget final
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> view;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
};

[[nodiscard]] HRESULT CreateRenderTarget(std::uint32_t width, std::uint32_t height, RenderTarget& target) noexcept
{
    constexpr std::array featureLevels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    RenderTarget created{};
    created.width = width;
    created.height = height;
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       featureLevels.data(), static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION,
                                       created.device.put(), &created.featureLevel, created.context.put());
    if (FAILED(result))
    {
        std::wprintf(L"Desk Clock initial WARP composition failed: 0x%08X\n", static_cast<unsigned int>(result));
        return result;
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    result = created.device->CreateTexture2D(&description, nullptr, created.texture.put());
    if (SUCCEEDED(result))
    {
        result = created.device->CreateRenderTargetView(created.texture.get(), nullptr, created.view.put());
    }
    if (FAILED(result))
    {
        return result;
    }
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    result = created.device->CreateTexture2D(&description, nullptr, created.staging.put());
    if (FAILED(result))
    {
        return result;
    }
    target = std::move(created);
    return S_OK;
}

[[nodiscard]] HRESULT RenderFrame(IRedXeGpuWidget& widget, RenderTarget& target, float elapsedSeconds,
                                  float deltaSeconds, std::vector<std::uint8_t>* pixels,
                                  std::uint32_t dpi = USER_DEFAULT_SCREEN_DPI) noexcept
{
    try
    {
        target.context->ClearState();
        ID3D11RenderTargetView* views[] = {target.view.get()};
        target.context->OMSetRenderTargets(1, views, nullptr);
        const D3D11_VIEWPORT viewport{
            0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f,
        };
        target.context->RSSetViewports(1, &viewport);
        const RedXeWidgetFrameContext widgetFrame{
            sizeof(RedXeWidgetFrameContext), target.width, target.height, dpi, elapsedSeconds, deltaSeconds,
        };
        const RedXeGpuFrameContext frame{sizeof(RedXeGpuFrameContext), &widgetFrame, target.context.get(), viewport};
        HRESULT result = widget.Render(&frame);
        if (FAILED(result) || !pixels)
        {
            return result;
        }

        target.context->CopyResource(target.staging.get(), target.texture.get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        result = target.context->Map(target.staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(result))
        {
            return result;
        }
        const auto unmap = wil::scope_exit([&]() noexcept { target.context->Unmap(target.staging.get(), 0); });
        const std::size_t rowBytes = static_cast<std::size_t>(target.width) * 4U;
        pixels->resize(rowBytes * target.height);
        for (std::uint32_t row = 0; row < target.height; ++row)
        {
            std::memcpy(pixels->data() + static_cast<std::size_t>(row) * rowBytes,
                        static_cast<const std::uint8_t*>(mapped.pData) +
                            static_cast<std::size_t>(row) * mapped.RowPitch,
                        rowBytes);
        }
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] bool Near(std::uint8_t value, std::uint8_t target, std::uint8_t tolerance) noexcept
{
    return value >= static_cast<std::uint8_t>(target > tolerance ? target - tolerance : 0) &&
           value <= static_cast<std::uint8_t>(target < 255U - tolerance ? target + tolerance : 255U);
}

[[nodiscard]] std::size_t CountColor(const std::vector<std::uint8_t>& pixels, std::array<std::uint8_t, 3> color,
                                     std::uint8_t tolerance) noexcept
{
    std::size_t count = 0;
    for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4)
    {
        if (Near(pixels[offset], color[0], tolerance) && Near(pixels[offset + 1], color[1], tolerance) &&
            Near(pixels[offset + 2], color[2], tolerance) && pixels[offset + 3] == 255)
        {
            ++count;
        }
    }
    return count;
}

struct PixelBounds final
{
    std::uint32_t minimumX = 0;
    std::uint32_t minimumY = 0;
    std::uint32_t maximumX = 0;
    std::uint32_t maximumY = 0;
    std::size_t count = 0;
};

[[nodiscard]] PixelBounds FindColorBounds(const std::vector<std::uint8_t>& pixels, std::uint32_t imageWidth,
                                          std::array<std::uint8_t, 3> color, std::uint8_t tolerance,
                                          std::uint32_t minimumY = 0, std::uint32_t maximumY = 0xFFFFFFFFU) noexcept
{
    PixelBounds bounds{};
    const std::uint32_t imageHeight =
        imageWidth == 0 ? 0 : static_cast<std::uint32_t>(pixels.size() / (static_cast<std::size_t>(imageWidth) * 4U));
    bounds.minimumX = imageWidth;
    bounds.minimumY = imageHeight;
    const std::uint32_t firstY = std::min(minimumY, imageHeight);
    const std::uint32_t lastY = std::min(maximumY, imageHeight);
    for (std::uint32_t y = firstY; y < lastY; ++y)
    {
        for (std::uint32_t x = 0; x < imageWidth; ++x)
        {
            const std::size_t offset = (static_cast<std::size_t>(y) * imageWidth + x) * 4U;
            if (!Near(pixels[offset], color[0], tolerance) || !Near(pixels[offset + 1], color[1], tolerance) ||
                !Near(pixels[offset + 2], color[2], tolerance) || pixels[offset + 3] != 255)
            {
                continue;
            }
            if (bounds.count == 0)
            {
                bounds.minimumX = x;
                bounds.minimumY = y;
                bounds.maximumX = x;
                bounds.maximumY = y;
            }
            else
            {
                bounds.minimumX = std::min(bounds.minimumX, x);
                bounds.minimumY = std::min(bounds.minimumY, y);
                bounds.maximumX = std::max(bounds.maximumX, x);
                bounds.maximumY = std::max(bounds.maximumY, y);
            }
            ++bounds.count;
        }
    }
    return bounds;
}

[[nodiscard]] bool RectangleEquals(const std::vector<std::uint8_t>& left, const std::vector<std::uint8_t>& right,
                                   std::uint32_t imageWidth, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                   std::uint32_t height) noexcept
{
    if (left.size() != right.size() || x + width > imageWidth)
    {
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(imageWidth) * 4U;
    for (std::uint32_t row = y; row < y + height; ++row)
    {
        const std::size_t offset = static_cast<std::size_t>(row) * rowBytes + static_cast<std::size_t>(x) * 4U;
        if (offset + static_cast<std::size_t>(width) * 4U > left.size() ||
            std::memcmp(left.data() + offset, right.data() + offset, static_cast<std::size_t>(width) * 4U) != 0)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] HRESULT ValidateComposition(const std::vector<std::uint8_t>& pixels) noexcept
{
    const std::size_t background = CountColor(pixels, {0, 0, 0}, 0);
    const std::size_t cards = CountColor(pixels, {255, 59, 67}, 2);
    const std::size_t digits = CountColor(pixels, {255, 255, 255}, 3);
    const std::size_t date = CountColor(pixels, {216, 216, 216}, 5);
    if (!(background > 10'000 && cards > 20'000 && digits > 1'000 && date > 25))
    {
        std::wprintf(L"Desk Clock pixel counts: background=%zu cards=%zu digits=%zu date=%zu\n", background, cards,
                     digits, date);
    }
    return background > 10'000 && cards > 20'000 && digits > 1'000 && date > 25 ? S_OK : kTestFailure;
}

[[nodiscard]] HRESULT ValidateReferenceComposition(const std::vector<std::uint8_t>& pixels) noexcept
{
    constexpr std::uint32_t width = 450;
    const PixelBounds cards = FindColorBounds(pixels, width, {255, 59, 67}, 2);
    const PixelBounds date = FindColorBounds(pixels, width, {216, 216, 216}, 5);
    const std::uint32_t dateWidth = date.count == 0 ? 0 : date.maximumX - date.minimumX + 1U;
    const std::uint32_t dateHeight = date.count == 0 ? 0 : date.maximumY - date.minimumY + 1U;
    const bool valid = cards.count > 20'000 && cards.minimumX >= 18 && cards.minimumX <= 20 && cards.maximumX >= 429 &&
                       cards.maximumX <= 431 && cards.minimumY >= 28 && cards.minimumY <= 30 && cards.maximumY >= 122 &&
                       cards.maximumY <= 124 && date.count > 50 && dateWidth >= 54 && dateWidth <= 64 &&
                       dateHeight >= 14 && dateHeight <= 18 && date.minimumY >= 141 && date.minimumY <= 144 &&
                       date.maximumY >= 156 && date.maximumY <= 159;
    if (!valid)
    {
        std::wprintf(L"Desk Clock reference bounds: cards=%zu [%u,%u]-[%u,%u], date=%zu [%u,%u]-[%u,%u].\n",
                     cards.count, cards.minimumX, cards.minimumY, cards.maximumX, cards.maximumY, date.count,
                     date.minimumX, date.minimumY, date.maximumX, date.maximumY);
    }
    return valid ? S_OK : kTestFailure;
}

[[nodiscard]] HRESULT WriteSnapshot(const std::vector<std::uint8_t>& pixels, std::uint32_t width, std::uint32_t height,
                                    const wchar_t* fileName) noexcept
{
    try
    {
        std::array<wchar_t, 1024> path{};
        HRESULT result = BuildSiblingPath(fileName, path);
        if (FAILED(result))
        {
            return result;
        }
        wil::unique_hfile file{
            CreateFileW(path.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (!file)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        const std::uint32_t pixelBytes = width * height * 4U;
        BITMAPFILEHEADER fileHeader{};
        fileHeader.bfType = 0x4D42;
        fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        fileHeader.bfSize = fileHeader.bfOffBits + pixelBytes;
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(width);
        info.biHeight = static_cast<LONG>(height);
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;
        info.biSizeImage = pixelBytes;
        DWORD written = 0;
        if (!WriteFile(file.get(), &fileHeader, sizeof(fileHeader), &written, nullptr) ||
            written != sizeof(fileHeader) || !WriteFile(file.get(), &info, sizeof(info), &written, nullptr) ||
            written != sizeof(info))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        std::vector<std::uint8_t> bgra(pixelBytes);
        for (std::uint32_t y = 0; y < height; ++y)
        {
            const std::uint32_t sourceY = height - 1U - y;
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t source = (static_cast<std::size_t>(sourceY) * width + x) * 4U;
                const std::size_t destination = (static_cast<std::size_t>(y) * width + x) * 4U;
                bgra[destination] = pixels[source + 2];
                bgra[destination + 1] = pixels[source + 1];
                bgra[destination + 2] = pixels[source];
                bgra[destination + 3] = pixels[source + 3];
            }
        }
        if (!WriteFile(file.get(), bgra.data(), pixelBytes, &written, nullptr) || written != pixelBytes)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT CreateProvider(RedXeCreateFn create, std::string_view configuration,
                                     wil::com_ptr_nothrow<IRedXeWidgetProvider>& provider) noexcept
{
    try
    {
        std::string envelope;
        envelope.reserve(configuration.size() + 32);
        envelope.append("{\"plugin\":{},\"instance\":").append(configuration).append("}");
        RedXeFactoryOptions options{};
        options.sizeBytes = sizeof(options);
        options.configurationJsonUtf8 = envelope.data();
        options.configurationBytes = static_cast<std::uint32_t>(envelope.size());
        void* object = nullptr;
        const HRESULT result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kPluginId, &object);
        if (FAILED(result) || !object)
        {
            return FAILED(result) ? result : E_UNEXPECTED;
        }
        provider.attach(static_cast<IRedXeWidgetProvider*>(object));
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

[[nodiscard]] HRESULT ValidateFactoryAndSettings(RedXeCreateFn create, RedXeEnumeratePluginsFn enumerate,
                                                 RedXeGetPluginSettingsContractFn getContract) noexcept
{
    if (enumerate(nullptr, nullptr) != E_POINTER || getContract(kPluginId, nullptr) != E_POINTER)
    {
        return kTestFailure;
    }
    const RedXePluginMetadata* metadata = nullptr;
    std::uint32_t count = 0;
    HRESULT result = enumerate(&metadata, &count);
    if (FAILED(result) || !metadata || count != 1 || metadata[0].sizeBytes != sizeof(RedXePluginMetadata) ||
        !RedXeAsciiEqualsIgnoreCase(metadata[0].id, kPluginId) ||
        metadata[0].capabilities != RedXePluginCapabilityWidgetProvider)
    {
        return FAILED(result) ? result : kTestFailure;
    }
    const RedXePluginSettingsContract* contract = nullptr;
    result = getContract(kPluginId, &contract);
    if (FAILED(result) || !contract || contract->sizeBytes != sizeof(RedXePluginSettingsContract) ||
        std::string_view(contract->defaultsJsonUtf8, contract->defaultsBytes) != kDefaultConfiguration)
    {
        return FAILED(result) ? result : kTestFailure;
    }

    void* object = reinterpret_cast<void*>(1);
    if (create(__uuidof(IRedXeWidgetProvider), nullptr, nullptr, "missing.plugin", &object) !=
            HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ||
        object)
    {
        return kTestFailure;
    }
    object = reinterpret_cast<void*>(1);
    if (create(__uuidof(IRedXeGpuWidget), nullptr, nullptr, kPluginId, &object) != E_NOINTERFACE || object)
    {
        return kTestFailure;
    }

    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
    object = nullptr;
    result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kPluginId, &object);
    if (FAILED(result) || !object)
    {
        return FAILED(result) ? result : kTestFailure;
    }
    static_cast<IRedXeWidgetProvider*>(object)->Release();

    options.configurationJsonUtf8 = kDefaultConfiguration.data();
    options.configurationBytes = static_cast<std::uint32_t>(kDefaultConfiguration.size());
    object = reinterpret_cast<void*>(1);
    if (create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kPluginId, &object) !=
            HRESULT_FROM_WIN32(ERROR_INVALID_DATA) ||
        object)
    {
        return kTestFailure;
    }

    constexpr std::array<std::string_view, 7> invalidConfigurations{
        R"json({})json",
        R"json({"flipDurationMilliseconds":249,"backgroundColor":"#000000","cardColor":"#FF3B43","digitColor":"#FFFFFF","dateColor":"#D8D8D8"})json",
        R"json({"flipDurationMilliseconds":801,"backgroundColor":"#000000","cardColor":"#FF3B43","digitColor":"#FFFFFF","dateColor":"#D8D8D8"})json",
        R"json({"flipDurationMilliseconds":420,"backgroundColor":"000000","cardColor":"#FF3B43","digitColor":"#FFFFFF","dateColor":"#D8D8D8"})json",
        R"json({"flipDurationMilliseconds":420,"backgroundColor":"#000000","cardColor":"#FF3B4G","digitColor":"#FFFFFF","dateColor":"#D8D8D8"})json",
        R"json({"flipDurationMilliseconds":420,"backgroundColor":"#000000","cardColor":"#FF3B43","digitColor":"#FFFFFF","dateColor":"#D8D8D8","unknown":1})json",
        R"json({"flipDurationMilliseconds":420,"flipDurationMilliseconds":421,"backgroundColor":"#000000","cardColor":"#FF3B43","digitColor":"#FFFFFF","dateColor":"#D8D8D8"})json",
    };
    for (const std::string_view invalid : invalidConfigurations)
    {
        wil::com_ptr_nothrow<IRedXeWidgetProvider> invalidProvider;
        if (CreateProvider(create, invalid, invalidProvider) != HRESULT_FROM_WIN32(ERROR_INVALID_DATA) ||
            invalidProvider)
        {
            return kTestFailure;
        }
    }
    constexpr std::string_view invalidPluginConfiguration =
        R"json({"plugin":{"bad":1},"instance":{"flipDurationMilliseconds":420,"backgroundColor":"#000000","cardColor":"#FF3B43","digitColor":"#FFFFFF","dateColor":"#D8D8D8"}})json";
    options.configurationJsonUtf8 = invalidPluginConfiguration.data();
    options.configurationBytes = static_cast<std::uint32_t>(invalidPluginConfiguration.size());
    object = reinterpret_cast<void*>(1);
    if (create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kPluginId, &object) !=
            HRESULT_FROM_WIN32(ERROR_INVALID_DATA) ||
        object)
    {
        return kTestFailure;
    }
    const char marker = '{';
    options.configurationJsonUtf8 = &marker;
    options.configurationBytes = kRedXeMaximumFactoryConfigurationBytes + 1U;
    object = reinterpret_cast<void*>(1);
    if (create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kPluginId, &object) != E_INVALIDARG || object)
    {
        return kTestFailure;
    }

    constexpr std::array<std::string_view, 3> validConfigurations{
        kDefaultConfiguration,
        R"json({"flipDurationMilliseconds":250,"backgroundColor":"#aBcDeF","cardColor":"#000000","digitColor":"#123456","dateColor":"#ffffff"})json",
        R"json({"flipDurationMilliseconds":800,"backgroundColor":"#FFFFFF","cardColor":"#ABCDEF","digitColor":"#000000","dateColor":"#102030"})json",
    };
    for (const std::string_view valid : validConfigurations)
    {
        wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
        result = CreateProvider(create, valid, provider);
        if (FAILED(result))
        {
            return result;
        }
    }
    return S_OK;
}

[[nodiscard]] HRESULT ValidateAllocationFreeRender(IRedXeGpuWidget& widget, RenderTarget& target) noexcept
{
#if defined(_DEBUG)
    for (std::uint32_t index = 0; index < 128; ++index)
    {
        const HRESULT result = RenderFrame(widget, target, 1.0f, 0.0f, nullptr);
        if (FAILED(result))
        {
            return result;
        }
    }
    target.context->Flush();
    Sleep(25);
    gRenderAllocations = 0;
    gRenderThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(CountRenderAllocation);
    HRESULT result = S_OK;
    for (std::uint32_t index = 0; index < 120; ++index)
    {
        result = RenderFrame(widget, target, 1.0f, 0.0f, nullptr);
        if (FAILED(result))
        {
            break;
        }
    }
    (void)_CrtSetAllocHook(previous);
    gRenderThread.store(0, std::memory_order_relaxed);
    if (FAILED(result))
    {
        return result;
    }
    if (gRenderAllocations.load(std::memory_order_relaxed) != 0)
    {
        std::wprintf(L"Desk Clock render CRT allocations: %llu\n",
                     static_cast<unsigned long long>(gRenderAllocations.load(std::memory_order_relaxed)));
        return kTestFailure;
    }
#else
    (void)widget;
    (void)target;
#endif
    return S_OK;
}

[[nodiscard]] HRESULT WaitForQuery(ID3D11DeviceContext& context, ID3D11Query& query, void* data,
                                   std::uint32_t dataBytes) noexcept
{
    for (std::uint32_t attempt = 0; attempt < 100'000; ++attempt)
    {
        const HRESULT result = context.GetData(&query, data, dataBytes, 0);
        if (result == S_OK)
        {
            return S_OK;
        }
        if (result != S_FALSE)
        {
            return result;
        }
        if ((attempt & 255U) == 0)
        {
            SwitchToThread();
        }
    }
    return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}

[[nodiscard]] HRESULT MeasureReleaseSubmission(IRedXeGpuWidget& widget, RenderTarget& target,
                                               DeskClockGetTestDiagnosticsFn getDiagnostics,
                                               DeskClockSetTestTimeFn setTime, IRedXeScheduledWidget& scheduledWidget,
                                               const PROCESS_MEMORY_COUNTERS_EX& baselineMemory,
                                               const PROCESS_MEMORY_COUNTERS_EX& warmedMemory) noexcept
{
#if defined(NDEBUG)
    constexpr std::uint32_t sampleFrames = 240;
    DeskClockTestDiagnostics before{sizeof(DeskClockTestDiagnostics)};
    DeskClockTestDiagnostics after{sizeof(DeskClockTestDiagnostics)};
    if (FAILED(getDiagnostics(&before)))
    {
        return kTestFailure;
    }
    LARGE_INTEGER frequency{};
    LARGE_INTEGER baselineStart{};
    LARGE_INTEGER baselineEnd{};
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&baselineStart))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const D3D11_VIEWPORT benchmarkViewport{
        0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f,
    };
    for (std::uint32_t index = 0; index < sampleFrames; ++index)
    {
        target.context->ClearState();
        ID3D11RenderTargetView* views[] = {target.view.get()};
        target.context->OMSetRenderTargets(1, views, nullptr);
        target.context->RSSetViewports(1, &benchmarkViewport);
    }
    target.context->Flush();
    if (!QueryPerformanceCounter(&baselineEnd) || !QueryPerformanceCounter(&start))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    HRESULT result = S_OK;
    for (std::uint32_t index = 0; index < sampleFrames; ++index)
    {
        result = RenderFrame(widget, target, 10.0f, 0.0f, nullptr);
        if (FAILED(result))
        {
            return result;
        }
    }
    target.context->Flush();
    if (!QueryPerformanceCounter(&end) || FAILED(getDiagnostics(&after)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const double staticMilliseconds =
        static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
    const double disabledMilliseconds = static_cast<double>(baselineEnd.QuadPart - baselineStart.QuadPart) * 1000.0 /
                                        static_cast<double>(frequency.QuadPart);

    const DeskClockTestTime animationTime{sizeof(DeskClockTestTime), 2026, 11, 0, 1, 12, 34, 57, 0};
    std::uint32_t animationDelay = 0;
    result = setTime(&animationTime);
    if (SUCCEEDED(result))
    {
        result = scheduledWidget.GetNextFrameDelayMilliseconds(&animationDelay);
    }
    if (SUCCEEDED(result) && animationDelay != 1)
    {
        result = scheduledWidget.GetNextFrameDelayMilliseconds(&animationDelay);
    }
    if (FAILED(result) || animationDelay != 1 || FAILED(RenderFrame(widget, target, 10.0f, 0.0f, nullptr)))
    {
        return FAILED(result) ? result : kTestFailure;
    }
    DeskClockTestDiagnostics animationBefore{sizeof(DeskClockTestDiagnostics)};
    DeskClockTestDiagnostics animationAfter{sizeof(DeskClockTestDiagnostics)};
    if (FAILED(getDiagnostics(&animationBefore)) || !QueryPerformanceCounter(&start))
    {
        return kTestFailure;
    }
    constexpr std::uint32_t animationFrames = 25;
    for (std::uint32_t index = 0; index < animationFrames; ++index)
    {
        result = RenderFrame(widget, target, 10.0f + static_cast<float>(index) * 0.016f, 0.016f, nullptr);
        if (FAILED(result))
        {
            return result;
        }
    }
    target.context->Flush();
    if (!QueryPerformanceCounter(&end) || FAILED(getDiagnostics(&animationAfter)))
    {
        return kTestFailure;
    }
    const double animationMilliseconds =
        static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);

    D3D11_QUERY_DESC queryDescription{};
    queryDescription.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    wil::com_ptr_nothrow<ID3D11Query> disjoint;
    result = target.device->CreateQuery(&queryDescription, disjoint.put());
    queryDescription.Query = D3D11_QUERY_TIMESTAMP;
    wil::com_ptr_nothrow<ID3D11Query> gpuStart;
    wil::com_ptr_nothrow<ID3D11Query> gpuEnd;
    if (SUCCEEDED(result))
    {
        result = target.device->CreateQuery(&queryDescription, gpuStart.put());
    }
    if (SUCCEEDED(result))
    {
        result = target.device->CreateQuery(&queryDescription, gpuEnd.put());
    }
    double gpuMilliseconds = 0.0;
    if (SUCCEEDED(result))
    {
        target.context->Begin(disjoint.get());
        target.context->End(gpuStart.get());
        result = RenderFrame(widget, target, 11.0f, 0.0f, nullptr);
        target.context->End(gpuEnd.get());
        target.context->End(disjoint.get());
        target.context->Flush();
    }
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
    std::uint64_t gpuStartTick = 0;
    std::uint64_t gpuEndTick = 0;
    if (SUCCEEDED(result))
    {
        result = WaitForQuery(*target.context, *disjoint, &disjointData, sizeof(disjointData));
    }
    if (SUCCEEDED(result))
    {
        result = WaitForQuery(*target.context, *gpuStart, &gpuStartTick, sizeof(gpuStartTick));
    }
    if (SUCCEEDED(result))
    {
        result = WaitForQuery(*target.context, *gpuEnd, &gpuEndTick, sizeof(gpuEndTick));
    }
    if (FAILED(result) || disjointData.Disjoint || disjointData.Frequency == 0 || gpuEndTick < gpuStartTick)
    {
        return FAILED(result) ? result : kTestFailure;
    }
    gpuMilliseconds =
        static_cast<double>(gpuEndTick - gpuStartTick) * 1000.0 / static_cast<double>(disjointData.Frequency);

    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    constexpr double bytesPerMebibyte = 1024.0 * 1024.0;
    const auto mebibyteDelta = [=](SIZE_T current, SIZE_T baseline) noexcept
    { return (static_cast<double>(current) - static_cast<double>(baseline)) / bytesPerMebibyte; };
    std::wprintf(L"Desk Clock Release measurement: disabled/static/animation CPU %.4f/%.4f/%.4f ms/frame, active WARP "
                 L"timestamp %.4f ms/frame, WARP-inclusive activation private/working-set %.2f/%.2f MiB, "
                 L"steady private/working-set delta %.2f/%.2f MiB, static/animation draws %.2f/%.2f per frame.\n",
                 disabledMilliseconds / sampleFrames, staticMilliseconds / sampleFrames,
                 animationMilliseconds / animationFrames, gpuMilliseconds,
                 mebibyteDelta(memory.PrivateUsage, baselineMemory.PrivateUsage),
                 mebibyteDelta(memory.WorkingSetSize, baselineMemory.WorkingSetSize),
                 mebibyteDelta(memory.PrivateUsage, warmedMemory.PrivateUsage),
                 mebibyteDelta(memory.WorkingSetSize, warmedMemory.WorkingSetSize),
                 static_cast<double>(after.drawCalls - before.drawCalls) / sampleFrames,
                 static_cast<double>(animationAfter.drawCalls - animationBefore.drawCalls) / animationFrames);
#else
    (void)widget;
    (void)target;
    (void)getDiagnostics;
    (void)setTime;
    (void)scheduledWidget;
    (void)baselineMemory;
    (void)warmedMemory;
#endif
    return S_OK;
}

[[nodiscard]] HRESULT ValidateRendering(RedXeCreateFn create, DeskClockSetTestTimeFn setTime,
                                        DeskClockGetTestDiagnosticsFn getDiagnostics) noexcept
{
    std::string copiedConfiguration(kDefaultConfiguration);
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    HRESULT result = CreateProvider(create, copiedConfiguration, provider);
    if (FAILED(result))
    {
        return result;
    }
    std::fill(copiedConfiguration.begin(), copiedConfiguration.end(), 'X');

    wil::com_ptr_nothrow<IRedXeWidget> widget;
    result = provider->CreateWidget(kWidgetTypeId, "desk.clock.test", widget.put());
    if (FAILED(result) || !widget)
    {
        return FAILED(result) ? result : kTestFailure;
    }
    wil::com_ptr_nothrow<IRedXeGpuWidget> gpu;
    wil::com_ptr_nothrow<IRedXeScheduledWidget> scheduled;
    if (FAILED(widget.query_to(gpu.put())) || FAILED(widget.query_to(scheduled.put())) || !gpu || !scheduled)
    {
        return kTestFailure;
    }
    wil::com_ptr_nothrow<IUnknown> widgetIdentity;
    wil::com_ptr_nothrow<IUnknown> gpuIdentity;
    wil::com_ptr_nothrow<IUnknown> scheduledIdentity;
    if (FAILED(widget.query_to(widgetIdentity.put())) || FAILED(gpu.query_to(gpuIdentity.put())) ||
        FAILED(scheduled.query_to(scheduledIdentity.put())) || widgetIdentity.get() != gpuIdentity.get() ||
        widgetIdentity.get() != scheduledIdentity.get())
    {
        return kTestFailure;
    }
    void* unsupported = reinterpret_cast<void*>(1);
    if (widget->QueryInterface(__uuidof(IRedXeWindowWidget), &unsupported) != E_NOINTERFACE || unsupported ||
        scheduled->GetNextFrameDelayMilliseconds(nullptr) != E_POINTER)
    {
        return kTestFailure;
    }
    std::uint32_t delay = 99;
    if (scheduled->GetNextFrameDelayMilliseconds(&delay) != S_OK || delay != 1)
    {
        return kTestFailure;
    }

    RenderTarget target;
    result = CreateRenderTarget(800, 300, target);
    if (FAILED(result))
    {
        return result;
    }
    if (gpu->OnDeviceCreated(nullptr) != E_POINTER || gpu->Render(nullptr) != E_POINTER)
    {
        return kTestFailure;
    }
    const RedXeGpuDeviceContext deviceContext{
        sizeof(RedXeGpuDeviceContext),
        target.device.get(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        target.featureLevel,
    };
    result = gpu->OnDeviceCreated(&deviceContext);
    if (FAILED(result))
    {
        return result;
    }
    DeskClockTestDiagnostics typography{sizeof(DeskClockTestDiagnostics)};
    if (FAILED(getDiagnostics(&typography)) || typography.typographyBuilds == 0 ||
        typography.atlasBytes != 1024U * 1024U || typography.constantBytes != 400U)
    {
        std::wprintf(L"Desk Clock DirectWrite resource bounds were not established.\n");
        return kTestFailure;
    }
    RedXeGpuDeviceContext invalidDevice = deviceContext;
    invalidDevice.sizeBytes = sizeof(std::uint32_t);
    if (gpu->OnDeviceCreated(&invalidDevice) != E_INVALIDARG)
    {
        return kTestFailure;
    }
    RedXeWidgetFrameContext zeroWidget{
        sizeof(RedXeWidgetFrameContext), 0, 0, USER_DEFAULT_SCREEN_DPI, 0.0f, 0.0f,
    };
    D3D11_VIEWPORT zeroViewport{};
    const RedXeGpuFrameContext zeroFrame{
        sizeof(RedXeGpuFrameContext),
        &zeroWidget,
        target.context.get(),
        zeroViewport,
    };
    if (gpu->Render(&zeroFrame) != S_OK)
    {
        return kTestFailure;
    }
    RedXeGpuFrameContext invalidFrame = zeroFrame;
    invalidFrame.sizeBytes = sizeof(std::uint32_t);
    if (gpu->Render(&invalidFrame) != E_INVALIDARG)
    {
        return kTestFailure;
    }

    const DeskClockTestTime initial{sizeof(DeskClockTestTime), 2026, 8, 1, 31, 22, 24, 19, 125};
    result = setTime(&initial);
    std::vector<std::uint8_t> first;
    if (SUCCEEDED(result))
    {
        result = RenderFrame(*gpu, target, 0.0f, 0.0f, &first);
    }
    if (SUCCEEDED(result))
    {
        result = WriteSnapshot(first, target.width, target.height, L"DeskClockSnapshot.bmp");
    }
    if (SUCCEEDED(result))
    {
        result = ValidateComposition(first);
    }
    if (FAILED(result))
    {
        std::wprintf(L"Desk Clock initial WARP frame/composition failed: 0x%08X\n", static_cast<unsigned int>(result));
        return result;
    }
    DeskClockTestDiagnostics before{sizeof(DeskClockTestDiagnostics)};
    DeskClockTestDiagnostics after{sizeof(DeskClockTestDiagnostics)};
    if (FAILED(getDiagnostics(&before)) || FAILED(RenderFrame(*gpu, target, 0.0f, 0.0f, nullptr)) ||
        FAILED(getDiagnostics(&after)) || after.constantUploads != before.constantUploads ||
        after.drawCalls - before.drawCalls != 3)
    {
        std::wprintf(L"Desk Clock static upload/draw cache validation failed.\n");
        return kTestFailure;
    }
    delay = 0;
    if (scheduled->GetNextFrameDelayMilliseconds(&delay) != S_OK || delay < 800 || delay > 900)
    {
        std::wprintf(L"Desk Clock initial boundary delay was %u ms.\n", delay);
        return kTestFailure;
    }

    DeskClockTestTime next = initial;
    next.second = 20;
    next.milliseconds = 0;
    if (FAILED(setTime(&next)))
    {
        return kTestFailure;
    }
    delay = 0;
    if (scheduled->GetNextFrameDelayMilliseconds(&delay) != S_OK || delay != 1)
    {
        std::wprintf(L"Desk Clock changed-time schedule delay was %u ms.\n", delay);
        return kTestFailure;
    }
    std::array<std::vector<std::uint8_t>, 5> transition;
    constexpr std::array<float, 5> deltas{0.0f, 0.105f, 0.105f, 0.105f, 0.106f};
    float elapsed = 1.0f;
    for (std::size_t index = 0; index < transition.size(); ++index)
    {
        elapsed += deltas[index];
        result = RenderFrame(*gpu, target, elapsed, deltas[index], &transition[index]);
        if (FAILED(result))
        {
            return result;
        }
    }
    if (transition[1] == transition[2] || transition[2] == transition[3] || transition[3] == transition[4] ||
        first == transition[4])
    {
        std::wprintf(L"Desk Clock transition snapshots were not distinct.\n");
        return kTestFailure;
    }
    for (const auto& phase : transition)
    {
        if (!RectangleEquals(first, phase, target.width, 0, 0, 520, target.height))
        {
            std::wprintf(L"Desk Clock changed or hid an unchanged rollover tile.\n");
            return kTestFailure;
        }
    }
    constexpr std::array<const wchar_t*, 5> transitionNames{
        L"DeskClockTransition000.bmp", L"DeskClockTransition025.bmp", L"DeskClockTransition050.bmp",
        L"DeskClockTransition075.bmp", L"DeskClockTransition100.bmp",
    };
    for (std::size_t index = 0; index < transition.size(); ++index)
    {
        result = WriteSnapshot(transition[index], target.width, target.height, transitionNames[index]);
        if (FAILED(result))
        {
            return result;
        }
    }
    delay = 0;
    if (scheduled->GetNextFrameDelayMilliseconds(&delay) != S_OK || delay < 550 || delay > 650)
    {
        std::wprintf(L"Desk Clock post-transition boundary delay was %u ms.\n", delay);
        return kTestFailure;
    }

    const DeskClockTestTime midnight{sizeof(DeskClockTestTime), 2026, 9, 2, 1, 0, 0, 0, 0};
    if (FAILED(setTime(&midnight)))
    {
        return kTestFailure;
    }
    delay = 0;
    if (scheduled->GetNextFrameDelayMilliseconds(&delay) != S_OK || delay != 1)
    {
        return kTestFailure;
    }
    std::vector<std::uint8_t> midnightStart;
    std::vector<std::uint8_t> midnightPhase;
    std::vector<std::uint8_t> midnightFinal;
    result = RenderFrame(*gpu, target, elapsed, 0.0f, &midnightStart);
    elapsed += 0.105f;
    if (SUCCEEDED(result))
    {
        result = RenderFrame(*gpu, target, elapsed, 0.105f, &midnightPhase);
    }
    elapsed += 0.316f;
    if (SUCCEEDED(result))
    {
        result = RenderFrame(*gpu, target, elapsed, 0.316f, &midnightFinal);
    }
    if (FAILED(result) || RectangleEquals(transition[4], midnightPhase, target.width, 250, 220, 300, 60) ||
        RectangleEquals(midnightPhase, midnightFinal, target.width, 250, 220, 300, 60))
    {
        return FAILED(result) ? result : kTestFailure;
    }
    if (FAILED(WriteSnapshot(midnightPhase, target.width, target.height, L"DeskClockMidnightCrossFade.bmp")) ||
        FAILED(WriteSnapshot(midnightFinal, target.width, target.height, L"DeskClockMidnightFinal.bmp")))
    {
        return kTestFailure;
    }
    const PixelBounds doubleDigitDate = FindColorBounds(first, target.width, {216, 216, 216}, 5, 200, 280);
    const PixelBounds singleDigitDate = FindColorBounds(midnightFinal, target.width, {216, 216, 216}, 5, 200, 280);
    const std::uint32_t doubleDigitWidth = doubleDigitDate.maximumX - doubleDigitDate.minimumX + 1U;
    const std::uint32_t singleDigitWidth = singleDigitDate.maximumX - singleDigitDate.minimumX + 1U;
    const std::uint32_t doubleDigitCenter = doubleDigitDate.minimumX + doubleDigitDate.maximumX;
    const std::uint32_t singleDigitCenter = singleDigitDate.minimumX + singleDigitDate.maximumX;
    if (doubleDigitDate.count == 0 || singleDigitDate.count == 0 || doubleDigitWidth < singleDigitWidth + 5U ||
        doubleDigitCenter < 796U || doubleDigitCenter > 802U || singleDigitCenter < 796U || singleDigitCenter > 802U)
    {
        std::wprintf(L"Desk Clock native date advances/centering failed: double=%u/%u, single=%u/%u.\n",
                     doubleDigitWidth, doubleDigitCenter, singleDigitWidth, singleDigitCenter);
        return kTestFailure;
    }

    const auto animateTo = [&](const DeskClockTestTime& time) noexcept -> HRESULT
    {
        HRESULT animateResult = setTime(&time);
        if (FAILED(animateResult))
        {
            return animateResult;
        }
        std::uint32_t nextDelay = 0;
        animateResult = scheduled->GetNextFrameDelayMilliseconds(&nextDelay);
        if (SUCCEEDED(animateResult) && nextDelay != 1)
        {
            animateResult = scheduled->GetNextFrameDelayMilliseconds(&nextDelay);
        }
        if (FAILED(animateResult) || nextDelay != 1)
        {
            return FAILED(animateResult) ? animateResult : kTestFailure;
        }
        animateResult = RenderFrame(*gpu, target, elapsed, 0.0f, nullptr);
        elapsed += 0.421f;
        return SUCCEEDED(animateResult) ? RenderFrame(*gpu, target, elapsed, 0.421f, nullptr) : animateResult;
    };
    constexpr std::array rolloverTimes{
        DeskClockTestTime{sizeof(DeskClockTestTime), 2026, 9, 2, 1, 0, 0, 9, 0},
        DeskClockTestTime{sizeof(DeskClockTestTime), 2026, 9, 2, 1, 0, 0, 10, 0},
        DeskClockTestTime{sizeof(DeskClockTestTime), 2026, 9, 2, 1, 0, 0, 59, 0},
        DeskClockTestTime{sizeof(DeskClockTestTime), 2026, 9, 2, 1, 0, 1, 0, 0},
        DeskClockTestTime{sizeof(DeskClockTestTime), 2026, 12, 4, 31, 23, 59, 59, 0},
        DeskClockTestTime{sizeof(DeskClockTestTime), 2027, 1, 5, 1, 0, 0, 0, 0},
        DeskClockTestTime{sizeof(DeskClockTestTime), 2028, 2, 1, 28, 23, 59, 59, 0},
        DeskClockTestTime{sizeof(DeskClockTestTime), 2028, 2, 2, 29, 0, 0, 0, 0},
        DeskClockTestTime{sizeof(DeskClockTestTime), 2026, 11, 0, 1, 1, 0, 0, 0},
    };
    for (const DeskClockTestTime& time : rolloverTimes)
    {
        result = animateTo(time);
        if (FAILED(result))
        {
            return result;
        }
    }
    DeskClockTestTime rollback = rolloverTimes.back();
    rollback.hour = 0;
    if (FAILED(animateTo(rollback)))
    {
        return kTestFailure;
    }
    DeskClockTestTime recovered = rollback;
    recovered.hour = 12;
    recovered.minute = 34;
    recovered.second = 56;
    recovered.milliseconds = 250;
    if (FAILED(setTime(&recovered)))
    {
        return kTestFailure;
    }
    elapsed += 3.0f;
    result = RenderFrame(*gpu, target, elapsed, 3.0f, nullptr);
    delay = 0;
    if (FAILED(result) || scheduled->GetNextFrameDelayMilliseconds(&delay) != S_OK || delay < 700 || delay > 800)
    {
        return FAILED(result) ? result : kTestFailure;
    }

    result = ValidateAllocationFreeRender(*gpu, target);
    if (FAILED(result))
    {
        std::wprintf(L"Desk Clock allocation validation failed: 0x%08X\n", static_cast<unsigned int>(result));
        return result;
    }
#if defined(NDEBUG)
    gpu->OnDeviceLost();
    RenderTarget benchmarkTarget;
    result = CreateRenderTarget(2560, 720, benchmarkTarget);
    if (FAILED(result))
    {
        return result;
    }
    constexpr std::array<float, 4> benchmarkClear{0.0f, 0.0f, 0.0f, 1.0f};
    benchmarkTarget.context->ClearRenderTargetView(benchmarkTarget.view.get(), benchmarkClear.data());
    benchmarkTarget.context->CopyResource(benchmarkTarget.staging.get(), benchmarkTarget.texture.get());
    D3D11_MAPPED_SUBRESOURCE baselineMapped{};
    result = benchmarkTarget.context->Map(benchmarkTarget.staging.get(), 0, D3D11_MAP_READ, 0, &baselineMapped);
    if (FAILED(result))
    {
        return result;
    }
    benchmarkTarget.context->Unmap(benchmarkTarget.staging.get(), 0);
    PROCESS_MEMORY_COUNTERS_EX baselineMemory{};
    baselineMemory.cb = sizeof(baselineMemory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&baselineMemory),
                              sizeof(baselineMemory)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const RedXeGpuDeviceContext benchmarkDevice{
        sizeof(RedXeGpuDeviceContext),
        benchmarkTarget.device.get(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        benchmarkTarget.featureLevel,
    };
    result = gpu->OnDeviceCreated(&benchmarkDevice);
    if (SUCCEEDED(result))
    {
        result = RenderFrame(*gpu, benchmarkTarget, 9.0f, 3.0f, nullptr);
    }
    for (std::uint32_t warmup = 0; SUCCEEDED(result) && warmup < 120; ++warmup)
    {
        result = RenderFrame(*gpu, benchmarkTarget, 9.0f, 0.0f, nullptr);
    }
    if (FAILED(result))
    {
        return result;
    }
    const DeskClockTestTime benchmarkAnimationTime{sizeof(DeskClockTestTime), 2026, 11, 0, 1, 12, 34, 58, 0};
    std::uint32_t benchmarkAnimationDelay = 0;
    result = setTime(&benchmarkAnimationTime);
    if (SUCCEEDED(result))
    {
        result = scheduled->GetNextFrameDelayMilliseconds(&benchmarkAnimationDelay);
    }
    if (SUCCEEDED(result) && benchmarkAnimationDelay != 1)
    {
        result = scheduled->GetNextFrameDelayMilliseconds(&benchmarkAnimationDelay);
    }
    if (SUCCEEDED(result) && benchmarkAnimationDelay == 1)
    {
        result = RenderFrame(*gpu, benchmarkTarget, 9.0f, 0.0f, nullptr);
    }
    for (std::uint32_t warmup = 0; SUCCEEDED(result) && warmup < 27; ++warmup)
    {
        result = RenderFrame(*gpu, benchmarkTarget, 9.0f + static_cast<float>(warmup) * 0.016f, 0.016f, nullptr);
    }
    if (FAILED(result) || benchmarkAnimationDelay != 1)
    {
        return FAILED(result) ? result : kTestFailure;
    }
    benchmarkTarget.context->CopyResource(benchmarkTarget.staging.get(), benchmarkTarget.texture.get());
    D3D11_MAPPED_SUBRESOURCE warmupMapped{};
    result = benchmarkTarget.context->Map(benchmarkTarget.staging.get(), 0, D3D11_MAP_READ, 0, &warmupMapped);
    if (FAILED(result))
    {
        return result;
    }
    benchmarkTarget.context->Unmap(benchmarkTarget.staging.get(), 0);
    PROCESS_MEMORY_COUNTERS_EX warmedMemory{};
    warmedMemory.cb = sizeof(warmedMemory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&warmedMemory),
                              sizeof(warmedMemory)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    result = MeasureReleaseSubmission(*gpu, benchmarkTarget, getDiagnostics, setTime, *scheduled, baselineMemory,
                                      warmedMemory);
    std::vector<std::uint8_t> targetDisplaySnapshot;
    if (SUCCEEDED(result))
    {
        result = setTime(&initial);
    }
    if (SUCCEEDED(result))
    {
        result = RenderFrame(*gpu, benchmarkTarget, 12.0f, 3.0f, &targetDisplaySnapshot);
    }
    if (SUCCEEDED(result))
    {
        result = WriteSnapshot(targetDisplaySnapshot, benchmarkTarget.width, benchmarkTarget.height,
                               L"DeskClock2560x720.bmp");
    }
#else
    const PROCESS_MEMORY_COUNTERS_EX baselineMemory{};
    const PROCESS_MEMORY_COUNTERS_EX warmedMemory{};
    result = MeasureReleaseSubmission(*gpu, target, getDiagnostics, setTime, *scheduled, baselineMemory, warmedMemory);
#endif
    if (FAILED(result))
    {
        return result;
    }
    constexpr std::array<std::array<std::uint32_t, 3>, 3> geometryCases{
        {{300, 800, 144}, {320, 120, 192}, {1024, 256, 96}}};
    for (const auto& geometry : geometryCases)
    {
        RenderTarget geometryTarget;
        result = CreateRenderTarget(geometry[0], geometry[1], geometryTarget);
        if (FAILED(result))
        {
            return result;
        }
        const RedXeGpuDeviceContext geometryDevice{
            sizeof(RedXeGpuDeviceContext),
            geometryTarget.device.get(),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            geometryTarget.featureLevel,
        };
        result = gpu->OnDeviceCreated(&geometryDevice);
        if (SUCCEEDED(result))
        {
            result = RenderFrame(*gpu, geometryTarget, elapsed, 3.0f, nullptr, geometry[2]);
        }
        if (FAILED(result))
        {
            return result;
        }
    }
    RenderTarget referenceTarget;
    result = CreateRenderTarget(450, 190, referenceTarget);
    if (FAILED(result))
    {
        return result;
    }
    const RedXeGpuDeviceContext referenceDevice{
        sizeof(RedXeGpuDeviceContext),
        referenceTarget.device.get(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        referenceTarget.featureLevel,
    };
    const DeskClockTestTime referenceTime{sizeof(DeskClockTestTime), 2026, 9, 2, 1, 8, 44, 23, 0};
    std::vector<std::uint8_t> referenceSnapshot;
    result = setTime(&referenceTime);
    if (SUCCEEDED(result))
    {
        result = gpu->OnDeviceCreated(&referenceDevice);
    }
    if (SUCCEEDED(result))
    {
        elapsed += 3.0f;
        result = RenderFrame(*gpu, referenceTarget, elapsed, 3.0f, &referenceSnapshot);
    }
    if (SUCCEEDED(result))
    {
        result = WriteSnapshot(referenceSnapshot, referenceTarget.width, referenceTarget.height,
                               L"DeskClockReference450x190.bmp");
    }
    if (SUCCEEDED(result))
    {
        result = ValidateReferenceComposition(referenceSnapshot);
    }
    if (FAILED(result))
    {
        return result;
    }
    gpu->OnDeviceLost();
    DeskClockTestDiagnostics lost{sizeof(DeskClockTestDiagnostics)};
    if (FAILED(getDiagnostics(&lost)) || lost.liveDeviceResourceSets != 0)
    {
        std::wprintf(L"Desk Clock device-loss release validation failed.\n");
        return kTestFailure;
    }
    result = gpu->OnDeviceCreated(&deviceContext);
    if (SUCCEEDED(result))
    {
        result = RenderFrame(*gpu, target, elapsed, 3.0f, nullptr);
    }
    gpu->OnDeviceLost();
    if (FAILED(result))
    {
        std::wprintf(L"Desk Clock device recreation failed: 0x%08X\n", static_cast<unsigned int>(result));
        return result;
    }

    scheduledIdentity.reset();
    gpuIdentity.reset();
    widgetIdentity.reset();
    scheduled.reset();
    gpu.reset();
    widget.reset();
    provider.reset();
    DeskClockTestDiagnostics released{sizeof(DeskClockTestDiagnostics)};
    if (FAILED(getDiagnostics(&released)) || released.liveProviders != 0 || released.liveWidgets != 0 ||
        released.liveDeviceResourceSets != 0)
    {
        std::wprintf(L"Desk Clock COM/resource teardown validation failed.\n");
        return kTestFailure;
    }
    return setTime(nullptr);
}

[[nodiscard]] HRESULT Run() noexcept
{
    const bool compilerLoaded = GetModuleHandleW(L"d3dcompiler_47.dll") != nullptr;
    const bool directWriteLoaded = GetModuleHandleW(L"dwrite.dll") != nullptr;
    const bool wicLoaded = GetModuleHandleW(L"windowscodecs.dll") != nullptr;
    std::array<wchar_t, 1024> path{};
    HRESULT result = BuildSiblingPath(L"Plugins\\DeskClock.dll", path);
    if (FAILED(result))
    {
        return result;
    }
    wil::unique_hmodule module{
        LoadLibraryExW(path.data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    if (!module)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const RedXeCreateFn create = Resolve<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const RedXeEnumeratePluginsFn enumerate =
        Resolve<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    const RedXeGetPluginSettingsContractFn getContract =
        Resolve<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);
    const RedXePluginShutdownFn shutdown = Resolve<RedXePluginShutdownFn>(module.get(), kRedXePluginShutdownExport);
    const DeskClockSetTestTimeFn setTime = Resolve<DeskClockSetTestTimeFn>(module.get(), kDeskClockSetTestTimeExport);
    const DeskClockGetTestDiagnosticsFn diagnostics =
        Resolve<DeskClockGetTestDiagnosticsFn>(module.get(), kDeskClockGetTestDiagnosticsExport);
    if (!create || !enumerate || !getContract || !shutdown || !setTime || !diagnostics)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    result = ValidateFactoryAndSettings(create, enumerate, getContract);
    if (FAILED(result))
    {
        std::wprintf(L"Desk Clock factory/settings stage failed: 0x%08X\n", static_cast<unsigned int>(result));
    }
    if (SUCCEEDED(result))
    {
        result = ValidateRendering(create, setTime, diagnostics);
        if (FAILED(result))
        {
            std::wprintf(L"Desk Clock rendering stage failed: 0x%08X\n", static_cast<unsigned int>(result));
        }
    }
    shutdown();
    if (FAILED(result))
    {
        return result;
    }
    if ((!compilerLoaded && GetModuleHandleW(L"d3dcompiler_47.dll")) ||
        (!directWriteLoaded && GetModuleHandleW(L"dwrite.dll")) ||
        (!wicLoaded && GetModuleHandleW(L"windowscodecs.dll")))
    {
        return kTestFailure;
    }
    return S_OK;
}
} // namespace

int wmain() noexcept
{
    const HRESULT result = Run();
    if (FAILED(result))
    {
        std::wprintf(L"Desk Clock tests failed: 0x%08X\n", static_cast<unsigned int>(result));
        return 1;
    }
    std::wprintf(L"Desk Clock contract, scheduling, WARP rendering, allocation, and lifecycle tests passed.\n");
    return 0;
}
