#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/GpuWidget.h"
#include "PlugInterfaces/Widget.h"
#include "PlugInterfaces/WindowWidget.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <new>
#include <psapi.h>
#include <string>
#include <string_view>
#include <strsafe.h>
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
static_assert(std::is_base_of_v<IUnknown, IRedXeHost>);
static_assert(std::is_base_of_v<IUnknown, IRedXeWidget>);
static_assert(std::is_base_of_v<IUnknown, IRedXeWidgetProvider>);
static_assert(std::is_base_of_v<IUnknown, IRedXeGpuWidget>);
static_assert(std::is_base_of_v<IUnknown, IRedXeWindowWidget>);
static_assert(!std::is_base_of_v<IRedXeWidget, IRedXeGpuWidget>);
static_assert(!std::is_base_of_v<IRedXeWidget, IRedXeWindowWidget>);

constexpr char kPluginId[] = "builtin.rotating-triangle";
constexpr char kWidgetTypeId[] = "rotating-triangle";
constexpr char kGdiPluginId[] = "builtin.gdi-orbit";
constexpr char kGdiWidgetTypeId[] = "gdi-orbit";
constexpr char kMatrixPluginId[] = "builtin.matrix-rain";
constexpr char kMatrixWidgetTypeId[] = "matrix-rain";
constexpr std::string_view kDefaultMatrixConfiguration =
    R"json({"seed":1999,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35})json";
constexpr std::string_view kNormalizedMatrixConfiguration =
    R"json({"plugin":{},"instance":{"seed":1999,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35}})json";

#if defined(_DEBUG)
std::atomic<std::uint64_t> gMatrixRenderAllocationCount{0};
std::atomic<DWORD> gMatrixRenderThreadId{0};

int __cdecl CountMatrixRenderAllocation(int allocationType, void*, std::size_t, int, long, const unsigned char*,
                                        int) noexcept
{
    if (allocationType == _HOOK_ALLOC && gMatrixRenderThreadId.load(std::memory_order_relaxed) == GetCurrentThreadId())
    {
        ++gMatrixRenderAllocationCount;
    }
    return TRUE;
}
#endif

template <typename Function> [[nodiscard]] Function ResolveFunction(HMODULE module, const char* name) noexcept
{
    const FARPROC procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

[[nodiscard]] HRESULT ValidateEmptyNormalizedFactoryConfiguration(RedXeCreateFn create, const char* pluginId) noexcept
{
    if (!create || !pluginId)
    {
        return E_INVALIDARG;
    }
    constexpr std::string_view normalized = R"json({"plugin":{},"instance":{}})json";
    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
    options.configurationJsonUtf8 = normalized.data();
    options.configurationBytes = static_cast<std::uint32_t>(normalized.size());
    void* object = nullptr;
    HRESULT result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, pluginId, &object);
    if (FAILED(result) || !object)
    {
        return FAILED(result) ? result : E_UNEXPECTED;
    }
    static_cast<IRedXeWidgetProvider*>(object)->Release();

    constexpr std::string_view invalid = R"json({"plugin":{},"instance":{"unexpected":1}})json";
    options.configurationJsonUtf8 = invalid.data();
    options.configurationBytes = static_cast<std::uint32_t>(invalid.size());
    object = reinterpret_cast<void*>(1);
    result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, pluginId, &object);
    if (result != HRESULT_FROM_WIN32(ERROR_INVALID_DATA) || object)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    options.configurationJsonUtf8 = nullptr;
    options.configurationBytes = 1;
    object = reinterpret_cast<void*>(1);
    result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, pluginId, &object);
    return result == E_INVALIDARG && !object ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

