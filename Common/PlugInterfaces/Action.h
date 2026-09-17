#pragma once

#include <cstddef>
#include <cstdint>
#include <unknwn.h>
#include <windows.h>

// Every sizeBytes field must equal the current record's sizeof value. Each record below is pinned with a size
// assertion, and every record carrying a pointer is pinned with offset assertions, so a layout change that preserves
// size cannot pass the runtime sizeBytes guard unnoticed.
//
// An action is a named operation a binding (a Logicon key, a dialpad button or turn, a Launcher shortcut) asks the
// host to perform: "<namespace>.<verb>[.<verb>]" plus an optional target string. The host hardcodes the default
// namespaces (page, widget, redxe, system, keys, mouse). Any bundled plugin module — a widget provider, a service,
// or a dedicated <namespace>.action.dll — publishes further namespaces through RedXeGetActionContract and executes
// them through IRedXeActionPack. RedXe/BundledPlugins.h registers which plugin id owns which namespace.

// An action name is at most this many UTF-8 bytes excluding the terminator.
inline constexpr uint32_t kRedXeMaximumActionNameBytes = 64;
// A target is at most this many UTF-8 bytes excluding the terminator.
inline constexpr uint32_t kRedXeMaximumActionTargetBytes = 512;
// A namespace (the first name segment) is at most this many bytes.
inline constexpr uint32_t kRedXeMaximumActionNamespaceBytes = 32;
inline constexpr uint32_t kRedXeMaximumActionNamespacesPerPlugin = 4;
inline constexpr uint32_t kRedXeMaximumActionsPerNamespace = 64;
// Execute and every host-native action return within this budget on the UI thread; longer work is deferred.
inline constexpr uint32_t kRedXeActionExecuteBudgetMilliseconds = 20;

// Grammar of an action's target. The host validates a target from the descriptor alone (Common/Actions), so a
// publisher never runs code for validation.
enum RedXeActionTargetKind : uint32_t
{
    // The action takes no target; an authored target is ignored.
    RedXeActionTargetNone = 0,
    // 1 through kRedXeMaximumActionTargetBytes bytes of UTF-8.
    RedXeActionTargetText = 1,
    // An absolute Win32 path, a UNC path, or a URI with an alphabetic scheme of at least two characters.
    RedXeActionTargetPathOrUri = 2,
    // An absolute Win32 or UNC path only.
    RedXeActionTargetPath = 3,
    // An absolute executable path, optionally quoted, followed by arguments.
    RedXeActionTargetCommandLine = 4,
    // A dashboard page id or a zero-based page index.
    RedXeActionTargetPageRef = 5,
    // "<pageId>/<ordinal>" or "<ordinal>".
    RedXeActionTargetWidgetRef = 6,
    // A decimal integer within [targetMinimum, targetMaximum].
    RedXeActionTargetInteger = 7,
    // "n", "+n", or "-n"; relative forms are clamped to [targetMinimum, targetMaximum] at execution.
    RedXeActionTargetDelta = 8,
    // One of the '|'-separated options in targetOptions (case-sensitive).
    RedXeActionTargetEnum = 9,
    // Comma-separated key chords: "Ctrl+Shift+Esc,Win+D".
    RedXeActionTargetChords = 10,
    // "x,y", "+dx,+dy", or "center", optionally "@<monitor>".
    RedXeActionTargetPoint = 11,
    // "primary", "xeneon", "all", "<n>", or "name:<substring>".
    RedXeActionTargetMonitor = 12,
    // "foreground", "exe:<image.exe>", "class:<class>", or "title:<substring>".
    RedXeActionTargetWindow = 13,
    // A Zoom meeting URL or "<id>[:<passcode>]".
    RedXeActionTargetMeeting = 14,
    // "now", or a decimal delay in seconds within [targetMinimum, targetMaximum].
    RedXeActionTargetNowOrSeconds = 15,
};

enum RedXeActionFlags : uint32_t
{
    RedXeActionFlagNone = 0,
    // The target may be omitted (an empty target is valid).
    RedXeActionFlagTargetOptional = 1U << 0U,
    // Executing the action injects input with SendInput.
    RedXeActionFlagInjectsInput = 1U << 1U,
    // Execute returns S_FALSE and completes later on a host-owned lane.
    RedXeActionFlagDeferred = 1U << 2U,
    // The target must name the confirming argument (for example "now"); the host validator enforces it.
    RedXeActionFlagDestructive = 1U << 3U,
    // The target accepts a trailing "@<monitor>" selector.
    RedXeActionFlagMonitorSuffix = 1U << 4U,
    // The target accepts a trailing "@<window>" selector.
    RedXeActionFlagWindowSuffix = 1U << 5U,
};

