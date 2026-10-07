#pragma once

#include "PlugInterfaces/Host.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <shellapi.h>
#include <thread>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

// The host's launch lane. ShellExecuteExW, the file probe before it, and CreateProcessW can block the calling thread
// for tens of seconds (a target on an offline share took about 42 s), so system.launch, system.open, system.run,
// system.taskManager, and the tray's Edit settings never call them on the UI thread: the UI thread validates and counts
// a launch, copies it into one of kSlots fixed slots, and returns. One thread, started by the first request,
// initializes the single-threaded COM apartment the shell asks for, performs the launches in order, and otherwise
// blocks in one message-aware wait on its stop and wake events. A finished launch keeps its result in its slot and
// calls the completion callback, which posts to the UI thread; DrainCompletions logs the results there. Enqueue,
// DrainCompletions, and Stop are UI-thread operations.
class LaunchWorker final
{
  public:
    static constexpr size_t kSlots = 8;
    // A UTF-8 action target of kRedXeMaximumActionTargetBytes never needs more UTF-16 units than it has bytes.
    static constexpr size_t kTextCharacters = kRedXeMaximumActionTargetBytes + 1;
    // Process teardown waits this long for a launch in progress (Stop).
    static constexpr uint32_t kStopMilliseconds = 1000;

    enum class Kind : uint8_t
    {
        // ShellExecuteExW with the default verb and no wait for the started program.
        Shell,
        // CreateProcessW without a shell; its handles are closed at once.
        Process,
    };

    struct Request final
    {
        Kind kind = Kind::Shell;
        // Shell only: SEE_MASK_FLAG_NO_UI for actions; 0 keeps the shell's own UI (Open With, error boxes).
        ULONG shellMask = SEE_MASK_FLAG_NO_UI;
        // Shell only: an existing file runs in its own folder. The worker probes the target, because the probe itself
        // can block on an unreachable share.
        bool fileFolder = false;
        // Static log event of a failed launch.
        const char* failureEvent = "launch-failed";
        // What was launched, for the log line, for example `action "system.launch"`.
        std::array<char, 64> subject{};
        // Shell: the file, folder, or URI. Process: the executable.
        std::array<wchar_t, kTextCharacters> file{};
        // Process only: the quoted executable followed by its arguments; CreateProcessW may write to it.
        std::array<wchar_t, kTextCharacters + 3> commandLine{};
        // The working directory, or empty.
        std::array<wchar_t, kTextCharacters> directory{};
    };

    // Runs on the worker after each launch; it must only post.
    using Notify = void (*)(void* context) noexcept;

    LaunchWorker(Notify notify, void* context) noexcept : _notify(notify), _context(context) {}
    ~LaunchWorker();
    LaunchWorker(const LaunchWorker&) = delete;
    LaunchWorker& operator=(const LaunchWorker&) = delete;
    LaunchWorker(LaunchWorker&&) = delete;
    LaunchWorker& operator=(LaunchWorker&&) = delete;

    // Copies the request into a free slot, starts the thread on first use, and wakes it. S_OK when queued, ERROR_BUSY
    // when every slot holds a launch that is queued, running, or not yet drained, E_UNEXPECTED while a stopped thread
    // is still finishing a launch, or the thread-start failure.
    [[nodiscard]] HRESULT Enqueue(const Request& request) noexcept;
    // Logs every finished launch in order through host (a failure as one Warning record under its failure event, a
    // success as one Debug record) and frees its slot.
    void DrainCompletions(IRedXeHost* host) noexcept;
    // Drops queued launches and waits at most timeoutMilliseconds for the one in progress. True once the thread has
    // exited or never started; false leaves it to finish that launch, and the destructor joins it.
    [[nodiscard]] bool Stop(uint32_t timeoutMilliseconds) noexcept;
    [[nodiscard]] bool Running() const noexcept
    {
        return _thread.joinable();
    }

  private:
#if defined(REDXE_HOST_PLUGIN_TESTS)
    friend struct PluginHostTestAccess;
#endif
    enum class State : uint8_t
    {
        Empty,
        Queued,
        Running,
        Complete,
    };
    struct Slot final
    {
        Request request;
        State state = State::Empty;
        HRESULT result = S_OK;
    };
    using Perform = HRESULT (*)(Request& request) noexcept;

    [[nodiscard]] static HRESULT Launch(Request& request) noexcept;
    void Worker() noexcept;

    // Slots from _head in submission order; the worker takes the first Queued one, and DrainCompletions frees the
    // Complete ones at the head.
    SRWLOCK _lock = SRWLOCK_INIT;
    std::array<Slot, kSlots> _slots{};
    size_t _head = 0;
    size_t _count = 0;
    wil::unique_event_nothrow _stop;
    wil::unique_event_nothrow _wake;
    std::thread _thread;
    Notify _notify = nullptr;
    void* _context = nullptr;
    // The worker's launch call; tests substitute a probe that never reaches the shell.
    Perform _perform = &LaunchWorker::Launch;
};
