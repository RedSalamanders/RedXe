#include "PluginManager.h"

#include "PlugInterfaces/Factory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <strsafe.h>
#include <utility>

namespace
{
constexpr wchar_t kPluginId[] = L"builtin.rotating-triangle";
constexpr wchar_t kWidgetTypeId[] = L"rotating-triangle";
constexpr float kDesignWidth = 2560.0f;
constexpr float kDesignHeight = 720.0f;
constexpr float kOuterMargin = 40.0f;
constexpr float kGap = 24.0f;

template <typename Function> [[nodiscard]] Function ResolveFunction(HMODULE module, const char* name) noexcept
{
    const FARPROC procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

class WidgetTypeSink final : public IRedXeWidgetTypeSink
{
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeWidgetTypeSink))
        {
            *result = static_cast<IRedXeWidgetTypeSink*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return 2;
    }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        return 1;
    }

    HRESULT STDMETHODCALLTYPE AddWidgetType(const RedXeWidgetTypeDescriptor* descriptor) noexcept override
    {
        if (!descriptor || descriptor->sizeBytes < offsetof(RedXeWidgetTypeDescriptor, reserved) || !descriptor->typeId)
        {
            return E_INVALIDARG;
        }
        if (CompareStringOrdinal(descriptor->typeId, -1, kWidgetTypeId, -1, TRUE) == CSTR_EQUAL)
        {
            if (descriptor->renderPath != RedXeWidgetRenderPathStandard ||
                (descriptor->flags & RedXeWidgetFlagContinuousAnimation) == 0)
            {
                return E_INVALIDARG;
            }
            _found = true;
        }
        return S_OK;
    }

    [[nodiscard]] bool Found() const noexcept
    {
        return _found;
    }

  private:
    bool _found = false;
};

[[nodiscard]] HRESULT BuildBundledPluginPath(std::array<wchar_t, 32768>& path) noexcept
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

[[nodiscard]] HRESULT ValidateFactoryContract(RedXeCreateFn create, RedXeEnumeratePluginsFn enumerate) noexcept
{
    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);

    if (create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kPluginId, nullptr) != E_POINTER)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    void* object = reinterpret_cast<void*>(1);
    HRESULT result = create(__uuidof(IRedXeHost), &options, nullptr, kPluginId, &object);
    if (result != E_NOINTERFACE || object)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    object = reinterpret_cast<void*>(1);
    result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, L"missing.plugin", &object);
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

    object = nullptr;
    result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, nullptr, &object);
    if (FAILED(result) || !object)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    static_cast<IRedXeWidgetProvider*>(object)->Release();

    const RedXePluginMetadata* metadata = reinterpret_cast<const RedXePluginMetadata*>(1);
    std::uint32_t count = 1;
    result = enumerate(nullptr, &count);
    if (result != E_POINTER || count != 0)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    result = enumerate(&metadata, nullptr);
    if (result != E_POINTER || metadata)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}
} // namespace

PluginManager::~PluginManager()
{
    for (std::size_t index = 0; index < _widgetCount; ++index)
    {
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

HRESULT PluginManager::Initialize(std::uint32_t widgetInstanceCount, bool validateFactoryContract) noexcept
{
    if (_module || widgetInstanceCount < 2 || widgetInstanceCount > kMaximumWidgetInstances)
    {
        return E_INVALIDARG;
    }

    std::array<wchar_t, 32768> path{};
    HRESULT result = BuildBundledPluginPath(path);
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

    if (validateFactoryContract)
    {
        result = ValidateFactoryContract(create, enumerate);
        if (FAILED(result))
        {
            return result;
        }
    }

    const RedXePluginMetadata* metadata = nullptr;
    std::uint32_t metadataCount = 0;
    result = enumerate(&metadata, &metadataCount);
    if (FAILED(result))
    {
        return result;
    }
    if (!metadata || metadataCount == 0 || metadataCount > 256 ||
        metadata[0].sizeBytes < offsetof(RedXePluginMetadata, reserved) || !metadata[0].id ||
        CompareStringOrdinal(metadata[0].id, -1, kPluginId, -1, TRUE) != CSTR_EQUAL ||
        (metadata[0].capabilities & RedXePluginCapabilityWidgetProvider) == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
#if defined(_DEBUG)
    options.debugLevel = 1;
#endif

    void* providerObject = nullptr;
    result = create(__uuidof(IRedXeWidgetProvider), &options, nullptr, kPluginId, &providerObject);
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

    WidgetTypeSink typeSink;
    result = provider->EnumerateWidgetTypes(&typeSink);
    if (FAILED(result))
    {
        return result;
    }
    if (!typeSink.Found())
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    std::array<WidgetSlot, kMaximumWidgetInstances> widgets;
    constexpr std::array instanceIds{
        L"triangle.1", L"triangle.2", L"triangle.3", L"triangle.4",
        L"triangle.5", L"triangle.6", L"triangle.7", L"triangle.8",
    };

    const std::uint32_t columns = widgetInstanceCount == 2 ? 2U : 2U;
    const std::uint32_t rows = (widgetInstanceCount + columns - 1U) / columns;
    const float cellWidth =
        (kDesignWidth - 2.0f * kOuterMargin - kGap * static_cast<float>(columns - 1U)) / static_cast<float>(columns);
    const float cellHeight =
        (kDesignHeight - 2.0f * kOuterMargin - kGap * static_cast<float>(rows - 1U)) / static_cast<float>(rows);

    for (std::uint32_t index = 0; index < widgetInstanceCount; ++index)
    {
        result = provider->CreateWidget(kWidgetTypeId, instanceIds[index], widgets[index].widget.put());
        if (FAILED(result))
        {
            return result;
        }
        widgets[index].placement = WidgetPlacement{
            kOuterMargin + static_cast<float>(index % columns) * (cellWidth + kGap),
            kOuterMargin + static_cast<float>(index / columns) * (cellHeight + kGap),
            cellWidth,
            cellHeight,
        };
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

WidgetPlacement PluginManager::PlacementAt(std::size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].placement : WidgetPlacement{};
}
