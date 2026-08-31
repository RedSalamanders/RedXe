#include "CrashHandler.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <iterator>
#include <shellapi.h>
#include <shlobj_core.h>
#include <strsafe.h>

#include <dbghelp.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace CrashHandler
{
namespace
{
constexpr wchar_t kCrashDirectorySuffix[] = L"RedXe\\Crashes";
constexpr wchar_t kMarkerFileName[] = L"last_crash.txt";
constexpr DWORD kCrashExceptionCode = 0xE000CAFEU;
constexpr std::size_t kPathCapacity = 1024;
constexpr std::size_t kMarkerContentCapacity = 1024;
constexpr unsigned int kMaximumCallstackFrames = 64;
constexpr ULONG kFatalPathStackGuaranteeBytes = 128U * 1024U;

std::atomic<bool> g_installed{false};
std::atomic<bool> g_pathsReady{false};
std::atomic_flag g_dumpStarted = ATOMIC_FLAG_INIT;
wchar_t g_crashDirectory[kPathCapacity]{};
wchar_t g_markerPath[kPathCapacity]{};
wchar_t g_dumpPath[kPathCapacity]{};
volatile LONG g_stackConsumptionDepth = 0;

struct DbgHelpFunctions final
{
    wil::unique_hmodule module;
    decltype(&MiniDumpWriteDump) miniDumpWriteDump = nullptr;
    decltype(&SymSetOptions) symSetOptions = nullptr;
    decltype(&SymInitializeW) symInitializeW = nullptr;
    decltype(&SymCleanup) symCleanup = nullptr;
    decltype(&StackWalk64) stackWalk64 = nullptr;
    decltype(&SymFunctionTableAccess64) symFunctionTableAccess64 = nullptr;
    decltype(&SymGetModuleBase64) symGetModuleBase64 = nullptr;
    decltype(&SymFromAddrW) symFromAddrW = nullptr;
    decltype(&SymGetModuleInfoW64) symGetModuleInfoW64 = nullptr;
    decltype(&SymGetLineFromAddrW64) symGetLineFromAddrW64 = nullptr;
};

template <typename Function> [[nodiscard]] Function ResolveDbgHelpFunction(HMODULE module, const char* name) noexcept
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

[[nodiscard]] bool LoadDbgHelp(DbgHelpFunctions& functions) noexcept
{
    functions.module.reset(LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
    if (!functions.module)
    {
        return false;
    }

    const HMODULE module = functions.module.get();
    functions.miniDumpWriteDump =
        ResolveDbgHelpFunction<decltype(functions.miniDumpWriteDump)>(module, "MiniDumpWriteDump");
    functions.symSetOptions = ResolveDbgHelpFunction<decltype(functions.symSetOptions)>(module, "SymSetOptions");
    functions.symInitializeW = ResolveDbgHelpFunction<decltype(functions.symInitializeW)>(module, "SymInitializeW");
    functions.symCleanup = ResolveDbgHelpFunction<decltype(functions.symCleanup)>(module, "SymCleanup");
    functions.stackWalk64 = ResolveDbgHelpFunction<decltype(functions.stackWalk64)>(module, "StackWalk64");
    functions.symFunctionTableAccess64 =
        ResolveDbgHelpFunction<decltype(functions.symFunctionTableAccess64)>(module, "SymFunctionTableAccess64");
    functions.symGetModuleBase64 =
        ResolveDbgHelpFunction<decltype(functions.symGetModuleBase64)>(module, "SymGetModuleBase64");
    functions.symFromAddrW = ResolveDbgHelpFunction<decltype(functions.symFromAddrW)>(module, "SymFromAddrW");
    functions.symGetModuleInfoW64 =
        ResolveDbgHelpFunction<decltype(functions.symGetModuleInfoW64)>(module, "SymGetModuleInfoW64");
    functions.symGetLineFromAddrW64 =
        ResolveDbgHelpFunction<decltype(functions.symGetLineFromAddrW64)>(module, "SymGetLineFromAddrW64");
    return functions.miniDumpWriteDump != nullptr;
}

[[nodiscard]] HRESULT SetCrashDirectory(const wchar_t* directory) noexcept
{
    if (!directory || directory[0] == L'\0')
    {
        return E_INVALIDARG;
    }

    g_pathsReady.store(false, std::memory_order_release);

    wchar_t absolutePath[kPathCapacity]{};
    const DWORD length =
        GetFullPathNameW(directory, static_cast<DWORD>(std::size(absolutePath)), absolutePath, nullptr);
    if (length == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (length >= std::size(absolutePath))
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    std::size_t trimmedLength = length;
    while (trimmedLength > 3 && (absolutePath[trimmedLength - 1] == L'\\' || absolutePath[trimmedLength - 1] == L'/'))
    {
        absolutePath[--trimmedLength] = L'\0';
    }

    HRESULT result = StringCchCopyW(g_crashDirectory, std::size(g_crashDirectory), absolutePath);
    if (SUCCEEDED(result))
    {
        result = StringCchPrintfW(g_markerPath, std::size(g_markerPath), L"%s\\%s", absolutePath, kMarkerFileName);
    }
    if (FAILED(result))
    {
        g_crashDirectory[0] = L'\0';
        g_markerPath[0] = L'\0';
        return result;
    }

    g_pathsReady.store(true, std::memory_order_release);
    return S_OK;
}

void ConfigureDefaultCrashDirectory() noexcept
{
    PWSTR localAppDataValue = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppDataValue)))
    {
        return;
    }
    wil::unique_cotaskmem_string localAppData{localAppDataValue};

    wchar_t directory[kPathCapacity]{};
    if (SUCCEEDED(
            StringCchPrintfW(directory, std::size(directory), L"%s\\%s", localAppData.get(), kCrashDirectorySuffix)))
    {
        static_cast<void>(SetCrashDirectory(directory));
    }
}

[[nodiscard]] bool EnsureCrashDirectoryExists() noexcept
{
    const int result = SHCreateDirectoryExW(nullptr, g_crashDirectory, nullptr);
    return result == ERROR_SUCCESS || result == ERROR_FILE_EXISTS || result == ERROR_ALREADY_EXISTS;
}

[[nodiscard]] bool BuildDumpPath(wchar_t (&dumpPath)[kPathCapacity]) noexcept
{
    SYSTEMTIME time{};
    GetLocalTime(&time);

    return SUCCEEDED(StringCchPrintfW(
        dumpPath, std::size(dumpPath), L"%s\\RedXe-%04u%02u%02u-%02u%02u%02u-p%lu-t%lu.dmp", g_crashDirectory,
        static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth), static_cast<unsigned>(time.wDay),
        static_cast<unsigned>(time.wHour), static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond),
        static_cast<unsigned long>(GetCurrentProcessId()), static_cast<unsigned long>(GetCurrentThreadId())));
}

