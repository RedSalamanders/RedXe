#include "PluginHost.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <memory>
#include <new>
#include <strsafe.h>
#include <utility>

namespace
{
constexpr size_t kInitialPathCapacity = 512;
constexpr size_t kMaximumPathCapacity = 32768;

template <typename Function> [[nodiscard]] Function ResolveFunction(HMODULE module, const char* name) noexcept
{
    const FARPROC procedure = GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(procedure));
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

[[nodiscard]] size_t FindBundledPluginIndex(const char* pluginId) noexcept
{
    if (!RedXeIsValidMachineId(pluginId))
    {
        return kRedXeBundledPlugins.size();
    }
    for (size_t index = 0; index < kRedXeBundledPlugins.size(); ++index)
    {
        const RedXeBundledPluginSpec& candidate = kRedXeBundledPlugins[index];
        if (RedXeAsciiEqualsIgnoreCase(candidate.pluginId, pluginId))
        {
            return index;
        }
    }
    return kRedXeBundledPlugins.size();
}

[[nodiscard]] HRESULT BuildPluginPath(const wchar_t* moduleName, wchar_t* path, size_t capacity) noexcept
{
    if (!moduleName || moduleName[0] == L'\0' || !path || capacity == 0 ||
        capacity > static_cast<size_t>(MAXDWORD))
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
    const size_t prefixLength = static_cast<size_t>(separator - path) + 1;
    HRESULT result = StringCchCopyW(separator + 1, capacity - prefixLength, L"Plugins\\");
    if (FAILED(result))
    {
        return result;
    }
    return StringCchCatW(separator + 1, capacity - prefixLength, moduleName);
}

[[nodiscard]] HRESULT ValidateMetadata(const RedXePluginMetadata* metadata, uint32_t count,
                                       const char* expectedPluginId, uint32_t& capabilities) noexcept
{
    capabilities = RedXePluginCapabilityNone;
    if (!metadata || count == 0 || count > 256 || !RedXeIsValidMachineId(expectedPluginId))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    bool selected = false;
    for (uint32_t index = 0; index < count; ++index)
    {
        const RedXePluginMetadata& candidate = metadata[index];
        if (candidate.sizeBytes != sizeof(RedXePluginMetadata) || !RedXeIsValidMachineId(candidate.id) ||
            !candidate.displayName || !candidate.description || !candidate.author || !candidate.version ||
            (candidate.capabilities & ~(RedXePluginCapabilityWidgetProvider | RedXePluginCapabilityDataSource)) != 0)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        for (uint32_t previous = 0; previous < index; ++previous)
        {
            if (RedXeAsciiEqualsIgnoreCase(metadata[previous].id, candidate.id))
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
        }
        if (RedXeAsciiEqualsIgnoreCase(candidate.id, expectedPluginId))
        {
            capabilities = candidate.capabilities;
            selected = true;
        }
    }
    return selected ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

[[nodiscard]] int CompareDueDataSetIds(const char* left, const char* right) noexcept
{
    const bool leftStatus = left != nullptr && RedXeAsciiEqualsIgnoreCase(left, "source.status");
    const bool rightStatus = right != nullptr && RedXeAsciiEqualsIgnoreCase(right, "source.status");
    if (leftStatus != rightStatus)
    {
        return leftStatus ? 1 : -1;
    }
    if (!left)
    {
        return right ? -1 : 0;
    }
    if (!right)
    {
        return 1;
    }
    while (*left != '\0' && *right != '\0')
    {
        unsigned char leftValue = static_cast<unsigned char>(*left);
        unsigned char rightValue = static_cast<unsigned char>(*right);
        if (leftValue >= 'A' && leftValue <= 'Z')
        {
            leftValue = static_cast<unsigned char>(leftValue - 'A' + 'a');
        }
        if (rightValue >= 'A' && rightValue <= 'Z')
        {
            rightValue = static_cast<unsigned char>(rightValue - 'A' + 'a');
        }
        if (leftValue != rightValue)
        {
            return leftValue < rightValue ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right)
    {
        return 0;
    }
    return *left == '\0' ? -1 : 1;
}

void SortDueDataSets(std::array<const char*, RedXeDataCollectMaximumDataSets>& ids,
                     std::array<uint32_t, RedXeDataCollectMaximumDataSets>& indices, uint32_t count) noexcept
{
    for (uint32_t index = 1; index < count; ++index)
    {
        const char* id = ids[index];
        const uint32_t dataSetIndex = indices[index];
        uint32_t insert = index;
        while (insert > 0 && CompareDueDataSetIds(ids[insert - 1], id) > 0)
        {
            ids[insert] = ids[insert - 1];
            indices[insert] = indices[insert - 1];
            --insert;
        }
        ids[insert] = id;
        indices[insert] = dataSetIndex;
    }
}

[[nodiscard]] bool CollectResultMatchesRequest(const RedXeDataCollectRequest& request,
                                               const RedXeDataCollectResult* result) noexcept
{
    if (!result || result->sizeBytes != sizeof(RedXeDataCollectResult) ||
        result->snapshotCount != request.dataSetCount || !result->snapshots)
    {
        return false;
    }
    for (uint32_t index = 0; index < request.dataSetCount; ++index)
    {
        const RedXeDataSnapshot* snapshot = result->snapshots[index];
        if (!snapshot || snapshot->sizeBytes != sizeof(RedXeDataSnapshot) || !snapshot->dataSetId ||
            !RedXeAsciiEqualsIgnoreCase(snapshot->dataSetId, request.dataSetIds[index]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] DWORD WaitMilliseconds(uint64_t now, uint64_t due) noexcept
{
    if (due <= now)
    {
        return 0;
    }
    return static_cast<DWORD>(std::min<uint64_t>(due - now, MAXDWORD - 1ULL));
}
} // namespace

class PluginHost::DataProvider final : public IRedXeDataProvider
{
  public:
    DataProvider(PluginHost& host, size_t providerIndex) noexcept : _host(&host), _providerIndex(providerIndex) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId != __uuidof(IUnknown) && interfaceId != __uuidof(IRedXeDataProvider))
        {
            return E_NOINTERFACE;
        }
        *result = static_cast<IRedXeDataProvider*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return ++_references;
    }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        const ULONG references = --_references;
        if (references == 0)
        {
            delete this;
        }
        return references;
    }

    HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors,
                                          uint32_t* count) noexcept override
    {
        return _host ? _host->GetDataSets(_providerIndex, descriptors, count) : E_UNEXPECTED;
    }

    HRESULT STDMETHODCALLTYPE Subscribe(const RedXeDataSubscriptionOptions* options, IRedXeDataSink* sink,
                                        IRedXeDataSubscription** subscription) noexcept override
    {
        return _host ? _host->Subscribe(_providerIndex, options, sink, subscription) : E_UNEXPECTED;
    }

  private:
    std::atomic<ULONG> _references{1};
    PluginHost* _host;
    size_t _providerIndex;
};

class PluginHost::Subscription final : public IRedXeDataSubscription
{
  public:
    Subscription(PluginHost& host, size_t index, uint64_t token) noexcept
        : _host(&host), _index(index), _token(token)
    {
    }

