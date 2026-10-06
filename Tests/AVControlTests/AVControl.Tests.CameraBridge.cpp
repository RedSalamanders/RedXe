#include "Camera/CameraBridgeServer.h"
#include "Camera/CameraController.h"
#include <atomic>
#include <chrono>
#include <mfapi.h>
#include <sddl.h>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
using namespace AVControl::Camera;
uint32_t checks = 0;
void Check(bool value, const char* message)
{
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class Condition> void Until(Condition condition, const char* message)
{
    const auto deadline = GetTickCount64() + 2000;
    while (!condition() && GetTickCount64() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    Check(condition(), message);
}
std::wstring CurrentSid()
{
    wil::unique_handle token;
    Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()) != FALSE,
          "bridge fixture reads its own identity");
    alignas(TOKEN_USER) std::array<BYTE, sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE> data{};
    DWORD bytes = 0;
    Check(GetTokenInformation(token.get(), TokenUser, data.data(), static_cast<DWORD>(data.size()), &bytes) != FALSE,
          "bridge fixture reads bounded token user");
    wil::unique_hlocal_string sid;
    Check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, sid.put()) != FALSE,
          "bridge fixture canonical SID");
    return sid.get();
}
struct Observer final : BridgeObserver
{
    std::atomic<uint32_t> calls{0}, stops{0};
    void OnBridgeWork() noexcept override
    {
        ++calls;
    }
    void OnBridgeStopping() noexcept override
    {
        ++stops;
    }
};
wil::unique_handle OpenPipe(const BridgeIdentity& identity)
{
    return wil::unique_handle(
        CreateFileW(identity.pipeName.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
}
} // namespace
uint32_t RunCameraBridgeTests()
{
    checks = 0;
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "bridge fixture MTA");
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    Check(SUCCEEDED(MFStartup(MF_VERSION)), "bridge fixture Media Foundation");
    const auto mf = wil::scope_exit([] { (void)MFShutdown(); });
    const auto sid = CurrentSid();
    GUID guid{};
    Check(SUCCEEDED(CoCreateGuid(&guid)), "bridge fixture unique namespace");
    BridgeIdentity identity;
    Check(MakeBridgeIdentity(sid.c_str(), identity, &guid) == S_OK && identity.synthetic,
          "bridge isolated admission is explicit");
    BridgeIdentity preserved = identity;
    Check(FAILED(MakeBridgeIdentity(L"bad)SID", identity)) && identity.pipeName == preserved.pipeName,
          "bridge rejects injected or invalid SID transactionally");
    Observer observer;
    BridgeServer server;
    Check(server.Start(identity, &observer) == S_OK, "bridge server claims a private local-only pipe");
    const auto cleanup = wil::scope_exit([&] { server.Stop(); });
    BridgeServer duplicate;
    Check(FAILED(duplicate.Start(identity, nullptr)), "bridge rejects an existing first pipe instance");
    const auto idleCalls = observer.calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(observer.calls == idleCalls, "bridge idle lane blocks with no polling callbacks");
    constexpr size_t imageBytes = static_cast<size_t>(FrameWidth) * FrameHeight * 3 / 2;
    std::vector<BYTE> frame(imageBytes, 100), output(imageBytes);
    std::fill_n(frame.data(), static_cast<size_t>(FrameWidth) * FrameHeight, BYTE{235});
    BridgeFrameProvider provider(identity);
    Check(provider.Start() == S_OK, "consumer connects through the real bounded handshake");
    Until([&] { return server.Consumers() == 1; }, "bridge tracks one established consumer");
    Check(server.DemandingConsumers() == 0, "stream start alone does not request physical capture");
    Check(provider.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime()) == S_OK &&
              output[0] == 16,
          "a connected route starts neutral before camera-on");
    provider.EndFrame();
    Until([&] { return server.DemandingConsumers() == 1; }, "a requested sample renews the capture-demand lease");
    std::this_thread::sleep_for(std::chrono::milliseconds(275));
    Check(server.DemandingConsumers() == 0 && server.Consumers() == 1,
          "paused sample demand expires without closing the selected camera stream");
    Check(server.SetGate(2, true) == S_OK, "bridge acknowledges enabled generation");
    Check(server.Publish(2, frame, MFGetSystemTime()) == S_OK, "helper publishes one latest synthetic image");
    Check(provider.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime()) == S_OK &&
              output[0] == 235,
          "consumer receives the admitted current image");
    // Keep the media publication lease until after the failed Off. Off may not claim success while this
    // publication can still occur; the failed peer is disconnected and cannot accept another old callback.
    HRESULT off = S_OK;
    std::thread offThread([&] { off = server.SetGate(3, false); });
    offThread.join();
    Check(off == HRESULT_FROM_WIN32(ERROR_TIMEOUT),
          "camera off cannot acknowledge past an uncompleted media publication");
    provider.EndFrame();
    Check(server.Publish(2, frame, MFGetSystemTime()) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),
          "old-source callback rejected after off intent");
    Until([&] { return server.Consumers() == 0; }, "contended peer disconnects instead of falsely acknowledging off");
    Check(FAILED(provider.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime())),
          "disconnected provider requests full neutral replacement immediately");
    provider.EndFrame();
    provider.Stop();
    Check(provider.Start() == S_OK, "consumer reconnects after failed publication fence");
    Until([&] { return server.Consumers() == 1; }, "reconnected consumer is counted once");
    Check(server.SetGate(4, true) == S_OK && server.Publish(4, frame, MFGetSystemTime()) == S_OK,
          "new generation restarts from a fresh image");
    Check(server.SetGate(5, false) == S_OK, "uncontended off is acknowledged");
    Check(provider.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime()) == S_OK &&
              output[0] == 16,
          "acknowledged off has no old physical pixels");
    provider.EndFrame();
    Check(server.SetGate(6, true) == S_OK && server.Publish(6, frame, MFGetSystemTime()) == S_OK &&
              server.SetGate(7, true) == S_OK,
          "source replacement changes generation while remaining on");
    Check(provider.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime()) == S_OK &&
              output[0] == 16,
          "source replacement clears the previous source until a new frame");
    provider.EndFrame();
    Check(server.Publish(6, frame, MFGetSystemTime()) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),
          "late replaced-source callback is rejected");
    provider.Stop();
    Until([&] { return server.Consumers() == 0; }, "last consumer close releases its pipe and frame lease");

    // Slow and malformed handshakes never take a consumer slot and cannot monopolize admission.
    auto stalled = OpenPipe(identity);
    Check(!!stalled, "fixture opens an intentionally stalled peer");
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    stalled.reset();
    Check(server.Consumers() == 0, "stalled handshake has no active media lease");
    auto invalid = OpenPipe(identity);
    Check(!!invalid, "fixture opens malformed handshake peer");
    BridgeHello hello;
    hello.version = 0;
    BridgeReply reply;
    auto deadline = GetTickCount64() + BridgeDeadlineMs;
    Check(BridgeTransfer(invalid.get(), true, &hello, sizeof(hello), deadline) == S_OK &&
              BridgeTransfer(invalid.get(), false, &reply, sizeof(reply), deadline) == S_OK && FAILED(reply.status),
          "wrong protocol is explicitly rejected");
    invalid.reset();

    std::array<std::unique_ptr<BridgeFrameProvider>, MaximumConsumers> peers;
    for (auto& peer : peers)
    {
        peer = std::make_unique<BridgeFrameProvider>(identity);
        Check(peer->Start() == S_OK, "bounded consumer admitted");
    }
    Until([&] { return server.Consumers() == MaximumConsumers; }, "all four bounded consumer slots work");
    BridgeFrameProvider excess(identity);
    Check(excess.Start() == S_OK &&
              FAILED(excess.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime())),
          "excess consumer stays neutral with no unbounded slot allocation");
    excess.EndFrame();
    excess.Stop();
    Check(server.Consumers() == MaximumConsumers, "excess peer cannot evict existing consumers");
    for (auto& peer : peers)
        peer.reset();
    Until([&] { return server.Consumers() == 0; }, "all consumer disconnect notifications drain");

    server.Stop();
    Check(observer.stops == 1, "bridge shutdown invokes capture cleanup once");
    server.Stop();
    Check(observer.stops == 1, "bridge shutdown is idempotent");
    Check(server.Start(identity, &observer) == S_OK, "bridge namespace is reusable after an owned restart");
    Check(provider.Start() == S_OK, "existing route reconnects to restarted helper");
    Until([&] { return server.Consumers() == 1; }, "restart consumer admission");
    server.Stop();
    Check(FAILED(provider.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime())),
          "helper death causes immediate neutral fallback");
    provider.EndFrame();
    provider.Stop();

    // Production does not admit an ordinary user process as Frame Server, even with the correct per-user ACL.
    // Retain the unique test pipe namespace to avoid touching any installed/active RedXe camera route.
    identity.synthetic = false;
    Check(server.Start(identity, nullptr) == S_OK,
          "fixture starts production principal admission in its isolated namespace");
    auto untrusted = OpenPipe(identity);
    Check(!!untrusted, "own user can reach admission for authorization check");
    hello = {};
    deadline = GetTickCount64() + BridgeDeadlineMs;
    Check(BridgeTransfer(untrusted.get(), true, &hello, sizeof(hello), deadline) == S_OK &&
              BridgeTransfer(untrusted.get(), false, &reply, sizeof(reply), deadline) == S_OK &&
              reply.status == E_ACCESSDENIED,
          "production requires Session-0 Local Service, not just pipe access");
    Check(server.Consumers() == 0, "rejected principal never opens a media channel");
    return checks;
}

