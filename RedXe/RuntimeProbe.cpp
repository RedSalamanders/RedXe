#include "RuntimeProbe.h"
#include "PluginHost.h"
#include <atomic>
#include <bit>
#include <cstdio>

namespace
{
std::atomic<uint64_t> g_nextPageStageId{0};
[[nodiscard]] bool QueryCallingThreadCpu(uint64_t& cpu) noexcept
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        return false;
    const auto ticks = [](const FILETIME& value) noexcept
    { return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
    cpu = ticks(kernel) + ticks(user);
    return true;
}
} // namespace

void RuntimeProbe::Complete(RuntimeProbeSample& sample, uint64_t now) noexcept
{
    sample.tick = now;
    sample.intervalMilliseconds = now - _lastTick;
    sample.deltaValid = 0;
    sample.presents = sample.presentDelta = sample.refreshDelta = sample.syncRefreshDelta = 0;
    sample.syncQpcDelta = sample.syncGpuDelta = 0;
    const auto& current = sample.graphics;
    if (current.generation != 0 && current.generation == _previous.generation)
    {
        if (current.presentResult == S_OK && _previous.presentResult == S_OK)
        {
            sample.deltaValid |= 1;
            // UINT counters wrap modulo 2^32, unlike generations/QPC.
            sample.presents = current.presentCount - _previous.presentCount;
        }
        if (current.statisticsResult == S_OK && _previous.statisticsResult == S_OK &&
            current.statistics.SyncQPCTime.QuadPart >= _previous.statistics.SyncQPCTime.QuadPart &&
            current.statistics.SyncGPUTime.QuadPart >= _previous.statistics.SyncGPUTime.QuadPart)
        {
            sample.deltaValid |= 2;
            sample.presentDelta = current.statistics.PresentCount - _previous.statistics.PresentCount;
            sample.refreshDelta = current.statistics.PresentRefreshCount - _previous.statistics.PresentRefreshCount;
            sample.syncRefreshDelta = current.statistics.SyncRefreshCount - _previous.statistics.SyncRefreshCount;
            sample.syncQpcDelta = static_cast<uint64_t>(current.statistics.SyncQPCTime.QuadPart -
                                                        _previous.statistics.SyncQPCTime.QuadPart);
            sample.syncGpuDelta = static_cast<uint64_t>(current.statistics.SyncGPUTime.QuadPart -
                                                        _previous.statistics.SyncGPUTime.QuadPart);
        }
    }
    _previous = current;
    _lastTick = now;
    _deadline = now + _interval;
}

uint32_t FormatRuntimeProbeLine(const RuntimeProbeSample& s, char* line, size_t capacity) noexcept
{
    if (!line || capacity == 0)
        return 0;
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    const auto& p = s.process;
    const auto& g = s.graphics;
    const auto& a = s.av;
    // Compact numeric arrays keep the complete record in the existing 1024-byte logger slot. HRESULTs are unsigned
    // 32-bit values; a non-S_OK result/clear validity bit denotes missing data, never a successful measured zero.
    const int bytes = std::snprintf(
        line, capacity,
        "{\"ts\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"level\":\"info\",\"event\":\"probe\",\"v\":1,"
        "\"pid\":%lu,\"tick\":%llu,\"ms\":%llu,\"sample_us\":%llu,\"page\":%u,\"state\":%u,\"deadline\":%llu,"
        "\"process\":[%u,%llu,%llu,%llu,%llu,%llu,%lu,%lu,%lu,%lu,%lu],"
        "\"present\":[%llu,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu],"
        "\"local\":[%u,%llu,%llu,%llu,%llu],\"nonlocal\":[%u,%llu,%llu,%llu,%llu],"
        "\"av\":[%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu]}\n",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds, GetCurrentProcessId(),
        s.tick, s.intervalMilliseconds, s.sampleMicroseconds, s.page, s.state, s.scheduledDeadline, p.valid,
        p.memory.privateBytes, p.memory.workingSetBytes, p.memory.peakCommitBytes, p.memory.peakWorkingSetBytes,
        p.cpu100ns, p.handles, p.threads, p.gdi, p.user, p.modules, g.generation,
        static_cast<uint32_t>(g.presentResult), g.presentCount, s.deltaValid, s.presents,
        static_cast<uint32_t>(g.statisticsResult), s.presentDelta, s.refreshDelta, s.syncRefreshDelta, s.syncQpcDelta,
        s.syncGpuDelta, static_cast<uint32_t>(g.localResult), g.local.Budget, g.local.CurrentUsage,
        g.local.AvailableForReservation, g.local.CurrentReservation, static_cast<uint32_t>(g.nonLocalResult),
        g.nonLocal.Budget, g.nonLocal.CurrentUsage, g.nonLocal.AvailableForReservation, g.nonLocal.CurrentReservation,
        static_cast<uint32_t>(s.avResult), a.widgets, a.preparations, a.composites, a.surfaceBytes,
        a.replacementPeakBytes, a.surfaceAllocations, a.cachedBrushes, a.cachedTextFormats);
    if (bytes <= 0 || static_cast<size_t>(bytes) >= capacity)
    {
        line[0] = '\0';
        return 0;
    }
    return static_cast<uint32_t>(bytes);
}