[[nodiscard]] bool ReplacePathExtension(wchar_t (&path)[kPathCapacity], const wchar_t* extensionValue) noexcept
{
    wchar_t* extension = wcsrchr(path, L'.');
    if (!extension)
    {
        return false;
    }
    const std::size_t remaining = std::size(path) - static_cast<std::size_t>(extension - path);
    return SUCCEEDED(StringCchCopyW(extension, remaining, extensionValue));
}

[[nodiscard]] bool WriteMiniDumpFile(const DbgHelpFunctions& functions, const wchar_t* dumpPath,
                                     EXCEPTION_POINTERS* exceptionPointers) noexcept
{

    wil::unique_handle file(CreateFileW(dumpPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
    {
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION exceptionInformation{};
    exceptionInformation.ThreadId = GetCurrentThreadId();
    exceptionInformation.ExceptionPointers = exceptionPointers;
    exceptionInformation.ClientPointers = FALSE;

    constexpr MINIDUMP_TYPE dumpType =
        static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    if (!functions.miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file.get(), dumpType,
                                     exceptionPointers ? &exceptionInformation : nullptr, nullptr, nullptr))
    {
        file.reset();
        static_cast<void>(DeleteFileW(dumpPath));
        return false;
    }

    static_cast<void>(FlushFileBuffers(file.get()));
    return true;
}

[[nodiscard]] bool WriteAll(HANDLE file, const void* data, DWORD byteCount) noexcept
{
    const auto* next = static_cast<const std::byte*>(data);
    DWORD remaining = byteCount;
    while (remaining > 0)
    {
        DWORD written = 0;
        if (!WriteFile(file, next, remaining, &written, nullptr) || written == 0)
        {
            return false;
        }
        next += written;
        remaining -= written;
    }
    return true;
}

[[nodiscard]] bool WriteWideText(HANDLE file, const wchar_t* text) noexcept
{
    const std::size_t byteCount = wcslen(text) * sizeof(wchar_t);
    return byteCount <= MAXDWORD && WriteAll(file, text, static_cast<DWORD>(byteCount));
}

[[nodiscard]] bool PrepareStackFrame(const CONTEXT& context, STACKFRAME64& frame, DWORD& machine) noexcept
{
#if defined(_M_X64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_ARM64)
    machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = context.Pc;
    frame.AddrFrame.Offset = context.Fp;
    frame.AddrStack.Offset = context.Sp;
#else
    machine = 0;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    return machine != 0 && frame.AddrPC.Offset != 0;
}

void PrepareSymbolSearchPath(wchar_t (&searchPath)[kMarkerContentCapacity]) noexcept
{
    const DWORD length = GetModuleFileNameW(nullptr, searchPath, static_cast<DWORD>(std::size(searchPath)));
    if (length == 0 || length >= std::size(searchPath))
    {
        static_cast<void>(StringCchCopyW(searchPath, std::size(searchPath), g_crashDirectory));
        return;
    }

    wchar_t* separator = wcsrchr(searchPath, L'\\');
    if (!separator)
    {
        separator = wcsrchr(searchPath, L'/');
    }
    if (!separator)
    {
        static_cast<void>(StringCchCopyW(searchPath, std::size(searchPath), g_crashDirectory));
        return;
    }
    if (separator == searchPath + 2 && searchPath[1] == L':')
    {
        separator[1] = L'\0';
    }
    else
    {
        *separator = L'\0';
    }
}

[[nodiscard]] bool WriteCrashReportFile(const DbgHelpFunctions& functions, const wchar_t* reportPath,
                                        EXCEPTION_POINTERS* exceptionPointers) noexcept
{
    wil::unique_handle report(CreateFileW(reportPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!report)
    {
        return false;
    }

    constexpr wchar_t bom = 0xFEFF;
    if (!WriteAll(report.get(), &bom, sizeof(bom)))
    {
        report.reset();
        static_cast<void>(DeleteFileW(reportPath));
        return false;
    }

    CONTEXT context{};
    if (exceptionPointers && exceptionPointers->ContextRecord)
    {
        context = *exceptionPointers->ContextRecord;
    }
    else
    {
        RtlCaptureContext(&context);
    }

    const DWORD exceptionCode =
        exceptionPointers && exceptionPointers->ExceptionRecord ? exceptionPointers->ExceptionRecord->ExceptionCode : 0;
    const DWORD64 exceptionAddress =
        exceptionPointers && exceptionPointers->ExceptionRecord
            ? reinterpret_cast<DWORD64>(exceptionPointers->ExceptionRecord->ExceptionAddress)
            : 0;

#if defined(_M_X64)
    constexpr wchar_t architecture[] = L"x64";
#elif defined(_M_ARM64)
    constexpr wchar_t architecture[] = L"ARM64";
#else
    constexpr wchar_t architecture[] = L"unknown";
#endif

    wchar_t header[512]{};
    if (FAILED(StringCchPrintfW(header, std::size(header),
                                L"ExceptionCode=0x%08lX\r\nExceptionAddress=0x%016llX\r\nProcessId=%lu\r\n"
                                L"ThreadId=%lu\r\nArchitecture=%s\r\n\r\nCallstack:\r\n",
                                static_cast<unsigned long>(exceptionCode),
                                static_cast<unsigned long long>(exceptionAddress),
                                static_cast<unsigned long>(GetCurrentProcessId()),
                                static_cast<unsigned long>(GetCurrentThreadId()), architecture)) ||
        !WriteWideText(report.get(), header))
    {
        report.reset();
        static_cast<void>(DeleteFileW(reportPath));
        return false;
    }

    const bool canUseSymbols = functions.symSetOptions && functions.symInitializeW && functions.symCleanup &&
                               functions.stackWalk64 && functions.symFunctionTableAccess64 &&
                               functions.symGetModuleBase64;
    bool symbolsInitialized = false;
    const HANDLE process = GetCurrentProcess();
    if (canUseSymbols)
    {
        wchar_t searchPath[kMarkerContentCapacity]{};
        PrepareSymbolSearchPath(searchPath);
        static_cast<void>(functions.symSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES |
                                                  SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS));
        symbolsInitialized = functions.symInitializeW(process, searchPath, TRUE) != FALSE;
    }
    const auto symbolCleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (symbolsInitialized)
            {
                static_cast<void>(functions.symCleanup(process));
            }
        });

    STACKFRAME64 frame{};
    DWORD machine = 0;
    unsigned int frameCount = 0;
    if (PrepareStackFrame(context, frame, machine))
    {
        const HANDLE thread = GetCurrentThread();
        for (; frameCount < kMaximumCallstackFrames && frame.AddrPC.Offset != 0; ++frameCount)
        {
            const DWORD64 address = frame.AddrPC.Offset;
            const wchar_t* moduleName = L"(unknown)";
            IMAGEHLP_MODULEW64 moduleInformation{};
            moduleInformation.SizeOfStruct = sizeof(moduleInformation);
            if (symbolsInitialized && functions.symGetModuleInfoW64 &&
                functions.symGetModuleInfoW64(process, address, &moduleInformation) &&
                moduleInformation.ModuleName[0] != L'\0')
            {
                moduleName = moduleInformation.ModuleName;
            }

            const wchar_t* symbolName = L"(unknown)";
            DWORD64 symbolDisplacement = 0;
            SYMBOL_INFO_PACKAGEW symbolPackage{};
            symbolPackage.si.SizeOfStruct = sizeof(SYMBOL_INFOW);
            symbolPackage.si.MaxNameLen = MAX_SYM_NAME;
            if (symbolsInitialized && functions.symFromAddrW &&
                functions.symFromAddrW(process, address, &symbolDisplacement, &symbolPackage.si))
            {
                symbolName = symbolPackage.si.Name;
            }

            wchar_t sourceLocation[768]{};
            IMAGEHLP_LINEW64 sourceLine{};
            sourceLine.SizeOfStruct = sizeof(sourceLine);
            DWORD lineDisplacement = 0;
            if (symbolsInitialized && functions.symGetLineFromAddrW64 &&
                functions.symGetLineFromAddrW64(process, address, &lineDisplacement, &sourceLine) &&
                sourceLine.FileName)
            {
                static_cast<void>(StringCchPrintfW(
                    sourceLocation, std::size(sourceLocation), L" %s:%lu(+%lu)", sourceLine.FileName,
                    static_cast<unsigned long>(sourceLine.LineNumber), static_cast<unsigned long>(lineDisplacement)));
            }

            wchar_t frameLine[4096]{};
            if (FAILED(StringCchPrintfW(frameLine, std::size(frameLine), L"%02u 0x%016llX %s!%s+0x%llX%s\r\n",
                                        frameCount, static_cast<unsigned long long>(address), moduleName, symbolName,
                                        static_cast<unsigned long long>(symbolDisplacement), sourceLocation)) ||
                !WriteWideText(report.get(), frameLine))
            {
                report.reset();
                static_cast<void>(DeleteFileW(reportPath));
                return false;
            }

            if (!symbolsInitialized)
            {
                ++frameCount;
                break;
            }

            const DWORD64 currentAddress = frame.AddrPC.Offset;
            if (!functions.stackWalk64(machine, process, thread, &frame, &context, nullptr,
                                       functions.symFunctionTableAccess64, functions.symGetModuleBase64, nullptr))
            {
                ++frameCount;
                break;
            }
            if (frame.AddrPC.Offset == currentAddress &&
                (!functions.stackWalk64(machine, process, thread, &frame, &context, nullptr,
                                        functions.symFunctionTableAccess64, functions.symGetModuleBase64, nullptr) ||
                 frame.AddrPC.Offset == currentAddress))
            {
                ++frameCount;
                break;
            }
        }
    }

    wchar_t footer[64]{};
    if (FAILED(StringCchPrintfW(footer, std::size(footer), L"FrameCount=%u\r\n", frameCount)) ||
        !WriteWideText(report.get(), footer))
    {
        report.reset();
        static_cast<void>(DeleteFileW(reportPath));
        return false;
    }

    static_cast<void>(FlushFileBuffers(report.get()));
    if (frameCount == 0)
    {
        report.reset();
        static_cast<void>(DeleteFileW(reportPath));
        return false;
    }
    return true;
}

