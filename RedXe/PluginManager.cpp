#include "PluginManager.h"

#include "BundledPlugins.h"

#include "PlugInterfaces/Factory.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

// The third fixed pattern the subset accepts: an action name (PlugInterfaces/Action.h).
constexpr std::string_view kActionNamePattern = "^[a-z][a-zA-Z0-9]*(\\.[a-z][a-zA-Z0-9]*){1,3}$";

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

// Validates a plugin-published settings value against the bounded schema subset RedXe accepts. The supported subset
// is exactly: "object" with "properties", "additionalProperties", and "required"; "array" with "items", "minItems",
// and "maxItems" where items is one closed object; "integer" and "number" with "minimum" and "maximum"; "string"
// with scalar-value "minLength"/"maxLength", "enum" and hex-colour or ASCII machine-ID "pattern"; and "boolean". Nested
// arrays, "$ref", composition keywords, or another pattern are rejected rather than silently accepted. Plugins_API.md
// names this subset normatively.
[[nodiscard]] bool SchemaKeyIsAllowed(std::string_view typeName, std::string_view key) noexcept
{
    if (key == "type")
    {
        return true;
    }
    if (key.size() >= 5 && key.substr(0, 5) == "x-ui-")
    {
        return true;
    }
    if (typeName == "object")
    {
        return key == "properties" || key == "additionalProperties" || key == "required";
    }
    if (typeName == "array")
    {
        return key == "items" || key == "minItems" || key == "maxItems";
    }
    if (typeName == "integer" || typeName == "number")
    {
        return key == "minimum" || key == "maximum";
    }
    if (typeName == "string")
    {
        return key == "enum" || key == "pattern" || key == "minLength" || key == "maxLength";
    }
    return false;
}

