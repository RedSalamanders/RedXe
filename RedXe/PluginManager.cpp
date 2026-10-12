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
#include <span>
#include <type_traits>
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
// A validation witness contains logical settings bytes, never padding, unused text tails or the source document.
// The same bounded walk counts/writes on a cold pass and compares without allocation on subsequent page selections.
constexpr size_t kMaximumValidationEvidenceBytes = 64U * 1024U;
class SettingsEvidenceStream final
{
  public:
    explicit SettingsEvidenceStream(std::vector<std::byte>* output = nullptr) noexcept : _output(output) {}
    explicit SettingsEvidenceStream(std::span<const std::byte> expected) noexcept : _expected(expected), _compare(true)
    {
    }
    template <typename T> [[nodiscard]] bool Scalar(T value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return Append(&value, sizeof(value));
    }
    template <size_t N> [[nodiscard]] bool Stored(const std::array<char, N>& text, uint32_t bytes, bool object = false)
    {
        if (bytes >= N || (object && bytes < 2) || text[bytes] != '\0' || std::memchr(text.data(), '\0', bytes))
            return false;
        return Scalar(bytes) && Append(text.data(), bytes);
    }
    [[nodiscard]] bool Text(const SettingsText& text)
    {
        return Stored(text.utf8, text.bytes);
    }
    [[nodiscard]] bool Json(const JsonObjectSettings& json)
    {
        return Scalar(json.Bytes()) && Append(json.Data(), json.Bytes());
    }
    [[nodiscard]] size_t Size() const noexcept
    {
        return _size;
    }
    [[nodiscard]] bool Complete() const noexcept
    {
        return !_compare || _size == _expected.size();
    }

  private:
    [[nodiscard]] bool Append(const void* data, size_t size)
    {
        if (size > kMaximumValidationEvidenceBytes - _size)
            return false;
        if (_compare && (_size > _expected.size() || size > _expected.size() - _size ||
                         std::memcmp(_expected.data() + _size, data, size) != 0))
            return false;
        if (_output && size != 0)
        {
            const auto* bytes = static_cast<const std::byte*>(data);
            _output->insert(_output->end(), bytes, bytes + size);
        }
        _size += size;
        return true;
    }
    std::vector<std::byte>* _output = nullptr;
    std::span<const std::byte> _expected;
    size_t _size = 0;
    bool _compare = false;
};
[[nodiscard]] bool WalkSettingsEvidence(const AppSettings& settings, SettingsEvidenceStream& stream)
{
    // Structured bindings deliberately make additions to these records require a witness-policy decision.
    const auto& [major, minor, retention, background, dock, tray, rate, source, plugins, pluginCount, services,
                 serviceCount, retired, dashboard] = settings;
    const auto& [columns, rows, activeId, pages, pageCount, activeIndex, wrap] = dashboard;
    (void)source;
    (void)activeIndex;
    if (pluginCount > kMaximumSettingsPlugins || plugins.size() != pluginCount ||
        serviceCount > kMaximumSettingsServices || services.size() != serviceCount ||
        retired.size() > kMaximumSettingsServices - serviceCount || pageCount == 0 ||
        pageCount > kMaximumDashboardPages || pages.size() != pageCount || activeId.bytes == 0 ||
        activeId.bytes >= activeId.utf8.size() || activeId.utf8[activeId.bytes] != '\0' ||
        std::memchr(activeId.utf8.data(), '\0', activeId.bytes))
        return false;
    const auto& [edge, monitor, thickness, mode, reserve, peek, revealDelay, hideDelay, animation] = dock;
    if (!(stream.Scalar(major) && stream.Scalar(minor) && stream.Scalar(retention) && stream.Scalar(background) &&
          stream.Scalar(edge) && stream.Text(monitor) && stream.Scalar(thickness) && stream.Scalar(mode) &&
          stream.Scalar(reserve) && stream.Scalar(peek) && stream.Scalar(revealDelay) && stream.Scalar(hideDelay) &&
          stream.Scalar(animation) && stream.Scalar(tray) && stream.Scalar(rate) && stream.Scalar(pluginCount)))
        return false;
    for (const auto& plugin : plugins)
    {
        const auto& [id, enabled, configuration] = plugin;
        if (!(stream.Text(id) && stream.Scalar(enabled) && stream.Json(configuration)))
            return false;
    }
    if (!stream.Scalar(serviceCount))
        return false;
    for (const auto& service : services)
    {
        const auto& [name, pluginId, configuration] = service;
        if (!(stream.Text(name) && stream.Text(pluginId) && stream.Json(configuration)))
            return false;
    }
    if (!stream.Scalar(static_cast<uint32_t>(retired.size())))
        return false;
    for (const auto& id : retired)
        if (!stream.Text(id))
            return false;
    if (!(stream.Scalar(columns) && stream.Scalar(rows) && stream.Scalar(pageCount) && stream.Scalar(wrap)))
        return false;
    bool activeFound = false;
    for (const auto& page : pages)
    {
        const auto& [id, name, widgets, count] = page;
        if (count > kMaximumWidgetsPerPage || widgets.size() != count ||
            !(stream.Text(id) && stream.Text(name) && stream.Scalar(count)))
            return false;
        activeFound = activeFound || SettingsIdEquals(id.View(), activeId.View());
        for (const auto& widget : widgets)
        {
            const auto& [widgetId, pluginId, typeId, placement, adaptive, usesAdaptive, overridesBackground,
                         widgetBackground, configuration] = widget;
            const auto& [column, row, columnSpan, rowSpan] = placement;
            const auto& [steps, depth] = adaptive;
            if (!(stream.Text(widgetId) && stream.Text(pluginId) && stream.Text(typeId) && stream.Scalar(column) &&
                  stream.Scalar(row) && stream.Scalar(columnSpan) && stream.Scalar(rowSpan) && stream.Scalar(depth)))
                return false;
            for (const auto& step : steps)
            {
                const auto& [axis, preceding, size, total] = step;
                if (!(stream.Scalar(axis) && stream.Scalar(preceding) && stream.Scalar(size) && stream.Scalar(total)))
                    return false;
            }
            if (!(stream.Scalar(usesAdaptive) && stream.Scalar(overridesBackground) &&
                  stream.Scalar(widgetBackground) && stream.Json(configuration)))
                return false;
        }
    }
    return activeFound && stream.Complete();
}
[[nodiscard]] HRESULT ValidatePluginDocument(const AppSettings& settings, bool& complete) noexcept
{
    HRESULT result = ValidateAppSettings(settings);
    if (FAILED(result))
    {
        return result;
    }
    SchemaCache schemaCache;
    complete = true;

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
            complete = false;
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
                complete = false;
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
            unique_contract_doc valueDocument{yyjson_read(widget.privateConfiguration.Data(),
                                                          widget.privateConfiguration.Bytes(), YYJSON_READ_NOFLAG)};
            if (!valueDocument ||
                !ValidateValueAgainstPublishedSchema(schemaRoot, yyjson_doc_get_root(valueDocument.get())))
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
        }
    }

    return S_OK;
}
} // namespace