void WriteMarkerFile(const wchar_t* dumpPath) noexcept
{
    wil::unique_handle marker(CreateFileW(g_markerPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                          FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!marker)
    {
        return;
    }

    constexpr wchar_t bom = 0xFEFF;
    DWORD written = 0;
    if (!WriteFile(marker.get(), &bom, sizeof(bom), &written, nullptr) || written != sizeof(bom))
    {
        marker.reset();
        static_cast<void>(DeleteFileW(g_markerPath));
        return;
    }

    const std::size_t dumpPathLength = wcslen(dumpPath);
    const std::size_t byteCount = dumpPathLength * sizeof(wchar_t);
    if (byteCount > MAXDWORD || !WriteFile(marker.get(), dumpPath, static_cast<DWORD>(byteCount), &written, nullptr) ||
        written != static_cast<DWORD>(byteCount))
    {
        marker.reset();
        static_cast<void>(DeleteFileW(g_markerPath));
        return;
    }
    static_cast<void>(FlushFileBuffers(marker.get()));
}

void WriteDumpAndMarker(EXCEPTION_POINTERS* exceptionPointers) noexcept
{
    if (g_dumpStarted.test_and_set(std::memory_order_acq_rel) || !g_pathsReady.load(std::memory_order_acquire))
    {
        return;
    }
    if (!EnsureCrashDirectoryExists())
    {
        return;
    }

    DbgHelpFunctions functions;
    if (!BuildDumpPath(g_dumpPath) || !LoadDbgHelp(functions) ||
        !WriteMiniDumpFile(functions, g_dumpPath, exceptionPointers))
    {
        return;
    }
    if (ReplacePathExtension(g_dumpPath, L".txt"))
    {
        static_cast<void>(WriteCrashReportFile(functions, g_dumpPath, exceptionPointers));
        if (!ReplacePathExtension(g_dumpPath, L".dmp"))
        {
            return;
        }
    }
    WriteMarkerFile(g_dumpPath);
}

// The recursion is deliberate: this exists only behind --crash-test-stack-overflow.
#pragma warning(push)
#pragma warning(disable : 4717)
__declspec(noinline) ULONG_PTR ConsumeStackUntilOverflow() noexcept
{
    volatile unsigned char stackBlock[4096]{};
    const LONG depth = InterlockedIncrement(&g_stackConsumptionDepth);
    const std::size_t index = static_cast<std::size_t>(depth) % std::size(stackBlock);
    stackBlock[index] = static_cast<unsigned char>(depth);
    const ULONG_PTR child = ConsumeStackUntilOverflow();
    return child + stackBlock[index];
}
#pragma warning(pop)

LONG WINAPI UnhandledExceptionFilterThunk(EXCEPTION_POINTERS* exceptionPointers) noexcept
{
    WriteDumpAndMarker(exceptionPointers);
    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void TerminateProcessAfterDump() noexcept
{
    WriteDumpAndMarker(nullptr);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(kCrashExitCode));
    __assume(false);
}

void TerminateHandler() noexcept
{
    TerminateProcessAfterDump();
}

void __cdecl PureCallHandler() noexcept
{
    TerminateProcessAfterDump();
}

void __cdecl InvalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) noexcept
{
    TerminateProcessAfterDump();
}

