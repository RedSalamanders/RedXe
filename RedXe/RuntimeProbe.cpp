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