[[nodiscard]] HRESULT ValidateSettingsContract(HMODULE module, const char* pluginId, bool expectEmpty) noexcept
{
    const RedXeGetPluginSettingsContractFn getContract =
        ResolveFunction<RedXeGetPluginSettingsContractFn>(module, kRedXeGetPluginSettingsContractExport);
    if (!getContract)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    const RedXePluginSettingsContract* contract = reinterpret_cast<const RedXePluginSettingsContract*>(1);
    if (getContract(pluginId, nullptr) != E_POINTER ||
        getContract("missing.plugin", &contract) != HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || contract)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    HRESULT result = getContract(pluginId, &contract);
    if (FAILED(result) || !contract || contract->sizeBytes < sizeof(*contract) || contract->versionMajor != 1 ||
        contract->versionMinor != 0 || !contract->schemaJsonUtf8 || contract->schemaBytes == 0 ||
        !contract->defaultsJsonUtf8 || contract->defaultsBytes == 0)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const std::string_view schema(contract->schemaJsonUtf8, contract->schemaBytes);
    const std::string_view defaults(contract->defaultsJsonUtf8, contract->defaultsBytes);
    if (!schema.starts_with('{') || !schema.ends_with('}') || !defaults.starts_with('{') || !defaults.ends_with('}') ||
        (expectEmpty ? defaults != "{}" : defaults == "{}"))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

[[nodiscard]] HRESULT BuildPluginPath(const wchar_t* moduleName, std::array<wchar_t, 1024>& path) noexcept
{
    if (!moduleName)
    {
        return E_INVALIDARG;
    }
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (length >= path.size() - 1)
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    wchar_t* separator = std::wcsrchr(path.data(), L'\\');
    if (!separator)
    {
        return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
    }
    const std::size_t prefixLength = static_cast<std::size_t>(separator - path.data()) + 1;
    HRESULT result = StringCchCopyW(separator + 1, path.size() - prefixLength, L"Plugins\\");
    if (FAILED(result))
    {
        return result;
    }
    return StringCchCatW(separator + 1, path.size() - prefixLength, moduleName);
}

[[nodiscard]] HRESULT RunContractTests() noexcept
{
    std::array<wchar_t, 1024> path{};
    HRESULT result = BuildPluginPath(L"RotatingTriangle.dll", path);
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
    if (GetModuleHandleW(L"d3dcompiler_47.dll"))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    const RedXeCreateFn create = ResolveFunction<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const RedXeEnumeratePluginsFn enumerate =
        ResolveFunction<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    if (!create || !enumerate)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    result = ValidateSettingsContract(module.get(), kPluginId, true);
    if (FAILED(result))
    {
        return result;
    }
    result = ValidateEmptyNormalizedFactoryConfiguration(create, kPluginId);
    if (FAILED(result))
    {
        return result;
    }

    RedXeFactoryOptions options{};
    options.sizeBytes = kRedXeFactoryOptionsV1Size;
    if (create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kPluginId, nullptr) != E_POINTER)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    void* object = reinterpret_cast<void*>(1);
    result = create(__uuidof(IRedXeHost), &options, nullptr, kPluginId, &object);
    if (result != E_NOINTERFACE || object)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    object = reinterpret_cast<void*>(1);
    result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, "missing.plugin", &object);
    if (result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || object)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    RedXeFactoryOptions undersized{};
    undersized.sizeBytes = sizeof(undersized.sizeBytes);
    object = reinterpret_cast<void*>(1);
    result = create(__uuidof(IRedXeWidgetProvider), &undersized, nullptr, kPluginId, &object);
    if (result != E_INVALIDARG || object)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    RedXeFactoryOptions oversized{};
    oversized.sizeBytes = sizeof(oversized) + 32;
    object = nullptr;
    result = create(__uuidof(IRedXeWidgetProvider), &oversized, nullptr, nullptr, &object);
    if (FAILED(result) || !object)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    provider.attach(static_cast<IRedXeWidgetProvider*>(object));

    const RedXePluginMetadata* metadata = reinterpret_cast<const RedXePluginMetadata*>(1);
    std::uint32_t metadataCount = 1;
    result = enumerate(nullptr, &metadataCount);
    if (result != E_POINTER || metadataCount != 0)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    result = enumerate(&metadata, nullptr);
    if (result != E_POINTER || metadata)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    result = enumerate(&metadata, &metadataCount);
    if (FAILED(result) || !metadata || metadataCount != 1 || metadata[0].sizeBytes != sizeof(RedXePluginMetadata) ||
        !RedXeIsValidMachineId(metadata[0].id))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const RedXePluginMetadata* secondMetadata = nullptr;
    std::uint32_t secondMetadataCount = 0;
    result = enumerate(&secondMetadata, &secondMetadataCount);
    if (FAILED(result) || secondMetadata != metadata || secondMetadataCount != metadataCount)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    const RedXeWidgetTypeDescriptor* widgetTypes = nullptr;
    std::uint32_t widgetTypeCount = 0;
    if (provider->GetWidgetTypes(nullptr, &widgetTypeCount) != E_POINTER || widgetTypeCount != 0)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    widgetTypes = reinterpret_cast<const RedXeWidgetTypeDescriptor*>(1);
    if (provider->GetWidgetTypes(&widgetTypes, nullptr) != E_POINTER || widgetTypes)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    result = provider->GetWidgetTypes(&widgetTypes, &widgetTypeCount);
    if (FAILED(result) || !widgetTypes || widgetTypeCount != 1 ||
        widgetTypes[0].sizeBytes != sizeof(RedXeWidgetTypeDescriptor) || !RedXeIsValidMachineId(widgetTypes[0].typeId))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const RedXeWidgetTypeDescriptor* secondWidgetTypes = nullptr;
    std::uint32_t secondWidgetTypeCount = 0;
    result = provider->GetWidgetTypes(&secondWidgetTypes, &secondWidgetTypeCount);
    if (FAILED(result) || secondWidgetTypes != widgetTypes || secondWidgetTypeCount != widgetTypeCount)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    if (provider->CreateWidget(kWidgetTypeId, "contract.instance", nullptr) != E_POINTER)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    IRedXeWidget* rejectedWidget = reinterpret_cast<IRedXeWidget*>(1);
    result = provider->CreateWidget("missing.type", "contract.instance", &rejectedWidget);
    if (result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || rejectedWidget)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    rejectedWidget = reinterpret_cast<IRedXeWidget*>(1);
    result = provider->CreateWidget(kWidgetTypeId, "", &rejectedWidget);
    if (result != E_INVALIDARG || rejectedWidget)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    wil::com_ptr_nothrow<IRedXeWidget> widget;
    result = provider->CreateWidget(kWidgetTypeId, "contract.instance", widget.put());
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IRedXeGpuWidget> gpuWidget;
    result = widget.query_to(gpuWidget.put());
    if (FAILED(result) || !gpuWidget)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    void* unsupportedWidget = reinterpret_cast<void*>(1);
    result = widget->QueryInterface(__uuidof(IRedXeWindowWidget), &unsupportedWidget);
    if (result != E_NOINTERFACE || unsupportedWidget)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    wil::com_ptr_nothrow<IUnknown> widgetIdentity;
    wil::com_ptr_nothrow<IUnknown> gpuIdentity;
    if (FAILED(widget.query_to(widgetIdentity.put())) || FAILED(gpuWidget.query_to(gpuIdentity.put())) ||
        widgetIdentity.get() != gpuIdentity.get())
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    if (gpuWidget->OnDeviceCreated(nullptr) != E_POINTER)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    RedXeGpuDeviceContext invalidDevice{};
    invalidDevice.sizeBytes = sizeof(invalidDevice);
    if (gpuWidget->OnDeviceCreated(&invalidDevice) != E_INVALIDARG || gpuWidget->Render(nullptr) != E_POINTER)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    RedXeGpuFrameContext invalidFrame{};
    invalidFrame.sizeBytes = sizeof(invalidFrame);
    if (gpuWidget->Render(&invalidFrame) != E_POINTER)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    gpuWidget->OnDeviceLost();

    return S_OK;
}

[[nodiscard]] HRESULT RunWindowPluginContractTests() noexcept
{
    std::array<wchar_t, 1024> path{};
    HRESULT result = BuildPluginPath(L"GdiOrbit.dll", path);
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

    const RedXeCreateFn create = ResolveFunction<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const RedXeEnumeratePluginsFn enumerate =
        ResolveFunction<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    if (!create || !enumerate)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    result = ValidateSettingsContract(module.get(), kGdiPluginId, true);
    if (FAILED(result))
    {
        return result;
    }
    result = ValidateEmptyNormalizedFactoryConfiguration(create, kGdiPluginId);
    if (FAILED(result))
    {
        return result;
    }

    const RedXePluginMetadata* metadata = nullptr;
    std::uint32_t metadataCount = 0;
    result = enumerate(&metadata, &metadataCount);
    if (FAILED(result) || !metadata || metadataCount != 1 || !RedXeAsciiEqualsIgnoreCase(metadata[0].id, kGdiPluginId))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    RedXeFactoryOptions options{};
    options.sizeBytes = kRedXeFactoryOptionsV1Size;
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    void* providerObject = nullptr;
    result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kGdiPluginId, &providerObject);
    if (FAILED(result) || !providerObject)
    {
        return FAILED(result) ? result : E_UNEXPECTED;
    }
    provider.attach(static_cast<IRedXeWidgetProvider*>(providerObject));

    const RedXeWidgetTypeDescriptor* widgetTypes = nullptr;
    std::uint32_t widgetTypeCount = 0;
    result = provider->GetWidgetTypes(&widgetTypes, &widgetTypeCount);
    if (FAILED(result) || !widgetTypes || widgetTypeCount != 1 ||
        !RedXeAsciiEqualsIgnoreCase(widgetTypes[0].typeId, kGdiWidgetTypeId))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    wil::com_ptr_nothrow<IRedXeWidget> widget;
    result = provider->CreateWidget(kGdiWidgetTypeId, "contract.gdi.1", widget.put());
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IRedXeWindowWidget> windowWidget;
    result = widget.query_to(windowWidget.put());
    if (FAILED(result) || !windowWidget)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    void* unsupportedGpu = reinterpret_cast<void*>(1);
    result = widget->QueryInterface(__uuidof(IRedXeGpuWidget), &unsupportedGpu);
    if (result != E_NOINTERFACE || unsupportedGpu)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    wil::com_ptr_nothrow<IUnknown> widgetIdentity;
    wil::com_ptr_nothrow<IUnknown> windowIdentity;
    if (FAILED(widget.query_to(widgetIdentity.put())) || FAILED(windowWidget.query_to(windowIdentity.put())) ||
        widgetIdentity.get() != windowIdentity.get())
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    if (windowWidget->Attach(nullptr) != E_POINTER || windowWidget->Resize(nullptr) != E_POINTER ||
        windowWidget->SetVisible(TRUE) != E_UNEXPECTED)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    RedXeWindowWidgetAttachContext invalidAttach{};
    invalidAttach.sizeBytes = sizeof(invalidAttach);
    if (windowWidget->Attach(&invalidAttach) != E_INVALIDARG)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    wil::unique_hwnd parent{
        CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 640, 360, nullptr, nullptr, nullptr, nullptr)};
    if (!parent)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    wil::unique_hwnd container{
        CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 640, 360, parent.get(), nullptr, nullptr, nullptr)};
    if (!container)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const RedXeWindowWidgetAttachContext attach{
        sizeof(RedXeWindowWidgetAttachContext), container.get(), 640, 360, USER_DEFAULT_SCREEN_DPI,
    };
    result = windowWidget->Attach(&attach);
    if (FAILED(result) || !GetWindow(container.get(), GW_CHILD) || windowWidget->Attach(&attach) != E_UNEXPECTED)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    RedXeWindowWidgetSizeContext invalidSize{};
    invalidSize.sizeBytes = sizeof(invalidSize);
    if (windowWidget->Resize(&invalidSize) != E_INVALIDARG)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    const RedXeWindowWidgetSizeContext resized{
        sizeof(RedXeWindowWidgetSizeContext),
        480,
        240,
        144,
    };
    result = windowWidget->Resize(&resized);
    if (FAILED(result) || FAILED(windowWidget->SetVisible(TRUE)) || FAILED(windowWidget->SetVisible(FALSE)))
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    windowWidget->Detach();
    if (GetWindow(container.get(), GW_CHILD))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    windowWidget->Detach();

    // The production host keeps v1 modules mapped; preserve that policy for this registered-window-class fixture.
    (void)module.release();
    return S_OK;
}

