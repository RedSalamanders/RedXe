#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/GpuWidget.h"
#include "PlugInterfaces/Widget.h"
#include "PlugInterfaces/WindowWidget.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <strsafe.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr char kPluginId[] = "builtin.rotating-triangle";
constexpr char kWidgetTypeId[] = "rotating-triangle";

template <typename Function> [[nodiscard]] Function ResolveFunction(HMODULE module, const char* name) noexcept
{
    const FARPROC procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

[[nodiscard]] HRESULT BuildPluginPath(std::array<wchar_t, 1024>& path) noexcept
{
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
    return StringCchCopyW(separator + 1, path.size() - prefixLength, L"Plugins\\RotatingTriangle.dll");
}

[[nodiscard]] HRESULT RunContractTests() noexcept
{
    std::array<wchar_t, 1024> path{};
    HRESULT result = BuildPluginPath(path);
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
} // namespace

int wmain()
{
    const HRESULT result = RunContractTests();
    if (FAILED(result))
    {
        return static_cast<int>(result & 0xFF);
    }
    return 0;
}
