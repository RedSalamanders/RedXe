#include "Settings.Tests.FrameRate.h"
#include "../../RedXe/Settings.h"

#include <array>
#include <cstdio>
#include <string_view>

HRESULT TestContinuousFrameRateSettings() noexcept
{
    try
    {
        AppSettings omitted;
        AppSettings limited;
        AppSettings display;
        if (FAILED(ParseAppSettingsJson(R"({"version":{"major":5,"minor":0},"pages":[{}]})", omitted)) ||
            omitted.continuousFrameRate != ContinuousFrameRate::Display ||
            FAILED(ParseAppSettingsJson(R"({"version":{"major":5,"minor":4},"continuousFrameRate":30,"pages":[{}]})",
                                        limited)) ||
            limited.continuousFrameRate != ContinuousFrameRate::Thirty ||
            FAILED(ParseAppSettingsJson(
                R"({"version":{"major":5,"minor":4},"continuousFrameRate":"display","pages":[{}]})", display)) ||
            display.continuousFrameRate != ContinuousFrameRate::Display || RuntimeSettingsEqual(limited, display) ||
            !ActiveDashboardRuntimeEquals(limited, display))
            return E_FAIL;
        AppSettings equivalent = limited;
        equivalent.continuousFrameRate = ContinuousFrameRate::Display;
        equivalent.sourceDocument = display.sourceDocument;
        if (equivalent != display)
            return E_FAIL;
        for (const std::string_view value :
             std::array{"null", "true", "0", "60", "30.0", "30e0", "-30", "\"30\"", "\"Display\"", "{}", "[]"})
        {
            const std::string document =
                "{\"version\":{\"major\":5,\"minor\":4},\"continuousFrameRate\":" + std::string(value) +
                ",\"pages\":[{}]}";
            AppSettings candidate = limited;
            SettingsParseDiagnostic diagnostic;
            if (SUCCEEDED(ParseAppSettingsJsonDetailed(document, candidate, diagnostic)) ||
                diagnostic.path != "$.continuousFrameRate" || candidate != limited)
                return E_FAIL;
        }
        SettingsParseDiagnostic diagnostic;
        AppSettings duplicate = limited;
        if (SUCCEEDED(ParseAppSettingsJsonDetailed(
                R"({"version":{"major":5,"minor":4},"continuousFrameRate":30,"continuousFrameRate":"display","pages":[{}]})",
                duplicate, diagnostic)) ||
            duplicate != limited)
            return E_FAIL;
        limited.continuousFrameRate = static_cast<ContinuousFrameRate>(60);
        if (SUCCEEDED(ValidateAppSettings(limited)))
            return E_FAIL;
        std::wprintf(L"Continuous frame-rate parsing, rejection and active-runtime classification passed.\n");
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
}
