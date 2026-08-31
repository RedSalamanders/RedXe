#include "PlugInterfaces/DataProvider.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Widget.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
static_assert(std::is_base_of_v<IUnknown, IRedXeDataProvider>);
static_assert(!std::is_base_of_v<IRedXeWidget, IRedXeDataProvider>);

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
    Expect(function != nullptr, "required SystemData export is missing");
    return function;
}

[[nodiscard]] std::filesystem::path PluginPath()
{
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    Expect(length != 0 && length < executable.size(), "test executable path is unavailable");
    executable.resize(length);
    return std::filesystem::path(executable).parent_path() / L"Plugins" / L"SystemData.dll";
}

[[nodiscard]] const RedXeDataSetDescriptor* FindDataSet(const RedXeDataSetDescriptor* descriptors, std::uint32_t count,
                                                        const char* id)
{
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (descriptors[index].dataSetId && RedXeAsciiEqualsIgnoreCase(descriptors[index].dataSetId, id))
        {
            return &descriptors[index];
        }
    }
    return nullptr;
}

[[nodiscard]] std::uint32_t FindColumn(const RedXeDataSetDescriptor& descriptor, const char* id)
{
    for (std::uint32_t index = 0; index < descriptor.columnCount; ++index)
    {
        if (descriptor.columns[index].columnId && RedXeAsciiEqualsIgnoreCase(descriptor.columns[index].columnId, id))
        {
            return index;
        }
    }
    throw std::runtime_error("required data column is missing");
}

void ValidateSnapshot(const RedXeDataSnapshot& snapshot, const RedXeDataSetDescriptor& descriptor)
{
    Expect(snapshot.sizeBytes >= sizeof(RedXeDataSnapshot), "snapshot record is too small");
    Expect(snapshot.dataSetId && RedXeAsciiEqualsIgnoreCase(snapshot.dataSetId, descriptor.dataSetId),
           "snapshot dataset identity is wrong");
    Expect(snapshot.sequence != 0 && snapshot.timestampFileTime100ns != 0, "snapshot lacks sequence or timestamp");
    Expect(snapshot.rowCount <= descriptor.maximumRows, "snapshot exceeds its declared row bound");
    Expect(snapshot.columnCount == descriptor.columnCount, "snapshot column count differs from its descriptor");
    Expect(snapshot.rowCount == 0 || snapshot.rows, "non-empty snapshot has no rows");

    for (std::uint32_t rowIndex = 0; rowIndex < snapshot.rowCount; ++rowIndex)
    {
        const RedXeDataRow& row = snapshot.rows[rowIndex];
        Expect(row.sizeBytes >= sizeof(RedXeDataRow), "row record is too small");
        Expect(row.values && row.valueCount == descriptor.columnCount, "row has the wrong value shape");
        for (std::uint32_t valueIndex = 0; valueIndex < row.valueCount; ++valueIndex)
        {
            const RedXeDataValue& value = row.values[valueIndex];
            Expect(value.sizeBytes >= sizeof(RedXeDataValue), "value record is too small");
            Expect(value.valueType == descriptor.columns[valueIndex].valueType, "value type differs from descriptor");
            Expect(value.quality <= RedXeDataQualityInitializing, "value quality is outside the v1 contract");
            if (value.valueType == RedXeDataValueTypeFloat64)
            {
                Expect(std::isfinite(value.float64Value), "floating-point value is not finite");
                if (std::strstr(descriptor.columns[valueIndex].columnId, "Cpu") ||
                    std::strstr(descriptor.columns[valueIndex].columnId, "cpu"))
                {
                    Expect(value.float64Value >= 0.0 && value.float64Value <= 100.0, "CPU percentage is outside 0-100");
                }
            }
            if (value.valueType == RedXeDataValueTypeUtf16 && value.quality == RedXeDataQualityGood)
            {
                Expect(value.utf16Value != nullptr, "good UTF-16 value has no storage");
            }
        }
    }
}

