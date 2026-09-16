#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Widget.h"
#include "StudioClockTestContract.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <psapi.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <windows.h>

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
static_assert(std::is_base_of_v<IUnknown, IRedXeScheduledWidget>);
static_assert(std::is_base_of_v<IUnknown, IRedXeRaisedWidget>);
static_assert(!std::is_base_of_v<IRedXeWidget, IRedXeScheduledWidget>);
static_assert(!std::is_base_of_v<IRedXeWidget, IRedXeRaisedWidget>);

constexpr char kPluginId[] = "builtin.studio-clock";
constexpr char kWidgetTypeId[] = "studio-clock";
constexpr std::string_view kDefaults =
    R"json({"showSecondProgress":true,"externalDotsAlwaysOn":true,"showSeconds":true,"secondsColor":"#FF1616","showDate":false,"dateFormat":"dd-mm-yyyy","timeColor":"#FF1616"})json";

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Function> [[nodiscard]] Function Resolve(HMODULE module, const char* name)
{
    const FARPROC procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    Expect(function != nullptr, "required StudioClock export is missing");
    return function;
}

[[nodiscard]] std::filesystem::path PluginPath()
{
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    Expect(length != 0 && length < executable.size(), "test executable path is unavailable");
    executable.resize(length);
    return std::filesystem::path(executable).parent_path() / L"Plugins" / L"StudioClock.dll";
}

struct Exports final
{
    RedXeCreateFn create = nullptr;
    RedXeEnumeratePluginsFn enumerate = nullptr;
    RedXeGetPluginSettingsContractFn getSettings = nullptr;
    RedXePluginShutdownFn shutdown = nullptr;
    StudioClockSetTestTimeFn setTime = nullptr;
    StudioClockGetTestDiagnosticsFn getDiagnostics = nullptr;
};

[[nodiscard]] std::string Configuration(bool showProgress, bool showSeconds, bool showDate,
                                        bool externalDotsAlwaysOn = true, std::string_view dateFormat = "dd-mm-yyyy",
                                        std::string_view secondsColor = "#FF1616",
                                        std::string_view timeColor = "#FF1616")
{
    std::string result;
    result.reserve(256);
    result.append("{\"showSecondProgress\":").append(showProgress ? "true" : "false");
    result.append(",\"externalDotsAlwaysOn\":").append(externalDotsAlwaysOn ? "true" : "false");
    result.append(",\"showSeconds\":").append(showSeconds ? "true" : "false");
    result.append(",\"secondsColor\":\"").append(secondsColor).append("\"");
    result.append(",\"showDate\":").append(showDate ? "true" : "false");
    result.append(",\"dateFormat\":\"").append(dateFormat).append("\"");
    result.append(",\"timeColor\":\"").append(timeColor).append("\"}");
    return result;
}

// backgroundColor is the host-resolved dashboard background (opaque ARGB); the plugin no longer owns a setting for it.
[[nodiscard]] HRESULT TryCreateProvider(RedXeCreateFn create, std::string_view configuration,
                                        wil::com_ptr_nothrow<IRedXeWidgetProvider>& provider,
                                        uint32_t backgroundColor = 0xFF111111) noexcept
{
    provider.reset();
    std::string envelope;
    try
    {
        envelope.reserve(configuration.size() + 32);
        envelope.append("{\"plugin\":{},\"instance\":").append(configuration).append("}");
        configuration = envelope;
        RedXeFactoryOptions options{};
        options.sizeBytes = sizeof(options);
        options.configurationJsonUtf8 = configuration.data();
        options.configurationBytes = static_cast<uint32_t>(configuration.size());
        options.backgroundColor = backgroundColor;
        void* object = nullptr;
        const HRESULT result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kPluginId, &object);
        if (SUCCEEDED(result))
        {
            provider.attach(static_cast<IRedXeWidgetProvider*>(object));
        }
        return result;
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

struct WidgetInterfaces final
{
    wil::com_ptr_nothrow<IRedXeWidget> widget;
    wil::com_ptr_nothrow<IRedXeGpuWidget> gpu;
    wil::com_ptr_nothrow<IRedXeScheduledWidget> scheduled;
};

[[nodiscard]] WidgetInterfaces CreateWidget(IRedXeWidgetProvider& provider, const char* instanceId)
{
    WidgetInterfaces created;
    Expect(provider.CreateWidget(kWidgetTypeId, instanceId, created.widget.put()) == S_OK && created.widget,
           "Studio Clock widget creation failed");
    Expect(created.widget.query_to(created.gpu.put()) == S_OK && created.gpu,
           "Studio Clock GPU sibling is unavailable");
    Expect(created.widget.query_to(created.scheduled.put()) == S_OK && created.scheduled,
           "Studio Clock scheduled sibling is unavailable");
    wil::com_ptr_nothrow<IRedXeRaisedWidget> raised;
    Expect(created.widget.query_to(raised.put()) == S_OK && raised, "Studio Clock raised sibling is unavailable");
    Expect(raised->GetRaisedExtent(nullptr) == E_POINTER, "Studio Clock raised extent accepted a null output");
    RedXeRaisedExtent extent = static_cast<RedXeRaisedExtent>(0);
    Expect(raised->GetRaisedExtent(&extent) == S_OK && extent == RedXeRaisedExtentHalf,
           "Studio Clock raised extent is not half");
    Expect(raised->SetRaised(TRUE) == S_OK && raised->SetRaised(FALSE) == S_OK, "Studio Clock SetRaised failed");
    return created;
}

struct RenderTarget final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> view;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    uint32_t width = 0;
    uint32_t height = 0;
};

[[nodiscard]] HRESULT CreateRenderTarget(uint32_t width, uint32_t height, RenderTarget& target) noexcept
{
    target = {};
    constexpr std::array featureLevels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       featureLevels.data(), static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION,
                                       target.device.put(), &target.featureLevel, target.context.put());
    if (result == E_INVALIDARG)
    {
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                   featureLevels.data() + 1, 1, D3D11_SDK_VERSION, target.device.put(),
                                   &target.featureLevel, target.context.put());
    }
    if (FAILED(result))
    {
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
    result = target.device->CreateTexture2D(&description, nullptr, target.texture.put());
    if (FAILED(result))
    {
        return result;
    }
    result = target.device->CreateRenderTargetView(target.texture.get(), nullptr, target.view.put());
    if (FAILED(result))
    {
        return result;
    }
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    result = target.device->CreateTexture2D(&description, nullptr, target.staging.put());
    if (FAILED(result))
    {
        return result;
    }
    target.width = width;
    target.height = height;
    return S_OK;
}

[[nodiscard]] RedXeGpuDeviceContext DeviceContextFor(const RenderTarget& target) noexcept
{
    return {
        sizeof(RedXeGpuDeviceContext),
        target.device.get(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        target.featureLevel,
    };
}

[[nodiscard]] HRESULT RenderOnly(IRedXeGpuWidget& widget, RenderTarget& target, float elapsedSeconds = 0.0f,
                                 uint32_t dpi = USER_DEFAULT_SCREEN_DPI) noexcept
{
    ID3D11RenderTargetView* views[] = {target.view.get()};
    target.context->OMSetRenderTargets(1, views, nullptr);
    const D3D11_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f,
    };
    target.context->RSSetViewports(1, &viewport);
    const RedXeWidgetFrameContext frame{
        sizeof(RedXeWidgetFrameContext), target.width, target.height, dpi, elapsedSeconds, 0.0f,
    };
    const RedXeGpuFrameContext gpuFrame{
        sizeof(RedXeGpuFrameContext),
        &frame,
        target.context.get(),
        viewport,
    };
    return widget.Render(&gpuFrame);
}