    ~Subscription()
    {
        if (_host)
        {
            _host->RemoveSubscription(_index, _token);
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId != __uuidof(IUnknown) && interfaceId != __uuidof(IRedXeDataSubscription))
        {
            return E_NOINTERFACE;
        }
        *result = static_cast<IRedXeDataSubscription*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return ++_references;
    }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        const ULONG references = --_references;
        if (references == 0)
        {
            delete this;
        }
        return references;
    }

    HRESULT STDMETHODCALLTYPE SetActive(BOOL active) noexcept override
    {
        return _host ? _host->SetSubscriptionActive(_index, _token, active != FALSE) : E_UNEXPECTED;
    }

  private:
    std::atomic<ULONG> _references{1};
    PluginHost* _host;
    size_t _index;
    uint64_t _token;
};

PluginHost::~PluginHost()
{
    StopDataService();
    ShutdownModules();
}

IRedXeHost* PluginHost::Interface() noexcept
{
    return static_cast<IRedXeHost*>(this);
}

HRESULT PluginHost::QueryInterface(REFIID interfaceId, void** result) noexcept
{
    if (!result)
    {
        return E_POINTER;
    }
    *result = nullptr;
    if (interfaceId != __uuidof(IUnknown) && interfaceId != __uuidof(IRedXeHost))
    {
        return E_NOINTERFACE;
    }
    *result = static_cast<IRedXeHost*>(this);
    AddRef();
    return S_OK;
}