namespace
{
std::wstring ReadyName(PCWSTR guid)
{
    return std::wstring(L"Local\\RedXe.Camera.Test.Ready.") + guid;
}
struct ChildFrames final : BridgeObserver
{
    explicit ChildFrames(BridgeServer& value)
        : server(value), pixels(static_cast<size_t>(FrameWidth) * FrameHeight * 3 / 2, 128)
    {
        std::fill_n(pixels.data(), static_cast<size_t>(FrameWidth) * FrameHeight, BYTE{210});
        done.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    }
    BridgeServer& server;
    std::vector<BYTE> pixels;
    wil::unique_event done;
    bool admitted = false;
    void OnBridgeWork() noexcept override
    {
        if (!admitted && server.Consumers())
        {
            admitted = true;
            if (FAILED(server.SetGate(2, true)) || FAILED(server.Publish(2, pixels, MFGetSystemTime())))
                SetEvent(done.get());
        }
        else if (admitted && !server.Consumers())
            SetEvent(done.get());
    }
    void OnBridgeStopping() noexcept override {}
};
} // namespace
int RunCameraBridgeChild(const wchar_t* identifier)
{
    using namespace AVControl::Camera;
    GUID guid{};
    if (!identifier || wcsnlen_s(identifier, 40) != 38 || FAILED(CLSIDFromString(identifier, &guid)))
        return 2;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        return 3;
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    if (FAILED(MFStartup(MF_VERSION)))
        return 4;
    const auto mf = wil::scope_exit([] { (void)MFShutdown(); });
    BridgeIdentity identity;
    if (FAILED(MakeBridgeIdentity(CurrentSid().c_str(), identity, &guid)))
        return 5;
    BridgeServer server;
    ChildFrames observer(server);
    const auto stop = wil::scope_exit([&] { server.Stop(); });
    if (!observer.done || FAILED(server.Start(identity, &observer)))
        return 6;
    wil::unique_handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, ReadyName(identifier).c_str()));
    if (!ready || !SetEvent(ready.get()))
        return 7;
    return WaitForSingleObject(observer.done.get(), 5000) == WAIT_OBJECT_0 && observer.admitted &&
                   server.Consumers() == 0
               ? 0
               : 8;
}
uint32_t RunCameraCrossProcessTests()
{
    using namespace AVControl::Camera;
    checks = 0;
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "cross-process bridge fixture MTA");
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    Check(SUCCEEDED(MFStartup(MF_VERSION)), "cross-process bridge fixture MF");
    const auto mf = wil::scope_exit([] { (void)MFShutdown(); });
    GUID guid{};
    wchar_t identifier[40]{};
    Check(SUCCEEDED(CoCreateGuid(&guid)) &&
              StringFromGUID2(guid, identifier, static_cast<int>(std::size(identifier))) != 0,
          "cross-process bridge has a unique isolated identity");
    wil::unique_event ready(CreateEventW(nullptr, TRUE, FALSE, ReadyName(identifier).c_str()));
    Check(!!ready && GetLastError() != ERROR_ALREADY_EXISTS, "cross-process ready event is exclusively this fixture's");
    wchar_t executable[32768]{};
    Check(GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable))) != 0,
          "bridge fixture locates its own child executable");
    std::wstring command = std::wstring(L"\"") + executable + L"\" --camera-bridge-fixture " + identifier;
    wil::unique_handle job(CreateJobObjectW(nullptr, nullptr));
    Check(!!job, "cross-process fixture owns its child job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    Check(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE,
          "fixture job contains exactly one owned child");
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION created{};
    Check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                         nullptr, nullptr, &startup, &created) != FALSE,
          "fixture starts a separate camera helper without inherited handles");
    wil::unique_handle process(created.hProcess), thread(created.hThread);
    if (!AssignProcessToJobObject(job.get(), process.get()))
    {
        (void)TerminateProcess(process.get(), ERROR_CANCELLED);
        Check(false, "fixture assigns the suspended child before any execution");
    }
    Check(ResumeThread(thread.get()) != DWORD(-1), "owned fixture child resumes");
    Check(WaitForSingleObject(ready.get(), 3000) == WAIT_OBJECT_0,
          "child bridge accepts connections within the startup deadline");
    BridgeIdentity identity;
    Check(MakeBridgeIdentity(CurrentSid().c_str(), identity, &guid) == S_OK,
          "parent uses the exact child fixture identity");
    BridgeFrameProvider provider(identity);
    Check(provider.Start() == S_OK, "provider authenticates a server in another process");
    std::vector<BYTE> output(static_cast<size_t>(FrameWidth) * FrameHeight * 3 / 2);
    bool bright = false;
    const auto deadline = GetTickCount64() + 1000;
    while (!bright && GetTickCount64() < deadline)
    {
        const HRESULT result =
            provider.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime());
        provider.EndFrame();
        bright = result == S_OK && output.front() == 210 && output[FrameWidth * FrameHeight - 1] == 210 &&
                 output.back() == 128;
        if (!bright)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(bright, "a separate process transfers actual NV12 pixels through the admitted mapping");
    provider.Stop();
    Check(WaitForSingleObject(process.get(), 3000) == WAIT_OBJECT_0,
          "consumer disconnect releases the child acquisition lease");
    DWORD result = 99;
    Check(GetExitCodeProcess(process.get(), &result) != FALSE && result == 0,
          "cross-process camera fixture exits cleanly with no orphan helper");
    return checks;
}

