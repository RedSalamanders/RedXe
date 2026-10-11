#include "../../RedXe/PluginHost.h"
#include "../../RedXe/RuntimeProbe.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <yyjson.h>

void TestRuntimeProbe(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] production resource probe: deadlines, resets, bounded JSONL and writer\n";
    auto check = [&](bool condition, const wchar_t* name)
    {
        if (!condition)
        {
            success = false;
            std::wcerr << L"FAIL runtime probe: " << name << L'\n';
        }
    };
    try
    {
        RuntimeProbe probe;
        probe.Start(0, 100);
        check(!probe.Enabled() && !probe.Due(UINT64_MAX) && probe.Deadline() == 0 &&
                  RuntimeProbeWaitTimeout(100, 0, 0) == INFINITE,
              L"disabled owns no timeout");
        probe.Start(5, 100);
        check(!probe.Due(5099) && probe.Due(5100), L"first sample is due at the selected interval");
        check(RuntimeProbeWaitTimeout(100, 2100, probe.Deadline()) == 2000 &&
                  RuntimeProbeWaitTimeout(100, 8100, probe.Deadline()) == 5000 &&
                  RuntimeProbeWaitTimeout(5100, 0, probe.Deadline()) == 0,
              L"diagnostic and frame deadlines retain the earliest wait");
        RuntimeProbeSample s;
        s.graphics.generation = 1;
        s.graphics.presentResult = s.graphics.statisticsResult = S_OK;
        s.graphics.presentCount = UINT32_MAX - 2;
        s.graphics.statistics.PresentCount = 10;
        s.graphics.statistics.PresentRefreshCount = 11;
        s.graphics.statistics.SyncRefreshCount = 12;
        s.graphics.statistics.SyncQPCTime.QuadPart = 100;
        probe.Complete(s, 5100);
        check(s.deltaValid == 0 && s.intervalMilliseconds == 5000, L"first sample has no fabricated delta");
        s.graphics.presentCount = 2;
        s.graphics.statistics.PresentCount = 13;
        s.graphics.statistics.PresentRefreshCount = 15;
        s.graphics.statistics.SyncRefreshCount = 17;
        s.graphics.statistics.SyncQPCTime.QuadPart = 200;
        probe.Complete(s, 18000);
        check(s.deltaValid == 3 && s.presents == 5 && s.presentDelta == 3 && s.refreshDelta == 4 &&
                  s.syncRefreshDelta == 5 && s.syncQpcDelta == 100 && s.intervalMilliseconds == 12900 &&
                  probe.Deadline() == 23000 && !probe.Due(18001),
              L"counter wrap and late sampling cause one sample, no catch-up burst");
        s.graphics.statisticsResult = DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
        probe.Complete(s, 23000);
        check(s.deltaValid == 1, L"disjoint DXGI statistics are unavailable");
        s.graphics.statisticsResult = S_OK;
        probe.Complete(s, 28000);
        check(s.deltaValid == 1, L"first statistics after a failure have no delta");
        s.graphics.generation = 2;
        probe.Complete(s, 33000);
        check(s.deltaValid == 0, L"device/swap-chain recreation resets all deltas");
        s.graphics.statistics.SyncQPCTime.QuadPart = 50;
        probe.Complete(s, 38000);
        check(s.deltaValid == 1, L"QPC reset never becomes a huge unsigned interval");

        s.process = QueryProcessResources();
        check(s.process.valid == 127 && s.process.memory.privateBytes > 0 && s.process.memory.workingSetBytes > 0 &&
                  s.process.threads > 0 && s.process.modules > 0 && s.process.handles > 0,
              L"real process resources have independent validity bits");
        std::array<char, kRuntimeProbeLineCapacity> line{};
        auto bytes = FormatRuntimeProbeLine(s, line.data(), line.size());
        using Document = wil::unique_any<yyjson_doc*, decltype(&yyjson_doc_free), yyjson_doc_free>;
        Document document(yyjson_read(line.data(), bytes, 0));
        check(document && yyjson_arr_size(yyjson_obj_get(yyjson_doc_get_root(document.get()), "process")) == 11 &&
                  yyjson_arr_size(yyjson_obj_get(yyjson_doc_get_root(document.get()), "present")) == 11,
              L"record is structured JSON with every diagnostic field");
        s.tick = s.intervalMilliseconds = s.sampleMicroseconds = s.scheduledDeadline = UINT64_MAX;
        s.page = s.state = s.process.valid = UINT32_MAX;
        s.process.memory = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
        s.process.cpu100ns = UINT64_MAX;
        s.process.handles = s.process.threads = s.process.gdi = s.process.user = s.process.modules = MAXDWORD;
        s.graphics.generation = UINT64_MAX;
        s.graphics.presentResult = s.graphics.statisticsResult = s.graphics.localResult = s.graphics.nonLocalResult =
            static_cast<HRESULT>(-1);
        s.graphics.presentCount = s.deltaValid = s.presents = s.presentDelta = s.refreshDelta = s.syncRefreshDelta =
            UINT32_MAX;
        s.syncQpcDelta = s.syncGpuDelta = UINT64_MAX;
        s.graphics.local = s.graphics.nonLocal = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
        s.avResult = static_cast<HRESULT>(-1);
        s.av = {sizeof(s.av), UINT32_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
                UINT64_MAX,   UINT64_MAX, UINT64_MAX, UINT64_MAX};
        bytes = FormatRuntimeProbeLine(s, line.data(), line.size());
        document.reset(yyjson_read(line.data(), bytes, 0));
        check(bytes > 0 && bytes < line.size() && document &&
                  yyjson_get_uint(yyjson_arr_get(yyjson_obj_get(yyjson_doc_get_root(document.get()), "av"), 8)) ==
                      UINT64_MAX,
              L"all extreme counters fit the unchanged 1024-byte ring without truncation");
        std::array<char, 16> shortBuffer{};
        check(FormatRuntimeProbeLine(s, shortBuffer.data(), shortBuffer.size()) == 0 && shortBuffer[0] == '\0',
              L"insufficient destination refuses a partial record");
        RuntimePageStageProbe disabledStage(false, 1, -1);
        disabledStage.Select(2);
        disabledStage.Mark(RuntimePageStagePhase::Providers);
        const auto disabledSample = disabledStage.Complete(S_OK);
        check(!disabledStage.Enabled() && disabledSample.tick == 0 && disabledSample.phaseMask == 0 &&
                  disabledSample.cpuValid == 0 && !disabledStage.Callbacks() && disabledSample.callbacks.stageId == 0,
              L"disabled staging diagnostics publish no timing or CPU measurement");
        RuntimePageCallbackContext disabledCallbacks;
        uint32_t calls = 0;
        check(TraceRuntimePageCallback(nullptr, 0, RuntimePageCallbackKind::DeviceCreated,
                                       [&]() noexcept
                                       {
                                           ++calls;
                                           return S_FALSE;
                                       }) == S_FALSE &&
                  TraceRuntimePageCallback(&disabledCallbacks, 31, RuntimePageCallbackKind::Visibility,
                                           [&]() noexcept
                                           {
                                               ++calls;
                                               return E_FAIL;
                                           }) == E_FAIL &&
                  calls == 2 && disabledCallbacks.dropped == 0 && disabledCallbacks.counts == std::array<uint32_t, 4>{},
              L"disabled wrappers call once, preserve success/failure and publish no records");
        RuntimePageStageProbe stage(true, 1, -1);
        stage.Select(2);
        RuntimePageStageProbe anotherStage(true, 1, 1);
        check(stage.Callbacks() && stage.Callbacks()->page == 2 && stage.Callbacks()->stageId != 0 &&
                  anotherStage.Callbacks()->stageId != stage.Callbacks()->stageId,
              L"enabled stages have distinct process identities and the selected page");
        RuntimePageCallbackProbe callback(*stage.Callbacks(), 31, RuntimePageCallbackKind::Visibility, 1);
        Sleep(3);
        auto callbackSample = callback.Complete(E_ABORT);
        check(callbackSample.stageId == stage.Callbacks()->stageId && callbackSample.page == 2 &&
                  callbackSample.widget == 31 && callbackSample.kind == RuntimePageCallbackKind::Visibility &&
                  callbackSample.detail == 1 && callbackSample.result == E_ABORT && callbackSample.cpuValid == 1 &&
                  callbackSample.wallMicroseconds >= 1000,
              L"real callback timing preserves ordinal, visibility, failure and independent CPU validity");
        RuntimePagePreparationProbe pending;
        int owners[3]{};
        check(pending.Empty() && pending.Arm(nullptr, *stage.Callbacks(), 1) == E_INVALIDARG &&
                  pending.Arm(&owners[0], disabledCallbacks, 1) == E_INVALIDARG && pending.Empty(),
              L"invalid preparation identities never occupy a slot");
        pending.Finish(nullptr);
        check(pending.Arm(&owners[0], *stage.Callbacks(), 0x80000001U) == S_OK &&
                  pending.Arm(&owners[1], *anotherStage.Callbacks(), 2) == S_OK &&
                  pending.Arm(&owners[2], *stage.Callbacks(), 1) == HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA),
              L"two bounded identities include bit 31 and refuse a third without replacing owners");
        auto* first = pending.Take(&owners[0], 31);
        check(first && first->stageId == stage.Callbacks()->stageId && !pending.Take(&owners[0], 31) &&
                  !pending.Take(&owners[0], 32) && !pending.Take(&owners[2], 0) && pending.Take(&owners[0], 0),
              L"only the first attempt clears each ordinal; invalid or foreign ordinals stay absent");
        pending.Finish(&owners[0]);
        check(!pending.Empty() && pending.Arm(&owners[2], *stage.Callbacks(), 4) == S_OK,
              L"completion releases only its owner and the slot can be reused");
        pending.Retain(&owners[1]);
        check(!pending.Take(&owners[2], 2) && pending.Take(&owners[1], 1),
              L"promotion retains its identity and cancels the retired owner's pending work");
        pending.Finish(&owners[1]);
        check(pending.Empty() && pending.Arm(&owners[0], *stage.Callbacks(), 0) == S_OK && pending.Empty(),
              L"a page with no prepared interfaces completes without retained state");
        check(pending.Arm(&owners[0], *stage.Callbacks(), 1) == S_OK &&
                  pending.Arm(&owners[0], *anotherStage.Callbacks(), 2) == S_OK && !pending.Take(&owners[0], 0),
              L"rearming retires the previous identity before reuse");
        pending.Retain(nullptr);
        check(pending.Empty(), L"device loss or shutdown retires all borrowed identities");
        stage.Mark(RuntimePageStagePhase::Retirement);
        Sleep(2);
        stage.Mark(RuntimePageStagePhase::SettingsCopy);
        stage.Mark(RuntimePageStagePhase::Count);
        auto stageSample = stage.Complete(E_INVALIDARG);
        check(stageSample.fromPage == 1 && stageSample.page == 2 && stageSample.direction == -1 &&
                  stageSample.result == E_INVALIDARG && stageSample.phaseMask == 67 && stageSample.cpuValid == 1 &&
                  stageSample.wallMicroseconds[6] >= stageSample.wallMicroseconds[1] &&
                  stageSample.wallMicroseconds[1] >= stageSample.wallMicroseconds[0],
              L"real staging clock preserves monotonic phases, failure and independent CPU validity");
        stageSample.tick = stageSample.cpu100ns = UINT64_MAX;
        stageSample.fromPage = stageSample.page = stageSample.phaseMask = stageSample.cpuValid = UINT32_MAX;
        stageSample.direction = INT32_MIN;
        stageSample.wallMicroseconds.fill(UINT64_MAX);
        stageSample.callbacks.stageId = UINT64_MAX;
        stageSample.callbacks.counts.fill(UINT32_MAX);
        stageSample.callbacks.dropped = UINT32_MAX;
        stageSample.preparationTraceResult = static_cast<HRESULT>(-1);
        bytes = FormatRuntimePageStageLine(stageSample, line.data(), line.size());
        document.reset(yyjson_read(line.data(), bytes, 0));
        check(bytes > 0 && bytes < line.size() && document &&
                  yyjson_arr_size(yyjson_obj_get(yyjson_doc_get_root(document.get()), "wall_us")) == 7 &&
                  yyjson_get_sint(yyjson_obj_get(yyjson_doc_get_root(document.get()), "direction")) == INT32_MIN &&
                  yyjson_get_uint(yyjson_obj_get(yyjson_doc_get_root(document.get()), "cpu_100ns")) == UINT64_MAX &&
                  yyjson_arr_size(yyjson_obj_get(yyjson_doc_get_root(document.get()), "callbacks")) == 4,
              L"extreme staging counters and negative direction fit the unchanged logger slot");
        check(FormatRuntimePageStageLine(stageSample, shortBuffer.data(), shortBuffer.size()) == 0 &&
                  shortBuffer[0] == '\0' && FormatRuntimePageStageLine(stageSample, nullptr, 0) == 0,
              L"staging diagnostics refuse a truncated or absent destination");
        callbackSample.stageId = callbackSample.tick = callbackSample.wallMicroseconds = callbackSample.cpu100ns =
            UINT64_MAX;
        callbackSample.page = callbackSample.widget = callbackSample.detail = callbackSample.cpuValid = UINT32_MAX;
        callbackSample.kind = static_cast<RuntimePageCallbackKind>(UINT32_MAX);
        callbackSample.result = static_cast<HRESULT>(-1);
        const auto oldCounts = stage.Callbacks()->counts;
        RecordRuntimePageCallback(*stage.Callbacks(), callbackSample);
        check(stage.Callbacks()->dropped == 1 && stage.Callbacks()->counts == oldCounts,
              L"invalid callback kind or identity is rejected before logging or indexing counts");
        bytes = FormatRuntimePageCallbackLine(callbackSample, line.data(), line.size());
        document.reset(yyjson_read(line.data(), bytes, 0));
        check(bytes > 0 && bytes < line.size() && document &&
                  yyjson_get_uint(yyjson_arr_get(
                      yyjson_arr_get(yyjson_obj_get(yyjson_doc_get_root(document.get()), "calls"), 0), 5)) ==
                      UINT64_MAX &&
                  yyjson_get_uint(yyjson_arr_get(
                      yyjson_arr_get(yyjson_obj_get(yyjson_doc_get_root(document.get()), "calls"), 0), 4)) ==
                      UINT32_MAX,
              L"maximum-width individual callback values fit one complete logger slot");
        std::array<RuntimePageCallbackSample, kRuntimePageCallbackBatchCapacity> batch{};
        batch.fill(callbackSample);
        bytes =
            FormatRuntimePageCallbackBatch(batch.data(), static_cast<uint32_t>(batch.size()), line.data(), line.size());
        document.reset(yyjson_read(line.data(), bytes, 0));
        check(bytes > 0 && bytes < line.size() && document &&
                  yyjson_arr_size(yyjson_obj_get(yyjson_doc_get_root(document.get()), "calls")) == batch.size(),
              L"six maximum-width individual timings fit one complete unchanged logger slot");
        ++batch[1].page;
        check(FormatRuntimePageCallbackBatch(batch.data(), static_cast<uint32_t>(batch.size()), line.data(),
                                             line.size()) == 0 &&
                  line[0] == '\0' && FormatRuntimePageCallbackBatch(nullptr, 1, line.data(), line.size()) == 0 &&
                  FormatRuntimePageCallbackBatch(batch.data(), 0, line.data(), line.size()) == 0 &&
                  FormatRuntimePageCallbackBatch(batch.data(), 7, line.data(), line.size()) == 0,
              L"mixed identities, absent samples and out-of-bound batch counts are refused");
        RuntimePagePreparationSample preparedSample{stageSample.callbacks, UINT32_MAX, UINT32_MAX, E_ABORT};
        bytes = FormatRuntimePagePreparationLine(preparedSample, line.data(), line.size());
        document.reset(yyjson_read(line.data(), bytes, 0));
        check(bytes > 0 && bytes < line.size() && document &&
                  yyjson_get_uint(yyjson_obj_get(yyjson_doc_get_root(document.get()), "pending_mask")) == UINT32_MAX,
              L"maximum-width preparation completion/cancellation fits one complete logger slot");
        check(FormatRuntimePageCallbackLine(callbackSample, shortBuffer.data(), shortBuffer.size()) == 0 &&
                  shortBuffer[0] == '\0' && FormatRuntimePageCallbackLine(callbackSample, nullptr, 0) == 0 &&
                  FormatRuntimePagePreparationLine(preparedSample, shortBuffer.data(), shortBuffer.size()) == 0 &&
                  shortBuffer[0] == '\0' && FormatRuntimePagePreparationLine(preparedSample, nullptr, 0) == 0,
              L"callback and terminal records refuse partial or absent destinations");
        const auto root =
            std::filesystem::temp_directory_path() /
            (L"RedXe-probe-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(root);
        const auto remove = wil::scope_exit(
            [&]() noexcept
            {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            });
        {
            PluginHost host;
            AVControl::ProbeStatistics absent;
            check(host.QueryAvProbeStatistics(absent) == S_FALSE && absent.widgets == 0,
                  L"diagnostics never map an absent AV module");
            check(SUCCEEDED(host.SetLogDirectory(root.c_str())) && host.LogRuntimeProbe(s) == S_OK &&
                      host.LogRuntimePageStage(stageSample) == S_OK && host.FlushLog(2000) == S_OK,
                  L"structured record drains through the production logger");
            check(host.LogRuntimePageCallback(callbackSample) == S_OK &&
                      host.LogRuntimePagePreparation(preparedSample) == S_OK && host.FlushLog(2000) == S_OK,
                  L"individual and terminal records drain through the same production writer");
        }
        SYSTEMTIME utc{};
        GetSystemTime(&utc);
        std::array<wchar_t, kRedXeLogFileNameCapacity> name{};
        check(RedXeFormatLogFileName(name.data(), name.size(), utc), L"UTC log filename");
        std::ifstream input(root / name.data(), std::ios::binary);
        const std::string log{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        check(log.find("\"event\":\"probe\",\"v\":1") != std::string::npos &&
                  log.find("\"event\":\"page-stage\",\"v\":1") != std::string::npos &&
                  log.find("\"event\":\"page-callback\",\"v\":1") != std::string::npos &&
                  log.find("\"event\":\"page-prepared\",\"v\":1") != std::string::npos &&
                  log.find("\"direction\":-2147483648") != std::string::npos &&
                  log.find("18446744073709551615") != std::string::npos,
              L"asynchronous writer preserves the full raw JSONL fields");
    }
    catch (...)
    {
        check(false, L"unexpected fixture failure");
    }
}