[[nodiscard]] std::vector<std::uint8_t> Readback(RenderTarget& target)
{
    target.context->CopyResource(target.staging.get(), target.texture.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Expect(target.context->Map(target.staging.get(), 0, D3D11_MAP_READ, 0, &mapped) == S_OK,
           "Studio Clock readback map failed");
    const auto unmap = wil::scope_exit([&]() noexcept { target.context->Unmap(target.staging.get(), 0); });
    const size_t rowBytes = static_cast<size_t>(target.width) * 4U;
    std::vector<std::uint8_t> pixels(rowBytes * target.height);
    for (uint32_t row = 0; row < target.height; ++row)
    {
        std::memcpy(pixels.data() + static_cast<size_t>(row) * rowBytes,
                    static_cast<const std::uint8_t*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch,
                    rowBytes);
    }
    return pixels;
}

[[nodiscard]] std::vector<std::uint8_t> RenderAndReadback(IRedXeGpuWidget& widget, RenderTarget& target,
                                                          float elapsedSeconds = 0.0f)
{
    Expect(RenderOnly(widget, target, elapsedSeconds) == S_OK, "Studio Clock render failed");
    return Readback(target);
}

[[nodiscard]] std::array<std::uint8_t, 4> PixelAt(const std::vector<std::uint8_t>& pixels, uint32_t width, uint32_t x,
                                                  uint32_t y)
{
    const size_t offset = (static_cast<size_t>(y) * width + x) * 4U;
    Expect(offset + 3 < pixels.size(), "pixel coordinate is outside the readback");
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

[[nodiscard]] std::uint8_t MaximumChannelNear(const std::vector<std::uint8_t>& pixels, uint32_t width, uint32_t height,
                                              float x, float y, uint32_t channel)
{
    const int centerX = static_cast<int>(std::lround(x));
    const int centerY = static_cast<int>(std::lround(y));
    std::uint8_t maximum = 0;
    for (int offsetY = -2; offsetY <= 2; ++offsetY)
    {
        for (int offsetX = -2; offsetX <= 2; ++offsetX)
        {
            const int sampleX = std::clamp(centerX + offsetX, 0, static_cast<int>(width) - 1);
            const int sampleY = std::clamp(centerY + offsetY, 0, static_cast<int>(height) - 1);
            maximum = std::max(maximum, PixelAt(pixels, width, static_cast<uint32_t>(sampleX),
                                                static_cast<uint32_t>(sampleY))[channel]);
        }
    }
    return maximum;
}

struct ClockLayout final
{
    float square;
    float originX;
    float originY;
};

[[nodiscard]] ClockLayout LayoutFor(uint32_t width, uint32_t height, bool showDate)
{
    constexpr float datedHeightScale = 10.0f / 9.0f;
    const float viewportWidth = static_cast<float>(width);
    const float viewportHeight = static_cast<float>(height);
    const float heightLimitedSquare = showDate ? viewportHeight / datedHeightScale : viewportHeight;
    const float square = std::min(viewportWidth, heightLimitedSquare);
    const float compositionHeight = square * (showDate ? datedHeightScale : 1.0f);
    return ClockLayout{square, (viewportWidth - square) * 0.5f, (viewportHeight - compositionHeight) * 0.5f};
}

[[nodiscard]] uint32_t CountActiveRingDots(const std::vector<std::uint8_t>& pixels, uint32_t width, uint32_t height,
                                           bool showDate = false)
{
    const ClockLayout layout = LayoutFor(width, height, showDate);
    const float square = layout.square;
    const std::uint8_t activeThreshold = square < 200.0f ? 100U : 180U;
    uint32_t active = 0;
    for (uint32_t index = 0; index < 60; ++index)
    {
        const float angle = static_cast<float>(index) * 0.10471975511965977f - 1.5707963267948966f;
        const float x = layout.originX + (0.5f + std::cos(angle) * 0.418f) * square;
        const float y = layout.originY + (0.5f + std::sin(angle) * 0.418f) * square;
        const std::uint8_t red = MaximumChannelNear(pixels, width, height, x, y, 0);
        if (red < 24)
        {
            throw std::runtime_error("Studio Clock ring position " + std::to_string(index) +
                                     " is missing (red=" + std::to_string(red) + ")");
        }
        active += red >= activeThreshold ? 1U : 0U;
    }
    return active;
}

[[nodiscard]] uint32_t CountActiveFiveSecondDots(const std::vector<std::uint8_t>& pixels, uint32_t width,
                                                 uint32_t height, bool showDate = false)
{
    const ClockLayout layout = LayoutFor(width, height, showDate);
    const float square = layout.square;
    const std::uint8_t activeThreshold = square < 200.0f ? 100U : 180U;
    uint32_t active = 0;
    for (uint32_t index = 0; index < 12; ++index)
    {
        const float angle = static_cast<float>(index * 5U) * 0.10471975511965977f - 1.5707963267948966f;
        const float x = layout.originX + (0.5f + std::cos(angle) * 0.447f) * square;
        const float y = layout.originY + (0.5f + std::sin(angle) * 0.447f) * square;
        const std::uint8_t red = MaximumChannelNear(pixels, width, height, x, y, 0);
        if (red < 24)
        {
            throw std::runtime_error("Studio Clock five-second emphasis position " + std::to_string(index) +
                                     " is missing (red=" + std::to_string(red) + ")");
        }
        active += red >= activeThreshold ? 1U : 0U;
    }
    return active;
}

[[nodiscard]] StudioClockTestDiagnostics Diagnostics(const Exports& exports)
{
    StudioClockTestDiagnostics diagnostics{};
    diagnostics.sizeBytes = sizeof(diagnostics);
    Expect(exports.getDiagnostics(&diagnostics) == S_OK, "Studio Clock diagnostics query failed");
    return diagnostics;
}

void SetTime(const Exports& exports, uint16_t year, uint16_t month, uint16_t day, uint16_t hour, uint16_t minute,
             uint16_t second, uint16_t milliseconds)
{
    const StudioClockTestTime time{sizeof(StudioClockTestTime), year, month, day, hour, minute, second, milliseconds};
    Expect(exports.setTime(&time) == S_OK, "Studio Clock deterministic time was rejected");
}

void ValidateDescriptor(IRedXeWidgetProvider& provider, bool showDate)
{
    const RedXeWidgetTypeDescriptor* descriptors = nullptr;
    uint32_t count = 0;
    Expect(provider.GetWidgetTypes(&descriptors, &count) == S_OK && descriptors && count == 1,
           "Studio Clock descriptor enumeration failed");
    const RedXeWidgetTypeDescriptor& descriptor = descriptors[0];
    Expect(RedXeAsciiEqualsIgnoreCase(descriptor.typeId, kWidgetTypeId) && descriptor.defaultWidth == 720.0f &&
               descriptor.minimumWidth == 160.0f && descriptor.flags == RedXeWidgetFlagNone,
           "Studio Clock descriptor identity or width is wrong");
    Expect(descriptor.defaultHeight == (showDate ? 800.0f : 720.0f) &&
               descriptor.minimumHeight == (showDate ? 178.0f : 160.0f),
           "Studio Clock descriptor did not reflect the date layout");
}

void ValidateFactoryAndSettings(const Exports& exports)
{
    const RedXePluginMetadata* metadata = reinterpret_cast<const RedXePluginMetadata*>(1);
    uint32_t count = 99;
    Expect(exports.enumerate(nullptr, &count) == E_POINTER && count == 0,
           "Studio Clock enumeration did not clear count");
    Expect(exports.enumerate(&metadata, &count) == S_OK && metadata && count == 1,
           "Studio Clock metadata enumeration failed");
    Expect(RedXeAsciiEqualsIgnoreCase(metadata[0].id, kPluginId) &&
               metadata[0].capabilities == RedXePluginCapabilityWidgetProvider,
           "Studio Clock metadata is invalid");

    const RedXePluginSettingsContract* contract = reinterpret_cast<const RedXePluginSettingsContract*>(1);
    Expect(exports.getSettings(kPluginId, nullptr) == E_POINTER, "Studio Clock settings accepted a null output");
    Expect(exports.getSettings("missing", &contract) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !contract,
           "Studio Clock settings accepted an unknown ID");
    Expect(exports.getSettings(kPluginId, &contract) == S_OK && contract &&
               contract->sizeBytes == sizeof(RedXePluginSettingsContract) &&
               std::string_view(contract->defaultsJsonUtf8, contract->defaultsBytes) == kDefaults,
           "Studio Clock defaults differ from the contract");
    const std::string_view schema(contract->schemaJsonUtf8, contract->schemaBytes);
    Expect(schema.find("showSecondProgress") != std::string_view::npos &&
               schema.find("externalDotsAlwaysOn") != std::string_view::npos &&
               schema.find("additionalProperties\":false") != std::string_view::npos,
           "Studio Clock schema is not closed or complete");

    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
    void* object = nullptr;
    Expect(exports.create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kPluginId, &object) == S_OK && object,
           "Studio Clock default creation failed");
    static_cast<IRedXeWidgetProvider*>(object)->Release();
    object = reinterpret_cast<void*>(1);
    Expect(exports.create(__uuidof(IRedXeWindowWidget), &options, nullptr, kPluginId, &object) == E_NOINTERFACE &&
               !object,
           "Studio Clock accepted an unsupported factory IID");
    object = reinterpret_cast<void*>(1);
    Expect(exports.create(__uuidof(IRedXeWidgetProvider), &options, nullptr, "missing", &object) ==
                   HRESULT_FROM_WIN32(ERROR_NOT_FOUND) &&
               !object,
           "Studio Clock accepted an unknown plugin ID");
    Expect(exports.create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kPluginId, nullptr) == E_POINTER,
           "Studio Clock accepted a null factory output");
    object = reinterpret_cast<void*>(1);
    Expect(exports.create(__uuidof(IRedXeWidgetProvider), &options, nullptr, nullptr, &object) == E_INVALIDARG &&
               !object,
           "Studio Clock accepted a null plugin ID");
    RedXeFactoryOptions oversizedOptions{};
    oversizedOptions.sizeBytes = sizeof(oversizedOptions) + 1;
    object = reinterpret_cast<void*>(1);
    Expect(exports.create(__uuidof(IRedXeWidgetProvider), &oversizedOptions, nullptr, kPluginId, &object) ==
                   E_INVALIDARG &&
               !object,
           "Studio Clock accepted an oversized factory record");

    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    Expect(TryCreateProvider(exports.create, kDefaults, provider) == S_OK && provider,
           "Studio Clock current effective settings were rejected");
    ValidateDescriptor(*provider, false);

    RedXeFactoryOptions directOptions{};
    directOptions.sizeBytes = sizeof(directOptions);
    directOptions.configurationJsonUtf8 = kDefaults.data();
    directOptions.configurationBytes = static_cast<uint32_t>(kDefaults.size());
    object = reinterpret_cast<void*>(1);
    Expect(exports.create(__uuidof(IRedXeWidgetProvider), &directOptions, nullptr, kPluginId, &object) ==
                   HRESULT_FROM_WIN32(ERROR_INVALID_DATA) &&
               !object,
           "Studio Clock accepted the removed direct settings form");

    wil::com_ptr_nothrow<IRedXeWidgetProvider> datedProvider;
    const std::string datedConfiguration = Configuration(true, true, true);
    Expect(TryCreateProvider(exports.create, datedConfiguration, datedProvider) == S_OK && datedProvider,
           "Studio Clock dated provider creation failed");
    ValidateDescriptor(*datedProvider, true);

    for (uint32_t bits = 0; bits < 16; ++bits)
    {
        const std::string configuration =
            Configuration((bits & 1U) != 0, (bits & 2U) != 0, (bits & 4U) != 0, (bits & 8U) != 0);
        provider.reset();
        Expect(TryCreateProvider(exports.create, configuration, provider) == S_OK && provider,
               "Studio Clock rejected a valid boolean combination");
    }
    constexpr std::array dateFormats{"dd-mm-yyyy", "mm-dd-yyyy", "yyyy-mm-dd"};
    for (const char* dateFormat : dateFormats)
    {
        const std::string configuration = Configuration(true, true, true, true, dateFormat, "#aBc123", "#012aBC");
        provider.reset();
        Expect(TryCreateProvider(exports.create, configuration, provider) == S_OK && provider,
               "Studio Clock rejected a valid date format or mixed-case color");
    }

    struct Mutation final
    {
        std::string_view before;
        std::string_view after;
    };
    constexpr std::array mutations{
        Mutation{"\"showSecondProgress\":true", "\"showSecondProgress\":1"},
        Mutation{"\"externalDotsAlwaysOn\":true", "\"externalDotsAlwaysOn\":1"},
        Mutation{"\"showSeconds\":true", "\"showSeconds\":\"true\""},
        Mutation{"\"secondsColor\":\"#FF1616\"", "\"secondsColor\":\"FF1616\""},
        Mutation{"\"showDate\":false", "\"showDate\":null"},
        Mutation{"\"dateFormat\":\"dd-mm-yyyy\"", "\"dateFormat\":\"locale\""},
        Mutation{"\"timeColor\":\"#FF1616\"", "\"timeColor\":\"#FF161G\""},
        Mutation{"\"timeColor\":\"#FF1616\"", "\"timeColor\":\"#FF1616\",\"backgroundColor\":\"#111111\""},
        Mutation{"\"showSeconds\":true", "\"showSeconds\":true,\"showSeconds\":false"},
        Mutation{"\"showDate\":false", "\"showDate\":false,\"unknown\":1"},
    };
    for (const Mutation& mutation : mutations)
    {
        std::string invalid(kDefaults);
        const size_t offset = invalid.find(mutation.before);
        Expect(offset != std::string::npos, "Studio Clock invalid mutation could not be prepared");
        invalid.replace(offset, mutation.before.size(), mutation.after);
        provider.reset();
        Expect(TryCreateProvider(exports.create, invalid, provider) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA) &&
                   !provider,
               "Studio Clock accepted malformed settings");
    }

    RedXeFactoryOptions invalidOptions{};
    invalidOptions.sizeBytes = sizeof(invalidOptions);
    char marker = '{';
    invalidOptions.configurationJsonUtf8 = &marker;
    invalidOptions.configurationBytes = kRedXeMaximumFactoryConfigurationBytes + 1;
    object = reinterpret_cast<void*>(1);
    Expect(exports.create(__uuidof(IRedXeWidgetProvider), &invalidOptions, nullptr, kPluginId, &object) ==
                   E_INVALIDARG &&
               !object,
           "Studio Clock accepted an oversized configuration");
}