// One action. Module-owned and immutable while the module is mapped.
struct RedXeActionDescriptor final
{
    uint32_t sizeBytes;
    uint32_t flags;
    // Full name including the namespace, for example "zoom.mute".
    const char* name;
    const wchar_t* displayName;
    // Human-readable target syntax for documentation and diagnostics.
    const wchar_t* targetSyntax;
    // RedXeActionTargetKind.
    uint32_t targetKind;
    // Bounds for Integer and Delta targets; ignored otherwise.
    int32_t targetMinimum;
    int32_t targetMaximum;
    // '|'-separated options for Enum targets; null otherwise.
    const char* targetOptions;
};

static_assert(sizeof(RedXeActionDescriptor) == 56);
static_assert(offsetof(RedXeActionDescriptor, name) == 8);
static_assert(offsetof(RedXeActionDescriptor, displayName) == 16);
static_assert(offsetof(RedXeActionDescriptor, targetSyntax) == 24);
static_assert(offsetof(RedXeActionDescriptor, targetKind) == 32);
static_assert(offsetof(RedXeActionDescriptor, targetMinimum) == 36);
static_assert(offsetof(RedXeActionDescriptor, targetMaximum) == 40);
static_assert(offsetof(RedXeActionDescriptor, targetOptions) == 48);

// One namespace and its actions. Every action name starts with "<name>.".
struct RedXeActionNamespace final
{
    uint32_t sizeBytes;
    uint32_t actionCount;
    const char* name;
    const RedXeActionDescriptor* actions;
};

static_assert(sizeof(RedXeActionNamespace) == 24);
static_assert(offsetof(RedXeActionNamespace, name) == 8);
static_assert(offsetof(RedXeActionNamespace, actions) == 16);

// The static action contract of one plugin id. Discovered like RedXeGetPluginSettingsContract: the host reads it
// when it maps the module, creates no object, and borrows the records while the module stays mapped.
struct RedXeActionContract final
{
    uint32_t sizeBytes;
    uint32_t namespaceCount;
    const RedXeActionNamespace* namespaces;
};

static_assert(sizeof(RedXeActionContract) == 16);
static_assert(offsetof(RedXeActionContract, namespaces) == 8);

enum RedXeActionRequestFlags : uint32_t
{
    RedXeActionRequestFlagNone = 0,
    // Automated host (--self-test, HostPluginTests, REDXE_AUTOMATED_HOST): the executor validates, counts, and
    // returns S_OK (or S_FALSE with a no-op completion). It MUST NOT inject input, start a process, or change
    // display, power, audio, clipboard, or meeting state.
    RedXeActionRequestFlagDeviceAccessDisabled = 1U << 0U,
};

// Borrowed action request. Strings are valid for the duration of the call only; the callee copies what it retains.
struct RedXeActionRequest final
{
    uint32_t sizeBytes;
    uint32_t flags;
    // Full action name, at most kRedXeMaximumActionNameBytes bytes.
    const char* actionUtf8;
    // Optional target, at most kRedXeMaximumActionTargetBytes bytes. Null or empty when the action takes none.
    const char* targetUtf8;
    // Optional borrowed plugin id of the requester, for logs. Null for host-originated requests.
    const char* sourcePluginId;
};

static_assert(sizeof(RedXeActionRequest) == 32);
static_assert(offsetof(RedXeActionRequest, actionUtf8) == 8);
static_assert(offsetof(RedXeActionRequest, targetUtf8) == 16);
static_assert(offsetof(RedXeActionRequest, sourcePluginId) == 24);