[[nodiscard]] bool ValidatePublishedSchemaShape(yyjson_val* schema, uint32_t arrayDepth) noexcept;

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
        yyjson_val* required = yyjson_obj_get(schema, "required");
        if (required)
        {
            if (!yyjson_is_arr(required))
            {
                return false;
            }
            const size_t requiredCount = yyjson_arr_size(required);
            for (size_t index = 0; index < requiredCount; ++index)
            {
                yyjson_val* name = yyjson_arr_get(required, index);
                if (!yyjson_is_str(name) || !yyjson_obj_getn(value, yyjson_get_str(name), yyjson_get_len(name)))
                {
                    return false;
                }
            }
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
    if (typeName == "number")
    {
        if (!yyjson_is_num(value))
        {
            return false;
        }
        const double number = yyjson_get_num(value);
        if (!std::isfinite(number))
        {
            return false;
        }
        yyjson_val* minimum = yyjson_obj_get(schema, "minimum");
        yyjson_val* maximum = yyjson_obj_get(schema, "maximum");
        return (!minimum || (yyjson_is_num(minimum) && number >= yyjson_get_num(minimum))) &&
               (!maximum || (yyjson_is_num(maximum) && number <= yyjson_get_num(maximum)));
    }
    if (typeName == "string")
    {
        if (!yyjson_is_str(value))
        {
            return false;
        }
        // yyjson already validates UTF-8. JSON Schema lengths count Unicode scalar values, not encoded bytes.
        uint64_t scalarCount = 0;
        const std::string_view text{yyjson_get_str(value), yyjson_get_len(value)};
        for (unsigned char byte : text)
            if ((byte & 0xC0U) != 0x80U)
                ++scalarCount;
        yyjson_val* minimum = yyjson_obj_get(schema, "minLength");
        yyjson_val* maximum = yyjson_obj_get(schema, "maxLength");
        if ((minimum && (!yyjson_is_uint(minimum) || scalarCount < yyjson_get_uint(minimum))) ||
            (maximum && (!yyjson_is_uint(maximum) || scalarCount > yyjson_get_uint(maximum))))
            return false;
        yyjson_val* allowedValues = yyjson_obj_get(schema, "enum");
        if (allowedValues)
        {
            if (!yyjson_is_arr(allowedValues))
            {
                return false;
            }
            bool matched = false;
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
        if (!yyjson_is_str(pattern))
            return false;
        const std::string_view expression{yyjson_get_str(pattern), yyjson_get_len(pattern)};
        if (expression == "^#[0-9A-Fa-f]{6}$")
            return IsHexColor(value);
        if (expression == kActionNamePattern)
        {
            // An action name (Action.h grammar); whether it resolves is the host validator's job.
            std::array<char, kRedXeMaximumActionNameBytes + 1> terminated{};
            if (text.size() > kRedXeMaximumActionNameBytes)
                return false;
            std::memcpy(terminated.data(), text.data(), text.size());
            return RedXeIsActionNameSyntax(terminated.data());
        }
        if (expression != "^[A-Za-z0-9_-]+$" || text.empty())
            return false;
        for (unsigned char byte : text)
            if (!((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
                  byte == '_' || byte == '-'))
                return false;
        return true;
    }
    if (typeName == "boolean")
    {
        return yyjson_is_bool(value);
    }
    if (typeName == "array")
    {
        if (!yyjson_is_arr(value))
        {
            return false;
        }
        const uint64_t count = yyjson_arr_size(value);
        yyjson_val* minimum = yyjson_obj_get(schema, "minItems");
        yyjson_val* maximum = yyjson_obj_get(schema, "maxItems");
        if ((minimum && (!yyjson_is_uint(minimum) || count < yyjson_get_uint(minimum))) ||
            (maximum && (!yyjson_is_uint(maximum) || count > yyjson_get_uint(maximum))))
        {
            return false;
        }
        yyjson_val* items = yyjson_obj_get(schema, "items");
        if (!items)
        {
            return false;
        }
        for (size_t index = 0; index < yyjson_arr_size(value); ++index)
        {
            if (!ValidateValueAgainstPublishedSchema(items, yyjson_arr_get(value, index)))
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

[[nodiscard]] bool ValidatePublishedSchemaShape(yyjson_val* schema, uint32_t arrayDepth) noexcept
{
    if (!yyjson_is_obj(schema) || arrayDepth > 1)
    {
        return false;
    }
    yyjson_val* type = yyjson_obj_get(schema, "type");
    if (!yyjson_is_str(type))
    {
        return false;
    }
    const std::string_view typeName{yyjson_get_str(type), yyjson_get_len(type)};
    yyjson_obj_iter iterator = yyjson_obj_iter_with(schema);
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator))
    {
        const std::string_view name{yyjson_get_str(key), yyjson_get_len(key)};
        if (!SchemaKeyIsAllowed(typeName, name))
        {
            return false;
        }
    }
    if (typeName == "object")
    {
        yyjson_val* properties = yyjson_obj_get(schema, "properties");
        if (properties)
        {
            if (!yyjson_is_obj(properties))
            {
                return false;
            }
            yyjson_obj_iter propertyIterator = yyjson_obj_iter_with(properties);
            while (yyjson_val* key = yyjson_obj_iter_next(&propertyIterator))
            {
                if (!ValidatePublishedSchemaShape(yyjson_obj_iter_get_val(key), arrayDepth))
                {
                    return false;
                }
            }
        }
        yyjson_val* additional = yyjson_obj_get(schema, "additionalProperties");
        if (additional && !yyjson_is_bool(additional))
        {
            return false;
        }
        yyjson_val* required = yyjson_obj_get(schema, "required");
        if (required && !yyjson_is_arr(required))
        {
            return false;
        }
        return true;
    }
    if (typeName == "array")
    {
        if (arrayDepth != 0)
        {
            return false;
        }
        yyjson_val* items = yyjson_obj_get(schema, "items");
        yyjson_val* itemsType = items ? yyjson_obj_get(items, "type") : nullptr;
        if (!yyjson_is_obj(items) || !yyjson_is_str(itemsType) ||
            std::string_view{yyjson_get_str(itemsType), yyjson_get_len(itemsType)} != "object")
        {
            return false;
        }
        yyjson_val* minimum = yyjson_obj_get(schema, "minItems");
        yyjson_val* maximum = yyjson_obj_get(schema, "maxItems");
        if ((minimum && !yyjson_is_uint(minimum)) || (maximum && !yyjson_is_uint(maximum)))
        {
            return false;
        }
        return ValidatePublishedSchemaShape(items, 1);
    }
    if (typeName == "string")
    {
        yyjson_val* minimum = yyjson_obj_get(schema, "minLength");
        yyjson_val* maximum = yyjson_obj_get(schema, "maxLength");
        if ((minimum && !yyjson_is_uint(minimum)) || (maximum && !yyjson_is_uint(maximum)) ||
            (minimum && maximum && yyjson_get_uint(minimum) > yyjson_get_uint(maximum)))
            return false;
        yyjson_val* pattern = yyjson_obj_get(schema, "pattern");
        if (pattern)
        {
            if (!yyjson_is_str(pattern))
                return false;
            const std::string_view expression{yyjson_get_str(pattern), yyjson_get_len(pattern)};
            if (expression != "^#[0-9A-Fa-f]{6}$" && expression != "^[A-Za-z0-9_-]+$" &&
                expression != kActionNamePattern)
                return false;
        }
        yyjson_val* values = yyjson_obj_get(schema, "enum");
        if (values)
        {
            if (!yyjson_is_arr(values) || yyjson_arr_size(values) == 0)
                return false;
            for (size_t i = 0; i < yyjson_arr_size(values); ++i)
                if (!yyjson_is_str(yyjson_arr_get(values, i)))
                    return false;
        }
        return true;
    }
    if (typeName == "integer" || typeName == "number" || typeName == "boolean")
    {
        return true;
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
        !ValidatePublishedSchemaShape(yyjson_doc_get_root(schemaDocument.get()), 0) ||
        !ValidateValueAgainstPublishedSchema(yyjson_doc_get_root(schemaDocument.get()),
                                             yyjson_doc_get_root(defaultsDocument.get())))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    *contract = selected;
    return S_OK;
}

// S_OK: the catalogued module is mapped and its settings contract is valid.
// S_FALSE: LoadLibrary could not map the DLL; skip published-schema checks and placeholder instances.
// Failed: the mapped module published an invalid contract (document-fatal).
[[nodiscard]] HRESULT TryMapBundledWidgetModule(const char* pluginId, PluginHost::ModuleView& module) noexcept
{
    module = {};
    const HRESULT mapResult =
        PluginHost::Instance().GetPluginModule(pluginId, RedXePluginCapabilityWidgetProvider, &module);
    if (FAILED(mapResult))
    {
        OutputDebugStringW(
            L"A bundled plugin module could not be mapped; widget instances will use placeholder tiles.\n");
        module = {};
        (void)RedXeHostLog(PluginHost::Instance().Interface(), RedXeLogLevelError, pluginId, nullptr,
                           "module-map-failed", "catalogued plugin DLL could not be mapped.", mapResult);
        return S_FALSE;
    }

    const RedXePluginSettingsContract* contract = nullptr;
    const HRESULT contractResult = GetAndValidateSettingsContract(module, pluginId, &contract);
    if (FAILED(contractResult))
    {
        return contractResult;
    }
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

// One parsed schema per referenced plugin for the duration of a single staging pass. Without it the same plugin
// schema is re-parsed once per widget appearance on every page, on every settings apply.
class SchemaCache final
{
  public:
    SchemaCache() = default;
    SchemaCache(const SchemaCache&) = delete;
    SchemaCache& operator=(const SchemaCache&) = delete;
    SchemaCache(SchemaCache&&) = delete;
    SchemaCache& operator=(SchemaCache&&) = delete;

    [[nodiscard]] HRESULT Acquire(const PluginHost::ModuleView& module, const char* pluginId,
                                  yyjson_val*& root) noexcept
    {
        root = nullptr;
        for (size_t index = 0; index < _count; ++index)
        {
            if (RedXeAsciiEqualsIgnoreCase(_entries[index].pluginId, pluginId))
            {
                root = _entries[index].root;
                return S_OK;
            }
        }
        if (_count >= _entries.size())
        {
            return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
        }

        const RedXePluginSettingsContract* contract = nullptr;
        const HRESULT result = GetAndValidateSettingsContract(module, pluginId, &contract);
        if (FAILED(result) || !contract)
        {
            return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        Entry& entry = _entries[_count];
        entry.document.reset(yyjson_read(contract->schemaJsonUtf8, contract->schemaBytes, YYJSON_READ_NOFLAG));
        if (!entry.document)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        entry.pluginId = pluginId;
        entry.root = yyjson_doc_get_root(entry.document.get());
        ++_count;
        root = entry.root;
        return root ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

  private:
    struct Entry final
    {
        unique_contract_doc document;
        const char* pluginId = nullptr;
        yyjson_val* root = nullptr;
    };

    std::array<Entry, kRedXeBundledWidgets.size()> _entries{};
    size_t _count = 0;
};
} // namespace

PluginManager::~PluginManager()
{
    ReleaseWidgets();

    for (size_t index = 0; index < _providerCount; ++index)
    {
        _providers[index].provider.reset();
    }
    _providerCount = 0;

    _initialized = false;
}

HRESULT PluginManager::CreateBundledProvider(const char* pluginId, const char* configurationJson,
                                             uint32_t configurationBytes, uint32_t backgroundRgb,
                                             IRedXeWidgetProvider** provider) noexcept
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
    HRESULT result = PluginHost::Instance().GetPluginModule(pluginId, RedXePluginCapabilityWidgetProvider, &module);
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
    options.backgroundColor = 0xFF000000u | (backgroundRgb & 0x00FFFFFFu);

    void* providerObject = nullptr;
    result = module.create(__uuidof(IRedXeWidgetProvider), &options, PluginHost::Instance().Interface(), pluginId,
                           &providerObject);
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
                                            uint32_t backgroundRgb, WidgetSlot& widgetSlot) noexcept
{
    if (!RedXeIsValidMachineId(settings.typeId.utf8.data()) || !RedXeIsValidMachineId(settings.id.utf8.data()) ||
        widgetSlot.widget || widgetSlot.gpuWidget || widgetSlot.preparedGpuWidget || widgetSlot.scheduledWidget ||
        widgetSlot.windowWidget || widgetSlot.raisedWidget || widgetSlot.interactiveWidget ||
        widgetSlot.keyboardWidget || widgetSlot.textInputWidget || widgetSlot.accessibilityWidget ||
        widgetSlot.networkWidget)
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
    const HRESULT preparedResult = widgetSlot.widget.query_to(widgetSlot.preparedGpuWidget.put());
    const HRESULT scheduledResult = widgetSlot.widget.query_to(widgetSlot.scheduledWidget.put());
    const HRESULT windowResult = widgetSlot.widget.query_to(widgetSlot.windowWidget.put());
    const HRESULT raisedResult = widgetSlot.widget.query_to(widgetSlot.raisedWidget.put());
    const HRESULT interactiveResult = widgetSlot.widget.query_to(widgetSlot.interactiveWidget.put());
    const HRESULT keyboardResult = widgetSlot.widget.query_to(widgetSlot.keyboardWidget.put());
    const HRESULT textResult = widgetSlot.widget.query_to(widgetSlot.textInputWidget.put());
    const HRESULT accessibilityResult = widgetSlot.widget.query_to(widgetSlot.accessibilityWidget.put());
    const HRESULT networkResult = widgetSlot.widget.query_to(widgetSlot.networkWidget.put());
    if (gpuResult != S_OK && gpuResult != E_NOINTERFACE)
    {
        return gpuResult;
    }
    if (preparedResult != S_OK && preparedResult != E_NOINTERFACE)
    {
        return preparedResult;
    }
    if (widgetSlot.preparedGpuWidget && !widgetSlot.gpuWidget)
    {
        return E_NOINTERFACE;
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
    if (interactiveResult != S_OK && interactiveResult != E_NOINTERFACE)
    {
        return interactiveResult;
    }
    if (keyboardResult != S_OK && keyboardResult != E_NOINTERFACE)
        return keyboardResult;
    if (widgetSlot.keyboardWidget && !widgetSlot.gpuWidget)
        return E_NOINTERFACE;
    if (textResult != S_OK && textResult != E_NOINTERFACE)
        return textResult;
    if (widgetSlot.textInputWidget && (!widgetSlot.preparedGpuWidget || !widgetSlot.keyboardWidget))
        return E_NOINTERFACE;
    if (accessibilityResult != S_OK && accessibilityResult != E_NOINTERFACE)
        return accessibilityResult;
    if (widgetSlot.accessibilityWidget && (!widgetSlot.preparedGpuWidget || !widgetSlot.keyboardWidget))
        return E_NOINTERFACE;
    if (networkResult != S_OK && networkResult != E_NOINTERFACE)
    {
        return networkResult;
    }
    if (!widgetSlot.gpuWidget && !widgetSlot.windowWidget)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    if (widgetSlot.gpuWidget)
    {
        widgetSlot.windowWidget.reset();
    }
    if (widgetSlot.networkWidget)
    {
        const HRESULT registerResult = PluginHost::Instance().RegisterNetworkWidget(widgetSlot.networkWidget.get());
        if (FAILED(registerResult))
        {
            widgetSlot.networkWidget.reset();
            return registerResult;
        }
    }
    widgetSlot.instanceId = settings.id;
    widgetSlot.placement = settings.placement;
    widgetSlot.adaptivePlacement = settings.adaptivePlacement;
    widgetSlot.usesAdaptivePlacement = settings.usesAdaptivePlacement;
    widgetSlot.backgroundRgb = backgroundRgb;
    widgetSlot.flags = widgetType->flags;
    return S_OK;
}

void PluginManager::MakePlaceholder(WidgetSlot& widgetSlot, const WidgetInstanceSettings& settings,
                                    uint32_t backgroundRgb, HRESULT failure) noexcept
{
    widgetSlot = WidgetSlot{};
    widgetSlot.instanceId = settings.id;
    widgetSlot.placement = settings.placement;
    widgetSlot.adaptivePlacement = settings.adaptivePlacement;
    widgetSlot.usesAdaptivePlacement = settings.usesAdaptivePlacement;
    widgetSlot.backgroundRgb = backgroundRgb;
    widgetSlot.flags = RedXeWidgetFlagNone;
    widgetSlot.placeholder = true;
    widgetSlot.failure = FAILED(failure) ? failure : E_FAIL;
    OutputDebugStringW(L"A widget instance could not be created; the host is drawing a placeholder tile.\n");
    (void)RedXeHostLog(PluginHost::Instance().Interface(), RedXeLogLevelError, settings.pluginId.utf8.data(),
                       settings.id.utf8.data(), "widget-placeholder",
                       "widget instance could not be constructed; host placeholder tile.", widgetSlot.failure);
}

void PluginManager::ClearWidgetStatuses() noexcept
{
    for (size_t index = 0; index < _widgetCount; ++index)
    {
        PluginHost::Instance().ClearWidgetStatus(_widgets[index].instanceId.utf8.data());
    }
}

void PluginManager::ReleaseWidgets() noexcept
{
    for (size_t index = 0; index < _widgetCount; ++index)
    {
        if (_widgets[index].networkWidget)
        {
            PluginHost::Instance().UnregisterNetworkWidget(_widgets[index].networkWidget.get());
        }
        const auto instanceId = _widgets[index].instanceId;
        _widgets[index] = WidgetSlot{};
        // Worker callbacks and widget destruction must finish before discarding their queued settings/status.
        PluginHost::Instance().ClearWidgetStatus(instanceId.utf8.data());
    }
    _widgetCount = 0;
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
    SchemaCache schemaCache;

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
        const HRESULT mapResult = TryMapBundledWidgetModule(pluginSpec->pluginId, module);
        if (mapResult == S_FALSE)
        {
            continue;
        }
        if (FAILED(mapResult))
        {
            return mapResult;
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
            const HRESULT mapResult = TryMapBundledWidgetModule(pluginSpec->pluginId, module);
            if (mapResult == S_FALSE)
            {
                continue;
            }
            if (FAILED(mapResult))
            {
                return mapResult;
            }
            yyjson_val* schemaRoot = nullptr;
            result = schemaCache.Acquire(module, pluginSpec->pluginId, schemaRoot);
            if (FAILED(result) || !schemaRoot)
            {
                return FAILED(result) ? result : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            unique_contract_doc valueDocument{yyjson_read(widget.privateConfiguration.utf8.data(),
                                                          widget.privateConfiguration.bytes, YYJSON_READ_NOFLAG)};
            if (!valueDocument ||
                !ValidateValueAgainstPublishedSchema(schemaRoot, yyjson_doc_get_root(valueDocument.get())))
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
    }

    const DashboardPageSettings* page = FindActiveDashboardPage(settings);
    if (!page || page->widgetCount > kMaximumWidgetInstances)
    {
        return E_INVALIDARG;
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
        const uint32_t backgroundRgb = EffectiveWidgetBackgroundRgb(settings, instance);

        size_t providerIndex = providerCount;
        for (size_t candidate = 0; candidate < providerCount; ++candidate)
        {
            const ProviderBuildKey& key = providerKeys[candidate];
            if (key.pluginId == pluginSpec->pluginId && key.configurationBytes == configurationBytes &&
                key.backgroundRgb == backgroundRgb &&
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
                                           backgroundRgb, providers[providerCount].provider.put());
            if (FAILED(result))
            {
                // Runtime construction failure is isolated to this instance. The document is already validated, so a
                // provider that cannot be built must not take the rest of the page or startup down with it.
                MakePlaceholder(widgets[index], instance, backgroundRgb, result);
                ++widgetCount;
                continue;
            }
            ProviderBuildKey& key = providerKeys[providerCount];
            key.configuration = configuration;
            key.configurationBytes = configurationBytes;
            key.backgroundRgb = backgroundRgb;
            key.pluginId = pluginSpec->pluginId;
            ++providerCount;
        }

        result = CreateWidgetInstance(*providers[providerIndex].provider, instance, backgroundRgb, widgets[index]);
        if (FAILED(result))
        {
            MakePlaceholder(widgets[index], instance, backgroundRgb, result);
        }
        else
        {
            (void)RedXeHostLog(PluginHost::Instance().Interface(), RedXeLogLevelInfo, pluginSpec->pluginId,
                               instance.id.utf8.data(), "widget-created", "widget instance constructed.");
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
    _backgroundRgb = settings.backgroundRgb;
    _initialized = true;
    return S_OK;
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

    ReleaseWidgets();
    _widgets = std::move(widgets);
    _widgetCount = widgetCount;
    _providers = std::move(providers);
    _providerCount = providerCount;
    _gridColumns = settings.dashboard.gridColumns;
    _gridRows = settings.dashboard.gridRows;
    _backgroundRgb = settings.backgroundRgb;
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

IRedXePreparedGpuWidget* PluginManager::PreparedGpuWidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].preparedGpuWidget.get() : nullptr;
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

IRedXeInteractiveWidget* PluginManager::InteractiveWidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].interactiveWidget.get() : nullptr;
}
IRedXeKeyboardWidget* PluginManager::KeyboardWidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].keyboardWidget.get() : nullptr;
}
IRedXeTextInputWidget* PluginManager::TextInputWidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].textInputWidget.get() : nullptr;
}
IRedXeAccessibilityWidget* PluginManager::AccessibilityWidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].accessibilityWidget.get() : nullptr;
}