void ValidateInterfacesAndScheduling(const Exports& exports)
{
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    Expect(TryCreateProvider(exports.create, kDefaults, provider) == S_OK, "Studio Clock provider creation failed");
    WidgetInterfaces created = CreateWidget(*provider, "studio.contract");

    void* unsupported = reinterpret_cast<void*>(1);
    Expect(created.widget->QueryInterface(__uuidof(IRedXeWindowWidget), &unsupported) == E_NOINTERFACE && !unsupported,
           "Studio Clock exposed the window-widget mechanism");
    wil::com_ptr_nothrow<IUnknown> widgetIdentity;
    wil::com_ptr_nothrow<IUnknown> gpuIdentity;
    wil::com_ptr_nothrow<IUnknown> scheduledIdentity;
    Expect(created.widget.query_to(widgetIdentity.put()) == S_OK && created.gpu.query_to(gpuIdentity.put()) == S_OK &&
               created.scheduled.query_to(scheduledIdentity.put()) == S_OK &&
               widgetIdentity.get() == gpuIdentity.get() && widgetIdentity.get() == scheduledIdentity.get(),
           "Studio Clock siblings do not share one controlling IUnknown");

    IRedXeWidget* invalidWidget = reinterpret_cast<IRedXeWidget*>(1);
    Expect(provider->CreateWidget("missing", "studio.invalid", &invalidWidget) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) &&
               !invalidWidget,
           "Studio Clock accepted an unknown widget type");
    Expect(provider->CreateWidget(kWidgetTypeId, "", &invalidWidget) == E_INVALIDARG && !invalidWidget,
           "Studio Clock accepted an empty instance ID");
    Expect(created.scheduled->GetNextFrameDelayMilliseconds(nullptr) == E_POINTER,
           "Studio Clock schedule query accepted a null output");

    SetTime(exports, 2024, 2, 29, 12, 34, 0, 0);
    uint32_t delay = 99;
    Expect(created.scheduled->GetNextFrameDelayMilliseconds(&delay) == S_OK && delay == 1001,
           "Studio Clock second-boundary delay is wrong at second zero");
    SetTime(exports, 2024, 12, 31, 23, 59, 59, 999);
    Expect(created.scheduled->GetNextFrameDelayMilliseconds(&delay) == S_OK && delay == 2,
           "Studio Clock second-boundary delay is wrong at year rollover");

    const std::string minuteConfiguration = Configuration(false, false, true);
    wil::com_ptr_nothrow<IRedXeWidgetProvider> minuteProvider;
    Expect(TryCreateProvider(exports.create, minuteConfiguration, minuteProvider) == S_OK,
           "Studio Clock minute provider creation failed");
    WidgetInterfaces minuteWidget = CreateWidget(*minuteProvider, "studio.minute");
    SetTime(exports, 2024, 3, 31, 1, 34, 10, 250);
    Expect(minuteWidget.scheduled->GetNextFrameDelayMilliseconds(&delay) == S_OK && delay == 49751,
           "Studio Clock minute-boundary delay is wrong");

    StudioClockTestTime invalidTime{sizeof(StudioClockTestTime), 2023, 2, 29, 12, 0, 0, 0};
    Expect(exports.setTime(&invalidTime) == E_INVALIDARG, "Studio Clock accepted an invalid Gregorian date");
    invalidTime = {sizeof(StudioClockTestTime), 2024, 1, 1, 12, 0, 60, 0};
    Expect(exports.setTime(&invalidTime) == E_INVALIDARG, "Studio Clock accepted second 60");
}

