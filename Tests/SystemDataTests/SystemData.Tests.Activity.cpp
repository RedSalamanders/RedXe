#include "../../Plugins/SystemData/NativeQuery.h"
#include "../../Plugins/SystemData/SystemData.Tests.Contract.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <memory>
#include <stdexcept>

namespace
{
void Require(bool passed, const char* message)
{
    if (!passed)
        throw std::runtime_error(message);
}

SystemDataTestDiagnostics Diagnostics(IRedXeSystemDataTestSource& test)
{
    SystemDataTestDiagnostics value{sizeof(value)};
    Require(test.GetTestDiagnostics(&value) == S_OK, "activity diagnostics failed");
    return value;
}

const RedXeDataCollectResult& Collect(IRedXeDataSource& source, const char* const* ids, uint32_t count)
{
    const RedXeDataCollectResult* result = nullptr;
    RedXeDataCollectRequest request{sizeof(request), ids, count};
    Require(source.CollectSnapshots(&request, &result) == S_OK && result && result->snapshotCount == count,
            "activity collection failed");
    return *result;
}
} // namespace

void TestProcessActivity(IRedXeDataSource& source, IRedXeSystemDataTestSource& test)
{
    constexpr const char* activity[] = {"process.activity"};
    constexpr const char* detailFirst[] = {"process.list", "process.activity"};
    constexpr const char* activityFirst[] = {"process.activity", "process.list"};
    auto liveBefore = Diagnostics(test);
    const auto& live = *Collect(source, activity, 1).snapshots[0];
    auto liveAfter = Diagnostics(test);
    Require(live.columnCount == 8 && live.rowCount != 0 && liveAfter.processBackend == 1 &&
                liveAfter.processValueStride == 8 && liveAfter.processHandleOpens == liveBefore.processHandleOpens &&
                liveAfter.processCollections == liveBefore.processCollections + 1 &&
                liveAfter.processHistoryCommits == liveBefore.processHistoryCommits + 1,
            "live native activity collected details or opened a process handle");
    liveBefore = Diagnostics(test);
    const auto& liveBatch = Collect(source, detailFirst, 2);
    liveAfter = Diagnostics(test);
    const auto& liveDetail = *liveBatch.snapshots[0];
    const auto& liveActivity = *liveBatch.snapshots[1];
    Require(liveDetail.rowCount == liveActivity.rowCount &&
                liveAfter.processCollections == liveBefore.processCollections + 1 &&
                liveAfter.processHistoryCommits == liveBefore.processHistoryCommits + 1 &&
                liveAfter.processHandleOpens == liveBefore.processHandleOpens + liveDetail.rowCount,
            "live activity/detail batch repeated collection or omitted legacy handle queries");
    for (uint32_t row = 0; row != liveDetail.rowCount; ++row)
        Require(liveDetail.rows[row].valueCount == 39 && liveActivity.rows[row].valueCount == 8 &&
                    liveDetail.rows[row].values == liveActivity.rows[row].values,
                "live activity/detail leading fields differ");
    auto sample = std::make_unique<RedXeNativeWalkSample>();
    Require(test.SetNextProcessWalkForTest(nullptr, 0) == E_POINTER, "activity walk accepted a null fixture");
    sample->valid = true;
    sample->processCount = 2;
    constexpr wchar_t name[] = L"activity.exe";
    std::wmemcpy(sample->imageArena.data(), name, std::size(name));
    sample->imageArenaUsed = static_cast<uint32_t>(std::size(name));
    for (uint32_t index = 0; index != sample->processCount; ++index)
    {
        auto& row = sample->processes[index];
        row.processId = 0x0FFFFFFEU - index;
        row.parentProcessId = 123;
        row.hasParent = index == 0;
        row.createTime = 100 + index;
        row.userTime = 1000;
        row.kernelTime = 2000;
        row.threadCount = 7;
        row.handleCount = 11;
        row.workingSetBytes = 65536;
        row.privateBytes = 32768;
        row.ioReadBytes = 1000;
        row.pageFaultCount = 100;
        row.imageCharacters = static_cast<uint32_t>(std::size(name) - 1);
    }
    uint64_t total = 100000;
    auto use = [&]
    {
        total += 1000;
        Require(test.SetNextProcessWalkForTest(sample.get(), total) == S_OK, "activity walk fixture failed");
    };
    auto before = Diagnostics(test);
    use();
    const auto& first = *Collect(source, activity, 1).snapshots[0];
    auto after = Diagnostics(test);
    Require(first.columnCount == 8 && first.rowCount == 2 && first.rows[0].valueCount == 8,
            "activity-only shape differs");
    Require(after.processHandleOpens == before.processHandleOpens &&
                after.processCollections == before.processCollections + 1 &&
                after.processHistoryCommits == before.processHistoryCommits + 1 && after.processValueStride == 8 &&
                after.processBackend == 1,
            "activity-only collection opened handles or populated detail values");
    Require(first.rows[0].values[2].quality == RedXeDataQualityInitializing &&
                first.rows[0].values[7].uint64Value == 123 && first.rows[0].values[7].quality == RedXeDataQualityGood &&
                first.rows[1].values[7].quality == RedXeDataQualityUnavailable,
            "activity identity or initial rate quality differs");
    const auto* leading = first.rows[0].values;
    Require(leading[0].uint64Value == sample->processes[0].processId &&
                leading[1].utf16Characters == std::size(name) - 1 &&
                std::wmemcmp(leading[1].utf16Value, name, std::size(name)) == 0 && leading[3].uint64Value == 65536 &&
                leading[4].uint64Value == 32768 && leading[5].uint64Value == 7 && leading[6].uint64Value == 11,
            "activity changed the native leading fields");
    for (uint32_t column : {0U, 1U, 3U, 4U, 5U, 6U})
        Require(leading[column].quality == RedXeDataQualityGood, "activity degraded native leading-field quality");

    for (bool reversed : {false, true})
    {
        for (uint32_t index = 0; index != sample->processCount; ++index)
        {
            sample->processes[index].userTime += 200;
            sample->processes[index].ioReadBytes += 500;
            sample->processes[index].pageFaultCount += 10;
        }
        before = Diagnostics(test);
        use();
        const auto& batch = Collect(source, reversed ? activityFirst : detailFirst, 2);
        const auto& activityView = *batch.snapshots[reversed ? 0 : 1];
        const auto& full = *batch.snapshots[reversed ? 1 : 0];
        after = Diagnostics(test);
        Require(activityView.columnCount == 8 && full.columnCount == 39 && activityView.rowCount == full.rowCount &&
                    activityView.sequence == full.sequence &&
                    activityView.timestampFileTime100ns == full.timestampFileTime100ns,
                "activity/detail batch epochs or shapes differ");
        Require(after.processCollections == before.processCollections + 1 &&
                    after.processHistoryCommits == before.processHistoryCommits + 1 &&
                    after.processHandleOpens == before.processHandleOpens + sample->processCount &&
                    after.processValueStride == 39,
                "activity/detail batch collected or committed history twice");
        for (uint32_t row = 0; row != full.rowCount; ++row)
            Require(activityView.rows[row].valueCount == 8 && full.rows[row].valueCount == 39 &&
                        activityView.rows[row].values == full.rows[row].values,
                    "activity/detail leading values are not one shared sample");
        Require(full.rows[0].values[2].quality == RedXeDataQualityGood &&
                    std::abs(full.rows[0].values[2].float64Value - 20.0) < 0.000001 &&
                    full.rows[0].values[18].quality == RedXeDataQualityGood &&
                    std::abs(full.rows[0].values[18].float64Value - 5000000.0) < 0.001,
                "activity lost CPU or I/O history before detail collection");
    }

    ++sample->processes[0].createTime;
    sample->processes[0].userTime += 10000;
    use();
    const auto& reused = *Collect(source, activity, 1).snapshots[0];
    Require(reused.rows[0].values[2].quality == RedXeDataQualityInitializing,
            "activity reused a different process creation time");
    constexpr const char* aborted[] = {"process.activity", "missing"};
    RedXeDataCollectRequest invalid{sizeof(invalid), aborted, 2};
    const RedXeDataCollectResult* failed = reinterpret_cast<const RedXeDataCollectResult*>(1);
    const uint64_t sequence = reused.sequence;
    use();
    Require(source.CollectSnapshots(&invalid, &failed) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !failed,
            "failed activity batch published snapshots");
    use();
    const auto& recovered = *Collect(source, activityFirst, 2).snapshots[0];
    Require(recovered.sequence == sequence + 1 && recovered.rows[0].values[2].quality == RedXeDataQualityInitializing,
            "failed activity batch retained live history or consumed a sequence");

    const auto original = sample->processes[0];
    sample->processCount = static_cast<uint32_t>(sample->processes.size());
    for (uint32_t row = 0; row != sample->processCount; ++row)
    {
        sample->processes[row] = original;
        sample->processes[row].processId = 0x0FFF0000U + row;
    }
    before = Diagnostics(test);
    use();
    const auto& capped = *Collect(source, activity, 1).snapshots[0];
    Require(capped.rowCount == 2048 && capped.columnCount == 8 && capped.flags == RedXeDataSnapshotFlagNone &&
                Diagnostics(test).processHandleOpens == before.processHandleOpens,
            "activity row cap opened handles or truncated short names");
    std::fill_n(sample->imageArena.data(), 63, L'x');
    sample->imageArena[63] = L'\0';
    sample->imageArenaUsed = 64;
    for (auto& row : sample->processes)
        row.imageCharacters = 63;
    use();
    const auto& names = *Collect(source, activity, 1).snapshots[0];
    Require(names.rowCount == 1024 && (names.flags & RedXeDataSnapshotFlagTruncated) != 0,
            "activity name arena overflow was not bounded");

    sample->valid = false;
    sample->processCount = 0;
    before = Diagnostics(test);
    use();
    const auto& fallback = *Collect(source, activity, 1).snapshots[0];
    after = Diagnostics(test);
    Require(fallback.columnCount == 8 && after.processBackend == 2 &&
                after.processHandleOpens > before.processHandleOpens,
            "activity fallback did not preserve per-process leading-field acquisition");
    bool ownRow = false;
    for (uint32_t index = 0; index != fallback.rowCount; ++index)
    {
        const auto& row = fallback.rows[index];
        if (row.values[0].uint64Value != GetCurrentProcessId())
            continue;
        ownRow = row.values[1].quality == RedXeDataQualityGood && row.values[1].utf16Characters != 0 &&
                 row.values[2].quality != RedXeDataQualityUnavailable &&
                 row.values[3].quality == RedXeDataQualityGood && row.values[4].quality == RedXeDataQualityGood &&
                 row.values[5].quality == RedXeDataQualityGood && row.values[6].quality == RedXeDataQualityGood &&
                 row.values[7].quality == RedXeDataQualityGood;
    }
    Require(ownRow, "activity fallback degraded accessible leading fields");
}
