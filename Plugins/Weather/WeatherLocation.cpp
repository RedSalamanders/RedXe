#include "WeatherLocation.h"
#include "WeatherModel.h"

#include <array>
#include <cstddef>
#include <cwchar>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

bool WeatherTryAutomaticLocation(wchar_t* name, size_t nameCapacity, char* countryCode, size_t countryCapacity,
                                 double& latitude, double& longitude) noexcept
{
    if (name && nameCapacity != 0)
        name[0] = L'\0';
    if (countryCode && countryCapacity != 0)
        countryCode[0] = '\0';
    return SUCCEEDED(WeatherLocateWithHelper(nullptr, latitude, longitude));
}

HRESULT WeatherLocateWithHelper(HANDLE cancelEvent, double& latitude, double& longitude,
                                const wchar_t* testArgument) noexcept
{
    latitude = longitude = 0.0;
    if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0)
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&WeatherLocateWithHelper), &module))
        return HRESULT_FROM_WIN32(GetLastError());
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    wchar_t* name = std::wcsrchr(path.data(), L'\\');
    if (!name)
        return E_UNEXPECTED;
    ++name;
    if (wcscpy_s(name, path.size() - static_cast<size_t>(name - path.data()), L"WeatherLocation.exe") != 0)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    std::array<wchar_t, 32768> command{};
    if (_snwprintf_s(command.data(), command.size(), _TRUNCATE, L"\"%s\" %s", path.data(),
                     testArgument ? testArgument : L"") < 0)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    wil::unique_handle readPipe, writePipe;
    if (!CreatePipe(readPipe.put(), writePipe.put(), &security, 256) ||
        !SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, 0))
        return HRESULT_FROM_WIN32(GetLastError());
    SIZE_T bytes = 0;
    (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    alignas(void*) std::array<std::byte, 512> attributes{};
    if (bytes > attributes.size())
        return E_OUTOFMEMORY;
    auto* attributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &bytes))
        return HRESULT_FROM_WIN32(GetLastError());
    const auto cleanupAttributes = wil::scope_exit([&]() noexcept { DeleteProcThreadAttributeList(attributeList); });
    HANDLE inherited = writePipe.get();
    if (!UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherited, sizeof(inherited),
                                   nullptr, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdOutput = writePipe.get();
    startup.StartupInfo.hStdError = writePipe.get();
    startup.lpAttributeList = attributeList;
    wil::unique_process_information process;
    if (!CreateProcessW(path.data(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo,
                        &process))
        return HRESULT_FROM_WIN32(GetLastError());
    writePipe.reset();
    // Cancellation/timeout kills and joins only this owned helper, never the application or another process.
    const auto stopHelper = wil::scope_exit(
        [&]() noexcept
        {
            if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0)
            {
                (void)TerminateProcess(process.hProcess, ERROR_CANCELLED);
                (void)WaitForSingleObject(process.hProcess, INFINITE);
            }
        });
    const HANDLE waits[]{process.hProcess, cancelEvent};
    const DWORD wait = WaitForMultipleObjects(cancelEvent ? 2 : 1, waits, FALSE, 20000);
    if (wait != WAIT_OBJECT_0)
        return HRESULT_FROM_WIN32(wait == WAIT_OBJECT_0 + 1 ? ERROR_CANCELLED
                                  : wait == WAIT_TIMEOUT    ? ERROR_TIMEOUT
                                                            : GetLastError());
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process.hProcess, &exitCode))
        return HRESULT_FROM_WIN32(GetLastError());
    if (exitCode != 0)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    std::array<char, 128> response{};
    DWORD received = 0;
    if (!ReadFile(readPipe.get(), response.data(), static_cast<DWORD>(response.size() - 1), &received, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());
    return WeatherParseLatLon(std::string_view(response.data(), received), latitude, longitude)
               ? S_OK
               : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}