#if defined(_DEBUG)
std::atomic<uint64_t> gRenderAllocationCount{0};
std::atomic<DWORD> gRenderThreadId{0};

int __cdecl CountRenderAllocation(int allocationType, void*, size_t, int, long, const unsigned char*, int)
{
    if (GetCurrentThreadId() == gRenderThreadId.load(std::memory_order_relaxed) && allocationType != _CRT_BLOCK)
    {
        gRenderAllocationCount.fetch_add(1, std::memory_order_relaxed);
    }
    return TRUE;
}
#endif

void ValidateAllocationFreeRender(IRedXeGpuWidget& widget, RenderTarget& target)
{
#if defined(_DEBUG)
    for (uint32_t warmup = 0; warmup < 64; ++warmup)
    {
        Expect(RenderOnly(widget, target) == S_OK, "Studio Clock render warmup failed");
    }
    target.context->Flush();
    Sleep(50);
    gRenderAllocationCount = 0;
    gRenderThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const _CRT_ALLOC_HOOK previousHook = _CrtSetAllocHook(CountRenderAllocation);
    HRESULT result = S_OK;
    for (uint32_t frame = 0; frame < 120 && SUCCEEDED(result); ++frame)
    {
        result = RenderOnly(widget, target);
    }
    (void)_CrtSetAllocHook(previousHook);
    gRenderThreadId.store(0, std::memory_order_relaxed);
    Expect(SUCCEEDED(result) && gRenderAllocationCount.load(std::memory_order_relaxed) == 0,
           "Studio Clock steady render allocated CRT memory");
#else
    (void)widget;
    (void)target;
#endif
}