struct MatrixRenderTarget final
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> renderTarget;
    wil::com_ptr_nothrow<ID3D11Texture2D> staging;
    wil::com_ptr_nothrow<ID3D11Query> completionQuery;
};

[[nodiscard]] HRESULT CreateMatrixRenderTarget(std::uint32_t width, std::uint32_t height, MatrixRenderTarget& target,
                                               D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_WARP) noexcept
{
    if (width == 0 || height == 0)
    {
        return E_INVALIDARG;
    }

    constexpr std::array featureLevels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    MatrixRenderTarget created{};
    created.width = width;
    created.height = height;
    HRESULT result = D3D11CreateDevice(nullptr, driverType, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       featureLevels.data(), static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION,
                                       created.device.put(), &created.featureLevel, created.context.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = width;
    textureDescription.Height = height;
    textureDescription.MipLevels = 1;
    textureDescription.ArraySize = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Usage = D3D11_USAGE_DEFAULT;
    textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET;
    result = created.device->CreateTexture2D(&textureDescription, nullptr, created.texture.put());
    if (FAILED(result))
    {
        return result;
    }
    result = created.device->CreateRenderTargetView(created.texture.get(), nullptr, created.renderTarget.put());
    if (FAILED(result))
    {
        return result;
    }

    textureDescription.Usage = D3D11_USAGE_STAGING;
    textureDescription.BindFlags = 0;
    textureDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    result = created.device->CreateTexture2D(&textureDescription, nullptr, created.staging.put());
    if (FAILED(result))
    {
        return result;
    }

    D3D11_QUERY_DESC queryDescription{};
    queryDescription.Query = D3D11_QUERY_EVENT;
    result = created.device->CreateQuery(&queryDescription, created.completionQuery.put());
    if (FAILED(result))
    {
        return result;
    }

    target = std::move(created);
    return S_OK;
}

[[nodiscard]] HRESULT CreateMatrixProvider(RedXeCreateFn create, std::string_view configuration,
                                           wil::com_ptr_nothrow<IRedXeWidgetProvider>& provider) noexcept
{
    if (!create || provider)
    {
        return E_INVALIDARG;
    }
    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
    options.configurationJsonUtf8 = configuration.data();
    options.configurationBytes = static_cast<std::uint32_t>(configuration.size());
    void* object = nullptr;
    const HRESULT result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kMatrixPluginId, &object);
    if (FAILED(result))
    {
        return result;
    }
    if (!object)
    {
        return E_UNEXPECTED;
    }
    provider.attach(static_cast<IRedXeWidgetProvider*>(object));
    return S_OK;
}