[[nodiscard]] bool ReadMarkerDumpPath(wchar_t (&dumpPath)[kMarkerContentCapacity]) noexcept
{
    wil::unique_handle marker(CreateFileW(g_markerPath, GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!marker)
    {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(marker.get(), &size) || size.QuadPart <= 0 ||
        size.QuadPart > static_cast<LONGLONG>(sizeof(dumpPath) - sizeof(wchar_t)) ||
        (size.QuadPart % sizeof(wchar_t)) != 0)
    {
        return false;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(marker.get(), dumpPath, static_cast<DWORD>(size.QuadPart), &bytesRead, nullptr) ||
        bytesRead != static_cast<DWORD>(size.QuadPart))
    {
        dumpPath[0] = L'\0';
        return false;
    }

    std::size_t characterCount = bytesRead / sizeof(wchar_t);
    dumpPath[characterCount] = L'\0';
    std::size_t start = dumpPath[0] == 0xFEFF ? 1U : 0U;
    while (characterCount > start)
    {
        const wchar_t character = dumpPath[characterCount - 1];
        if (character != L'\0' && character != L'\r' && character != L'\n' && character != L' ' && character != L'\t')
        {
            break;
        }
        dumpPath[--characterCount] = L'\0';
    }
    if (start != 0)
    {
        MoveMemory(dumpPath, dumpPath + start, (characterCount - start + 1) * sizeof(wchar_t));
    }
    return dumpPath[0] != L'\0';
}
} // namespace