void ValidateRendering(const Exports& exports)
{
    RenderTarget target;
    Expect(CreateRenderTarget(720, 720, target) == S_OK, "Studio Clock WARP target creation failed");
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    Expect(TryCreateProvider(exports.create, kDefaults, provider) == S_OK, "Studio Clock provider creation failed");
    WidgetInterfaces created = CreateWidget(*provider, "studio.render");

    Expect(created.gpu->OnDeviceCreated(nullptr) == E_POINTER && created.gpu->Render(nullptr) == E_POINTER,
           "Studio Clock GPU methods accepted null contexts");
    RedXeGpuDeviceContext invalidDevice{};
    invalidDevice.sizeBytes = sizeof(invalidDevice);
    Expect(created.gpu->OnDeviceCreated(&invalidDevice) == E_POINTER, "Studio Clock accepted a null borrowed device");
    const RedXeGpuDeviceContext deviceContext = DeviceContextFor(target);
    Expect(created.gpu->OnDeviceCreated(&deviceContext) == S_OK, "Studio Clock device creation failed");

    SetTime(exports, 2024, 12, 31, 12, 34, 0, 0);
    std::vector<std::uint8_t> pixels = RenderAndReadback(*created.gpu, target);
    StudioClockTestDiagnostics diagnostics = Diagnostics(exports);
    Expect(diagnostics.lastMapCount == 1 && diagnostics.lastDrawCount == 2 && diagnostics.lastInstanceCount == 228,
           "Studio Clock default render budgets are wrong");
    Expect(PixelAt(pixels, target.width, 4, 4) == std::array<std::uint8_t, 4>{0x11, 0x11, 0x11, 0xFF},
           "Studio Clock background color is wrong");
    Expect(CountActiveRingDots(pixels, target.width, target.height) == 1,
           "Studio Clock second-zero ring state is wrong");
    Expect(CountActiveFiveSecondDots(pixels, target.width, target.height) == 12,
           "Studio Clock default outward dots are not always on");

    const std::vector<std::uint8_t> identical = RenderAndReadback(*created.gpu, target);
    diagnostics = Diagnostics(exports);
    Expect(identical == pixels && diagnostics.lastMapCount == 0,
           "Studio Clock did not reuse unchanged constants or pixels");
    Expect(RenderOnly(*created.gpu, target, 0.0f, 144) == S_OK, "Studio Clock DPI-change render failed");
    const std::vector<std::uint8_t> dpiPixels = Readback(target);
    diagnostics = Diagnostics(exports);
    Expect(dpiPixels == pixels && diagnostics.lastMapCount == 1,
           "Studio Clock DPI change did not refresh cached layout exactly once");
    Expect(RenderOnly(*created.gpu, target, 0.0f, 144) == S_OK && Diagnostics(exports).lastMapCount == 0,
           "Studio Clock repeated an unchanged DPI upload");
    ValidateAllocationFreeRender(*created.gpu, target);

    D3D11_VIEWPORT shifted{
        -160.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f,
    };
    ID3D11RenderTargetView* shiftedViews[] = {target.view.get()};
    target.context->OMSetRenderTargets(1, shiftedViews, nullptr);
    target.context->RSSetViewports(1, &shifted);
    const RedXeWidgetFrameContext shiftedFrame{
        sizeof(RedXeWidgetFrameContext), target.width, target.height, USER_DEFAULT_SCREEN_DPI, 0.0f, 0.0f,
    };
    const RedXeGpuFrameContext shiftedGpu{
        sizeof(RedXeGpuFrameContext),
        &shiftedFrame,
        target.context.get(),
        shifted,
    };
    Expect(created.gpu->Render(&shiftedGpu) == S_OK, "Studio Clock rejected a negative viewport origin");

    constexpr std::array seconds{0U, 1U, 30U, 59U};
    for (const uint32_t second : seconds)
    {
        SetTime(exports, 2024, 12, 31, 12, 34, static_cast<uint16_t>(second), 0);
        pixels = RenderAndReadback(*created.gpu, target);
        Expect(CountActiveRingDots(pixels, target.width, target.height) == second + 1,
               "Studio Clock active ring count is wrong");
        Expect(CountActiveFiveSecondDots(pixels, target.width, target.height) == 12,
               "Studio Clock default outward dots changed with progress");
    }

    const std::string linkedExternalDots = Configuration(true, true, false, false);
    wil::com_ptr_nothrow<IRedXeWidgetProvider> linkedProvider;
    Expect(TryCreateProvider(exports.create, linkedExternalDots, linkedProvider) == S_OK,
           "Studio Clock linked outward-dot provider failed");
    WidgetInterfaces linkedWidget = CreateWidget(*linkedProvider, "studio.linked-dots");
    Expect(linkedWidget.gpu->OnDeviceCreated(&deviceContext) == S_OK, "Studio Clock linked outward-dot device failed");
    for (const uint32_t second : seconds)
    {
        SetTime(exports, 2024, 12, 31, 12, 34, static_cast<uint16_t>(second), 0);
        const std::vector<std::uint8_t> linkedPixels = RenderAndReadback(*linkedWidget.gpu, target);
        Expect(CountActiveRingDots(linkedPixels, target.width, target.height) == second + 1,
               "Studio Clock linked mode changed ordinary progress");
        Expect(CountActiveFiveSecondDots(linkedPixels, target.width, target.height) == second / 5U + 1U,
               "Studio Clock disabled always-on option did not restore linked outward progress");
    }
    linkedWidget.gpu->OnDeviceLost();

    created.gpu->OnDeviceLost();
    created.gpu->OnDeviceLost();
    Expect(Diagnostics(exports).liveSharedDeviceResourceSetCount == 0,
           "Studio Clock device resources survived device loss");
    RenderTarget recreated;
    Expect(CreateRenderTarget(720, 720, recreated) == S_OK, "Studio Clock recreated WARP target failed");
    const RedXeGpuDeviceContext recreatedContext = DeviceContextFor(recreated);
    Expect(created.gpu->OnDeviceCreated(&recreatedContext) == S_OK, "Studio Clock device recreation failed");
    const std::vector<std::uint8_t> recreatedPixels = RenderAndReadback(*created.gpu, recreated);
    Expect(recreatedPixels == pixels, "Studio Clock pixels changed across device recreation");
    created.gpu->OnDeviceLost();

    const std::string colorful = Configuration(true, true, true, true, "dd-mm-yyyy", "#11EE44", "#2244FF");
    wil::com_ptr_nothrow<IRedXeWidgetProvider> colorfulProvider;
    Expect(TryCreateProvider(exports.create, colorful, colorfulProvider, 0xFF050607) == S_OK,
           "Studio Clock colorful provider failed");
    WidgetInterfaces colorfulWidget = CreateWidget(*colorfulProvider, "studio.color");
    Expect(colorfulWidget.gpu->OnDeviceCreated(&deviceContext) == S_OK, "Studio Clock colorful device failed");
    SetTime(exports, 2024, 12, 31, 20, 59, 46, 0);
    pixels = RenderAndReadback(*colorfulWidget.gpu, target);
    diagnostics = Diagnostics(exports);
    Expect(diagnostics.lastInstanceCount == 402 && diagnostics.lastDrawCount == 2 && diagnostics.lastMapCount == 1,
           "Studio Clock maximum-content render budgets are wrong");
    Expect(PixelAt(pixels, target.width, 4, 4) == std::array<std::uint8_t, 4>{0x05, 0x06, 0x07, 0xFF},
           "Studio Clock host-supplied background color is wrong");
    const ClockLayout colorfulLayout = LayoutFor(target.width, target.height, true);
    const float primaryDotX = colorfulLayout.originX + 0.215f * colorfulLayout.square;
    const float primaryDotY = colorfulLayout.originY + 0.4116f * colorfulLayout.square;
    Expect(MaximumChannelNear(pixels, target.width, target.height, primaryDotX, primaryDotY, 2) > 180,
           "Studio Clock primary time did not use timeColor");
    Expect(PixelAt(pixels, target.width,
                   static_cast<uint32_t>(std::lround(primaryDotX + 0.0055f * colorfulLayout.square)),
                   static_cast<uint32_t>(std::lround(primaryDotY)))[2] > 180 &&
               PixelAt(pixels, target.width,
                       static_cast<uint32_t>(std::lround(primaryDotX + 0.0105f * colorfulLayout.square)),
                       static_cast<uint32_t>(std::lround(primaryDotY)))[2] < 100,
           "Studio Clock primary dot radius does not match the overlay target");
    Expect(MaximumChannelNear(pixels, target.width, target.height,
                              colorfulLayout.originX + 0.4217f * colorfulLayout.square,
                              colorfulLayout.originY + 0.6603f * colorfulLayout.square, 1) > 180,
           "Studio Clock numeric seconds did not use secondsColor");
    Expect(MaximumChannelNear(pixels, target.width, target.height,
                              colorfulLayout.originX + 0.504f * colorfulLayout.square,
                              colorfulLayout.originY + 0.4703f * colorfulLayout.square, 2) > 180 &&
               MaximumChannelNear(pixels, target.width, target.height,
                                  colorfulLayout.originX + 0.494f * colorfulLayout.square,
                                  colorfulLayout.originY + 0.5313f * colorfulLayout.square, 2) > 180,
           "Studio Clock oblique colon geometry or color is wrong");
    Expect(MaximumChannelNear(pixels, target.width, target.height,
                              colorfulLayout.originX + 0.5f * colorfulLayout.square,
                              colorfulLayout.originY + 0.082f * colorfulLayout.square, 1) > 180,
           "Studio Clock active progress did not use secondsColor");
    Expect(MaximumChannelNear(pixels, target.width, target.height,
                              colorfulLayout.originX + 0.5f * colorfulLayout.square,
                              colorfulLayout.originY + 0.053f * colorfulLayout.square, 1) > 180,
           "Studio Clock active five-second emphasis did not use secondsColor");
    const float colorfulClockBottom = colorfulLayout.originY + colorfulLayout.square;
    const float colorfulDateY = colorfulLayout.originY + 1.062f * colorfulLayout.square;
    Expect(colorfulDateY > colorfulClockBottom &&
               MaximumChannelNear(pixels, target.width, target.height,
                                  colorfulLayout.originX + 0.38125f * colorfulLayout.square, colorfulDateY, 2) > 180 &&
               MaximumChannelNear(pixels, target.width, target.height,
                                  colorfulLayout.originX + 0.51625f * colorfulLayout.square, colorfulDateY, 2) > 180 &&
               PixelAt(pixels, target.width,
                       static_cast<uint32_t>(std::lround(colorfulLayout.originX + 0.40025f * colorfulLayout.square)),
                       static_cast<uint32_t>(std::lround(colorfulDateY)))[2] < 100 &&
               PixelAt(pixels, target.width,
                       static_cast<uint32_t>(std::lround(colorfulLayout.originX + 0.53525f * colorfulLayout.square)),
                       static_cast<uint32_t>(std::lround(colorfulDateY)))[2] < 100,
           "Studio Clock date was not rendered below the square clock");
    colorfulWidget.gpu->OnDeviceLost();

    std::vector<std::uint8_t> priorDate;
    constexpr std::array dateFormats{"dd-mm-yyyy", "mm-dd-yyyy", "yyyy-mm-dd"};
    for (const char* format : dateFormats)
    {
        const std::string configuration = Configuration(false, false, true, true, format);
        wil::com_ptr_nothrow<IRedXeWidgetProvider> dateProvider;
        Expect(TryCreateProvider(exports.create, configuration, dateProvider) == S_OK,
               "Studio Clock date provider failed");
        WidgetInterfaces dateWidget = CreateWidget(*dateProvider, format);
        Expect(dateWidget.gpu->OnDeviceCreated(&deviceContext) == S_OK, "Studio Clock date device failed");
        SetTime(exports, 2024, 12, 31, 20, 59, 46, 0);
        const std::vector<std::uint8_t> datePixels = RenderAndReadback(*dateWidget.gpu, target);
        const ClockLayout dateLayout = LayoutFor(target.width, target.height, true);
        const float separatorY = dateLayout.originY + 1.062f * dateLayout.square;
        Expect(separatorY > dateLayout.originY + dateLayout.square &&
                   MaximumChannelNear(datePixels, target.width, target.height,
                                      dateLayout.originX + 0.38125f * dateLayout.square, separatorY, 0) > 180 &&
                   MaximumChannelNear(datePixels, target.width, target.height,
                                      dateLayout.originX + 0.51625f * dateLayout.square, separatorY, 0) > 180,
               "Studio Clock date separator is not below the square clock");
        if (!priorDate.empty())
        {
            Expect(datePixels != priorDate, "Studio Clock date formats produced identical output");
        }
        priorDate = datePixels;
        dateWidget.gpu->OnDeviceLost();
    }

    for (const std::pair dimensions : {std::pair{900U, 500U}, std::pair{500U, 900U}, std::pair{160U, 160U}})
    {
        RenderTarget shaped;
        Expect(CreateRenderTarget(dimensions.first, dimensions.second, shaped) == S_OK,
               "Studio Clock shaped target creation failed");
        wil::com_ptr_nothrow<IRedXeWidgetProvider> shapedProvider;
        Expect(TryCreateProvider(exports.create, kDefaults, shapedProvider) == S_OK,
               "Studio Clock shaped provider failed");
        WidgetInterfaces shapedWidget = CreateWidget(*shapedProvider, "studio.shape");
        const RedXeGpuDeviceContext shapedContext = DeviceContextFor(shaped);
        Expect(shapedWidget.gpu->OnDeviceCreated(&shapedContext) == S_OK, "Studio Clock shaped device failed");
        SetTime(exports, 2024, 1, 1, 0, 0, 0, 0);
        const std::vector<std::uint8_t> shapedPixels = RenderAndReadback(*shapedWidget.gpu, shaped);
        Expect(CountActiveRingDots(shapedPixels, shaped.width, shaped.height) == 1,
               "Studio Clock square-fit layout is wrong");
        Expect(CountActiveFiveSecondDots(shapedPixels, shaped.width, shaped.height) == 12,
               "Studio Clock square-fit five-second markers are wrong");
        shapedWidget.gpu->OnDeviceLost();
    }

    for (const std::pair dimensions : {std::pair{720U, 800U}, std::pair{720U, 720U}, std::pair{900U, 500U},
                                       std::pair{500U, 900U}, std::pair{160U, 178U}})
    {
        RenderTarget shaped;
        Expect(CreateRenderTarget(dimensions.first, dimensions.second, shaped) == S_OK,
               "Studio Clock dated target creation failed");
        const std::string datedConfiguration = Configuration(true, false, true);
        wil::com_ptr_nothrow<IRedXeWidgetProvider> shapedProvider;
        Expect(TryCreateProvider(exports.create, datedConfiguration, shapedProvider) == S_OK,
               "Studio Clock dated shaped provider failed");
        WidgetInterfaces shapedWidget = CreateWidget(*shapedProvider, "studio.dated-shape");
        const RedXeGpuDeviceContext shapedContext = DeviceContextFor(shaped);
        Expect(shapedWidget.gpu->OnDeviceCreated(&shapedContext) == S_OK, "Studio Clock dated shaped device failed");
        SetTime(exports, 2024, 12, 31, 20, 59, 0, 0);
        const std::vector<std::uint8_t> shapedPixels = RenderAndReadback(*shapedWidget.gpu, shaped);
        const ClockLayout layout = LayoutFor(shaped.width, shaped.height, true);
        const float separatorY = layout.originY + 1.062f * layout.square;
        const std::uint8_t dateThreshold = layout.square < 200.0f ? 24U : 100U;
        const std::uint8_t dateMaximum = MaximumChannelNear(shapedPixels, shaped.width, shaped.height,
                                                            layout.originX + 0.38125f * layout.square, separatorY, 0);
        if (dateMaximum <= dateThreshold)
        {
            std::wcerr << L"dated shape diagnostic: " << shaped.width << L'x' << shaped.height << L", square="
                       << layout.square << L", dateMaximum=" << static_cast<unsigned>(dateMaximum) << L'\n';
        }
        Expect(CountActiveRingDots(shapedPixels, shaped.width, shaped.height, true) == 1 &&
                   CountActiveFiveSecondDots(shapedPixels, shaped.width, shaped.height, true) == 12,
               "Studio Clock dated square ring layout is wrong");
        Expect(separatorY > layout.originY + layout.square && separatorY < static_cast<float>(shaped.height) &&
                   dateMaximum > dateThreshold,
               "Studio Clock dated shaped layout did not keep the date below the square");
        shapedWidget.gpu->OnDeviceLost();
    }
}