bool RuntimePageStageProbe::QueryCpu(uint64_t& cpu) noexcept
{
    return QueryCallingThreadCpu(cpu);
}

RuntimePageStageProbe::RuntimePageStageProbe(bool enabled, uint32_t fromPage, int32_t direction) noexcept
    : _enabled(enabled)
{
    if (!_enabled)
        return;
    _sample.fromPage = fromPage;
    _sample.direction = direction;
    do
    {
        _callbacks.stageId = g_nextPageStageId.fetch_add(1, std::memory_order_relaxed) + 1;
    } while (_callbacks.stageId == 0);
    _sample.tick = GetTickCount64();
    _cpuStartedValid = QueryCpu(_cpuStarted);
    _started = std::chrono::steady_clock::now();
}

void RuntimePageStageProbe::Select(uint32_t page) noexcept
{
    if (_enabled)
    {
        _sample.page = page;
        _callbacks.page = page;
    }
}

void RuntimePageStageProbe::Mark(RuntimePageStagePhase phase) noexcept
{
    const auto index = static_cast<size_t>(phase);
    if (!_enabled || index >= _sample.wallMicroseconds.size())
        return;
    _sample.wallMicroseconds[index] = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - _started).count());
    _sample.phaseMask |= uint32_t{1} << index;
}

RuntimePageStageSample RuntimePageStageProbe::Complete(HRESULT result) noexcept
{
    if (!_enabled)
        return _sample;
    Mark(RuntimePageStagePhase::Complete);
    _sample.result = result;
    uint64_t cpu = 0;
    if (_cpuStartedValid && QueryCpu(cpu) && cpu >= _cpuStarted)
    {
        _sample.cpuValid = 1;
        _sample.cpu100ns = cpu - _cpuStarted;
    }
    FlushRuntimePageCallbacks(_callbacks);
    _sample.callbacks = _callbacks;
    return _sample;
}

uint32_t FormatRuntimePageStageLine(const RuntimePageStageSample& s, char* line, size_t capacity) noexcept
{
    if (!line || capacity == 0)
        return 0;
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    const auto& w = s.wallMicroseconds;
    const int bytes = std::snprintf(
        line, capacity,
        "{\"ts\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"level\":\"info\",\"event\":\"page-stage\",\"v\":1,"
        "\"pid\":%lu,\"tick\":%llu,\"from\":%u,\"page\":%u,\"direction\":%d,\"hr\":%u,\"phase_mask\":%u,"
        "\"cpu_valid\":%u,\"cpu_100ns\":%llu,\"wall_us\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu],"
        "\"stage_id\":%llu,\"callbacks\":[%u,%u,%u,%u],\"callback_dropped\":%u,\"prepare_trace_hr\":%u}\n",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds, GetCurrentProcessId(),
        s.tick, s.fromPage, s.page, s.direction, static_cast<uint32_t>(s.result), s.phaseMask, s.cpuValid, s.cpu100ns,
        w[0], w[1], w[2], w[3], w[4], w[5], w[6], s.callbacks.stageId, s.callbacks.counts[0], s.callbacks.counts[1],
        s.callbacks.counts[2], s.callbacks.counts[3], s.callbacks.dropped,
        static_cast<uint32_t>(s.preparationTraceResult));
    if (bytes <= 0 || static_cast<size_t>(bytes) >= capacity)
    {
        line[0] = '\0';
        return 0;
    }
    return static_cast<uint32_t>(bytes);
}

