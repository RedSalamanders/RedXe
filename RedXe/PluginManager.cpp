#include "PluginManager.h"

#include "PlugInterfaces/Factory.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <memory>
#include <new>
#include <strsafe.h>
#include <utility>

#include <yyjson.h>

namespace
{
using unique_contract_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
constexpr char kTrianglePluginId[] = "builtin.rotating-triangle";
constexpr char kTriangleWidgetTypeId[] = "rotating-triangle";
constexpr char kGdiPluginId[] = "builtin.gdi-orbit";
constexpr char kGdiWidgetTypeId[] = "gdi-orbit";
constexpr char kMatrixPluginId[] = "builtin.matrix-rain";
constexpr char kMatrixWidgetTypeId[] = "matrix-rain";
constexpr std::size_t kInitialPathCapacity = 512;
constexpr std::size_t kMaximumPathCapacity = 32768;

struct BundledPluginSpec final
{
    const char* pluginId;
    const char* typeId;
    const wchar_t* moduleName;
    std::size_t moduleIndex;
};

constexpr std::array kBundledPlugins{
    BundledPluginSpec{kTrianglePluginId, kTriangleWidgetTypeId, L"RotatingTriangle.dll", 0},
    BundledPluginSpec{kGdiPluginId, kGdiWidgetTypeId, L"GdiOrbit.dll", 1},
    BundledPluginSpec{kMatrixPluginId, kMatrixWidgetTypeId, L"MatrixRain.dll", 2},
};
static_assert(kBundledPlugins.size() <= kMaximumSettingsPlugins);

[[nodiscard]] const BundledPluginSpec* FindBundledPlugin(std::string_view pluginId, std::string_view typeId) noexcept
{
    for (const BundledPluginSpec& candidate : kBundledPlugins)
    {
        if (SettingsIdEquals(pluginId, candidate.pluginId) && SettingsIdEquals(typeId, candidate.typeId))
        {
            return &candidate;
        }
    }
    return nullptr;
}

[[nodiscard]] const BundledPluginSpec* FindBundledPlugin(std::string_view pluginId) noexcept
{
    for (const BundledPluginSpec& candidate : kBundledPlugins)
    {
        if (SettingsIdEquals(pluginId, candidate.pluginId))
        {
            return &candidate;
        }
    }
    return nullptr;
}

[[nodiscard]] bool IsHexColor(yyjson_val* value) noexcept
{
    const char* text = yyjson_is_str(value) ? yyjson_get_str(value) : nullptr;
    if (!text || yyjson_get_len(value) != 7 || text[0] != '#')
    {
        return false;
    }
    for (std::size_t index = 1; index < 7; ++index)
    {
        if (!std::isxdigit(static_cast<unsigned char>(text[index])))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidateValueAgainstPublishedSchema(yyjson_val* schema, yyjson_val* value) noexcept
{
    if (!yyjson_is_obj(schema))
    {
        return false;
    }
    yyjson_val* type = yyjson_obj_get(schema, "type");
    if (!yyjson_is_str(type))
    {
        return false;
    }
    const std::string_view typeName{yyjson_get_str(type), yyjson_get_len(type)};
    if (typeName == "object")
    {
        if (!yyjson_is_obj(value))
        {
            return false;
        }
        yyjson_val* properties = yyjson_obj_get(schema, "properties");
        if (properties && !yyjson_is_obj(properties))
        {
            return false;
        }
        yyjson_val* additional = yyjson_obj_get(schema, "additionalProperties");
        if (additional && !yyjson_is_bool(additional))
        {
            return false;
        }
        yyjson_obj_iter iterator = yyjson_obj_iter_with(value);
        while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
        {
            yyjson_val* propertySchema =
                properties ? yyjson_obj_getn(properties, yyjson_get_str(key), yyjson_get_len(key)) : nullptr;
            if (!propertySchema)
            {
                if (!additional || !yyjson_get_bool(additional))
                {
                    return false;
                }
                continue;
            }
            if (!ValidateValueAgainstPublishedSchema(propertySchema, yyjson_obj_iter_get_val(key)))
            {
                return false;
            }
        }
        return true;
    }
    if (typeName == "integer")
    {
        if (!yyjson_is_uint(value))
        {
            return false;
        }
        const std::uint64_t number = yyjson_get_uint(value);
        yyjson_val* minimum = yyjson_obj_get(schema, "minimum");
        yyjson_val* maximum = yyjson_obj_get(schema, "maximum");
        return (!minimum || (yyjson_is_uint(minimum) && number >= yyjson_get_uint(minimum))) &&
               (!maximum || (yyjson_is_uint(maximum) && number <= yyjson_get_uint(maximum)));
    }
    if (typeName == "string")
    {
        if (!yyjson_is_str(value))
        {
            return false;
        }
        yyjson_val* pattern = yyjson_obj_get(schema, "pattern");
        if (!pattern)
        {
            return true;
        }
        return yyjson_is_str(pattern) &&
               std::string_view{yyjson_get_str(pattern), yyjson_get_len(pattern)} == "^#[0-9A-Fa-f]{6}$" &&
               IsHexColor(value);
    }
    return false;
}

template <typename Function> [[nodiscard]] Function ResolveFunction(HMODULE module, const char* name) noexcept
{
    const FARPROC procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

[[nodiscard]] HRESULT BuildBundledPluginPath(const wchar_t* moduleName, wchar_t* path, std::size_t capacity) noexcept
{
    if (!moduleName || moduleName[0] == L'\0' || !path || capacity == 0 ||
        capacity > static_cast<std::size_t>(MAXDWORD))
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
    HRESULT result = StringCchCopyW(separator + 1, capacity - prefixLength, L"Plugins\\");
    if (FAILED(result))
    {
        return result;
    }
    return StringCchCatW(separator + 1, capacity - prefixLength, moduleName);
}

[[nodiscard]] HRESULT ValidateMetadata(const RedXePluginMetadata* metadata, std::uint32_t count,
                                       const char* expectedPluginId) noexcept
{
    if (!metadata || count == 0 || count > 256 || !RedXeIsValidMachineId(expectedPluginId))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    bool selected = false;
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
        if (RedXeAsciiEqualsIgnoreCase(candidate.id, expectedPluginId))
        {
            if ((candidate.capabilities & RedXePluginCapabilityWidgetProvider) == 0)
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            selected = true;
        }
    }
    return selected ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

[[nodiscard]] HRESULT FindWidgetType(IRedXeWidgetProvider& provider, const char* expectedTypeId,
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
    if (!types || count == 0 || count > 256 || !RedXeIsValidMachineId(expectedTypeId))
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
        if (RedXeAsciiEqualsIgnoreCase(candidate.typeId, expectedTypeId))
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

    for (std::size_t index = 0; index < _providerCount; ++index)
    {
        _providers[index].provider.reset();
    }
    _providerCount = 0;

    for (std::size_t index = _modules.size(); index > 0; --index)
    {
        ModuleSlot& slot = _modules[index - 1];
        if (slot.shutdown)
        {
            slot.shutdown();
        }
        slot.shutdown = nullptr;
        slot.getSettingsContract = nullptr;
        slot.create = nullptr;
        if (slot.module)
        {
            (void)slot.module.release();
        }
    }
    _initialized = false;
}

HRESULT PluginManager::LoadBundledModule(const wchar_t* moduleName, const char* pluginId,
                                         ModuleSlot& moduleSlot) noexcept
{
    if (!moduleName || !RedXeIsValidMachineId(pluginId) || moduleSlot.module || moduleSlot.create ||
        moduleSlot.getSettingsContract || moduleSlot.shutdown)
    {
        return E_INVALIDARG;
    }

    std::array<wchar_t, kInitialPathCapacity> shortPath{};
    std::unique_ptr<wchar_t[]> longPath;
    wchar_t* pluginPath = shortPath.data();
    std::size_t pathCapacity = shortPath.size();
    HRESULT result = BuildBundledPluginPath(moduleName, pluginPath, pathCapacity);
    if (result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
    {
        longPath.reset(new (std::nothrow) wchar_t[kMaximumPathCapacity]);
        if (!longPath)
        {
            return E_OUTOFMEMORY;
        }
        pluginPath = longPath.get();
        pathCapacity = kMaximumPathCapacity;
        result = BuildBundledPluginPath(moduleName, pluginPath, pathCapacity);
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

    const RedXeCreateFn createFunction = ResolveFunction<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    if (!createFunction)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    const RedXeGetPluginSettingsContractFn getSettingsContract =
        ResolveFunction<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);
    if (!getSettingsContract)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    const RedXePluginSettingsContract* settingsContract = nullptr;
    result = getSettingsContract(pluginId, &settingsContract);
    if (FAILED(result) || !settingsContract || settingsContract->sizeBytes < sizeof(*settingsContract) ||
        settingsContract->versionMajor != 1 || !settingsContract->schemaJsonUtf8 ||
        settingsContract->schemaBytes == 0 || settingsContract->schemaBytes > kPrivateConfigurationCapacity ||
        !settingsContract->defaultsJsonUtf8 || settingsContract->defaultsBytes == 0 ||
        settingsContract->defaultsBytes > kPrivateConfigurationCapacity)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    unique_contract_doc schemaDocument{
        yyjson_read(settingsContract->schemaJsonUtf8, settingsContract->schemaBytes, YYJSON_READ_NOFLAG)};
    unique_contract_doc defaultsDocument{
        yyjson_read(settingsContract->defaultsJsonUtf8, settingsContract->defaultsBytes, YYJSON_READ_NOFLAG)};
    if (!schemaDocument || !defaultsDocument || !yyjson_is_obj(yyjson_doc_get_root(schemaDocument.get())) ||
        !yyjson_is_obj(yyjson_doc_get_root(defaultsDocument.get())) ||
        !ValidateValueAgainstPublishedSchema(yyjson_doc_get_root(schemaDocument.get()),
                                             yyjson_doc_get_root(defaultsDocument.get())))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    const RedXeEnumeratePluginsFn enumerate =
        ResolveFunction<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    if (enumerate)
    {
        const RedXePluginMetadata* metadata = nullptr;
        std::uint32_t metadataCount = 0;
        result = enumerate(&metadata, &metadataCount);
        if (SUCCEEDED(result))
        {
            result = ValidateMetadata(metadata, metadataCount, pluginId);
        }
        if (FAILED(result))
        {
            return result;
        }
    }

    moduleSlot.shutdown = ResolveFunction<RedXePluginShutdownFn>(module.get(), kRedXePluginShutdownExport);
    moduleSlot.getSettingsContract = getSettingsContract;
    moduleSlot.create = createFunction;
    moduleSlot.module = std::move(module);
    return S_OK;
}

HRESULT PluginManager::CreateBundledProvider(ModuleSlot& moduleSlot, const char* pluginId,
                                             const char* configurationJson, std::uint32_t configurationBytes,
                                             IRedXeWidgetProvider** provider) noexcept
{
    if (!provider)
    {
        return E_POINTER;
    }
    *provider = nullptr;
    if (!moduleSlot.module || !moduleSlot.create || !RedXeIsValidMachineId(pluginId) || !configurationJson ||
        configurationBytes == 0 || configurationBytes > kRedXeMaximumFactoryConfigurationBytes)
    {
        return E_INVALIDARG;
    }

    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
#if defined(_DEBUG)
    options.debugLevel = 1;
#endif
    options.configurationJsonUtf8 = configurationJson;
    options.configurationBytes = configurationBytes;

    void* providerObject = nullptr;
    const HRESULT result =
        moduleSlot.create(__uuidof(IRedXeWidgetProvider), &options, nullptr, pluginId, &providerObject);
    if (FAILED(result))
    {
        return result;
    }
    if (!providerObject)
    {
        return E_UNEXPECTED;
    }
    *provider = static_cast<IRedXeWidgetProvider*>(providerObject);
    return S_OK;
}

HRESULT PluginManager::CreateWidgetInstance(IRedXeWidgetProvider& provider, const WidgetInstanceSettings& settings,
                                            WidgetSlot& widgetSlot) noexcept
{
    if (!RedXeIsValidMachineId(settings.typeId.utf8.data()) || !RedXeIsValidMachineId(settings.id.utf8.data()) ||
        widgetSlot.widget || widgetSlot.gpuWidget || widgetSlot.windowWidget)
    {
        return E_INVALIDARG;
    }

    const RedXeWidgetTypeDescriptor* widgetType = nullptr;
    HRESULT result = FindWidgetType(provider, settings.typeId.utf8.data(), widgetType);
    if (FAILED(result))
    {
        return result;
    }

    result = provider.CreateWidget(settings.typeId.utf8.data(), settings.id.utf8.data(), widgetSlot.widget.put());
    if (FAILED(result))
    {
        return result;
    }

    const HRESULT gpuResult = widgetSlot.widget.query_to(widgetSlot.gpuWidget.put());
    const HRESULT windowResult = widgetSlot.widget.query_to(widgetSlot.windowWidget.put());
    if (gpuResult != S_OK && gpuResult != E_NOINTERFACE)
    {
        return gpuResult;
    }
    if (windowResult != S_OK && windowResult != E_NOINTERFACE)
    {
        return windowResult;
    }
    if (!widgetSlot.gpuWidget && !widgetSlot.windowWidget)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    if (widgetSlot.gpuWidget)
    {
        widgetSlot.windowWidget.reset();
    }
    widgetSlot.instanceId = settings.id;
    widgetSlot.placement = settings.placement;
    widgetSlot.adaptivePlacement = settings.adaptivePlacement;
    widgetSlot.usesAdaptivePlacement = settings.usesAdaptivePlacement;
    widgetSlot.flags = widgetType->flags;
    return S_OK;
}

HRESULT PluginManager::StageActivePage(const AppSettings& settings,
                                       std::array<ModuleSlot, kMaximumModules>& loadedModules,
                                       std::array<ProviderSlot, kMaximumWidgetInstances>& providers,
                                       std::array<ProviderBuildKey, kMaximumWidgetInstances>& providerKeys,
                                       std::size_t& providerCount,
                                       std::array<WidgetSlot, kMaximumWidgetInstances>& widgets,
                                       std::size_t& widgetCount) noexcept
{
    providerCount = 0;
    widgetCount = 0;
    HRESULT result = ValidateAppSettings(settings);
    if (FAILED(result))
    {
        return result;
    }

    // Static discovery covers every effective widget in the document. This maps each referenced module once and
    // validates its immutable settings contract without creating providers or widget resources.
    for (std::uint32_t pluginIndex = 0; pluginIndex < settings.pluginCount; ++pluginIndex)
    {
        const PluginSettings& plugin = settings.plugins[pluginIndex];
        if (!plugin.enabled)
        {
            continue;
        }
        const BundledPluginSpec* spec = FindBundledPlugin(plugin.id.View());
        if (!spec || spec->moduleIndex >= loadedModules.size())
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        if (!_modules[spec->moduleIndex].module && !loadedModules[spec->moduleIndex].module)
        {
            result = LoadBundledModule(spec->moduleName, spec->pluginId, loadedModules[spec->moduleIndex]);
            if (FAILED(result))
            {
                return result;
            }
        }
    }

    for (std::uint32_t pageIndex = 0; pageIndex < settings.dashboard.pageCount; ++pageIndex)
    {
        const DashboardPageSettings& candidatePage = settings.dashboard.pages[pageIndex];
        for (std::uint32_t widgetIndex = 0; widgetIndex < candidatePage.widgetCount; ++widgetIndex)
        {
            const WidgetInstanceSettings& widget = candidatePage.widgets[widgetIndex];
            const BundledPluginSpec* spec = FindBundledPlugin(widget.pluginId.View(), widget.typeId.View());
            if (!spec)
            {
                return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            }
            ModuleSlot& module =
                _modules[spec->moduleIndex].module ? _modules[spec->moduleIndex] : loadedModules[spec->moduleIndex];
            const RedXePluginSettingsContract* contract = nullptr;
            if (!module.getSettingsContract || FAILED(module.getSettingsContract(spec->pluginId, &contract)) ||
                !contract)
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            unique_contract_doc schemaDocument{
                yyjson_read(contract->schemaJsonUtf8, contract->schemaBytes, YYJSON_READ_NOFLAG)};
            unique_contract_doc valueDocument{yyjson_read(widget.privateConfiguration.utf8.data(),
                                                          widget.privateConfiguration.bytes, YYJSON_READ_NOFLAG)};
            if (!schemaDocument || !valueDocument ||
                !ValidateValueAgainstPublishedSchema(yyjson_doc_get_root(schemaDocument.get()),
                                                     yyjson_doc_get_root(valueDocument.get())))
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
    }

    const DashboardPageSettings* page = SUCCEEDED(result) ? FindActiveDashboardPage(settings) : nullptr;
    if (FAILED(result) || !page || page->widgetCount > kMaximumWidgetInstances)
    {
        return FAILED(result) ? result : E_INVALIDARG;
    }
    if (page->widgetCount == 0)
    {
        return S_OK;
    }

    for (std::uint32_t index = 0; index < page->widgetCount; ++index)
    {
        const WidgetInstanceSettings& instance = page->widgets[index];
        const PluginSettings* plugin = FindPluginSettings(settings, instance.pluginId.View());
        const BundledPluginSpec* spec = FindBundledPlugin(instance.pluginId.View(), instance.typeId.View());
        if (!plugin || !plugin->enabled || !spec || spec->moduleIndex >= _modules.size())
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }

        std::array<char, kFactoryConfigurationCapacity> configuration{};
        std::uint32_t configurationBytes = 0;
        result = SerializeFactoryConfigurationJson(*plugin, instance, configuration, configurationBytes);
        if (FAILED(result) || configurationBytes > kRedXeMaximumFactoryConfigurationBytes)
        {
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }

        std::size_t providerIndex = providerCount;
        for (std::size_t candidate = 0; candidate < providerCount; ++candidate)
        {
            const ProviderBuildKey& key = providerKeys[candidate];
            if (key.moduleIndex == spec->moduleIndex && key.configurationBytes == configurationBytes &&
                std::memcmp(key.configuration.data(), configuration.data(), configurationBytes) == 0)
            {
                providerIndex = candidate;
                break;
            }
        }

        if (providerIndex == providerCount)
        {
            if (providerCount >= providers.size())
            {
                return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
            }
            ModuleSlot* module = nullptr;
            if (_modules[spec->moduleIndex].module)
            {
                module = &_modules[spec->moduleIndex];
            }
            else
            {
                module = &loadedModules[spec->moduleIndex];
                if (!module->module)
                {
                    result = LoadBundledModule(spec->moduleName, spec->pluginId, *module);
                    if (FAILED(result))
                    {
                        return result;
                    }
                }
            }

            result = CreateBundledProvider(*module, spec->pluginId, configuration.data(), configurationBytes,
                                           providers[providerCount].provider.put());
            if (FAILED(result))
            {
                return result;
            }
            ProviderBuildKey& key = providerKeys[providerCount];
            key.configuration = configuration;
            key.configurationBytes = configurationBytes;
            key.moduleIndex = spec->moduleIndex;
            ++providerCount;
        }

        result = CreateWidgetInstance(*providers[providerIndex].provider, instance, widgets[index]);
        if (FAILED(result))
        {
            return result;
        }
        ++widgetCount;
    }
    return S_OK;
}

HRESULT PluginManager::Initialize(const AppSettings& settings) noexcept
{
    if (_initialized || _widgetCount != 0 || _providerCount != 0)
    {
        return E_INVALIDARG;
    }

    std::array<ModuleSlot, kMaximumModules> loadedModules;
    std::array<ProviderSlot, kMaximumWidgetInstances> providers;
    std::array<ProviderBuildKey, kMaximumWidgetInstances> providerKeys;
    std::array<WidgetSlot, kMaximumWidgetInstances> widgets;
    std::size_t providerCount = 0;
    std::size_t widgetCount = 0;
    const HRESULT result =
        StageActivePage(settings, loadedModules, providers, providerKeys, providerCount, widgets, widgetCount);
    if (FAILED(result))
    {
        return result;
    }

    _modules = std::move(loadedModules);
    _providers = std::move(providers);
    _widgets = std::move(widgets);
    _providerCount = providerCount;
    _widgetCount = widgetCount;
    _gridColumns = settings.dashboard.gridColumns;
    _gridRows = settings.dashboard.gridRows;
    _initialized = true;
    return S_OK;
}

HRESULT PluginManager::Reconfigure(const AppSettings& settings) noexcept
{
    if (!_initialized)
    {
        return E_INVALIDARG;
    }
    std::array<ModuleSlot, kMaximumModules> loadedModules;
    std::array<ProviderSlot, kMaximumWidgetInstances> providers;
    std::array<ProviderBuildKey, kMaximumWidgetInstances> providerKeys;
    std::array<WidgetSlot, kMaximumWidgetInstances> widgets;
    std::size_t providerCount = 0;
    std::size_t widgetCount = 0;
    const HRESULT result =
        StageActivePage(settings, loadedModules, providers, providerKeys, providerCount, widgets, widgetCount);
    if (FAILED(result))
    {
        return result;
    }

    for (std::size_t index = 0; index < _modules.size(); ++index)
    {
        if (loadedModules[index].module)
        {
            _modules[index] = std::move(loadedModules[index]);
        }
    }
    _widgets = std::move(widgets);
    _widgetCount = widgetCount;
    _providers = std::move(providers);
    _providerCount = providerCount;
    _gridColumns = settings.dashboard.gridColumns;
    _gridRows = settings.dashboard.gridRows;
    return S_OK;
}

std::size_t PluginManager::ProviderCount() const noexcept
{
    return _providerCount;
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

WidgetGridPlacement PluginManager::WidgetGridPlacementAt(std::size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].placement : WidgetGridPlacement{};
}

AdaptiveWidgetPlacement PluginManager::AdaptivePlacementAt(std::size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].adaptivePlacement : AdaptiveWidgetPlacement{};
}

bool PluginManager::UsesAdaptivePlacementAt(std::size_t index) const noexcept
{
    return index < _widgetCount && _widgets[index].usesAdaptivePlacement;
}

const char* PluginManager::WidgetInstanceIdAt(std::size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].instanceId.utf8.data() : nullptr;
}

std::uint32_t PluginManager::GridColumns() const noexcept
{
    return _initialized ? _gridColumns : 0;
}

std::uint32_t PluginManager::GridRows() const noexcept
{
    return _initialized ? _gridRows : 0;
}