void ValidateSharedResourcesAndToggles(const Exports& exports)
{
    RenderTarget target;
    Expect(CreateRenderTarget(320, 320, target) == S_OK, "Studio Clock resource target failed");
    const RedXeGpuDeviceContext deviceContext = DeviceContextFor(target);
    for (uint32_t bits = 0; bits < 16; ++bits)
    {
        const bool progress = (bits & 1U) != 0;
        const bool seconds = (bits & 2U) != 0;
        const bool date = (bits & 4U) != 0;
        const bool externalDotsAlwaysOn = (bits & 8U) != 0;
        const std::string configuration = Configuration(progress, seconds, date, externalDotsAlwaysOn);
        wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
        Expect(TryCreateProvider(exports.create, configuration, provider) == S_OK,
               "Studio Clock toggle provider failed");
        WidgetInterfaces widget = CreateWidget(*provider, "studio.toggle");
        Expect(widget.gpu->OnDeviceCreated(&deviceContext) == S_OK, "Studio Clock toggle device failed");
        SetTime(exports, 2024, 2, 29, 8, 7, 6, 0);
        Expect(RenderOnly(*widget.gpu, target) == S_OK, "Studio Clock toggle render failed");
        const uint32_t expectedInstances = 114U + (seconds ? 42U : 0U) + (date ? 174U : 0U) + (progress ? 72U : 0U);
        Expect(Diagnostics(exports).lastInstanceCount == expectedInstances,
               "Studio Clock toggle instance bound is wrong");
        widget.gpu->OnDeviceLost();
    }

    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    Expect(TryCreateProvider(exports.create, kDefaults, provider) == S_OK,
           "Studio Clock shared-resource provider failed");
    WidgetInterfaces first = CreateWidget(*provider, "studio.shared.1");
    WidgetInterfaces second = CreateWidget(*provider, "studio.shared.2");
    Expect(first.gpu->OnDeviceCreated(&deviceContext) == S_OK && second.gpu->OnDeviceCreated(&deviceContext) == S_OK,
           "Studio Clock shared-resource devices failed");
    StudioClockTestDiagnostics diagnostics = Diagnostics(exports);
    Expect(diagnostics.liveSharedDeviceResourceSetCount == 1 && diagnostics.liveConstantBufferCount == 2,
           "Studio Clock compatible instances did not share immutable resources");
    first.gpu->OnDeviceLost();
    diagnostics = Diagnostics(exports);
    Expect(diagnostics.liveSharedDeviceResourceSetCount == 1 && diagnostics.liveConstantBufferCount == 1,
           "Studio Clock released shared resources too early");
    second.gpu->OnDeviceLost();
    diagnostics = Diagnostics(exports);
    Expect(diagnostics.liveSharedDeviceResourceSetCount == 0 && diagnostics.liveConstantBufferCount == 0,
           "Studio Clock leaked shared resources");
}

