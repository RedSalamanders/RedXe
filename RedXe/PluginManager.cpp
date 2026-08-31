#include "PluginManager.h"

#include "PlugInterfaces/Factory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <memory>
#include <new>
#include <strsafe.h>
#include <utility>

namespace
{
constexpr char kPluginId[] = "builtin.rotating-triangle";
constexpr char kWidgetTypeId[] = "rotating-triangle";
constexpr std::size_t kInitialPathCapacity = 512;
constexpr std::size_t kMaximumPathCapacity = 32768;

template <typename Function> [[nodiscard]] Function ResolveFunction(HMODULE module, const char* name) noexcept
{
    const FARPROC procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

[[nodiscard]] HRESULT BuildBundledPluginPath(wchar_t* path, std::size_t capacity) noexcept
{
    if (!path || capacity == 0 || capacity > static_cast<std::size_t>(MAXDWORD))
    {
        return E_INVALIDARG;
    }

    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(capacity));
    if (length == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (length >= capacity - 1)
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    wchar_t* separator = std::wcsrchr(path, L'\\');
    if (!separator)
    {
        return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
    }

    const std::size_t prefixLength = static_cast<std::size_t>(separator - path) + 1;
    return StringCchCopyW(separator + 1, capacity - prefixLength, L"Plugins\\RotatingTriangle.dll");
}

[[nodiscard]] HRESULT ValidateMetadata(const RedXePluginMetadata* metadata, std::uint32_t count,
                                       const char*& selectedPluginId) noexcept
{
    selectedPluginId = nullptr;
    if (!metadata || count == 0 || count > 256)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    for (std::uint32_t index = 0; index < count; ++index)
    {
        const RedXePluginMetadata& candidate = metadata[index];
        if (candidate.sizeBytes < sizeof(RedXePluginMetadata) || !RedXeIsValidMachineId(candidate.id) ||
            !candidate.displayName || !candidate.description || !candidate.author || !candidate.version)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        for (std::uint32_t previous = 0; previous < index; ++previous)
        {
            if (RedXeAsciiEqualsIgnoreCase(metadata[previous].id, candidate.id))
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
        }
        if (RedXeAsciiEqualsIgnoreCase(candidate.id, kPluginId))
        {
            if ((candidate.capabilities & RedXePluginCapabilityWidgetProvider) == 0)
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            selectedPluginId = candidate.id;
        }
    }

    return selectedPluginId ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

[[nodiscard]] HRESULT FindWidgetType(IRedXeWidgetProvider& provider,
                                     const RedXeWidgetTypeDescriptor*& selectedType) noexcept
{
    selectedType = nullptr;
    const RedXeWidgetTypeDescriptor* types = nullptr;
    std::uint32_t count = 0;
    HRESULT result = provider.GetWidgetTypes(&types, &count);
    if (FAILED(result))
    {
        return result;
    }
    if (!types || count == 0 || count > 256)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    for (std::uint32_t index = 0; index < count; ++index)
    {
        const RedXeWidgetTypeDescriptor& candidate = types[index];
        if (candidate.sizeBytes < sizeof(RedXeWidgetTypeDescriptor) || !RedXeIsValidMachineId(candidate.typeId) ||
            !candidate.displayName || !candidate.description || candidate.minimumWidth <= 0.0f ||
            candidate.minimumHeight <= 0.0f)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        for (std::uint32_t previous = 0; previous < index; ++previous)
        {
            if (RedXeAsciiEqualsIgnoreCase(types[previous].typeId, candidate.typeId))
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
        }
        if (RedXeAsciiEqualsIgnoreCase(candidate.typeId, kWidgetTypeId))
        {
            selectedType = &candidate;
        }
    }

    return selectedType ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}
} // namespace

PluginManager::~PluginManager()
{
    for (std::size_t index = 0; index < _widgetCount; ++index)
    {
        _widgets[index].windowWidget.reset();
        _widgets[index].gpuWidget.reset();
        _widgets[index].widget.reset();
    }
    _widgetCount = 0;
    _provider.reset();
    if (_shutdown)
    {
        _shutdown();
    }

    // v1 keeps plugin modules mapped until process teardown so no stale function pointer can outlive its DLL.
    if (_module)
    {
        (void)_module.release();
    }
}

HRESULT PluginManager::Initialize(std::uint32_t widgetInstanceCount) noexcept
{
    if (_module || widgetInstanceCount < 2 || widgetInstanceCount > kMaximumWidgetInstances)
    {
        return E_INVALIDARG;
    }

    std::array<wchar_t, kInitialPathCapacity> shortPath{};
    std::unique_ptr<wchar_t[]> longPath;
    wchar_t* pluginPath = shortPath.data();
    std::size_t pathCapacity = shortPath.size();
    HRESULT result = BuildBundledPluginPath(pluginPath, pathCapacity);
    if (result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
    {
        longPath.reset(new (std::nothrow) wchar_t[kMaximumPathCapacity]);
        if (!longPath)
        {
            return E_OUTOFMEMORY;
        }
        pluginPath = longPath.get();
        pathCapacity = kMaximumPathCapacity;
        result = BuildBundledPluginPath(pluginPath, pathCapacity);
    }
    if (FAILED(result))
    {
        return result;
    }

    wil::unique_hmodule module{
        LoadLibraryExW(pluginPath, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    if (!module)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const RedXeCreateFn create = ResolveFunction<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    if (!create)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    const RedXeEnumeratePluginsFn enumerate =
        ResolveFunction<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    const char* requestedPluginId = nullptr;
    if (enumerate)
    {
        const RedXePluginMetadata* metadata = nullptr;
        std::uint32_t metadataCount = 0;
        result = enumerate(&metadata, &metadataCount);
        if (FAILED(result))
        {
            return result;
        }
        result = ValidateMetadata(metadata, metadataCount, requestedPluginId);
        if (FAILED(result))
        {
            return result;
        }
    }

    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
#if defined(_DEBUG)
    options.debugLevel = 1;
#endif

    void* providerObject = nullptr;
    result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, requestedPluginId, &providerObject);
    if (FAILED(result))
    {
        return result;
    }
    if (!providerObject)
    {
        return E_UNEXPECTED;
    }

    wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
    provider.attach(static_cast<IRedXeWidgetProvider*>(providerObject));

    const RedXeWidgetTypeDescriptor* widgetType = nullptr;
    result = FindWidgetType(*provider, widgetType);
    if (FAILED(result))
    {
        return result;
    }

    std::array<WidgetSlot, kMaximumWidgetInstances> widgets;
    constexpr std::array instanceIds{
        "triangle.1", "triangle.2", "triangle.3", "triangle.4", "triangle.5", "triangle.6", "triangle.7", "triangle.8",
    };
    for (std::uint32_t index = 0; index < widgetInstanceCount; ++index)
    {
        result = provider->CreateWidget(kWidgetTypeId, instanceIds[index], widgets[index].widget.put());
        if (FAILED(result))
        {
            return result;
        }
        const HRESULT gpuResult = widgets[index].widget.query_to(widgets[index].gpuWidget.put());
        const HRESULT windowResult = widgets[index].widget.query_to(widgets[index].windowWidget.put());
        if (gpuResult != S_OK && gpuResult != E_NOINTERFACE)
        {
            return gpuResult;
        }
        if (windowResult != S_OK && windowResult != E_NOINTERFACE)
        {
            return windowResult;
        }
        if (!widgets[index].gpuWidget && !widgets[index].windowWidget)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        widgets[index].flags = widgetType->flags;
    }

    _shutdown = ResolveFunction<RedXePluginShutdownFn>(module.get(), kRedXePluginShutdownExport);
    _module = std::move(module);
    _provider = std::move(provider);
    _widgets = std::move(widgets);
    _widgetCount = widgetInstanceCount;
    return S_OK;
}

std::size_t PluginManager::WidgetCount() const noexcept
{
    return _widgetCount;
}

IRedXeWidget* PluginManager::WidgetAt(std::size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].widget.get() : nullptr;
}

IRedXeGpuWidget* PluginManager::GpuWidgetAt(std::size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].gpuWidget.get() : nullptr;
}

IRedXeWindowWidget* PluginManager::WindowWidgetAt(std::size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].windowWidget.get() : nullptr;
}

std::uint32_t PluginManager::WidgetFlagsAt(std::size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].flags : RedXeWidgetFlagNone;
}
