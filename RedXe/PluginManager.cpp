#include "PluginManager.h"

#include "BundledPlugins.h"

#include "PlugInterfaces/Factory.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

#include <yyjson.h>

namespace
{
using unique_contract_doc = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;

static_assert(kRedXeBundledWidgets.size() <= kMaximumSettingsPlugins);

[[nodiscard]] const RedXeBundledPluginSpec* FindBundledPlugin(std::string_view pluginId) noexcept
{
    for (const RedXeBundledPluginSpec& candidate : kRedXeBundledPlugins)
    {
        if (SettingsIdEquals(pluginId, candidate.pluginId))
        {
            return &candidate;
        }
    }
    return nullptr;
}

[[nodiscard]] const RedXeBundledWidgetSpec* FindBundledWidget(std::string_view pluginId,
                                                              std::string_view typeId) noexcept
{
    for (const RedXeBundledWidgetSpec& candidate : kRedXeBundledWidgets)
    {
        if (SettingsIdEquals(pluginId, candidate.pluginId) && SettingsIdEquals(typeId, candidate.typeId))
        {
            return &candidate;
        }
    }
    return nullptr;
}

[[nodiscard]] const RedXeBundledWidgetSpec* FindBundledWidget(std::string_view pluginId) noexcept
{
    for (const RedXeBundledWidgetSpec& candidate : kRedXeBundledWidgets)
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
    for (size_t index = 1; index < 7; ++index)
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
        const uint64_t number = yyjson_get_uint(value);
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
        yyjson_val* allowedValues = yyjson_obj_get(schema, "enum");
        if (allowedValues)
        {
            if (!yyjson_is_arr(allowedValues))
            {
                return false;
            }
            bool matched = false;
            const std::string_view text{yyjson_get_str(value), yyjson_get_len(value)};
            const size_t count = yyjson_arr_size(allowedValues);
            for (size_t index = 0; index < count; ++index)
            {
                yyjson_val* candidate = yyjson_arr_get(allowedValues, index);
                matched = matched || (yyjson_is_str(candidate) &&
                                      text == std::string_view{yyjson_get_str(candidate), yyjson_get_len(candidate)});
            }
            if (!matched)
            {
                return false;
            }
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
    if (typeName == "boolean")
    {
        return yyjson_is_bool(value);
    }
    return false;
}

[[nodiscard]] HRESULT GetAndValidateSettingsContract(const PluginHost::ModuleView& module, const char* pluginId,
                                                     const RedXePluginSettingsContract** contract) noexcept
{
    if (contract)
    {
        *contract = nullptr;
    }
    if (!contract || !module.getSettingsContract || !RedXeIsValidMachineId(pluginId))
    {
        return E_INVALIDARG;
    }

    const RedXePluginSettingsContract* selected = nullptr;
    HRESULT result = module.getSettingsContract(pluginId, &selected);
    if (FAILED(result) || !selected || selected->sizeBytes != sizeof(*selected) || !selected->schemaJsonUtf8 ||
        selected->schemaBytes == 0 || selected->schemaBytes > kPrivateConfigurationCapacity ||
        !selected->defaultsJsonUtf8 || selected->defaultsBytes == 0 ||
        selected->defaultsBytes > kPrivateConfigurationCapacity)
    {
        return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    unique_contract_doc schemaDocument{
        yyjson_read(selected->schemaJsonUtf8, selected->schemaBytes, YYJSON_READ_NOFLAG)};
    unique_contract_doc defaultsDocument{
        yyjson_read(selected->defaultsJsonUtf8, selected->defaultsBytes, YYJSON_READ_NOFLAG)};
    if (!schemaDocument || !defaultsDocument || !yyjson_is_obj(yyjson_doc_get_root(schemaDocument.get())) ||
        !yyjson_is_obj(yyjson_doc_get_root(defaultsDocument.get())) ||
        !ValidateValueAgainstPublishedSchema(yyjson_doc_get_root(schemaDocument.get()),
                                             yyjson_doc_get_root(defaultsDocument.get())))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    *contract = selected;
    return S_OK;
}

[[nodiscard]] HRESULT FindWidgetType(IRedXeWidgetProvider& provider, const char* expectedTypeId,
                                     const RedXeWidgetTypeDescriptor*& selectedType) noexcept
{
    selectedType = nullptr;
    const RedXeWidgetTypeDescriptor* types = nullptr;
    uint32_t count = 0;
    HRESULT result = provider.GetWidgetTypes(&types, &count);
    if (FAILED(result))
    {
        return result;
    }
    if (!types || count == 0 || count > 256 || !RedXeIsValidMachineId(expectedTypeId))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    for (uint32_t index = 0; index < count; ++index)
    {
        const RedXeWidgetTypeDescriptor& candidate = types[index];
        if (candidate.sizeBytes != sizeof(RedXeWidgetTypeDescriptor) || !RedXeIsValidMachineId(candidate.typeId) ||
            !candidate.displayName || !candidate.description || candidate.minimumWidth <= 0.0f ||
            candidate.minimumHeight <= 0.0f)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        for (uint32_t previous = 0; previous < index; ++previous)
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
    for (size_t index = 0; index < _widgetCount; ++index)
    {
        _widgets[index].windowWidget.reset();
        _widgets[index].scheduledWidget.reset();
        _widgets[index].gpuWidget.reset();
        _widgets[index].raisedWidget.reset();
        _widgets[index].widget.reset();
    }
    _widgetCount = 0;

    for (size_t index = 0; index < _providerCount; ++index)
    {
        _providers[index].provider.reset();
    }
    _providerCount = 0;

    _initialized = false;
}

HRESULT PluginManager::CreateBundledProvider(const char* pluginId, const char* configurationJson,
                                             uint32_t configurationBytes, IRedXeWidgetProvider** provider) noexcept
{
    if (!provider)
    {
        return E_POINTER;
    }
    *provider = nullptr;
    if (!RedXeIsValidMachineId(pluginId) || !configurationJson || configurationBytes == 0 ||
        configurationBytes > kRedXeMaximumFactoryConfigurationBytes)
    {
        return E_INVALIDARG;
    }

    PluginHost::ModuleView module{};
    HRESULT result = _pluginHost.GetPluginModule(pluginId, RedXePluginCapabilityWidgetProvider, &module);
    if (FAILED(result))
    {
        return result;
    }

    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
#if defined(_DEBUG)
    options.debugLevel = 1;
#endif
    options.configurationJsonUtf8 = configurationJson;
    options.configurationBytes = configurationBytes;

    void* providerObject = nullptr;
    result =
        module.create(__uuidof(IRedXeWidgetProvider), &options, _pluginHost.Interface(), pluginId, &providerObject);
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
        widgetSlot.widget || widgetSlot.gpuWidget || widgetSlot.scheduledWidget || widgetSlot.windowWidget ||
        widgetSlot.raisedWidget)
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
    const HRESULT scheduledResult = widgetSlot.widget.query_to(widgetSlot.scheduledWidget.put());
    const HRESULT windowResult = widgetSlot.widget.query_to(widgetSlot.windowWidget.put());
    const HRESULT raisedResult = widgetSlot.widget.query_to(widgetSlot.raisedWidget.put());
    if (gpuResult != S_OK && gpuResult != E_NOINTERFACE)
    {
        return gpuResult;
    }
    if (windowResult != S_OK && windowResult != E_NOINTERFACE)
    {
        return windowResult;
    }
    if (scheduledResult != S_OK && scheduledResult != E_NOINTERFACE)
    {
        return scheduledResult;
    }
    if (raisedResult != S_OK && raisedResult != E_NOINTERFACE)
    {
        return raisedResult;
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
                                       std::array<ProviderSlot, kMaximumWidgetInstances>& providers,
                                       std::array<ProviderBuildKey, kMaximumWidgetInstances>& providerKeys,
                                       size_t& providerCount, std::array<WidgetSlot, kMaximumWidgetInstances>& widgets,
                                       size_t& widgetCount) noexcept
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
    for (uint32_t pluginIndex = 0; pluginIndex < settings.pluginCount; ++pluginIndex)
    {
        const PluginSettings& plugin = settings.plugins[pluginIndex];
        if (!plugin.enabled)
        {
            continue;
        }
        const RedXeBundledWidgetSpec* widgetSpec = FindBundledWidget(plugin.id.View());
        const RedXeBundledPluginSpec* pluginSpec = FindBundledPlugin(plugin.id.View());
        if (!widgetSpec || !pluginSpec)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        PluginHost::ModuleView module{};
        result = _pluginHost.GetPluginModule(pluginSpec->pluginId, RedXePluginCapabilityWidgetProvider, &module);
        if (FAILED(result))
        {
            return result;
        }
        const RedXePluginSettingsContract* contract = nullptr;
        result = GetAndValidateSettingsContract(module, pluginSpec->pluginId, &contract);
        if (FAILED(result))
        {
            return result;
        }
    }

    for (uint32_t pageIndex = 0; pageIndex < settings.dashboard.pageCount; ++pageIndex)
    {
        const DashboardPageSettings& candidatePage = settings.dashboard.pages[pageIndex];
        for (uint32_t widgetIndex = 0; widgetIndex < candidatePage.widgetCount; ++widgetIndex)
        {
            const WidgetInstanceSettings& widget = candidatePage.widgets[widgetIndex];
            const RedXeBundledWidgetSpec* widgetSpec = FindBundledWidget(widget.pluginId.View(), widget.typeId.View());
            const RedXeBundledPluginSpec* pluginSpec = FindBundledPlugin(widget.pluginId.View());
            if (!widgetSpec || !pluginSpec)
            {
                return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            }
            PluginHost::ModuleView module{};
            result = _pluginHost.GetPluginModule(pluginSpec->pluginId, RedXePluginCapabilityWidgetProvider, &module);
            if (FAILED(result))
            {
                return result;
            }
            const RedXePluginSettingsContract* contract = nullptr;
            if (FAILED(GetAndValidateSettingsContract(module, pluginSpec->pluginId, &contract)) || !contract)
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

    for (uint32_t index = 0; index < page->widgetCount; ++index)
    {
        const WidgetInstanceSettings& instance = page->widgets[index];
        const PluginSettings* plugin = FindPluginSettings(settings, instance.pluginId.View());
        const RedXeBundledWidgetSpec* widgetSpec = FindBundledWidget(instance.pluginId.View(), instance.typeId.View());
        const RedXeBundledPluginSpec* pluginSpec = FindBundledPlugin(instance.pluginId.View());
        if (!plugin || !plugin->enabled || !widgetSpec || !pluginSpec)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }

        std::array<char, kFactoryConfigurationCapacity> configuration{};
        uint32_t configurationBytes = 0;
        result = SerializeFactoryConfigurationJson(*plugin, instance, configuration, configurationBytes);
        if (FAILED(result) || configurationBytes > kRedXeMaximumFactoryConfigurationBytes)
        {
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }

        size_t providerIndex = providerCount;
        for (size_t candidate = 0; candidate < providerCount; ++candidate)
        {
            const ProviderBuildKey& key = providerKeys[candidate];
            if (key.pluginId == pluginSpec->pluginId && key.configurationBytes == configurationBytes &&
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
            result = CreateBundledProvider(pluginSpec->pluginId, configuration.data(), configurationBytes,
                                           providers[providerCount].provider.put());
            if (FAILED(result))
            {
                return result;
            }
            ProviderBuildKey& key = providerKeys[providerCount];
            key.configuration = configuration;
            key.configurationBytes = configurationBytes;
            key.pluginId = pluginSpec->pluginId;
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

    std::array<ProviderSlot, kMaximumWidgetInstances> providers;
    std::array<ProviderBuildKey, kMaximumWidgetInstances> providerKeys;
    std::array<WidgetSlot, kMaximumWidgetInstances> widgets;
    size_t providerCount = 0;
    size_t widgetCount = 0;
    const HRESULT result = StageActivePage(settings, providers, providerKeys, providerCount, widgets, widgetCount);
    if (FAILED(result))
    {
        return result;
    }

    _providers = std::move(providers);
    _widgets = std::move(widgets);
    _providerCount = providerCount;
    _widgetCount = widgetCount;
    _gridColumns = settings.dashboard.gridColumns;
    _gridRows = settings.dashboard.gridRows;
    _initialized = true;
    return S_OK;
}

void PluginManager::SetUiInvalidateTarget(HWND window) noexcept
{
    _pluginHost.SetUiInvalidateTarget(window);
}

void PluginManager::AcknowledgeUiInvalidate() noexcept
{
    _pluginHost.AcknowledgeUiInvalidate();
}

HRESULT PluginManager::Reconfigure(const AppSettings& settings) noexcept
{
    if (!_initialized)
    {
        return E_INVALIDARG;
    }
    std::array<ProviderSlot, kMaximumWidgetInstances> providers;
    std::array<ProviderBuildKey, kMaximumWidgetInstances> providerKeys;
    std::array<WidgetSlot, kMaximumWidgetInstances> widgets;
    size_t providerCount = 0;
    size_t widgetCount = 0;
    const HRESULT result = StageActivePage(settings, providers, providerKeys, providerCount, widgets, widgetCount);
    if (FAILED(result))
    {
        return result;
    }

    _widgets = std::move(widgets);
    _widgetCount = widgetCount;
    _providers = std::move(providers);
    _providerCount = providerCount;
    _gridColumns = settings.dashboard.gridColumns;
    _gridRows = settings.dashboard.gridRows;
    return S_OK;
}

size_t PluginManager::ProviderCount() const noexcept
{
    return _providerCount;
}

size_t PluginManager::WidgetCount() const noexcept
{
    return _widgetCount;
}

IRedXeWidget* PluginManager::WidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].widget.get() : nullptr;
}

IRedXeGpuWidget* PluginManager::GpuWidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].gpuWidget.get() : nullptr;
}

IRedXeScheduledWidget* PluginManager::ScheduledWidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].scheduledWidget.get() : nullptr;
}

IRedXeWindowWidget* PluginManager::WindowWidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].windowWidget.get() : nullptr;
}

IRedXeRaisedWidget* PluginManager::RaisedWidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].raisedWidget.get() : nullptr;
}

uint32_t PluginManager::WidgetFlagsAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].flags : RedXeWidgetFlagNone;
}

WidgetGridPlacement PluginManager::WidgetGridPlacementAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].placement : WidgetGridPlacement{};
}

AdaptiveWidgetPlacement PluginManager::AdaptivePlacementAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].adaptivePlacement : AdaptiveWidgetPlacement{};
}

bool PluginManager::UsesAdaptivePlacementAt(size_t index) const noexcept
{
    return index < _widgetCount && _widgets[index].usesAdaptivePlacement;
}

const char* PluginManager::WidgetInstanceIdAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].instanceId.utf8.data() : nullptr;
}

uint32_t PluginManager::GridColumns() const noexcept
{
    return _initialized ? _gridColumns : 0;
}

uint32_t PluginManager::GridRows() const noexcept
{
    return _initialized ? _gridRows : 0;
}