struct PluginManager::ValidationEvidence final
{
    std::vector<std::byte> facts;
};
std::shared_ptr<const PluginManager::ValidationEvidence> PluginManager::BuildValidationEvidence(
    const AppSettings& settings) noexcept
{
    try
    {
        SettingsEvidenceStream count;
        if (!WalkSettingsEvidence(settings, count))
            return {};
#if defined(REDXE_HOST_PLUGIN_TESTS)
        if (_failValidationEvidenceAllocation)
            throw std::bad_alloc{};
#endif
        auto evidence = std::make_shared<ValidationEvidence>();
        evidence->facts.reserve(count.Size());
        SettingsEvidenceStream output{&evidence->facts};
        if (!WalkSettingsEvidence(settings, output))
            return {};
        return evidence;
    }
    catch (...)
    {
        // This optional cache must not prevent use of the already-validated cold path on allocation failure.
        return {};
    }
}
bool PluginManager::ValidationEvidenceMatches(const ValidationEvidence& evidence, const AppSettings& settings) noexcept
{
    SettingsEvidenceStream input{std::span<const std::byte>{evidence.facts}};
    return WalkSettingsEvidence(settings, input);
}
#if defined(REDXE_HOST_PLUGIN_TESTS)
size_t PluginManager::ValidationEvidenceBytes() const noexcept
{
    return _validationEvidence ? _validationEvidence->facts.capacity() : 0;
}
#endif

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
        widgetSlot.widget || widgetSlot.gpuWidget || widgetSlot.preparedGpuWidget ||
        widgetSlot.constructionStateWidget || widgetSlot.scheduledWidget || widgetSlot.windowWidget ||
        widgetSlot.raisedWidget || widgetSlot.interactiveWidget || widgetSlot.keyboardWidget ||
        widgetSlot.textInputWidget || widgetSlot.accessibilityWidget || widgetSlot.networkWidget)
    {
        return E_INVALIDARG;
    }

    const RedXeWidgetTypeDescriptor* widgetType = nullptr;
    HRESULT result = FindWidgetType(provider, settings.typeId.utf8.data(), widgetType);
    if (FAILED(result))
    {
        return result;
    }

    result = PluginHost::Instance().BindWidgetStatus(settings.id.utf8.data(), widgetSlot.status);
    if (FAILED(result))
        return result;
    result = provider.CreateWidget(settings.typeId.utf8.data(), settings.id.utf8.data(), widgetSlot.widget.put());
    if (FAILED(result))
    {
        return result;
    }

    const HRESULT gpuResult = widgetSlot.widget.query_to(widgetSlot.gpuWidget.put());
    const HRESULT preparedResult = widgetSlot.widget.query_to(widgetSlot.preparedGpuWidget.put());
    const HRESULT constructionResult = widgetSlot.widget.query_to(widgetSlot.constructionStateWidget.put());
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
    if (constructionResult != S_OK && constructionResult != E_NOINTERFACE)
        return constructionResult;
    if (widgetSlot.constructionStateWidget && !widgetSlot.preparedGpuWidget)
        return E_NOINTERFACE;
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