IRedXeNetworkWidget* PluginManager::NetworkWidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].networkWidget.get() : nullptr;
}

uint32_t PluginManager::WidgetFlagsAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].flags : RedXeWidgetFlagNone;
}

bool PluginManager::IsPlaceholderAt(size_t index) const noexcept
{
    return index < _widgetCount && _widgets[index].placeholder;
}

HRESULT PluginManager::PlaceholderFailureAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].failure : S_OK;
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

uint32_t PluginManager::BackgroundRgb() const noexcept
{
    return _initialized ? _backgroundRgb : kRedXeDefaultBackgroundRgb;
}

uint32_t PluginManager::WidgetBackgroundRgbAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].backgroundRgb : BackgroundRgb();
}

uint32_t PluginManager::GridColumns() const noexcept
{
    return _initialized ? _gridColumns : 0;
}

uint32_t PluginManager::GridRows() const noexcept
{
    return _initialized ? _gridRows : 0;
}

HRESULT PluginManager::ValidatePluginPublishedSchema(std::string_view schemaJson,
                                                     std::string_view defaultsJson) noexcept
{
    if (schemaJson.empty() || defaultsJson.empty() || schemaJson.size() > kPrivateConfigurationCapacity ||
        defaultsJson.size() > kPrivateConfigurationCapacity)
    {
        return E_INVALIDARG;
    }
    unique_contract_doc schemaDocument{yyjson_read(schemaJson.data(), schemaJson.size(), YYJSON_READ_NOFLAG)};
    unique_contract_doc defaultsDocument{yyjson_read(defaultsJson.data(), defaultsJson.size(), YYJSON_READ_NOFLAG)};
    if (!schemaDocument || !defaultsDocument || !yyjson_is_obj(yyjson_doc_get_root(schemaDocument.get())) ||
        !yyjson_is_obj(yyjson_doc_get_root(defaultsDocument.get())) ||
        !ValidatePublishedSchemaShape(yyjson_doc_get_root(schemaDocument.get()), 0) ||
        !ValidateValueAgainstPublishedSchema(yyjson_doc_get_root(schemaDocument.get()),
                                             yyjson_doc_get_root(defaultsDocument.get())))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}