[[nodiscard]] PROCESS_MEMORY_COUNTERS_EX ProcessMemory()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    Expect(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                sizeof(counters)) != FALSE,
           "process memory counters are unavailable");
    return counters;
}

[[nodiscard]] double MeasureGpuMillisecondsPerFrame(IRedXeGpuWidget& widget, RenderTarget& target, uint32_t frameCount)
{
    D3D11_QUERY_DESC description{};
    description.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    wil::com_ptr_nothrow<ID3D11Query> disjoint;
    Expect(target.device->CreateQuery(&description, disjoint.put()) == S_OK,
           "Studio Clock GPU disjoint query creation failed");
    description.Query = D3D11_QUERY_TIMESTAMP;
    wil::com_ptr_nothrow<ID3D11Query> start;
    wil::com_ptr_nothrow<ID3D11Query> end;
    Expect(target.device->CreateQuery(&description, start.put()) == S_OK &&
               target.device->CreateQuery(&description, end.put()) == S_OK,
           "Studio Clock GPU timestamp query creation failed");

    target.context->Begin(disjoint.get());
    target.context->End(start.get());
    for (uint32_t frame = 0; frame < frameCount; ++frame)
    {
        Expect(RenderOnly(widget, target) == S_OK, "Studio Clock GPU benchmark render failed");
    }
    target.context->End(end.get());
    target.context->End(disjoint.get());
    target.context->Flush();

    auto waitForData = [&target](ID3D11Query* query, void* data, uint32_t bytes)
    {
        const ULONGLONG deadline = GetTickCount64() + 5000;
        HRESULT result = S_FALSE;
        while (result == S_FALSE && GetTickCount64() < deadline)
        {
            result = target.context->GetData(query, data, bytes, 0);
            if (result == S_FALSE)
            {
                Sleep(1);
            }
        }
        Expect(result == S_OK, "Studio Clock GPU timestamp query timed out");
    };

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
    uint64_t startTimestamp = 0;
    uint64_t endTimestamp = 0;
    waitForData(disjoint.get(), &disjointData, sizeof(disjointData));
    waitForData(start.get(), &startTimestamp, sizeof(startTimestamp));
    waitForData(end.get(), &endTimestamp, sizeof(endTimestamp));
    Expect(!disjointData.Disjoint && disjointData.Frequency != 0 && endTimestamp >= startTimestamp,
           "Studio Clock GPU timestamp interval is invalid");
    return static_cast<double>(endTimestamp - startTimestamp) * 1000.0 / static_cast<double>(disjointData.Frequency) /
           frameCount;
}

void RunBenchmark(const Exports& exports)
{
    RenderTarget target;
    Expect(CreateRenderTarget(2560, 720, target) == S_OK, "Studio Clock benchmark target failed");
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    Expect(TryCreateProvider(exports.create, kDefaults, provider) == S_OK, "Studio Clock benchmark provider failed");
    WidgetInterfaces widget = CreateWidget(*provider, "studio.benchmark");
    const RedXeGpuDeviceContext deviceContext = DeviceContextFor(target);
    Expect(widget.gpu->OnDeviceCreated(&deviceContext) == S_OK, "Studio Clock benchmark device failed");
    SetTime(exports, 2024, 12, 31, 20, 59, 46, 0);
    for (uint32_t warmup = 0; warmup < 256; ++warmup)
    {
        Expect(RenderOnly(*widget.gpu, target) == S_OK, "Studio Clock benchmark warmup failed");
        target.context->Flush();
    }

    LARGE_INTEGER frequency{};
    LARGE_INTEGER baselineStart{};
    LARGE_INTEGER baselineEnd{};
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    QueryPerformanceFrequency(&frequency);
    constexpr uint32_t frameCount = 256;
    QueryPerformanceCounter(&baselineStart);
    for (uint32_t frame = 0; frame < frameCount; ++frame)
    {
        ID3D11RenderTargetView* views[] = {target.view.get()};
        target.context->OMSetRenderTargets(1, views, nullptr);
        target.context->Flush();
    }
    QueryPerformanceCounter(&baselineEnd);
    const PROCESS_MEMORY_COUNTERS_EX beforeMemory = ProcessMemory();
    QueryPerformanceCounter(&start);
    for (uint32_t frame = 0; frame < frameCount; ++frame)
    {
        Expect(RenderOnly(*widget.gpu, target) == S_OK, "Studio Clock benchmark render failed");
        target.context->Flush();
    }
    QueryPerformanceCounter(&end);
    const PROCESS_MEMORY_COUNTERS_EX afterMemory = ProcessMemory();
    const double cpuMicroseconds = static_cast<double>(end.QuadPart - start.QuadPart) * 1'000'000.0 /
                                   static_cast<double>(frequency.QuadPart) / frameCount;
    const double baselineMicroseconds = static_cast<double>(baselineEnd.QuadPart - baselineStart.QuadPart) *
                                        1'000'000.0 / static_cast<double>(frequency.QuadPart) / frameCount;
    const double gpuMilliseconds = MeasureGpuMillisecondsPerFrame(*widget.gpu, target, 256);
    const std::int64_t privateDelta =
        static_cast<std::int64_t>(afterMemory.PrivateUsage) - static_cast<std::int64_t>(beforeMemory.PrivateUsage);
    const std::int64_t workingSetDelta =
        static_cast<std::int64_t>(afterMemory.WorkingSetSize) - static_cast<std::int64_t>(beforeMemory.WorkingSetSize);
    const StudioClockTestDiagnostics diagnostics = Diagnostics(exports);
    std::wcout << L"StudioClock benchmark 2560x720: baseline_cpu_us_per_frame=" << baselineMicroseconds
               << L", clock_cpu_us_per_frame=" << cpuMicroseconds << L", clock_cpu_delta_us_per_frame="
               << (cpuMicroseconds - baselineMicroseconds) << L", gpu_ms_per_frame=" << gpuMilliseconds
               << L", private_delta_bytes=" << privateDelta << L", working_set_delta_bytes=" << workingSetDelta
               << L", maps_last_frame=" << diagnostics.lastMapCount << L", draws=" << diagnostics.lastDrawCount
               << L", instances=" << diagnostics.lastInstanceCount << L'\n';
    Expect(diagnostics.lastMapCount == 0 && diagnostics.lastDrawCount == 2 && diagnostics.lastInstanceCount <= 402,
           "Studio Clock benchmark exceeded a structural budget");
    widget.gpu->OnDeviceLost();
}

void RunSoak(const Exports& exports, uint32_t seconds)
{
    RenderTarget target;
    Expect(CreateRenderTarget(2560, 720, target) == S_OK, "Studio Clock soak target failed");
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    Expect(TryCreateProvider(exports.create, kDefaults, provider) == S_OK, "Studio Clock soak provider failed");
    WidgetInterfaces widget = CreateWidget(*provider, "studio.soak");
    const RedXeGpuDeviceContext deviceContext = DeviceContextFor(target);
    Expect(widget.gpu->OnDeviceCreated(&deviceContext) == S_OK, "Studio Clock soak device failed");
    Expect(exports.setTime(nullptr) == S_OK, "Studio Clock soak could not restore local time");
    Expect(RenderOnly(*widget.gpu, target) == S_OK, "Studio Clock soak first render failed");
    const PROCESS_MEMORY_COUNTERS_EX beforeMemory = ProcessMemory();
    const ULONGLONG finish = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000ULL;
    uint32_t frames = 1;
    while (GetTickCount64() < finish)
    {
        uint32_t delay = 0;
        Expect(widget.scheduled->GetNextFrameDelayMilliseconds(&delay) == S_OK && delay >= 2 && delay <= 1001,
               "Studio Clock soak schedule failed");
        Sleep(delay);
        Expect(RenderOnly(*widget.gpu, target) == S_OK, "Studio Clock soak render failed");
        ++frames;
    }
    target.context->Flush();
    const PROCESS_MEMORY_COUNTERS_EX afterMemory = ProcessMemory();
    const StudioClockTestDiagnostics beforeHidden = Diagnostics(exports);
    Sleep(std::min(5000U, std::max(1000U, seconds * 10U)));
    const StudioClockTestDiagnostics afterHidden = Diagnostics(exports);
    Expect(beforeHidden.timeSampleCount == afterHidden.timeSampleCount,
           "Studio Clock performed hidden plugin-owned clock work");
    Expect(beforeHidden.liveProviderCount == afterHidden.liveProviderCount &&
               beforeHidden.liveWidgetCount == afterHidden.liveWidgetCount &&
               beforeHidden.liveSharedDeviceResourceSetCount == afterHidden.liveSharedDeviceResourceSetCount,
           "Studio Clock resource counts changed during hidden soak");
    const std::int64_t privateDelta =
        static_cast<std::int64_t>(afterMemory.PrivateUsage) - static_cast<std::int64_t>(beforeMemory.PrivateUsage);
    std::wcout << L"StudioClock soak: seconds=" << seconds << L", frames=" << frames << L", private_delta_bytes="
               << privateDelta << L'\n';
    widget.gpu->OnDeviceLost();
}