PluginManager::WidgetSlot::~WidgetSlot() noexcept
{
    ReleaseWidgetInterfaces(*this);
}

PluginManager::WidgetSlot& PluginManager::WidgetSlot::operator=(WidgetSlot&& other) noexcept
{
    if (this != &other)
    {
        ReleaseWidgetInterfaces(*this);
        status = std::move(other.status);
        gpuWidget = std::move(other.gpuWidget);
        preparedGpuWidget = std::move(other.preparedGpuWidget);
        constructionStateWidget = std::move(other.constructionStateWidget);
        scheduledWidget = std::move(other.scheduledWidget);
        windowWidget = std::move(other.windowWidget);
        raisedWidget = std::move(other.raisedWidget);
        interactiveWidget = std::move(other.interactiveWidget);
        keyboardWidget = std::move(other.keyboardWidget);
        textInputWidget = std::move(other.textInputWidget);
        accessibilityWidget = std::move(other.accessibilityWidget);
        networkWidget = std::move(other.networkWidget);
        widget = std::move(other.widget);
        instanceId = std::move(other.instanceId);
        placement = other.placement;
        adaptivePlacement = other.adaptivePlacement;
        usesAdaptivePlacement = other.usesAdaptivePlacement;
        backgroundRgb = other.backgroundRgb;
        flags = other.flags;
        placeholder = other.placeholder;
        failure = other.failure;
    }
    return *this;
}

void PluginManager::ReleaseWidgetInterfaces(WidgetSlot& slot) noexcept
{
    // Keep its status pin until all interface references and widget-owned subscription callbacks are gone. This is
    // also used before slot move-assignment, whose status member would otherwise release first.
    if (slot.networkWidget)
        PluginHost::Instance().UnregisterNetworkWidget(slot.networkWidget.get());
    slot.gpuWidget.reset();
    slot.preparedGpuWidget.reset();
    slot.constructionStateWidget.reset();
    slot.scheduledWidget.reset();
    slot.windowWidget.reset();
    slot.raisedWidget.reset();
    slot.interactiveWidget.reset();
    slot.keyboardWidget.reset();
    slot.textInputWidget.reset();
    slot.accessibilityWidget.reset();
    slot.networkWidget.reset();
    slot.widget.reset();
}

void PluginManager::ReleaseWidgets() noexcept
{
    for (size_t index = 0; index < _widgetCount; ++index)
    {
        const auto instanceId = _widgets[index].instanceId;
        _widgets[index] = WidgetSlot{};
        // Worker callbacks and widget destruction must finish before discarding their queued settings/status.
        PluginHost::Instance().ClearWidgetStatus(instanceId.utf8.data());
    }
    _widgetCount = 0;
}