void Run()
{
    const std::filesystem::path pluginPath = PluginPath();
    wil::unique_hmodule module{
        LoadLibraryExW(pluginPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    Expect(static_cast<bool>(module), "SystemData.dll could not be loaded");

    const RedXeCreateFn create = Resolve<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const RedXeEnumeratePluginsFn enumerate =
        Resolve<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    const RedXeGetPluginSettingsContractFn getSettings =
        Resolve<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);

    const RedXePluginMetadata* metadata = reinterpret_cast<const RedXePluginMetadata*>(1);
    std::uint32_t metadataCount = 99;
    Expect(enumerate(nullptr, &metadataCount) == E_POINTER && metadataCount == 0,
           "enumeration did not clear count for a null metadata output");
    Expect(enumerate(&metadata, &metadataCount) == S_OK && metadata && metadataCount == 1,
           "SystemData metadata enumeration failed");
    Expect(RedXeAsciiEqualsIgnoreCase(metadata[0].id, "builtin.system-data") &&
               metadata[0].capabilities == RedXePluginCapabilityDataProvider,
           "SystemData metadata is invalid");

    const RedXePluginSettingsContract* settings = reinterpret_cast<const RedXePluginSettingsContract*>(1);
    Expect(getSettings("missing", &settings) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !settings,
           "unknown settings lookup did not clear its output");
    Expect(getSettings("builtin.system-data", &settings) == S_OK && settings && settings->versionMajor == 1,
           "SystemData settings contract is unavailable");

    void* unsupported = reinterpret_cast<void*>(1);
    Expect(create(__uuidof(IRedXeWidgetProvider), nullptr, nullptr, "builtin.system-data", &unsupported) ==
                   E_NOINTERFACE &&
               !unsupported,
           "SystemData accepted the widget-provider IID or retained an output");
    void* missing = reinterpret_cast<void*>(1);
    Expect(create(__uuidof(IRedXeDataProvider), nullptr, nullptr, "missing", &missing) ==
                   HRESULT_FROM_WIN32(ERROR_NOT_FOUND) &&
               !missing,
           "SystemData accepted an unknown plugin ID or retained an output");

    void* providerObject = nullptr;
    Expect(create(__uuidof(IRedXeDataProvider), nullptr, nullptr, "builtin.system-data", &providerObject) == S_OK &&
               providerObject,
           "SystemData provider creation failed");
    wil::com_ptr_nothrow<IRedXeDataProvider> provider;
    provider.attach(static_cast<IRedXeDataProvider*>(providerObject));

    wil::com_ptr_nothrow<IUnknown> unknownFromProvider;
    wil::com_ptr_nothrow<IUnknown> unknownFromQuery;
    Expect(provider.query_to(unknownFromProvider.put()) == S_OK &&
               provider->QueryInterface(__uuidof(IRedXeDataProvider),
                                        reinterpret_cast<void**>(unknownFromQuery.put())) == S_OK,
           "SystemData QueryInterface failed");
    Expect(static_cast<IUnknown*>(provider.get()) == unknownFromProvider.get() &&
               unknownFromProvider.get() == unknownFromQuery.get(),
           "SystemData interfaces do not share one controlling IUnknown");

    const RedXeDataSetDescriptor* descriptors = reinterpret_cast<const RedXeDataSetDescriptor*>(1);
    std::uint32_t descriptorCount = 99;
    Expect(provider->GetDataSets(nullptr, &descriptorCount) == E_POINTER && descriptorCount == 0,
           "GetDataSets did not clear count on a null descriptor output");
    Expect(provider->GetDataSets(&descriptors, &descriptorCount) == S_OK && descriptors && descriptorCount == 2,
           "SystemData descriptors are unavailable");
    const RedXeDataSetDescriptor* summaryDescriptor = FindDataSet(descriptors, descriptorCount, "system.summary");
    const RedXeDataSetDescriptor* processDescriptor = FindDataSet(descriptors, descriptorCount, "process.list");
    Expect(summaryDescriptor && processDescriptor && summaryDescriptor->maximumRows == 1 &&
               processDescriptor->maximumRows == 2048,
           "SystemData descriptor bounds are wrong");

    const RedXeDataSnapshot* snapshot = reinterpret_cast<const RedXeDataSnapshot*>(1);
    Expect(provider->CollectSnapshot("missing", &snapshot) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !snapshot,
           "unknown dataset collection did not clear its output");
    Expect(provider->CollectSnapshot("system.summary", &snapshot) == S_OK && snapshot,
           "system summary collection failed");
    ValidateSnapshot(*snapshot, *summaryDescriptor);
    Expect(snapshot->rowCount == 1, "system summary does not contain exactly one row");
    const std::uint64_t firstSequence = snapshot->sequence;
    Expect(provider->CollectSnapshot("system.summary", &snapshot) == S_OK && snapshot->sequence > firstSequence,
           "system summary sequence did not advance");
    ValidateSnapshot(*snapshot, *summaryDescriptor);

    Expect(provider->CollectSnapshot("process.list", &snapshot) == S_OK && snapshot, "process list collection failed");
    ValidateSnapshot(*snapshot, *processDescriptor);
    const std::uint32_t processIdColumn = FindColumn(*processDescriptor, "processId");
    const std::uint32_t imageNameColumn = FindColumn(*processDescriptor, "imageName");
    bool foundCurrentProcess = false;
    for (std::uint32_t rowIndex = 0; rowIndex < snapshot->rowCount; ++rowIndex)
    {
        const RedXeDataRow& row = snapshot->rows[rowIndex];
        if (row.values[processIdColumn].uint64Value == GetCurrentProcessId())
        {
            foundCurrentProcess = row.values[imageNameColumn].quality == RedXeDataQualityGood &&
                                  row.values[imageNameColumn].utf16Characters != 0;
            break;
        }
    }
    Expect(foundCurrentProcess, "process list does not contain the test process");
    const std::uint64_t processSequence = snapshot->sequence;
    Expect(provider->CollectSnapshot("process.list", &snapshot) == S_OK && snapshot->sequence > processSequence,
           "process list sequence did not advance");
    ValidateSnapshot(*snapshot, *processDescriptor);
}
} // namespace

int wmain()
{
    try
    {
        Run();
        std::wcout << L"System data tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "System data tests failed: " << error.what() << '\n';
        return 1;
    }
}