RuntimePageCallbackProbe::RuntimePageCallbackProbe(const RuntimePageCallbackContext& context, uint32_t widget,
                                                   RuntimePageCallbackKind kind, uint32_t detail) noexcept
{
    _sample.stageId = context.stageId;
    _sample.page = context.page;
    _sample.widget = widget;
    _sample.kind = kind;
    _sample.detail = detail;
    _sample.tick = GetTickCount64();
    _cpuStartedValid = QueryCallingThreadCpu(_cpuStarted);
    _started = std::chrono::steady_clock::now();
}

RuntimePageCallbackSample RuntimePageCallbackProbe::Complete(HRESULT result) noexcept
{
    _sample.wallMicroseconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - _started).count());
    _sample.result = result;
    uint64_t cpu = 0;
    if (_cpuStartedValid && QueryCallingThreadCpu(cpu) && cpu >= _cpuStarted)
    {
        _sample.cpuValid = 1;
        _sample.cpu100ns = cpu - _cpuStarted;
    }
    return _sample;
}

void RecordRuntimePageCallback(RuntimePageCallbackContext& context, const RuntimePageCallbackSample& sample) noexcept
{
    const size_t kind = static_cast<size_t>(sample.kind);
    if (kind >= context.counts.size() || context.stageId != sample.stageId || context.page != sample.page)
    {
        ++context.dropped;
        return;
    }
    ++context.counts[kind];
    context.pending[context.pendingCount++] = sample;
    if (context.pendingCount == context.pending.size())
        FlushRuntimePageCallbacks(context);
}

void FlushRuntimePageCallbacks(RuntimePageCallbackContext& context) noexcept
{
    if (context.pendingCount == 0)
        return;
    if (PluginHost::Instance().LogRuntimePageCallbacks(context.pending.data(), context.pendingCount) != S_OK)
        context.dropped += context.pendingCount;
    context.pendingCount = 0;
}

uint32_t FormatRuntimePageCallbackLine(const RuntimePageCallbackSample& sample, char* line, size_t capacity) noexcept
{
    return FormatRuntimePageCallbackBatch(&sample, 1, line, capacity);
}

uint32_t FormatRuntimePageCallbackBatch(const RuntimePageCallbackSample* samples, uint32_t count, char* line,
                                        size_t capacity) noexcept
{
    if (!line || capacity == 0)
        return 0;
    line[0] = '\0';
    if (!samples || count == 0 || count > kRuntimePageCallbackBatchCapacity)
        return 0;
    const auto& first = samples[0];
    for (uint32_t index = 1; index < count; ++index)
        if (samples[index].stageId != first.stageId || samples[index].page != first.page)
            return 0;
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    int bytes = std::snprintf(
        line, capacity,
        "{\"ts\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"level\":\"info\",\"event\":\"page-callback\",\"v\":1,"
        "\"pid\":%lu,\"stage_id\":%llu,\"page\":%u,\"calls\":[",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds, GetCurrentProcessId(),
        first.stageId, first.page);
    size_t used = 0;
    const auto append = [&]() noexcept
    {
        if (bytes <= 0 || static_cast<size_t>(bytes) >= capacity - used)
            return false;
        used += static_cast<size_t>(bytes);
        return true;
    };
    if (!append())
        return line[0] = '\0', 0u;
    for (uint32_t index = 0; index < count; ++index)
    {
        const auto& s = samples[index];
        bytes = std::snprintf(line + used, capacity - used, "%s[%llu,%u,%u,%u,%u,%llu,%u,%llu]", index == 0 ? "" : ",",
                              s.tick, s.widget, static_cast<uint32_t>(s.kind), s.detail,
                              static_cast<uint32_t>(s.result), s.wallMicroseconds, s.cpuValid, s.cpu100ns);
        if (!append())
            return line[0] = '\0', 0u;
    }
    bytes = std::snprintf(line + used, capacity - used, "]}\n");
    if (!append())
        return line[0] = '\0', 0u;
    return static_cast<uint32_t>(used);
}

