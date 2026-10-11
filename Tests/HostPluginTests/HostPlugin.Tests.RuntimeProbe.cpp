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
                  disabledSample.cpuValid == 0,
              L"disabled staging diagnostics publish no timing or CPU measurement");
        RuntimePageStageProbe stage(true, 1, -1);
        stage.Select(2);
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
        bytes = FormatRuntimePageStageLine(stageSample, line.data(), line.size());
        document.reset(yyjson_read(line.data(), bytes, 0));
        check(bytes > 0 && bytes < line.size() && document &&
                  yyjson_arr_size(yyjson_obj_get(yyjson_doc_get_root(document.get()), "wall_us")) == 7 &&
                  yyjson_get_sint(yyjson_obj_get(yyjson_doc_get_root(document.get()), "direction")) == INT32_MIN &&
                  yyjson_get_uint(yyjson_obj_get(yyjson_doc_get_root(document.get()), "cpu_100ns")) == UINT64_MAX,
              L"extreme staging counters and negative direction fit the unchanged logger slot");
        check(FormatRuntimePageStageLine(stageSample, shortBuffer.data(), shortBuffer.size()) == 0 &&
                  shortBuffer[0] == '\0' && FormatRuntimePageStageLine(stageSample, nullptr, 0) == 0,
              L"staging diagnostics refuse a truncated or absent destination");
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
        }
        SYSTEMTIME utc{};
        GetSystemTime(&utc);
        std::array<wchar_t, kRedXeLogFileNameCapacity> name{};
        check(RedXeFormatLogFileName(name.data(), name.size(), utc), L"UTC log filename");
        std::ifstream input(root / name.data(), std::ios::binary);
        const std::string log{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        check(log.find("\"event\":\"probe\",\"v\":1") != std::string::npos &&
                  log.find("\"event\":\"page-stage\",\"v\":1") != std::string::npos &&
                  log.find("\"direction\":-2147483648") != std::string::npos &&
                  log.find("18446744073709551615") != std::string::npos,
              L"asynchronous writer preserves the full raw JSONL fields");
    }
    catch (...)
    {
        check(false, L"unexpected fixture failure");
    }
}