void WriteSnapshot(const Exports& exports, const std::filesystem::path& path, bool showDate)
{
    RenderTarget target;
    Expect(CreateRenderTarget(720, showDate ? 800U : 720U, target) == S_OK, "Studio Clock snapshot target failed");
    const std::string configuration = Configuration(true, true, showDate);
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    Expect(TryCreateProvider(exports.create, configuration, provider) == S_OK, "Studio Clock snapshot provider failed");
    WidgetInterfaces widget = CreateWidget(*provider, "studio.snapshot");
    const RedXeGpuDeviceContext deviceContext = DeviceContextFor(target);
    Expect(widget.gpu->OnDeviceCreated(&deviceContext) == S_OK, "Studio Clock snapshot device failed");
    SetTime(exports, 2024, 8, 31, 20, 59, 46, 0);
    const std::vector<std::uint8_t> source = RenderAndReadback(*widget.gpu, target);

    std::vector<std::uint8_t> bitmap(source.size());
    const size_t rowBytes = static_cast<size_t>(target.width) * 4U;
    for (uint32_t row = 0; row < target.height; ++row)
    {
        const std::uint8_t* sourceRow = source.data() + static_cast<size_t>(target.height - row - 1U) * rowBytes;
        std::uint8_t* destinationRow = bitmap.data() + static_cast<size_t>(row) * rowBytes;
        for (uint32_t column = 0; column < target.width; ++column)
        {
            destinationRow[column * 4U] = sourceRow[column * 4U + 2U];
            destinationRow[column * 4U + 1U] = sourceRow[column * 4U + 1U];
            destinationRow[column * 4U + 2U] = sourceRow[column * 4U];
            destinationRow[column * 4U + 3U] = sourceRow[column * 4U + 3U];
        }
    }

    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER));
    fileHeader.bfSize = fileHeader.bfOffBits + static_cast<DWORD>(bitmap.size());
    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = static_cast<LONG>(target.width);
    infoHeader.biHeight = static_cast<LONG>(target.height);
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = static_cast<DWORD>(bitmap.size());

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Expect(output.good(), "Studio Clock snapshot file could not be opened");
    output.write(reinterpret_cast<const char*>(&fileHeader), static_cast<std::streamsize>(sizeof(fileHeader)));
    output.write(reinterpret_cast<const char*>(&infoHeader), static_cast<std::streamsize>(sizeof(infoHeader)));
    output.write(reinterpret_cast<const char*>(bitmap.data()), static_cast<std::streamsize>(bitmap.size()));
    Expect(output.good(), "Studio Clock snapshot file write failed");
    widget.gpu->OnDeviceLost();
}

void Run(uint32_t soakSeconds, bool benchmark, const std::filesystem::path& snapshotPath,
         const std::filesystem::path& noDateSnapshotPath)
{
    const std::filesystem::path pluginPath = PluginPath();
    const bool compilerWasLoaded = GetModuleHandleW(L"d3dcompiler_47.dll") != nullptr;
    wil::unique_hmodule module{
        LoadLibraryExW(pluginPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    Expect(static_cast<bool>(module), "StudioClock.dll could not be loaded");
    const Exports exports{
        Resolve<RedXeCreateFn>(module.get(), kRedXeCreateExport),
        Resolve<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport),
        Resolve<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport),
        Resolve<RedXePluginShutdownFn>(module.get(), kRedXePluginShutdownExport),
        Resolve<StudioClockSetTestTimeFn>(module.get(), kStudioClockSetTestTimeExport),
        Resolve<StudioClockGetTestDiagnosticsFn>(module.get(), kStudioClockGetTestDiagnosticsExport),
    };

    Expect(exports.getDiagnostics(nullptr) == E_POINTER, "Studio Clock diagnostics accepted a null output");
    StudioClockTestDiagnostics undersized{};
    undersized.sizeBytes = sizeof(undersized.sizeBytes);
    Expect(exports.getDiagnostics(&undersized) == E_INVALIDARG,
           "Studio Clock diagnostics accepted an undersized record");
    ValidateFactoryAndSettings(exports);
    ValidateInterfacesAndScheduling(exports);
    ValidateRendering(exports);
    ValidateSharedResourcesAndToggles(exports);
    if (benchmark)
    {
        RunBenchmark(exports);
    }
    if (soakSeconds != 0)
    {
        RunSoak(exports, soakSeconds);
    }
    if (!snapshotPath.empty())
    {
        WriteSnapshot(exports, snapshotPath, true);
    }
    if (!noDateSnapshotPath.empty())
    {
        WriteSnapshot(exports, noDateSnapshotPath, false);
    }
    Expect(exports.setTime(nullptr) == S_OK, "Studio Clock test time could not be cleared");
    exports.shutdown();
    const StudioClockTestDiagnostics finalDiagnostics = Diagnostics(exports);
    Expect(finalDiagnostics.liveProviderCount == 0 && finalDiagnostics.liveWidgetCount == 0 &&
               finalDiagnostics.liveSharedDeviceResourceSetCount == 0 && finalDiagnostics.liveConstantBufferCount == 0,
           "Studio Clock retained live resources at shutdown");
    Expect(compilerWasLoaded || GetModuleHandleW(L"d3dcompiler_47.dll") == nullptr,
           "Studio Clock loaded the runtime shader compiler");
}
} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
    try
    {
        bool benchmark = false;
        uint32_t soakSeconds = 0;
        std::filesystem::path snapshotPath;
        std::filesystem::path noDateSnapshotPath;
        for (int index = 1; index < argumentCount; ++index)
        {
            const std::wstring_view argument(arguments[index]);
            if (argument == L"--benchmark")
            {
                benchmark = true;
            }
            else if (argument == L"--soak-seconds" && index + 1 < argumentCount)
            {
                const unsigned long parsed = std::wcstoul(arguments[++index], nullptr, 10);
                Expect(parsed > 0 && parsed <= 3600, "invalid Studio Clock soak duration");
                soakSeconds = static_cast<uint32_t>(parsed);
            }
            else if (argument == L"--snapshot" && index + 1 < argumentCount)
            {
                snapshotPath = arguments[++index];
            }
            else if (argument == L"--snapshot-no-date" && index + 1 < argumentCount)
            {
                noDateSnapshotPath = arguments[++index];
            }
            else
            {
                throw std::runtime_error("unknown Studio Clock test argument");
            }
        }
        Run(soakSeconds, benchmark, snapshotPath, noDateSnapshotPath);
        std::wcout << L"Studio Clock tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Studio Clock tests failed: " << error.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "Studio Clock tests failed with an unknown error.\n";
        return 1;
    }
}