[[nodiscard]] HRESULT CreateMatrixWidget(IRedXeWidgetProvider& provider, wil::com_ptr_nothrow<IRedXeWidget>& widget,
                                         wil::com_ptr_nothrow<IRedXeGpuWidget>& gpuWidget) noexcept
{
    HRESULT result = provider.CreateWidget(kMatrixWidgetTypeId, "matrix.contract.1", widget.put());
    if (FAILED(result))
    {
        return result;
    }
    result = widget.query_to(gpuWidget.put());
    return SUCCEEDED(result) && gpuWidget ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

[[nodiscard]] HRESULT RenderMatrixFrame(IRedXeGpuWidget& widget, MatrixRenderTarget& target, float elapsedSeconds,
                                        std::vector<std::uint8_t>& pixels) noexcept
{
    try
    {
        target.context->ClearState();
        ID3D11RenderTargetView* renderTargets[] = {target.renderTarget.get()};
        target.context->OMSetRenderTargets(1, renderTargets, nullptr);
        const D3D11_VIEWPORT viewport{
            0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f,
        };
        target.context->RSSetViewports(1, &viewport);
        const RedXeWidgetFrameContext frame{
            sizeof(RedXeWidgetFrameContext), target.width,   target.height,
            USER_DEFAULT_SCREEN_DPI,         elapsedSeconds, 1.0f / 60.0f,
        };
        const RedXeGpuFrameContext gpuFrame{
            sizeof(RedXeGpuFrameContext),
            &frame,
            target.context.get(),
            viewport,
        };
        HRESULT result = widget.Render(&gpuFrame);
        if (FAILED(result))
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
        pixels.resize(rowBytes * target.height);
        for (std::uint32_t row = 0; row < target.height; ++row)
        {
            std::memcpy(pixels.data() + static_cast<std::size_t>(row) * rowBytes,
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

[[nodiscard]] HRESULT ValidateAllocationFreeMatrixRender(IRedXeGpuWidget& widget, MatrixRenderTarget& target) noexcept
{
#if defined(_DEBUG)
    ID3D11RenderTargetView* renderTargets[] = {target.renderTarget.get()};
    target.context->OMSetRenderTargets(1, renderTargets, nullptr);
    const D3D11_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f,
    };
    target.context->RSSetViewports(1, &viewport);
    RedXeWidgetFrameContext frame{
        sizeof(RedXeWidgetFrameContext), target.width, target.height, USER_DEFAULT_SCREEN_DPI, 0.0f, 1.0f / 60.0f,
    };
    const RedXeGpuFrameContext gpuFrame{
        sizeof(RedXeGpuFrameContext),
        &frame,
        target.context.get(),
        viewport,
    };
    for (std::uint32_t warmup = 0; warmup < 1024; ++warmup)
    {
        frame.elapsedSeconds = static_cast<float>(warmup) / 60.0f;
        const HRESULT result = widget.Render(&gpuFrame);
        if (FAILED(result))
        {
            return result;
        }
    }
    target.context->Flush();
    Sleep(100);

    gMatrixRenderAllocationCount = 0;
    gMatrixRenderThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const _CRT_ALLOC_HOOK previousHook = _CrtSetAllocHook(CountMatrixRenderAllocation);
    HRESULT renderResult = S_OK;
    for (std::uint32_t frameIndex = 0; frameIndex < 240; ++frameIndex)
    {
        frame.elapsedSeconds = static_cast<float>(frameIndex) / 60.0f;
        renderResult = widget.Render(&gpuFrame);
        if (FAILED(renderResult))
        {
            break;
        }
    }
    (void)_CrtSetAllocHook(previousHook);
    gMatrixRenderThreadId.store(0, std::memory_order_relaxed);
    if (FAILED(renderResult))
    {
        return renderResult;
    }
    if (gMatrixRenderAllocationCount != 0)
    {
        std::wprintf(L"Matrix render CRT allocations: %llu\n",
                     static_cast<unsigned long long>(gMatrixRenderAllocationCount.load()));
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
#else
    (void)widget;
    (void)target;
#endif
    return S_OK;
}

[[nodiscard]] bool ContainsBackgroundAndGlyphPixels(const std::vector<std::uint8_t>& pixels,
                                                    std::array<std::uint8_t, 4> background) noexcept
{
    bool foundBackground = false;
    bool foundGlyph = false;
    for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4)
    {
        const bool matches = pixels[offset] == background[0] && pixels[offset + 1] == background[1] &&
                             pixels[offset + 2] == background[2] && pixels[offset + 3] == background[3];
        foundBackground = foundBackground || matches;
        foundGlyph = foundGlyph || !matches;
    }
    return foundBackground && foundGlyph;
}

[[nodiscard]] HRESULT ValidateMatrixConfigurationRejections(RedXeCreateFn create) noexcept
{
    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
    char present = '{';
    options.configurationJsonUtf8 = &present;
    options.configurationBytes = 0;
    void* object = reinterpret_cast<void*>(1);
    if (create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kMatrixPluginId, &object) != E_INVALIDARG || object)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    options.configurationJsonUtf8 = nullptr;
    options.configurationBytes = 1;
    object = reinterpret_cast<void*>(1);
    if (create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kMatrixPluginId, &object) != E_INVALIDARG || object)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    options.configurationJsonUtf8 = &present;
    options.configurationBytes = kRedXeMaximumFactoryConfigurationBytes + 1;
    object = reinterpret_cast<void*>(1);
    if (create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kMatrixPluginId, &object) != E_INVALIDARG || object)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    struct Mutation final
    {
        std::string_view before;
        std::string_view after;
    };
    constexpr std::array mutations{
        Mutation{"\"seed\":1999", "\"seed\":4294967296"},
        Mutation{"\"glyphHeightDips\":18", "\"glyphHeightDips\":11"},
        Mutation{"\"densityPercent\":70", "\"densityPercent\":101"},
        Mutation{"\"speedPercent\":100", "\"speedPercent\":24"},
        Mutation{"\"trailLengthGlyphs\":18", "\"trailLengthGlyphs\":49"},
        Mutation{"\"mutationPerSecond\":8", "\"mutationPerSecond\":31"},
        Mutation{"\"headColor\":\"#D8FFE5\"", "\"headColor\":\"D8FFE5\""},
        Mutation{"\"trailColor\":\"#00E65C\"", "\"trailColor\":\"#00E65G\""},
        Mutation{"\"backgroundColor\":\"#010502\"", "\"backgroundColor\":\"#01050\""},
        Mutation{"\"glowPercent\":35", "\"glowPercent\":101"},
        Mutation{"\"seed\":1999", "\"seed\":1999,\"seed\":1"},
        Mutation{"\"glowPercent\":35", "\"glowPercent\":35,\"unknown\":1"},
    };
    try
    {
        for (const Mutation& mutation : mutations)
        {
            std::string invalid(kDefaultMatrixConfiguration);
            const std::size_t offset = invalid.find(mutation.before);
            if (offset == std::string::npos)
            {
                return E_UNEXPECTED;
            }
            invalid.replace(offset, mutation.before.size(), mutation.after);
            options.configurationJsonUtf8 = invalid.data();
            options.configurationBytes = static_cast<std::uint32_t>(invalid.size());
            object = reinterpret_cast<void*>(1);
            const HRESULT result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kMatrixPluginId, &object);
            if (result != HRESULT_FROM_WIN32(ERROR_INVALID_DATA) || object)
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }

    constexpr std::array<std::string_view, 4> invalidNormalizedConfigurations{
        R"json({"plugin":{"unexpected":1},"instance":{"seed":1999,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35}})json",
        R"json({"plugin":{},"instance":{}})json",
        R"json({"plugin":{},"instance":{"seed":1999,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35},"unknown":1})json",
        R"json({"instance":{"seed":1999,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35}})json",
    };
    for (const std::string_view invalid : invalidNormalizedConfigurations)
    {
        options.configurationJsonUtf8 = invalid.data();
        options.configurationBytes = static_cast<std::uint32_t>(invalid.size());
        object = reinterpret_cast<void*>(1);
        if (create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kMatrixPluginId, &object) !=
                HRESULT_FROM_WIN32(ERROR_INVALID_DATA) ||
            object)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    return S_OK;
}

[[nodiscard]] HRESULT ValidateMatrixRendering(RedXeCreateFn create) noexcept
{
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    HRESULT result = CreateMatrixProvider(create, kDefaultMatrixConfiguration, provider);
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IRedXeWidget> widget;
    wil::com_ptr_nothrow<IRedXeGpuWidget> gpuWidget;
    result = CreateMatrixWidget(*provider, widget, gpuWidget);
    if (FAILED(result))
    {
        return result;
    }

    IRedXeWidget* duplicate = reinterpret_cast<IRedXeWidget*>(1);
    result = provider->CreateWidget(kMatrixWidgetTypeId, "matrix.contract.2", &duplicate);
    if (result != HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES) || duplicate)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    void* unsupported = reinterpret_cast<void*>(1);
    result = widget->QueryInterface(__uuidof(IRedXeWindowWidget), &unsupported);
    if (result != E_NOINTERFACE || unsupported)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    wil::com_ptr_nothrow<IUnknown> widgetIdentity;
    wil::com_ptr_nothrow<IUnknown> gpuIdentity;
    if (FAILED(widget.query_to(widgetIdentity.put())) || FAILED(gpuWidget.query_to(gpuIdentity.put())) ||
        widgetIdentity.get() != gpuIdentity.get())
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    if (gpuWidget->OnDeviceCreated(nullptr) != E_POINTER || gpuWidget->Render(nullptr) != E_POINTER)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    RedXeGpuDeviceContext invalidDevice{};
    invalidDevice.sizeBytes = sizeof(invalidDevice);
    if (gpuWidget->OnDeviceCreated(&invalidDevice) != E_POINTER)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    RedXeGpuFrameContext invalidFrame{};
    invalidFrame.sizeBytes = sizeof(std::uint32_t);
    if (gpuWidget->Render(&invalidFrame) != E_INVALIDARG)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    MatrixRenderTarget target;
    result = CreateMatrixRenderTarget(320, 180, target);
    if (FAILED(result))
    {
        return result;
    }
    const RedXeGpuDeviceContext deviceContext{
        sizeof(RedXeGpuDeviceContext),
        target.device.get(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        target.featureLevel,
    };
    result = gpuWidget->OnDeviceCreated(&deviceContext);
    if (FAILED(result))
    {
        return result;
    }
    result = ValidateAllocationFreeMatrixRender(*gpuWidget, target);
    if (FAILED(result))
    {
        return result;
    }

    std::vector<std::uint8_t> first;
    std::vector<std::uint8_t> identical;
    std::vector<std::uint8_t> animated;
    result = RenderMatrixFrame(*gpuWidget, target, 1.25f, first);
    if (SUCCEEDED(result))
    {
        result = RenderMatrixFrame(*gpuWidget, target, 1.25f, identical);
    }
    if (SUCCEEDED(result))
    {
        result = RenderMatrixFrame(*gpuWidget, target, 2.25f, animated);
    }
    if (FAILED(result) || first != identical || first == animated ||
        !ContainsBackgroundAndGlyphPixels(first, {0x01, 0x05, 0x02, 0xFF}))
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    gpuWidget->OnDeviceLost();
    MatrixRenderTarget recreatedTarget;
    result = CreateMatrixRenderTarget(320, 180, recreatedTarget);
    if (FAILED(result))
    {
        return result;
    }
    const RedXeGpuDeviceContext recreatedDeviceContext{
        sizeof(RedXeGpuDeviceContext),
        recreatedTarget.device.get(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        recreatedTarget.featureLevel,
    };
    result = gpuWidget->OnDeviceCreated(&recreatedDeviceContext);
    if (SUCCEEDED(result))
    {
        result = RenderMatrixFrame(*gpuWidget, recreatedTarget, 1.25f, identical);
    }
    gpuWidget->OnDeviceLost();
    if (FAILED(result) || first != identical)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    try
    {
        std::string borrowed(kDefaultMatrixConfiguration);
        const std::string_view oldColor = "#010502";
        const std::size_t colorOffset = borrowed.find(oldColor);
        if (colorOffset == std::string::npos)
        {
            return E_UNEXPECTED;
        }
        borrowed.replace(colorOffset, oldColor.size(), "#7A1133");
        wil::com_ptr_nothrow<IRedXeWidgetProvider> borrowedProvider;
        result = CreateMatrixProvider(create, borrowed, borrowedProvider);
        if (FAILED(result))
        {
            return result;
        }
        std::fill(borrowed.begin(), borrowed.end(), 'X');
        wil::com_ptr_nothrow<IRedXeWidget> borrowedWidget;
        wil::com_ptr_nothrow<IRedXeGpuWidget> borrowedGpuWidget;
        result = CreateMatrixWidget(*borrowedProvider, borrowedWidget, borrowedGpuWidget);
        if (FAILED(result))
        {
            return result;
        }
        result = borrowedGpuWidget->OnDeviceCreated(&recreatedDeviceContext);
        std::vector<std::uint8_t> borrowedPixels;
        if (SUCCEEDED(result))
        {
            result = RenderMatrixFrame(*borrowedGpuWidget, recreatedTarget, 1.25f, borrowedPixels);
        }
        borrowedGpuWidget->OnDeviceLost();
        if (FAILED(result) || !ContainsBackgroundAndGlyphPixels(borrowedPixels, {0x7A, 0x11, 0x33, 0xFF}))
        {
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_FAIL;
    }

    constexpr std::array<std::string_view, 2> limitConfigurations{
        R"json({"seed":0,"glyphHeightDips":12,"densityPercent":10,"speedPercent":25,"trailLengthGlyphs":6,"mutationPerSecond":0,"headColor":"#000000","trailColor":"#000000","backgroundColor":"#000000","glowPercent":0})json",
        R"json({"seed":4294967295,"glyphHeightDips":48,"densityPercent":100,"speedPercent":300,"trailLengthGlyphs":48,"mutationPerSecond":30,"headColor":"#FFFFFF","trailColor":"#FFFFFF","backgroundColor":"#FFFFFF","glowPercent":100})json",
    };
    for (const std::string_view configuration : limitConfigurations)
    {
        wil::com_ptr_nothrow<IRedXeWidgetProvider> limitProvider;
        result = CreateMatrixProvider(create, configuration, limitProvider);
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<IRedXeWidget> limitWidget;
        wil::com_ptr_nothrow<IRedXeGpuWidget> limitGpuWidget;
        result = CreateMatrixWidget(*limitProvider, limitWidget, limitGpuWidget);
        if (SUCCEEDED(result))
        {
            result = limitGpuWidget->OnDeviceCreated(&recreatedDeviceContext);
        }
        if (SUCCEEDED(result))
        {
            result = RenderMatrixFrame(*limitGpuWidget, recreatedTarget, 3.0f, identical);
        }
        limitGpuWidget->OnDeviceLost();
        if (FAILED(result))
        {
            return result;
        }
    }

    wil::com_ptr_nothrow<IRedXeWidgetProvider> differentSeedProvider;
    try
    {
        std::string differentSeed(kDefaultMatrixConfiguration);
        const std::size_t seedOffset = differentSeed.find("\"seed\":1999");
        differentSeed.replace(seedOffset, std::string_view("\"seed\":1999").size(), "\"seed\":2000");
        result = CreateMatrixProvider(create, differentSeed, differentSeedProvider);
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IRedXeWidget> differentSeedWidget;
    wil::com_ptr_nothrow<IRedXeGpuWidget> differentSeedGpuWidget;
    result = CreateMatrixWidget(*differentSeedProvider, differentSeedWidget, differentSeedGpuWidget);
    if (SUCCEEDED(result))
    {
        result = differentSeedGpuWidget->OnDeviceCreated(&recreatedDeviceContext);
    }
    std::vector<std::uint8_t> differentSeedPixels;
    if (SUCCEEDED(result))
    {
        result = RenderMatrixFrame(*differentSeedGpuWidget, recreatedTarget, 1.25f, differentSeedPixels);
    }
    differentSeedGpuWidget->OnDeviceLost();
    return FAILED(result) || first == differentSeedPixels
               ? (FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA))
               : S_OK;
}

[[nodiscard]] HRESULT RunMatrixRainContractTests() noexcept
{
    const bool compilerWasLoaded = GetModuleHandleW(L"d3dcompiler_47.dll") != nullptr;
    const bool directWriteWasLoaded = GetModuleHandleW(L"dwrite.dll") != nullptr;
    const bool wicWasLoaded = GetModuleHandleW(L"windowscodecs.dll") != nullptr;

    std::array<wchar_t, 1024> path{};
    HRESULT result = BuildPluginPath(L"MatrixRain.dll", path);
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
    if ((!compilerWasLoaded && GetModuleHandleW(L"d3dcompiler_47.dll")) ||
        (!directWriteWasLoaded && GetModuleHandleW(L"dwrite.dll")) ||
        (!wicWasLoaded && GetModuleHandleW(L"windowscodecs.dll")))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    const RedXeCreateFn create = ResolveFunction<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const RedXeEnumeratePluginsFn enumerate =
        ResolveFunction<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    if (!create || !enumerate)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    result = ValidateSettingsContract(module.get(), kMatrixPluginId, false);
    if (FAILED(result))
    {
        return result;
    }

    const RedXePluginMetadata* metadata = nullptr;
    std::uint32_t metadataCount = 0;
    result = enumerate(&metadata, &metadataCount);
    if (FAILED(result) || !metadata || metadataCount != 1 ||
        !RedXeAsciiEqualsIgnoreCase(metadata[0].id, kMatrixPluginId))
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    RedXeFactoryOptions v1Options{};
    v1Options.sizeBytes = kRedXeFactoryOptionsV1Size;
    void* v1Object = nullptr;
    result = create(__uuidof(IRedXeWidgetProvider), &v1Options, nullptr, kMatrixPluginId, &v1Object);
    if (FAILED(result) || !v1Object)
    {
        return FAILED(result) ? result : E_UNEXPECTED;
    }
    wil::com_ptr_nothrow<IRedXeWidgetProvider> v1Provider;
    v1Provider.attach(static_cast<IRedXeWidgetProvider*>(v1Object));

    const RedXeWidgetTypeDescriptor* widgetTypes = nullptr;
    std::uint32_t widgetTypeCount = 0;
    result = v1Provider->GetWidgetTypes(&widgetTypes, &widgetTypeCount);
    if (FAILED(result) || !widgetTypes || widgetTypeCount != 1 ||
        widgetTypes[0].sizeBytes != sizeof(RedXeWidgetTypeDescriptor) ||
        !RedXeAsciiEqualsIgnoreCase(widgetTypes[0].typeId, kMatrixWidgetTypeId) ||
        (widgetTypes[0].flags & RedXeWidgetFlagContinuousAnimation) == 0)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    RedXeFactoryOptions futureOptions{};
    futureOptions.sizeBytes = sizeof(futureOptions) + 32;
    void* futureObject = nullptr;
    result = create(__uuidof(IRedXeWidgetProvider), &futureOptions, nullptr, kMatrixPluginId, &futureObject);
    if (FAILED(result) || !futureObject)
    {
        return FAILED(result) ? result : E_UNEXPECTED;
    }
    wil::com_ptr_nothrow<IRedXeWidgetProvider> futureProvider;
    futureProvider.attach(static_cast<IRedXeWidgetProvider*>(futureObject));

    result = ValidateMatrixConfigurationRejections(create);
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IRedXeWidgetProvider> normalizedProvider;
    result = CreateMatrixProvider(create, kNormalizedMatrixConfiguration, normalizedProvider);
    if (FAILED(result))
    {
        return result;
    }
    return ValidateMatrixRendering(create);
}

[[nodiscard]] HRESULT SubmitBenchmarkFrame(IRedXeGpuWidget* widget, MatrixRenderTarget& target,
                                           float elapsedSeconds) noexcept
{
    ID3D11RenderTargetView* renderTargets[] = {target.renderTarget.get()};
    target.context->OMSetRenderTargets(1, renderTargets, nullptr);
    constexpr std::array clearColor{0.025f, 0.035f, 0.075f, 1.0f};
    target.context->ClearRenderTargetView(target.renderTarget.get(), clearColor.data());
    const D3D11_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f,
    };
    target.context->RSSetViewports(1, &viewport);
    if (!widget)
    {
        return S_OK;
    }
    const RedXeWidgetFrameContext frame{
        sizeof(RedXeWidgetFrameContext), target.width,   target.height,
        USER_DEFAULT_SCREEN_DPI,         elapsedSeconds, 1.0f / 60.0f,
    };
    const RedXeGpuFrameContext gpuFrame{
        sizeof(RedXeGpuFrameContext),
        &frame,
        target.context.get(),
        viewport,
    };
    return widget->Render(&gpuFrame);
}

[[nodiscard]] HRESULT WaitForBenchmarkGpu(MatrixRenderTarget& target) noexcept
{
    if (!target.completionQuery)
    {
        return E_UNEXPECTED;
    }
    target.context->End(target.completionQuery.get());
    target.context->Flush();
    const ULONGLONG deadline = GetTickCount64() + 30'000;
    HRESULT result = S_FALSE;
    while ((result = target.context->GetData(target.completionQuery.get(), nullptr, 0, 0)) == S_FALSE)
    {
        if (GetTickCount64() >= deadline)
        {
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        SwitchToThread();
    }
    return result;
}

[[nodiscard]] HRESULT MeasureCpuSubmission(IRedXeGpuWidget* widget, MatrixRenderTarget& target,
                                           std::uint32_t frameCount, double& microsecondsPerFrame) noexcept
{
    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&start))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    for (std::uint32_t frame = 0; frame < frameCount; ++frame)
    {
        const HRESULT result = SubmitBenchmarkFrame(widget, target, static_cast<float>(frame) / 60.0f);
        if (FAILED(result))
        {
            return result;
        }
    }
    if (!QueryPerformanceCounter(&end))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    microsecondsPerFrame = static_cast<double>(end.QuadPart - start.QuadPart) * 1'000'000.0 /
                           static_cast<double>(frequency.QuadPart) / static_cast<double>(frameCount);
    const HRESULT waitResult = WaitForBenchmarkGpu(target);
    if (SUCCEEDED(waitResult))
    {
        Sleep(100);
    }
    return waitResult;
}

[[nodiscard]] HRESULT MeasureGpuTime(IRedXeGpuWidget* widget, MatrixRenderTarget& target, std::uint32_t frameCount,
                                     double& millisecondsPerFrame) noexcept
{
    D3D11_QUERY_DESC description{};
    description.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    wil::com_ptr_nothrow<ID3D11Query> disjoint;
    HRESULT result = target.device->CreateQuery(&description, disjoint.put());
    if (FAILED(result))
    {
        return result;
    }
    description.Query = D3D11_QUERY_TIMESTAMP;
    wil::com_ptr_nothrow<ID3D11Query> start;
    wil::com_ptr_nothrow<ID3D11Query> end;
    result = target.device->CreateQuery(&description, start.put());
    if (SUCCEEDED(result))
    {
        result = target.device->CreateQuery(&description, end.put());
    }
    if (FAILED(result))
    {
        return result;
    }

    target.context->Begin(disjoint.get());
    target.context->End(start.get());
    for (std::uint32_t frame = 0; frame < frameCount; ++frame)
    {
        result = SubmitBenchmarkFrame(widget, target, static_cast<float>(frame) / 60.0f);
        if (FAILED(result))
        {
            return result;
        }
    }
    target.context->End(end.get());
    target.context->End(disjoint.get());
    target.context->Flush();

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{};
    const ULONGLONG deadline = GetTickCount64() + 30'000;
    while ((result = target.context->GetData(disjoint.get(), &timing, sizeof(timing), 0)) == S_FALSE)
    {
        if (GetTickCount64() >= deadline)
        {
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        SwitchToThread();
    }
    if (FAILED(result) || timing.Disjoint || timing.Frequency == 0)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    std::uint64_t startTicks = 0;
    std::uint64_t endTicks = 0;
    result = target.context->GetData(start.get(), &startTicks, sizeof(startTicks), 0);
    if (SUCCEEDED(result))
    {
        result = target.context->GetData(end.get(), &endTicks, sizeof(endTicks), 0);
    }
    if (FAILED(result) || endTicks < startTicks)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    millisecondsPerFrame = static_cast<double>(endTicks - startTicks) * 1'000.0 /
                           static_cast<double>(timing.Frequency) / static_cast<double>(frameCount);
    return S_OK;
}

struct ProcessMemorySnapshot final
{
    std::uint64_t privateBytes = 0;
    std::uint64_t workingSetBytes = 0;
};

[[nodiscard]] HRESULT QueryProcessMemorySnapshot(ProcessMemorySnapshot& snapshot) noexcept
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters)))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    snapshot.privateBytes = counters.PrivateUsage;
    snapshot.workingSetBytes = counters.WorkingSetSize;
    return S_OK;
}