void Install() noexcept
{
    if (g_installed.exchange(true, std::memory_order_acq_rel))
    {
        return;
    }

    ConfigureDefaultCrashDirectory();
    ULONG stackGuarantee = kFatalPathStackGuaranteeBytes;
    static_cast<void>(SetThreadStackGuarantee(&stackGuarantee));
    SetUnhandledExceptionFilter(&UnhandledExceptionFilterThunk);
    std::set_terminate(&TerminateHandler);
    _set_purecall_handler(&PureCallHandler);
    _set_invalid_parameter_handler(&InvalidParameterHandler);
}

int WriteDumpForException(EXCEPTION_POINTERS* exceptionPointers) noexcept
{
    WriteDumpAndMarker(exceptionPointers);
    return EXCEPTION_EXECUTE_HANDLER;
}

void ShowPreviousCrashUiIfPresent(HWND ownerWindow) noexcept
{
    const DWORD markerAttributes = GetFileAttributesW(g_markerPath);
    if (!g_pathsReady.load(std::memory_order_acquire) || markerAttributes == INVALID_FILE_ATTRIBUTES ||
        (markerAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return;
    }

    wchar_t dumpPath[kMarkerContentCapacity]{};
    const bool hasDumpPath = ReadMarkerDumpPath(dumpPath);
    static_cast<void>(DeleteFileW(g_markerPath));

    wchar_t message[2048]{};
    if (hasDumpPath)
    {
        static_cast<void>(
            StringCchPrintfW(message, std::size(message),
                             L"RedXe ended unexpectedly during its previous run. A diagnostic minidump "
                             L"and call-stack report were saved beside each other at:\n\n%s\n\nOpen the crash folder?",
                             dumpPath));
    }
    else
    {
        static_cast<void>(StringCchCopyW(message, std::size(message),
                                         L"RedXe ended unexpectedly during its previous run. Open the crash folder?"));
    }

    const int choice = MessageBoxW(ownerWindow, message, L"RedXe — previous crash detected",
                                   MB_YESNO | MB_ICONERROR | MB_DEFBUTTON2 | MB_SETFOREGROUND);
    if (choice == IDYES)
    {
        static_cast<void>(ShellExecuteW(ownerWindow, L"open", g_crashDirectory, nullptr, nullptr, SW_SHOWNORMAL));
    }
}

HRESULT SetCrashDirectoryForTesting(const wchar_t* directory) noexcept
{
    g_pathsReady.store(false, std::memory_order_release);
    if (!directory || directory[0] == L'\0')
    {
        return E_INVALIDARG;
    }

    const std::size_t prefixLength = wcsnlen_s(directory, 3);
    const bool driveAbsolute =
        prefixLength >= 3 && directory[1] == L':' && (directory[2] == L'\\' || directory[2] == L'/');
    const bool uncAbsolute = prefixLength >= 2 && (directory[0] == L'\\' || directory[0] == L'/') &&
                             (directory[1] == L'\\' || directory[1] == L'/');
    if (!driveAbsolute && !uncAbsolute)
    {
        return E_INVALIDARG;
    }
    return SetCrashDirectory(directory);
}

[[noreturn]] void TriggerCrashTest() noexcept
{
    RaiseException(kCrashExceptionCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(kCrashExitCode));
    __assume(false);
}

[[noreturn]] void TriggerStackOverflowCrashTest() noexcept
{
    static_cast<void>(ConsumeStackUntilOverflow());
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(kCrashExitCode));
    __assume(false);
}
} // namespace CrashHandler
