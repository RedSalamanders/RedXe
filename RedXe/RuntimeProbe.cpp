#include "RuntimeProbe.h"
#include <cstdio>

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
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        return false;
    const auto ticks = [](const FILETIME& value) noexcept
    { return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
    cpu = ticks(kernel) + ticks(user);
    return true;
}

RuntimePageStageProbe::RuntimePageStageProbe(bool enabled, uint32_t fromPage, int32_t direction) noexcept
    : _enabled(enabled)
{
    if (!_enabled)
        return;
    _sample.fromPage = fromPage;
    _sample.direction = direction;
    _sample.tick = GetTickCount64();
    _cpuStartedValid = QueryCpu(_cpuStarted);
    _started = std::chrono::steady_clock::now();
}

void RuntimePageStageProbe::Select(uint32_t page) noexcept
{
    if (_enabled)
        _sample.page = page;
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
        "\"cpu_valid\":%u,\"cpu_100ns\":%llu,\"wall_us\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu]}\n",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds, GetCurrentProcessId(),
        s.tick, s.fromPage, s.page, s.direction, static_cast<uint32_t>(s.result), s.phaseMask, s.cpuValid, s.cpu100ns,
        w[0], w[1], w[2], w[3], w[4], w[5], w[6]);
    if (bytes <= 0 || static_cast<size_t>(bytes) >= capacity)
    {
        line[0] = '\0';
        return 0;
    }
    return static_cast<uint32_t>(bytes);
}