struct HeapSnapshot final
{
    std::uint64_t busyBlocks = 0;
    std::uint64_t busyBytes = 0;
};

[[nodiscard]] HRESULT QueryHeapSnapshot(HeapSnapshot& snapshot) noexcept
{
    try
    {
        const DWORD heapCount = GetProcessHeaps(0, nullptr);
        if (heapCount == 0)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        std::vector<HANDLE> heaps(heapCount);
        const DWORD actualCount = GetProcessHeaps(heapCount, heaps.data());
        if (actualCount == 0 || actualCount > heapCount)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        HeapSnapshot measured{};
        for (DWORD heapIndex = 0; heapIndex < actualCount; ++heapIndex)
        {
            if (!HeapLock(heaps[heapIndex]))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            PROCESS_HEAP_ENTRY entry{};
            SetLastError(ERROR_SUCCESS);
            while (HeapWalk(heaps[heapIndex], &entry))
            {
                if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0)
                {
                    ++measured.busyBlocks;
                    measured.busyBytes += entry.cbData;
                }
            }
            const DWORD walkError = GetLastError();
            if (!HeapUnlock(heaps[heapIndex]))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            if (walkError != ERROR_NO_MORE_ITEMS)
            {
                return HRESULT_FROM_WIN32(walkError);
            }
        }
        snapshot = measured;
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

[[nodiscard]] HRESULT RunMatrixRainBenchmark(bool soak, bool disabledSoak) noexcept
{
    constexpr std::uint32_t width = 2560;
    constexpr std::uint32_t height = 720;
    constexpr std::uint32_t measurementFrames = 600;
    constexpr std::uint32_t gpuFrames = 120;

    MatrixRenderTarget target;
    bool hardware = true;
    HRESULT result = CreateMatrixRenderTarget(width, height, target, D3D_DRIVER_TYPE_HARDWARE);
    if (FAILED(result))
    {
        hardware = false;
        result = CreateMatrixRenderTarget(width, height, target, D3D_DRIVER_TYPE_WARP);
    }
    if (FAILED(result))
    {
        return result;
    }

    for (std::uint32_t frame = 0; frame < measurementFrames; ++frame)
    {
        result = SubmitBenchmarkFrame(nullptr, target, static_cast<float>(frame) / 60.0f);
        if (FAILED(result))
        {
            return result;
        }
    }
    result = WaitForBenchmarkGpu(target);
    if (FAILED(result))
    {
        return result;
    }

    ProcessMemorySnapshot disabledMemory{};
    HeapSnapshot baselineHeapBefore{};
    HeapSnapshot baselineHeapAfter{};
    double baselineCpuMicroseconds = 0.0;
    double baselineGpuMilliseconds = 0.0;
    result = QueryProcessMemorySnapshot(disabledMemory);
    if (SUCCEEDED(result))
    {
        result = QueryHeapSnapshot(baselineHeapBefore);
    }
    if (SUCCEEDED(result))
    {
        result = MeasureCpuSubmission(nullptr, target, measurementFrames, baselineCpuMicroseconds);
    }
    if (SUCCEEDED(result))
    {
        result = QueryHeapSnapshot(baselineHeapAfter);
    }
    if (SUCCEEDED(result))
    {
        result = MeasureGpuTime(nullptr, target, gpuFrames, baselineGpuMilliseconds);
    }
    if (FAILED(result))
    {
        return result;
    }

    std::array<wchar_t, 1024> path{};
    result = BuildPluginPath(L"MatrixRain.dll", path);
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
    const RedXeCreateFn create = ResolveFunction<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    result = CreateMatrixProvider(create, kDefaultMatrixConfiguration, provider);
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IRedXeWidget> widget;
    wil::com_ptr_nothrow<IRedXeGpuWidget> gpuWidget;
    result = CreateMatrixWidget(*provider, widget, gpuWidget);
    if (FAILED(result))
    {
        return result;
    }
    const RedXeGpuDeviceContext deviceContext{
        sizeof(RedXeGpuDeviceContext),
        target.device.get(),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        target.featureLevel,
    };
    result = gpuWidget->OnDeviceCreated(&deviceContext);
    if (FAILED(result))
    {
        return result;
    }

    for (std::uint32_t frame = 0; frame < measurementFrames; ++frame)
    {
        result = SubmitBenchmarkFrame(gpuWidget.get(), target, static_cast<float>(frame) / 60.0f);
        if (FAILED(result))
        {
            return result;
        }
    }
    result = WaitForBenchmarkGpu(target);
    if (FAILED(result))
    {
        return result;
    }
    Sleep(100);

    ProcessMemorySnapshot enabledMemory{};
    ProcessMemorySnapshot steadyMemory{};
    HeapSnapshot heapBefore{};
    HeapSnapshot heapAfter{};
    double pluginCpuMicroseconds = 0.0;
    double pluginGpuMilliseconds = 0.0;
    result = QueryProcessMemorySnapshot(enabledMemory);
    if (SUCCEEDED(result))
    {
        result = MeasureGpuTime(gpuWidget.get(), target, gpuFrames, pluginGpuMilliseconds);
    }
    if (SUCCEEDED(result))
    {
        result = QueryHeapSnapshot(heapBefore);
    }
    if (SUCCEEDED(result))
    {
        result = MeasureCpuSubmission(gpuWidget.get(), target, measurementFrames, pluginCpuMicroseconds);
    }
    if (SUCCEEDED(result))
    {
        result = QueryHeapSnapshot(heapAfter);
    }
    if (SUCCEEDED(result))
    {
        result = QueryProcessMemorySnapshot(steadyMemory);
    }
    if (FAILED(result))
    {
        return result;
    }

    HeapSnapshot soakBefore{};
    HeapSnapshot soakPreFence{};
    HeapSnapshot soakAfter{};
    ProcessMemorySnapshot soakMemoryBefore{};
    ProcessMemorySnapshot soakMemoryPreFence{};
    ProcessMemorySnapshot soakMemoryAfter{};
    std::uint64_t soakFrames = 0;
    if (soak)
    {
        result = QueryHeapSnapshot(soakBefore);
        if (SUCCEEDED(result))
        {
            result = QueryProcessMemorySnapshot(soakMemoryBefore);
        }
        const ULONGLONG endTime = GetTickCount64() + 300'000;
        while (SUCCEEDED(result) && GetTickCount64() < endTime)
        {
            result = SubmitBenchmarkFrame(disabledSoak ? nullptr : gpuWidget.get(), target,
                                          static_cast<float>(soakFrames) / 60.0f);
            ++soakFrames;
            Sleep(16);
        }
        if (SUCCEEDED(result))
        {
            result = QueryHeapSnapshot(soakPreFence);
        }
        if (SUCCEEDED(result))
        {
            result = QueryProcessMemorySnapshot(soakMemoryPreFence);
        }
        if (SUCCEEDED(result))
        {
            result = WaitForBenchmarkGpu(target);
        }
        if (SUCCEEDED(result))
        {
            result = QueryHeapSnapshot(soakAfter);
        }
        if (SUCCEEDED(result))
        {
            result = QueryProcessMemorySnapshot(soakMemoryAfter);
        }
        if (FAILED(result))
        {
            return result;
        }
    }

    gpuWidget->OnDeviceLost();
    std::wprintf(L"MatrixRain benchmark (%ls, 2560x720, Release defaults)\n", hardware ? L"hardware" : L"WARP");
    std::wprintf(L"cpu_us_per_frame baseline=%.3f enabled=%.3f delta=%.3f\n", baselineCpuMicroseconds,
                 pluginCpuMicroseconds, pluginCpuMicroseconds - baselineCpuMicroseconds);
    std::wprintf(L"gpu_ms_per_frame baseline=%.4f enabled=%.4f delta=%.4f\n", baselineGpuMilliseconds,
                 pluginGpuMilliseconds, pluginGpuMilliseconds - baselineGpuMilliseconds);
    std::wprintf(L"memory_bytes disabled_private=%llu enabled_private=%llu steady_private=%llu\n",
                 static_cast<unsigned long long>(disabledMemory.privateBytes),
                 static_cast<unsigned long long>(enabledMemory.privateBytes),
                 static_cast<unsigned long long>(steadyMemory.privateBytes));
    std::wprintf(L"working_set_bytes disabled=%llu enabled=%llu steady=%llu\n",
                 static_cast<unsigned long long>(disabledMemory.workingSetBytes),
                 static_cast<unsigned long long>(enabledMemory.workingSetBytes),
                 static_cast<unsigned long long>(steadyMemory.workingSetBytes));
    std::wprintf(
        L"baseline_heap busy_blocks_before=%llu after=%llu delta=%lld busy_bytes_before=%llu after=%llu "
        L"delta=%lld\n",
        static_cast<unsigned long long>(baselineHeapBefore.busyBlocks),
        static_cast<unsigned long long>(baselineHeapAfter.busyBlocks),
        static_cast<long long>(baselineHeapAfter.busyBlocks) - static_cast<long long>(baselineHeapBefore.busyBlocks),
        static_cast<unsigned long long>(baselineHeapBefore.busyBytes),
        static_cast<unsigned long long>(baselineHeapAfter.busyBytes),
        static_cast<long long>(baselineHeapAfter.busyBytes) - static_cast<long long>(baselineHeapBefore.busyBytes));
    std::wprintf(
        L"steady_heap busy_blocks_before=%llu after=%llu delta=%lld busy_bytes_before=%llu after=%llu "
        L"delta=%lld\n",
        static_cast<unsigned long long>(heapBefore.busyBlocks), static_cast<unsigned long long>(heapAfter.busyBlocks),
        static_cast<long long>(heapAfter.busyBlocks) - static_cast<long long>(heapBefore.busyBlocks),
        static_cast<unsigned long long>(heapBefore.busyBytes), static_cast<unsigned long long>(heapAfter.busyBytes),
        static_cast<long long>(heapAfter.busyBytes) - static_cast<long long>(heapBefore.busyBytes));
    if (soak)
    {
        std::wprintf(L"soak_5m mode=%ls frames=%llu heap_blocks_delta=%lld heap_bytes_delta=%lld "
                     L"private_bytes_delta=%lld working_set_delta=%lld\n",
                     disabledSoak ? L"disabled" : L"enabled", static_cast<unsigned long long>(soakFrames),
                     static_cast<long long>(soakAfter.busyBlocks) - static_cast<long long>(soakBefore.busyBlocks),
                     static_cast<long long>(soakAfter.busyBytes) - static_cast<long long>(soakBefore.busyBytes),
                     static_cast<long long>(soakMemoryAfter.privateBytes) -
                         static_cast<long long>(soakMemoryBefore.privateBytes),
                     static_cast<long long>(soakMemoryAfter.workingSetBytes) -
                         static_cast<long long>(soakMemoryBefore.workingSetBytes));
        std::wprintf(L"soak_5m_pre_fence heap_blocks_delta=%lld heap_bytes_delta=%lld private_bytes_delta=%lld "
                     L"working_set_delta=%lld\n",
                     static_cast<long long>(soakPreFence.busyBlocks) - static_cast<long long>(soakBefore.busyBlocks),
                     static_cast<long long>(soakPreFence.busyBytes) - static_cast<long long>(soakBefore.busyBytes),
                     static_cast<long long>(soakMemoryPreFence.privateBytes) -
                         static_cast<long long>(soakMemoryBefore.privateBytes),
                     static_cast<long long>(soakMemoryPreFence.workingSetBytes) -
                         static_cast<long long>(soakMemoryBefore.workingSetBytes));
    }
    return S_OK;
}
} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
    if (argumentCount >= 2 && std::wstring_view(arguments[1]) == L"--matrix-benchmark")
    {
        const std::wstring_view mode = argumentCount >= 3 ? std::wstring_view(arguments[2]) : std::wstring_view{};
        const bool disabledSoak = mode == L"--soak-disabled";
        const bool soak = mode == L"--soak" || disabledSoak;
        const HRESULT benchmarkResult = RunMatrixRainBenchmark(soak, disabledSoak);
        return FAILED(benchmarkResult) ? static_cast<int>(benchmarkResult & 0xFF) : 0;
    }
    const HRESULT result = RunContractTests();
    if (FAILED(result))
    {
        return static_cast<int>(result & 0xFF);
    }
    const HRESULT windowResult = RunWindowPluginContractTests();
    if (FAILED(windowResult))
    {
        return static_cast<int>(windowResult & 0xFF);
    }
    const HRESULT matrixResult = RunMatrixRainContractTests();
    if (FAILED(matrixResult))
    {
        return static_cast<int>(matrixResult & 0xFF);
    }
    return 0;
}