uint32_t FormatRuntimePagePreparationLine(const RuntimePagePreparationSample& s, char* line, size_t capacity) noexcept
{
    if (!line || capacity == 0)
        return 0;
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    const auto& c = s.context;
    const int bytes = std::snprintf(
        line, capacity,
        "{\"ts\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"level\":\"info\",\"event\":\"page-prepared\",\"v\":1,"
        "\"pid\":%lu,\"stage_id\":%llu,\"page\":%u,\"expected\":%u,\"pending_mask\":%u,\"hr\":%u,"
        "\"callbacks\":[%u,%u,%u,%u],\"callback_dropped\":%u}\n",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds, GetCurrentProcessId(),
        c.stageId, c.page, s.expected, s.pendingMask, static_cast<uint32_t>(s.result), c.counts[0], c.counts[1],
        c.counts[2], c.counts[3], c.dropped);
    if (bytes <= 0 || static_cast<size_t>(bytes) >= capacity)
    {
        line[0] = '\0';
        return 0;
    }
    return static_cast<uint32_t>(bytes);
}

HRESULT RuntimePagePreparationProbe::Arm(const void* owner, const RuntimePageCallbackContext& context,
                                         uint32_t mask) noexcept
{
    if (!owner || context.stageId == 0)
        return E_INVALIDARG;
    Retire(owner);
    for (auto& pending : _pending)
    {
        if (pending.owner)
            continue;
        pending.owner = owner;
        pending.context.stageId = context.stageId;
        pending.context.page = context.page;
        pending.mask = mask;
        pending.expected = static_cast<uint32_t>(std::popcount(mask));
        Finish(owner);
        return S_OK;
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
}

RuntimePageCallbackContext* RuntimePagePreparationProbe::Take(const void* owner, uint32_t widget) noexcept
{
    if (widget >= 32)
        return nullptr;
    const uint32_t bit = uint32_t{1} << widget;
    for (auto& pending : _pending)
        if (pending.owner == owner && (pending.mask & bit) != 0)
        {
            pending.mask &= ~bit;
            return &pending.context;
        }
    return nullptr;
}

void RuntimePagePreparationProbe::Complete(Pending& pending, HRESULT result) noexcept
{
    FlushRuntimePageCallbacks(pending.context);
    const RuntimePagePreparationSample sample{pending.context, pending.expected, pending.mask, result};
    (void)PluginHost::Instance().LogRuntimePagePreparation(sample);
    pending = {};
}

void RuntimePagePreparationProbe::Finish(const void* owner) noexcept
{
    for (auto& pending : _pending)
        if (pending.owner && pending.owner == owner && pending.mask == 0)
            Complete(pending, S_OK);
}

void RuntimePagePreparationProbe::Retire(const void* owner, HRESULT result) noexcept
{
    for (auto& pending : _pending)
        if (pending.owner && pending.owner == owner)
            Complete(pending, result);
}

void RuntimePagePreparationProbe::Retain(const void* owner) noexcept
{
    for (auto& pending : _pending)
        if (pending.owner && pending.owner != owner)
            Complete(pending, E_ABORT);
}

bool RuntimePagePreparationProbe::Empty() const noexcept
{
    return !_pending[0].owner && !_pending[1].owner;
}
