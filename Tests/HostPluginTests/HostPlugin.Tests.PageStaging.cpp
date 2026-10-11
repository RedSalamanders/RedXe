#include "../../RedXe/PluginManager.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>

struct PluginManagerPageStagingTestAccess final
{
    static void FailEvidenceAllocation(bool fail) noexcept
    {
        PluginManager::_failValidationEvidenceAllocation = fail;
    }
    static size_t EvidenceBytes(const PluginManager& manager) noexcept
    {
        return manager.ValidationEvidenceBytes();
    }
    static bool HasEvidence(const PluginManager& manager) noexcept
    {
        return bool(manager._validationEvidence);
    }
    static bool SharesEvidence(const PluginManager& left, const PluginManager& right) noexcept
    {
        return left._validationEvidence && left._validationEvidence == right._validationEvidence;
    }
};
void TestCheckedPageStaging(bool& success) noexcept
{
    std::wcout << L"[ RUN      ] checked page staging and immutable validation evidence\n";
    const auto check = [&](bool condition, const wchar_t* name)
    {
        if (!condition)
        {
            success = false;
            std::wcerr << L"FAIL page staging: " << name << L'\n';
        }
    };
    try
    {
        AppSettings settings;
        HRESULT result = ParseAppSettingsJson(
            R"({"version":{"major":5},"pages":[{"id":"primary","widgets":[{"plugin":"builtin.rotating-triangle"}]},{"id":"neighbor","widgets":[{"plugin":"builtin.process-viewer","topN":11}]}]})",
            settings);
        PluginManager primary;
        if (SUCCEEDED(result))
            result = primary.Initialize(settings);
        check(SUCCEEDED(result) && PluginManagerPageStagingTestAccess::HasEvidence(primary),
              L"cold whole-document validation yields reusable proof");
        if (FAILED(result))
            return;
        {
            PluginManagerPageStagingTestAccess::FailEvidenceAllocation(true);
            const auto restore =
                wil::scope_exit([]() noexcept { PluginManagerPageStagingTestAccess::FailEvidenceAllocation(false); });
            PluginManager uncached;
            result = uncached.InitializePage(settings, 0);
            check(SUCCEEDED(result) && uncached.WidgetCount() == 1 &&
                      !PluginManagerPageStagingTestAccess::HasEvidence(uncached),
                  L"optional proof allocation failure preserves the valid cold path");
        }
        std::wcout << L"page_stage_evidence payload_bytes="
                   << PluginManagerPageStagingTestAccess::EvidenceBytes(primary) << L'\n';
        PluginManager neighbor;
        result = neighbor.InitializePage(settings, 1, &primary);
        check(SUCCEEDED(result) && neighbor.WidgetCount() == 1 && !neighbor.IsPlaceholderAt(0) &&
                  std::strcmp(neighbor.WidgetInstanceIdAt(0), settings.dashboard.pages[1].widgets[0].id.utf8.data()) ==
                      0 &&
                  PluginManagerPageStagingTestAccess::SharesEvidence(primary, neighbor) &&
                  settings.dashboard.activePageIndex == 0,
              L"a narrow neighbor selector shares proof and leaves the caller's selector untouched");
        const auto* widget = primary.WidgetAt(0);
        AppSettings changed = settings;
        changed.sourceDocument.append("\n// only source changed\n");
        changed.dashboard.activePageIndex = 1;
        changed.dashboard.activePageId = changed.dashboard.pages[1].id;
        PluginManager sourceOnly;
        result = sourceOnly.InitializePage(changed, 0, &primary);
        check(SUCCEEDED(result) && PluginManagerPageStagingTestAccess::SharesEvidence(primary, sourceOnly),
              L"source-only and valid selector changes reuse validation");
        changed.dashboard.pages[1].widgets[0].privateConfiguration = {};
        constexpr char invalidJson[] = "{bad}";
        check(changed.dashboard.pages[1].widgets[0].privateConfiguration.Assign(invalidJson) == S_OK,
              L"malformed-byte fixture is representable before cold contract validation");
        PluginManager invalid;
        check(FAILED(invalid.InitializePage(changed, 0, &primary)) && invalid.WidgetCount() == 0,
              L"malformed inactive-page JSON rejects staging of an otherwise valid target");
        check(FAILED(primary.Reconfigure(changed)) && primary.WidgetAt(0) == widget &&
                  PluginManagerPageStagingTestAccess::SharesEvidence(primary, neighbor),
              L"failed reconfiguration preserves active objects and proof");
        changed = settings;
        changed.dashboard.pages[1].widgets[0].privateConfiguration = {};
        constexpr char invalidRange[] = "{\"topN\":99,\"hideIdle\":true}";
        check(changed.dashboard.pages[1].widgets[0].privateConfiguration.Assign(invalidRange) == S_OK,
              L"range fixture is representable before inactive-page validation");
        check(FAILED(invalid.InitializePage(changed, 0, &primary)),
              L"inactive-page configuration constraints cannot be bypassed by old proof");
        changed = settings;
        changed.dashboard.pages[1].widgets[0].pluginId.bytes = UINT32_MAX;
        check(FAILED(invalid.InitializePage(changed, 0, &primary)),
              L"invalid inactive text extent is rejected before creating a view");
        changed = settings;
        const std::string oversizedJson(kPrivateConfigurationCapacity + 1, 'x');
        check(changed.dashboard.pages[1].widgets[0].privateConfiguration.Assign(oversizedJson) == E_INVALIDARG &&
                  changed == settings,
              L"an invalid inactive JSON extent cannot replace the previous immutable owner");
        changed = settings;
        changed.dashboard.pages[1].widgets.pop_back();
        check(FAILED(invalid.InitializePage(changed, 0, &primary)),
              L"mismatched inactive widget vector cannot reuse proof");
        changed = settings;
        changed.dashboard.pages.pop_back();
        check(ValidateAppSettings(changed) == E_INVALIDARG &&
                  invalid.InitializePage(changed, 0, &primary) == E_INVALIDARG,
              L"mismatched page vector is rejected without out-of-range indexing");
        check(invalid.InitializePage(settings, settings.dashboard.pageCount, &primary) == E_INVALIDARG &&
                  invalid.WidgetCount() == 0,
              L"an out-of-range target leaves the new manager empty");
        changed = settings;
        changed.dashboard.activePageId.utf8[0] = 'x';
        check(FAILED(invalid.InitializePage(changed, 0, &primary)),
              L"unknown active ID remains invalid even with an explicit target");
        changed = settings;
        changed.backgroundRgb = 0x123456;
        PluginManager recolored;
        result = recolored.InitializePage(changed, 1, &primary);
        check(SUCCEEDED(result) && recolored.WidgetBackgroundRgbAt(0) == 0x123456 &&
                  !PluginManagerPageStagingTestAccess::SharesEvidence(primary, recolored) &&
                  PluginManagerPageStagingTestAccess::HasEvidence(recolored),
              L"valid changed facts revalidate and produce fresh proof and resources");
        changed = settings;
        changed.dashboard.pages[1].widgets[0].adaptivePlacement.steps[0].sizeRatio = 0;
        check(FAILED(invalid.InitializePage(changed, 0, &primary)),
              L"changed inactive layout facts still undergo validation");
        changed = settings;
        changed.plugins[0].enabled = false;
        check(FAILED(invalid.InitializePage(changed, 1, &primary)),
              L"disabled inactive dependencies remain document-fatal");
        changed = settings;
        changed.serviceCount = 1;
        check(FAILED(invalid.InitializePage(changed, 0, &primary)), L"mismatched service storage cannot reuse proof");
        uint32_t selected = UINT32_MAX;
        check(ResolveAdjacentDashboardPage(settings, 1, selected) == S_OK && selected == 1 &&
                  ResolveAdjacentDashboardPage(settings, -1, selected) == HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS),
              L"read-only adjacency preserves non-wrapping boundaries");
        settings.dashboard.wrapPages = true;
        check(ResolveAdjacentDashboardPage(settings, -1, selected) == S_OK && selected == 1 &&
                  MoveDashboardPage(settings, -1) == S_OK && settings.dashboard.activePageIndex == selected,
              L"read-only selection and mutation agree on wrapping");
        settings.dashboard.wrapPages = false;
        settings.dashboard.activePageIndex = 0;
        settings.dashboard.activePageId = settings.dashboard.pages[0].id;
        uint64_t batchCpu[2]{};
        uint64_t batchWall[2]{};
        for (size_t mode = 0; mode < 2; ++mode)
        {
            FILETIME created{}, exited{}, kernelBefore{}, userBefore{}, kernelAfter{}, userAfter{};
            const auto ticks = [](FILETIME f) { return (uint64_t{f.dwHighDateTime} << 32U) | f.dwLowDateTime; };
            const bool before =
                GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelBefore, &userBefore) != FALSE;
            const auto started = std::chrono::steady_clock::now();
            bool valid = true;
            for (uint32_t i = 0; i < 4096; ++i)
            {
                PluginManager stage;
                valid = SUCCEEDED(stage.InitializePage(settings, 0, mode == 0 ? nullptr : &primary)) &&
                        stage.WidgetCount() == 1 && valid;
            }
            batchWall[mode] = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started)
                    .count());
            const bool after = GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelAfter, &userAfter) != FALSE;
            if (before && after)
                batchCpu[mode] = ticks(kernelAfter) + ticks(userAfter) - ticks(kernelBefore) - ticks(userBefore);
            check(valid && before && after,
                  L"repeated real provider construction succeeds and has valid batch CPU readings");
        }
        std::wcout << L"page_stage_batch count=4096 cold_wall_us=" << batchWall[0] << L" warm_wall_us=" << batchWall[1]
                   << L" cold_cpu_100ns=" << batchCpu[0] << L" warm_cpu_100ns=" << batchCpu[1] << L'\n';
        // Large valid typed documents retain full validation instead of a second large document-sized witness.
        AppSettings large = settings;
        large.dashboard.gridColumns = 32;
        large.dashboard.gridRows = 9;
        large.dashboard.pages.resize(kMaximumDashboardPages, settings.dashboard.pages[0]);
        large.dashboard.pageCount = static_cast<uint32_t>(kMaximumDashboardPages);
        for (uint32_t p = 0; p < large.dashboard.pageCount; ++p)
        {
            auto& page = large.dashboard.pages[p];
            const std::string id = "large." + std::to_string(p);
            page.id = {};
            page.id.bytes = static_cast<uint32_t>(id.size());
            std::memcpy(page.id.utf8.data(), id.data(), id.size());
            page.widgets.resize(32, page.widgets[0]);
            page.widgetCount = 32;
            for (uint32_t w = 0; w < 32; ++w)
            {
                auto& item = page.widgets[w];
                item.usesAdaptivePlacement = false;
                item.placement = {w, 0, 1, 1};
                const std::string prefix = id + "." + std::to_string(w) + ".";
                item.id = {};
                item.id.bytes = 128;
                std::memset(item.id.utf8.data(), 'a', 128);
                std::memcpy(item.id.utf8.data(), prefix.data(), prefix.size());
            }
        }
        large.dashboard.activePageId = large.dashboard.pages[0].id;
        PluginManager oversized;
        check(ValidateAppSettings(large) == S_OK, L"the oversized fixture is a valid non-overlapping document");
        result = oversized.InitializePage(large, 0, &primary);
        check(SUCCEEDED(result) && oversized.WidgetCount() == 32 &&
                  !PluginManagerPageStagingTestAccess::HasEvidence(oversized),
              L"a valid document above the proof budget succeeds through full validation without retained proof");
        std::array<wchar_t, 32768> executablePath{};
        const DWORD pathSize =
            GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        check(pathSize != 0 && pathSize < executablePath.size(),
              L"the shipped storage fixture resolves its executable");
        const auto repo =
            std::filesystem::path(executablePath.data()).parent_path().parent_path().parent_path().parent_path();
        std::ifstream file(repo / L"Settings" / L"RedXe.settings.json", std::ios::binary);
        const std::string shippedJson{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        AppSettings shipped;
        result = ParseAppSettingsJson(shippedJson, shipped);
        PluginManager shippedPage;
        if (SUCCEEDED(result))
            result = shippedPage.InitializePage(shipped, 0);
        check(SUCCEEDED(result) && PluginManagerPageStagingTestAccess::HasEvidence(shippedPage),
              L"the shipped Release document fits the bounded evidence budget");
        size_t storage = sizeof(AppSettings) + shipped.sourceDocument.capacity() + 1 +
                         shipped.plugins.capacity() * sizeof(PluginSettings) +
                         shipped.services.capacity() * sizeof(ServiceSettings) +
                         shipped.retiredServices.capacity() * sizeof(SettingsText) +
                         shipped.dashboard.pages.capacity() * sizeof(DashboardPageSettings);
        std::unordered_set<const char*> owners;
        size_t payload = 0;
        const auto countJson = [&](const JsonObjectSettings& json)
        {
            if (json.OwnedPayloadBytes() && owners.insert(json.Data()).second)
                payload += json.OwnedPayloadBytes();
        };
        for (const auto& plugin : shipped.plugins)
            countJson(plugin.privateConfiguration);
        for (const auto& service : shipped.services)
            countJson(service.privateConfiguration);
        for (const auto& page : shipped.dashboard.pages)
        {
            storage += page.widgets.capacity() * sizeof(WidgetInstanceSettings);
            for (const auto& placedWidget : page.widgets)
                countJson(placedWidget.privateConfiguration);
        }
        storage += payload;
        std::wcout << L"page_stage_shipped json_payload_bytes=" << payload << L" json_owners=" << owners.size()
                   << L" (shared-control-block and allocator overhead excluded)\n";
        std::wcout << L"page_stage_shipped settings_storage_bytes=" << storage << L" source_bytes="
                   << shipped.sourceDocument.size() << L" witness_payload_bytes="
                   << PluginManagerPageStagingTestAccess::EvidenceBytes(shippedPage) << L'\n';
    }
    catch (...)
    {
        check(false, L"unexpected exception");
    }
}