uint64_t PluginManager::HashFactoryConfiguration(const char* json, uint32_t bytes) noexcept
{
    uint64_t hash = 14695981039346656037ULL;
    for (uint32_t index = 0; index < bytes; ++index)
    {
        hash ^= static_cast<unsigned char>(json[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool PluginManager::ProviderConfigurationMatches(const ProviderBuildKey& key, const DashboardPageSettings& page,
                                                 const WidgetInstanceSettings& instance, const char* pluginId,
                                                 uint64_t hash, uint32_t bytes, uint32_t backgroundRgb) noexcept
{
    if (key.pluginId != pluginId || key.configurationHash != hash || key.configurationBytes != bytes ||
        key.backgroundRgb != backgroundRgb || key.widgetIndex >= page.widgetCount)
    {
        return false;
    }
    // One canonical plugin ID resolves to the same plugin settings throughout this staging pass. The envelope is
    // the fixed concatenation of that plugin object and the instance object; comparing the latter's original bytes
    // proves equality even if the 64-bit filter collides, without retaining or re-serializing another 8 KiB buffer.
    const JsonObjectSettings& original = page.widgets[key.widgetIndex].privateConfiguration;
    return original == instance.privateConfiguration;
}

HRESULT PluginManager::StagePage(const AppSettings& settings, uint32_t pageIndex, const PluginManager* validated,
                                 std::shared_ptr<const ValidationEvidence>& evidence,
                                 std::array<ProviderSlot, kMaximumWidgetInstances>& providers,
                                 std::array<ProviderBuildKey, kMaximumWidgetInstances>& providerKeys,
                                 size_t& providerCount, std::array<WidgetSlot, kMaximumWidgetInstances>& widgets,
                                 size_t& widgetCount) noexcept
{
    providerCount = 0;
    widgetCount = 0;
    HRESULT result = S_OK;
    if (!PluginHost::Instance().IsShutdown() && _validationEvidence &&
        ValidationEvidenceMatches(*_validationEvidence, settings))
        evidence = _validationEvidence;
    else if (!PluginHost::Instance().IsShutdown() && validated && validated->_initialized &&
             validated->_validationEvidence && ValidationEvidenceMatches(*validated->_validationEvidence, settings))
        evidence = validated->_validationEvidence;
    else
    {
        bool complete = false;
        result = ValidatePluginDocument(settings, complete);
        if (FAILED(result))
            return result;
        if (complete && !PluginHost::Instance().IsShutdown())
            evidence = BuildValidationEvidence(settings);
    }
    const DashboardPageSettings* page =
        pageIndex == UINT32_MAX
            ? FindActiveDashboardPage(settings)
            : (pageIndex < settings.dashboard.pageCount ? &settings.dashboard.pages[pageIndex] : nullptr);
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
        const uint64_t configurationHash = HashFactoryConfiguration(configuration.data(), configurationBytes);

        size_t providerIndex = providerCount;
        for (size_t candidate = 0; candidate < providerCount; ++candidate)
        {
            const ProviderBuildKey& key = providerKeys[candidate];
            if (ProviderConfigurationMatches(key, *page, instance, pluginSpec->pluginId, configurationHash,
                                             configurationBytes, backgroundRgb))
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
            key.configurationHash = configurationHash;
            key.configurationBytes = configurationBytes;
            key.backgroundRgb = backgroundRgb;
            key.pluginId = pluginSpec->pluginId;
            key.widgetIndex = index;
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
    return InitializePageImpl(settings, UINT32_MAX, nullptr);
}
HRESULT PluginManager::InitializePage(const AppSettings& settings, uint32_t pageIndex,
                                      const PluginManager* validated) noexcept
{
    if (settings.dashboard.pages.size() != settings.dashboard.pageCount || pageIndex >= settings.dashboard.pageCount)
        return E_INVALIDARG;
    return InitializePageImpl(settings, pageIndex, validated);
}
HRESULT PluginManager::InitializePageImpl(const AppSettings& settings, uint32_t pageIndex,
                                          const PluginManager* validated) noexcept
{
    if (_initialized || _widgetCount != 0 || _providerCount != 0)
    {
        return E_INVALIDARG;
    }

    std::array<ProviderSlot, kMaximumWidgetInstances> providers;
    std::array<ProviderBuildKey, kMaximumWidgetInstances> providerKeys;
    std::array<WidgetSlot, kMaximumWidgetInstances> widgets;
    static_assert(sizeof(providers) + sizeof(providerKeys) + sizeof(widgets) < 28U * 1024U);
    size_t providerCount = 0;
    size_t widgetCount = 0;
    std::shared_ptr<const ValidationEvidence> evidence;
    const HRESULT result = StagePage(settings, pageIndex, validated, evidence, providers, providerKeys, providerCount,
                                     widgets, widgetCount);
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
    _validationEvidence = std::move(evidence);
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
    static_assert(sizeof(providers) + sizeof(providerKeys) + sizeof(widgets) < 28U * 1024U);
    size_t providerCount = 0;
    size_t widgetCount = 0;
    std::shared_ptr<const ValidationEvidence> evidence;
    const HRESULT result = StagePage(settings, UINT32_MAX, nullptr, evidence, providers, providerKeys, providerCount,
                                     widgets, widgetCount);
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
    _validationEvidence = std::move(evidence);
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

IRedXeConstructionStateWidget* PluginManager::ConstructionStateWidgetAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].constructionStateWidget.get() : nullptr;
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

uint32_t PluginManager::WidgetStatusAt(size_t index) const noexcept
{
    return index < _widgetCount ? _widgets[index].status.Status() : RedXeWidgetStatusOk;
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