// Executor for the namespaces a plugin id publishes.
//
//   - A dedicated action DLL or a widget provider: the host creates one object per process through
//     RedXeCreate(IID_IRedXeActionPack, options, host, pluginId, ...) on the first execution in one of its
//     namespaces, with the ordinary {"plugin":{},"instance":{}} envelope, and releases it at process teardown.
//   - A service: the host queries this interface on the started service object (same controlling IUnknown as
//     IRedXeService); a service that is not configured or failed to start cannot execute.
//
// Threading: Execute runs synchronously on the RedXe UI thread inside the host-action drain, is non-reentrant, and
// MUST return within kRedXeActionExecuteBudgetMilliseconds without waiting on another thread, pumping messages, or
// showing UI. An action flagged RedXeActionFlagDeferred returns S_FALSE after handing the work to a host-owned lane
// (IRedXeHost::QueueControlWork, or the publisher's own device lane through a bounded slot and its wake event). The
// executor MUST NOT call back into the host from Execute except QueueControlWork, Log, and RequestAction. Between
// executions a dedicated action DLL owns no thread, timer, window, hook, or COM registration. The request carries
// RedXeActionRequestFlagDeviceAccessDisabled on automated hosts; the executor then acts on nothing.
//
// Returns S_OK when done, S_FALSE when deferred, E_INVALIDARG for a name or target the publisher does not accept,
// HRESULT_FROM_WIN32(ERROR_BUSY) when its bounded slots are full, and E_NOT_VALID_STATE / E_ACCESSDENIED when the
// publisher's state or role does not allow the action.
interface __declspec(uuid("3C7A9E10-5B2D-4F81-9A6E-0D4C8B2F7E51")) __declspec(novtable) IRedXeActionPack : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Execute(const RedXeActionRequest* request) noexcept = 0;
};

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_ACTION_API __declspec(dllexport)
#else
#define REDXE_ACTION_API
#endif

extern "C"
{
    // Returns the borrowed action contract of one plugin id, or HRESULT_FROM_WIN32(ERROR_NOT_FOUND) when that id
    // publishes no actions. Creates nothing. A module that advertises RedXePluginCapabilityActions for an id MUST
    // answer for it.
    REDXE_ACTION_API HRESULT __stdcall RedXeGetActionContract(const char* pluginId,
                                                              const RedXeActionContract** contract) noexcept;
}

using RedXeGetActionContractFn = decltype(&RedXeGetActionContract);
inline constexpr char kRedXeGetActionContractExport[] = "RedXeGetActionContract";

#undef REDXE_ACTION_API

// Pure helpers over action names, shared by the host and every publisher.

// True when value is the action-name grammar: ^[a-z][a-zA-Z0-9]*(\.[a-z][a-zA-Z0-9]*){1,3}$ within the byte bound.
[[nodiscard]] constexpr bool RedXeIsActionNameSyntax(const char* value) noexcept
{
    if (!value || value[0] < 'a' || value[0] > 'z')
    {
        return false;
    }
    uint32_t length = 0;
    uint32_t segments = 1;
    bool segmentStart = false;
    while (value[length] != '\0')
    {
        const char character = value[length];
        if (length >= kRedXeMaximumActionNameBytes)
        {
            return false;
        }
        if (character == '.')
        {
            if (segmentStart || segments >= 4)
            {
                return false;
            }
            ++segments;
            segmentStart = true;
        }
        else if (segmentStart)
        {
            if (character < 'a' || character > 'z')
            {
                return false;
            }
            segmentStart = false;
        }
        else if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9')))
        {
            return false;
        }
        ++length;
    }
    return segments >= 2 && !segmentStart;
}

// Length of the namespace segment of an action name (the bytes before the first '.'), or 0 for no namespace.
[[nodiscard]] constexpr uint32_t RedXeActionNamespaceLength(const char* value) noexcept
{
    if (!value)
    {
        return 0;
    }
    uint32_t length = 0;
    while (value[length] != '\0' && value[length] != '.')
    {
        ++length;
    }
    return value[length] == '.' ? length : 0;
}

// True when the action name's namespace equals actionNamespace exactly.
[[nodiscard]] constexpr bool RedXeActionInNamespace(const char* actionName, const char* actionNamespace) noexcept
{
    const uint32_t length = RedXeActionNamespaceLength(actionName);
    if (length == 0 || !actionNamespace)
    {
        return false;
    }
    for (uint32_t index = 0; index < length; ++index)
    {
        if (actionNamespace[index] == '\0' || actionNamespace[index] != actionName[index])
        {
            return false;
        }
    }
    return actionNamespace[length] == '\0';
}