ULONG PluginHost::AddRef() noexcept
{
    return ++_references;
}

ULONG PluginHost::Release() noexcept
{
    return --_references;
}

HRESULT PluginHost::LoadModule(const RedXeBundledPluginSpec& spec, ModuleSlot& slot) noexcept
{
    if (slot.module || slot.create || slot.enumerate || slot.getSettingsContract || slot.shutdown)
    {
        return E_UNEXPECTED;
    }

    std::array<wchar_t, kInitialPathCapacity> shortPath{};
    std::unique_ptr<wchar_t[]> longPath;
    wchar_t* pluginPath = shortPath.data();
    size_t pathCapacity = shortPath.size();
    HRESULT result = BuildPluginPath(spec.moduleName, pluginPath, pathCapacity);
    if (result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
    {
        longPath.reset(new (std::nothrow) wchar_t[kMaximumPathCapacity]);
        if (!longPath)
        {
            return E_OUTOFMEMORY;
        }
        pluginPath = longPath.get();
        pathCapacity = kMaximumPathCapacity;
        result = BuildPluginPath(spec.moduleName, pluginPath, pathCapacity);
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
    const RedXeEnumeratePluginsFn enumerate =
        ResolveFunction<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    if (!create || !enumerate)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    const RedXePluginMetadata* metadata = nullptr;
    uint32_t metadataCount = 0;
    result = enumerate(&metadata, &metadataCount);
    uint32_t capabilities = RedXePluginCapabilityNone;
    if (SUCCEEDED(result))
    {
        result = ValidateMetadata(metadata, metadataCount, spec.pluginId, capabilities);
    }
    if (FAILED(result))
    {
        return result;
    }

    slot.create = create;
    slot.enumerate = enumerate;
    slot.getSettingsContract =
        ResolveFunction<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);
    slot.shutdown = ResolveFunction<RedXePluginShutdownFn>(module.get(), kRedXePluginShutdownExport);
    slot.capabilities = capabilities;
    slot.module = std::move(module);
    return S_OK;
}

HRESULT PluginHost::GetPluginModule(const char* pluginId, uint32_t requiredCapabilities,
                                    ModuleView* module) noexcept
{
    if (module)
    {
        *module = ModuleView{};
    }
    if (!module)
    {
        return E_POINTER;
    }
    const size_t pluginIndex = FindBundledPluginIndex(pluginId);
    if (pluginIndex >= kRedXeBundledPlugins.size())
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    const RedXeBundledPluginSpec& spec = kRedXeBundledPlugins[pluginIndex];
    ModuleSlot& slot = _modules[pluginIndex];
    HRESULT result = S_OK;
    if (!slot.module)
    {
        result = LoadModule(spec, slot);
    }
    if (FAILED(result))
    {
        return result;
    }
    if ((slot.capabilities & requiredCapabilities) != requiredCapabilities)
    {
        return E_NOINTERFACE;
    }
    module->create = slot.create;
    module->getSettingsContract = slot.getSettingsContract;
    return S_OK;
}

HRESULT PluginHost::ValidateDataSets(ProviderRuntime& runtime) noexcept
{
    if (!runtime.descriptors || runtime.descriptorCount == 0 || runtime.descriptorCount > runtime.dataSets.size())
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    for (uint32_t index = 0; index < runtime.descriptorCount; ++index)
    {
        const RedXeDataSetDescriptor& descriptor = runtime.descriptors[index];
        if (descriptor.sizeBytes != sizeof(RedXeDataSetDescriptor) || !RedXeIsValidMachineId(descriptor.dataSetId) ||
            !descriptor.displayName || !descriptor.description || !descriptor.columns || descriptor.columnCount == 0 ||
            descriptor.columnCount > 256 || descriptor.maximumRows == 0 ||
            descriptor.recommendedIntervalMilliseconds == 0 ||
            descriptor.recommendedIntervalMilliseconds > kMaximumSubscriptionIntervalMilliseconds)
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        for (uint32_t previous = 0; previous < index; ++previous)
        {
            if (RedXeAsciiEqualsIgnoreCase(runtime.descriptors[previous].dataSetId, descriptor.dataSetId))
            {
                return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            }
        }
        for (uint32_t columnIndex = 0; columnIndex < descriptor.columnCount; ++columnIndex)
        {
            const RedXeDataColumnDescriptor& column = descriptor.columns[columnIndex];
            if (column.sizeBytes != sizeof(RedXeDataColumnDescriptor) || !RedXeIsValidMachineId(column.columnId) ||
                !column.displayName || column.valueType <= RedXeDataValueTypeInvalid ||
                column.valueType > RedXeDataValueTypeUtf16)
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            for (uint32_t previous = 0; previous < columnIndex; ++previous)
            {
                if (RedXeAsciiEqualsIgnoreCase(descriptor.columns[previous].columnId, column.columnId))
                {
                    return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
                }
            }
        }
        runtime.dataSets[index].descriptor = &descriptor;
    }
    return S_OK;
}

HRESULT PluginHost::EnsureProvider(const char* providerId, size_t& providerIndex) noexcept
{
    providerIndex = _providers.size();
    if (!RedXeIsValidMachineId(providerId))
    {
        return E_INVALIDARG;
    }

    AcquireSRWLockShared(&_subscriptionLock);
    for (size_t index = 0; index < _providerCount; ++index)
    {
        if (RedXeAsciiEqualsIgnoreCase(_providers[index].providerId, providerId))
        {
            providerIndex = index;
            break;
        }
    }
    ReleaseSRWLockShared(&_subscriptionLock);
    if (providerIndex != _providers.size())
    {
        return S_OK;
    }

    if (_providerCount >= _providers.size())
    {
        return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    }

    ModuleView module{};
    HRESULT result = GetPluginModule(providerId, RedXePluginCapabilityDataSource, &module);
    if (FAILED(result))
    {
        return result;
    }

    ProviderRuntime runtime{};
    void* sourceObject = nullptr;
    result = module.create(__uuidof(IRedXeDataSource), nullptr, Interface(), providerId, &sourceObject);
    if (FAILED(result) || !sourceObject)
    {
        return FAILED(result) ? result : E_UNEXPECTED;
    }
    runtime.source.attach(static_cast<IRedXeDataSource*>(sourceObject));
    const size_t pluginIndex = FindBundledPluginIndex(providerId);
    runtime.providerId = kRedXeBundledPlugins[pluginIndex].pluginId;
    result = runtime.source->GetDataSets(&runtime.descriptors, &runtime.descriptorCount);
    if (SUCCEEDED(result))
    {
        result = ValidateDataSets(runtime);
    }
    if (FAILED(result))
    {
        return result;
    }

    const size_t newIndex = _providerCount;
    auto* provider = new (std::nothrow) DataProvider(*this, newIndex);
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    runtime.provider.attach(provider);

    AcquireSRWLockExclusive(&_subscriptionLock);
    _providers[newIndex] = std::move(runtime);
    ++_providerCount;
    providerIndex = newIndex;
    ReleaseSRWLockExclusive(&_subscriptionLock);
    return S_OK;
}

HRESULT PluginHost::GetDataProvider(const char* providerId, IRedXeDataProvider** provider) noexcept
{
    if (provider)
    {
        *provider = nullptr;
    }
    if (!provider)
    {
        return E_POINTER;
    }

    size_t providerIndex = 0;
    const HRESULT result = EnsureProvider(providerId, providerIndex);
    if (FAILED(result))
    {
        return result;
    }
    return _providers[providerIndex].provider->QueryInterface(__uuidof(IRedXeDataProvider),
                                                              reinterpret_cast<void**>(provider));
}

HRESULT PluginHost::GetDataSets(size_t providerIndex, const RedXeDataSetDescriptor** descriptors,
                                uint32_t* count) noexcept
{
    if (descriptors)
    {
        *descriptors = nullptr;
    }
    if (count)
    {
        *count = 0;
    }
    if (!descriptors || !count)
    {
        return E_POINTER;
    }
    if (providerIndex >= _providerCount || !_providers[providerIndex].source)
    {
        return E_UNEXPECTED;
    }
    *descriptors = _providers[providerIndex].descriptors;
    *count = _providers[providerIndex].descriptorCount;
    return S_OK;
}

HRESULT PluginHost::EnsureWorker() noexcept
{
    if (_worker.joinable())
    {
        return S_OK;
    }

    _stopEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    _changeEvent.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!_stopEvent || !_changeEvent)
    {
        const DWORD error = GetLastError();
        _changeEvent.reset();
        _stopEvent.reset();
        return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    try
    {
        _worker = std::jthread([this](std::stop_token) noexcept { Worker(); });
    }
    catch (const std::bad_alloc&)
    {
        _changeEvent.reset();
        _stopEvent.reset();
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        _changeEvent.reset();
        _stopEvent.reset();
        return E_FAIL;
    }
    return S_OK;
}

HRESULT PluginHost::Subscribe(size_t providerIndex, const RedXeDataSubscriptionOptions* options,
                              IRedXeDataSink* sink, IRedXeDataSubscription** subscription) noexcept
{
    if (subscription)
    {
        *subscription = nullptr;
    }
    if (!subscription || !sink)
    {
        return E_POINTER;
    }
    if (!options || options->sizeBytes != sizeof(RedXeDataSubscriptionOptions) ||
        !RedXeIsValidMachineId(options->dataSetId) || options->requestedIntervalMilliseconds == 0 ||
        options->requestedIntervalMilliseconds > kMaximumSubscriptionIntervalMilliseconds ||
        providerIndex >= _providerCount)
    {
        return E_INVALIDARG;
    }

    ProviderRuntime& runtime = _providers[providerIndex];
    size_t dataSetIndex = runtime.descriptorCount;
    for (size_t index = 0; index < runtime.descriptorCount; ++index)
    {
        if (RedXeAsciiEqualsIgnoreCase(runtime.descriptors[index].dataSetId, options->dataSetId))
        {
            dataSetIndex = index;
            break;
        }
    }
    if (dataSetIndex == runtime.descriptorCount)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    HRESULT result = EnsureWorker();
    if (FAILED(result))
    {
        return result;
    }

    wil::com_ptr_nothrow<IRedXeDataSink> sinkOwner;
    result = sink->QueryInterface(__uuidof(IRedXeDataSink), reinterpret_cast<void**>(sinkOwner.put()));
    if (FAILED(result))
    {
        return result;
    }

    AcquireSRWLockExclusive(&_subscriptionLock);
    size_t index = _subscriptions.size();
    for (size_t candidate = 0; candidate < _subscriptions.size(); ++candidate)
    {
        if (!_subscriptions[candidate].sink)
        {
            index = candidate;
            break;
        }
    }
    if (index == _subscriptions.size())
    {
        ReleaseSRWLockExclusive(&_subscriptionLock);
        return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    }

    uint64_t token = _nextToken++;
    if (token == 0)
    {
        token = _nextToken++;
    }
    auto* created = new (std::nothrow) Subscription(*this, index, token);
    if (!created)
    {
        ReleaseSRWLockExclusive(&_subscriptionLock);
        return E_OUTOFMEMORY;
    }

    SubscriptionSlot& slot = _subscriptions[index];
    slot.sink = std::move(sinkOwner);
    slot.token = token;
    slot.providerIndex = providerIndex;
    slot.dataSetIndex = dataSetIndex;
    slot.intervalMilliseconds = std::max(options->requestedIntervalMilliseconds,
                                         runtime.descriptors[dataSetIndex].recommendedIntervalMilliseconds);
    slot.active = false;
    ReleaseSRWLockExclusive(&_subscriptionLock);

    *subscription = static_cast<IRedXeDataSubscription*>(created);
    SetEvent(_changeEvent.get());
    return S_OK;
}

HRESULT PluginHost::SetSubscriptionActive(size_t index, uint64_t token, bool active) noexcept
{
    AcquireSRWLockExclusive(&_subscriptionLock);
    if (index >= _subscriptions.size() || !_subscriptions[index].sink || _subscriptions[index].token != token)
    {
        ReleaseSRWLockExclusive(&_subscriptionLock);
        return E_UNEXPECTED;
    }
    _subscriptions[index].active = active;
    ReleaseSRWLockExclusive(&_subscriptionLock);
    SetEvent(_changeEvent.get());
    return S_OK;
}

void PluginHost::RemoveSubscription(size_t index, uint64_t token) noexcept
{
    wil::com_ptr_nothrow<IRedXeDataSink> sink;
    AcquireSRWLockExclusive(&_subscriptionLock);
    if (index < _subscriptions.size() && _subscriptions[index].token == token)
    {
        sink = std::move(_subscriptions[index].sink);
        _subscriptions[index] = SubscriptionSlot{};
    }
    ReleaseSRWLockExclusive(&_subscriptionLock);
    sink.reset();
    if (_changeEvent)
    {
        SetEvent(_changeEvent.get());
    }
}

void PluginHost::Deliver(size_t providerIndex, size_t dataSetIndex,
                         const RedXeDataSnapshot* snapshot) noexcept
{
    if (!snapshot)
    {
        return;
    }
    AcquireSRWLockShared(&_subscriptionLock);
    for (SubscriptionSlot& slot : _subscriptions)
    {
        if (slot.sink && slot.active && slot.providerIndex == providerIndex && slot.dataSetIndex == dataSetIndex)
        {
            (void)slot.sink->OnDataSnapshot(snapshot);
        }
    }
    ReleaseSRWLockShared(&_subscriptionLock);
}

void PluginHost::Worker() noexcept
{
    HANDLE handles[] = {_stopEvent.get(), _changeEvent.get()};
    for (;;)
    {
        AcquireSRWLockExclusive(&_subscriptionLock);
        const size_t providerCount = _providerCount;
        for (size_t providerIndex = 0; providerIndex < providerCount; ++providerIndex)
        {
            ProviderRuntime& provider = _providers[providerIndex];
            for (size_t dataSetIndex = 0; dataSetIndex < provider.descriptorCount; ++dataSetIndex)
            {
                provider.dataSets[dataSetIndex].active = false;
                provider.dataSets[dataSetIndex].activeIntervalMilliseconds = kMaximumSubscriptionIntervalMilliseconds;
            }
        }
        for (const SubscriptionSlot& slot : _subscriptions)
        {
            if (!slot.sink || !slot.active || slot.providerIndex >= providerCount ||
                slot.dataSetIndex >= _providers[slot.providerIndex].descriptorCount)
            {
                continue;
            }
            DataSetRuntime& dataSet = _providers[slot.providerIndex].dataSets[slot.dataSetIndex];
            dataSet.active = true;
            dataSet.activeIntervalMilliseconds =
                std::min(dataSet.activeIntervalMilliseconds, slot.intervalMilliseconds);
        }
        ReleaseSRWLockExclusive(&_subscriptionLock);

        const uint64_t now = GetTickCount64();
        for (size_t providerIndex = 0; providerIndex < providerCount; ++providerIndex)
        {
            ProviderRuntime& provider = _providers[providerIndex];
            std::array<const char*, RedXeDataCollectMaximumDataSets> dueIds{};
            std::array<uint32_t, RedXeDataCollectMaximumDataSets> dueIndices{};
            uint32_t dueCount = 0;
            for (size_t dataSetIndex = 0; dataSetIndex < provider.descriptorCount; ++dataSetIndex)
            {
                DataSetRuntime& dataSet = provider.dataSets[dataSetIndex];
                if (!dataSet.active)
                {
                    dataSet.due = 0;
                    continue;
                }
                if ((dataSet.due == 0 || now >= dataSet.due) && dueCount < RedXeDataCollectMaximumDataSets &&
                    dataSet.descriptor && dataSet.descriptor->dataSetId)
                {
                    dueIds[dueCount] = dataSet.descriptor->dataSetId;
                    dueIndices[dueCount] = static_cast<uint32_t>(dataSetIndex);
                    ++dueCount;
                }
            }
            if (dueCount == 0)
            {
                continue;
            }
            SortDueDataSets(dueIds, dueIndices, dueCount);
            const RedXeDataCollectRequest request{
                sizeof(RedXeDataCollectRequest),
                dueIds.data(),
                dueCount,
            };
            const RedXeDataCollectResult* collectResult = nullptr;
            if (SUCCEEDED(provider.source->CollectSnapshots(&request, &collectResult)) &&
                CollectResultMatchesRequest(request, collectResult))
            {
                for (uint32_t index = 0; index < dueCount; ++index)
                {
                    Deliver(providerIndex, dueIndices[index], collectResult->snapshots[index]);
                }
            }
            const uint64_t afterCollection = GetTickCount64();
            for (uint32_t index = 0; index < dueCount; ++index)
            {
                DataSetRuntime& dataSet = provider.dataSets[dueIndices[index]];
                dataSet.due = afterCollection + dataSet.activeIntervalMilliseconds;
            }
        }

        DWORD timeout = INFINITE;
        const uint64_t afterCollection = GetTickCount64();
        for (size_t providerIndex = 0; providerIndex < providerCount; ++providerIndex)
        {
            ProviderRuntime& provider = _providers[providerIndex];
            for (size_t dataSetIndex = 0; dataSetIndex < provider.descriptorCount; ++dataSetIndex)
            {
                const DataSetRuntime& dataSet = provider.dataSets[dataSetIndex];
                if (dataSet.active)
                {
                    timeout = std::min(timeout, WaitMilliseconds(afterCollection, dataSet.due));
                }
            }
        }

        const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, timeout);
        if (waitResult == WAIT_OBJECT_0)
        {
            return;
        }
        if (waitResult == WAIT_OBJECT_0 + 1)
        {
            AcquireSRWLockExclusive(&_subscriptionLock);
            for (size_t providerIndex = 0; providerIndex < _providerCount; ++providerIndex)
            {
                ProviderRuntime& provider = _providers[providerIndex];
                for (size_t dataSetIndex = 0; dataSetIndex < provider.descriptorCount; ++dataSetIndex)
                {
                    provider.dataSets[dataSetIndex].due = 0;
                }
            }
            ReleaseSRWLockExclusive(&_subscriptionLock);
            continue;
        }
        if (waitResult != WAIT_TIMEOUT)
        {
            return;
        }
    }
}

void PluginHost::StopDataService() noexcept
{
    if (_stopEvent)
    {
        SetEvent(_stopEvent.get());
    }
    if (_worker.joinable())
    {
        _worker.join();
    }
    _worker = std::jthread{};

    for (SubscriptionSlot& slot : _subscriptions)
    {
        slot = SubscriptionSlot{};
    }
    for (size_t index = _providerCount; index > 0; --index)
    {
        ProviderRuntime& provider = _providers[index - 1];
        provider.provider.reset();
        provider.source.reset();
        provider = ProviderRuntime{};
    }
    _providerCount = 0;
    _changeEvent.reset();
    _stopEvent.reset();
}

void PluginHost::ShutdownModules() noexcept
{
    for (size_t index = _modules.size(); index > 0; --index)
    {
        ModuleSlot& slot = _modules[index - 1];
        if (slot.shutdown)
        {
            slot.shutdown();
        }
        slot.shutdown = nullptr;
        slot.getSettingsContract = nullptr;
        slot.enumerate = nullptr;
        slot.create = nullptr;
        slot.capabilities = RedXePluginCapabilityNone;
        if (slot.module)
        {
            (void)slot.module.release();
        }
    }
}