namespace
{
// Faults are compiled into the test executable only. The production source/activation and broker have no
// configuration or command-line switch that substitutes this capture session for a real physical camera.
class HungCapture final : public CaptureSession
{
  public:
    explicit HungCapture(uint32_t stage) : _stage(stage) {}
    HRESULT Open(const AVControl::DeviceId&, HANDLE) noexcept override
    {
        if (_stage == 0)
            Sleep(INFINITE);
        _opened = true;
        return S_OK;
    }
    void Close() noexcept override
    {
        if (_opened && _stage == 2)
            Sleep(INFINITE);
        _opened = false;
    }
    HRESULT ReadFrame(std::span<BYTE> output, LONGLONG& timestamp) noexcept override
    {
        if (_stage == 1)
            Sleep(INFINITE);
        if (_sent)
            return S_FALSE;
        std::fill(output.begin(), output.end(), BYTE{128});
        std::fill_n(output.data(), static_cast<size_t>(FrameWidth) * FrameHeight, BYTE{210});
        timestamp = MFGetSystemTime();
        _sent = true;
        return S_OK;
    }

  private:
    uint32_t _stage;
    bool _opened = false, _sent = false;
};
} // namespace
int RunCameraWatchdogChild(const wchar_t* identifier, const wchar_t* stage)
{
    using namespace AVControl::Camera;
    GUID guid{};
    if (!identifier || wcsnlen_s(identifier, 40) != 38 || FAILED(CLSIDFromString(identifier, &guid)) || !stage ||
        stage[0] < L'0' || stage[0] > L'2' || stage[1])
        return 2;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        return 3;
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    if (FAILED(MFStartup(MF_VERSION)))
        return 4;
    const auto mf = wil::scope_exit([] { (void)MFShutdown(); });
    BridgeIdentity identity;
    if (FAILED(MakeBridgeIdentity(CurrentSid().c_str(), identity, &guid)))
        return 5;
    wil::unique_event changed(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    CameraController controller(identity, std::make_unique<HungCapture>(static_cast<uint32_t>(stage[0] - L'0')));
    AVControl::DeviceId id;
    (void)id.Assign("isolated-hung-capture");
    if (!changed || FAILED(controller.Initialize(changed.get())) ||
        FAILED(controller.Select(id, controller.Snapshot().revision)) ||
        FAILED(controller.Enable(true, controller.Snapshot().revision)) || controller.WatchdogTimeout() != INFINITE)
        return 6;
    wil::unique_handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, ReadyName(identifier).c_str()));
    if (!ready || !SetEvent(ready.get()))
        return 7;
    // Same event/deadline policy as the real broker MTA, with no UI observations or commands after startup.
    // This proves driver containment while every AV tile is hidden, including Close after demand expires.
    for (;;)
    {
        const DWORD wait = WaitForSingleObject(controller.ProgressEvent(), controller.WatchdogTimeout());
        if (wait == WAIT_OBJECT_0)
            continue;
        if (wait == WAIT_TIMEOUT && controller.WatchdogTimeout() == 0)
        {
            (void)TerminateProcess(GetCurrentProcess(), ERROR_TIMEOUT);
            return 9;
        }
        if (wait != WAIT_TIMEOUT)
            return 8;
    }
}
uint32_t RunCameraWatchdogTests()
{
    using namespace AVControl::Camera;
    checks = 0;
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "camera watchdog fixture MTA");
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    Check(SUCCEEDED(MFStartup(MF_VERSION)), "camera watchdog fixture Media Foundation");
    const auto mf = wil::scope_exit([] { (void)MFShutdown(); });
    for (uint32_t stage = 0; stage < 3; ++stage)
    {
        GUID guid{};
        wchar_t identifier[40]{}, executable[32768]{};
        Check(SUCCEEDED(CoCreateGuid(&guid)) &&
                  StringFromGUID2(guid, identifier, static_cast<int>(std::size(identifier))) != 0,
              "watchdog child uses an isolated bridge identity");
        wil::unique_event ready(CreateEventW(nullptr, TRUE, FALSE, ReadyName(identifier).c_str()));
        Check(!!ready && GetLastError() != ERROR_ALREADY_EXISTS, "watchdog ready event is unique");
        Check(GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable))) != 0,
              "watchdog locates its own fixture executable");
        std::wstring command = std::wstring(L"\"") + executable + L"\" --camera-watchdog-fixture " + identifier + L" " +
                               std::to_wstring(stage);
        wil::unique_handle job(CreateJobObjectW(nullptr, nullptr));
        Check(!!job, "watchdog owns an isolated child job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit = 1;
        Check(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE,
              "watchdog test job contains one process with kill-on-close");
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION created{};
        Check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                             nullptr, nullptr, &startup, &created) != FALSE,
              "watchdog fixture child starts without inherited handles");
        wil::unique_handle process(created.hProcess), thread(created.hThread);
        if (!AssignProcessToJobObject(job.get(), process.get()))
        {
            (void)TerminateProcess(process.get(), ERROR_CANCELLED);
            Check(false, "watchdog child is assigned before running");
        }
        Check(ResumeThread(thread.get()) != DWORD(-1), "watchdog child resumes inside its job");
        Check(WaitForSingleObject(ready.get(), 3000) == WAIT_OBJECT_0,
              "watchdog child arms without opening capture or scheduling idle timeouts");
        BridgeIdentity identity;
        Check(MakeBridgeIdentity(CurrentSid().c_str(), identity, &guid) == S_OK,
              "watchdog parent uses the unique synthetic channel");
        BridgeFrameProvider provider(identity);
        Check(provider.Start() == S_OK, "watchdog fixture consumer connects");
        std::vector<BYTE> output(static_cast<size_t>(FrameWidth) * FrameHeight * 3 / 2);
        const auto started = GetTickCount64();
        Check(provider.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime()) == S_OK,
              "watchdog fixture requests its first frame");
        provider.EndFrame();
        if (stage == 2)
        {
            bool bright = false;
            const auto deadline = GetTickCount64() + 500;
            while (!bright && GetTickCount64() < deadline)
            {
                const HRESULT read =
                    provider.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime());
                provider.EndFrame();
                bright = read == S_OK && output.front() == 210;
                if (!bright)
                    Sleep(2);
            }
            Check(bright, "close-hang fixture publishes before consumer pauses");
        }
        Check(WaitForSingleObject(process.get(), 3200) == WAIT_OBJECT_0 && GetTickCount64() - started < 3500,
              "Open, ReadFrame or demand-expiry Close hang is contained without UI polling");
        DWORD status = 0;
        Check(GetExitCodeProcess(process.get(), &status) != FALSE && status == ERROR_TIMEOUT,
              "camera watchdog terminates only its owned helper with timeout status");
        Check(FAILED(provider.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime())),
              "terminated producer cannot replay its cached image through a successful fill");
        provider.EndFrame();
        provider.Stop();
    }
    return checks;
}
