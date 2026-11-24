#include "CrashHandler.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

bool TestCrashReportStore(const wchar_t* directory)
{
    if (FAILED(CrashHandler::SetCrashDirectoryForTesting(directory)))
        return false;
    const std::filesystem::path root(directory);
    const auto write = [](const std::filesystem::path& path, const char* bytes, std::streamsize count)
    {
        std::ofstream file(path, std::ios::binary);
        file.write(bytes, count);
        file.close();
        return static_cast<bool>(file);
    };
    const auto legacyDump  = root / L"RedXe-legacy.dmp";
    const auto legacyStack = root / L"RedXe-legacy.txt";
    const auto unrelated   = root / L"RedXe-report-unpaired.txt";
    if (! write(legacyDump, "legacy dump", 11) || ! write(legacyStack, "legacy stack", 12) || ! write(unrelated, "unrelated", 9))
        return false;
    CrashHandler::RetainRecentReports(root.c_str());

    std::array<std::filesystem::path, 11> dumps;
    std::array<std::filesystem::path, 11> stacks;
    const auto firstWrite = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
    for (size_t index = 0; index < dumps.size(); ++index)
    {
        const auto name = L"RedXe-report-" + std::to_wstring(index);
        dumps[index]    = root / (name + L".dmp");
        stacks[index]   = root / (name + L".txt");
        if (! write(dumps[index], "dump", 4) || ! write(stacks[index], "stack", 5))
            return false;
        std::error_code error;
        std::filesystem::last_write_time(dumps[index], firstWrite + std::chrono::minutes(static_cast<int>(index)), error);
        if (error)
            return false;
        if (index == 9)
        {
            CrashHandler::RetainRecentReports(root.c_str());
            for (size_t kept = 0; kept <= index; ++kept)
                if (! std::filesystem::exists(dumps[kept]) || ! std::filesystem::exists(stacks[kept]))
                    return false;
        }
    }
    CrashHandler::RetainRecentReports(root.c_str());
    if (std::filesystem::exists(dumps.front()) || std::filesystem::exists(stacks.front()) || ! std::filesystem::exists(legacyDump) ||
        ! std::filesystem::exists(legacyStack) || ! std::filesystem::exists(unrelated))
        return false;
    for (size_t index = 1; index < dumps.size(); ++index)
        if (! std::filesystem::exists(dumps[index]) || ! std::filesystem::exists(stacks[index]))
            return false;

    const auto marker          = root / L"last_crash.txt";
    const std::wstring content = L"\uFEFF" + dumps.back().native();
    if (! write(marker, reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size() * sizeof(wchar_t))))
        return false;
    wchar_t reported[CrashHandler::kCrashNoticePathCapacity]{};
    if (CrashHandler::TakePendingNotice(false, reported) || ! std::filesystem::exists(marker))
        return false;
    return CrashHandler::TakePendingNotice(true, reported) && std::filesystem::path(reported) == dumps.back() && ! std::filesystem::exists(marker) &&
           ! CrashHandler::TakePendingNotice(true, reported);
}
