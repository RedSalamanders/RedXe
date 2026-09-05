#include "AVControlBroker.h"
#include <filesystem>
#include <stdexcept>
#include <thread>

namespace
{
uint32_t brokerChecks = 0;
void Check(bool condition, const char* description)
{
    ++brokerChecks;
    if (!condition) throw std::runtime_error(description);
}
uint64_t CpuTime(HANDLE process)
{
    FILETIME created{}, exited{}, kernel{}, user{};
    Check(GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE, "broker process CPU counters");
    return (uint64_t(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) + (uint64_t(user.dwHighDateTime) << 32 | user.dwLowDateTime);
}
}
uint32_t RunBrokerTests()
{
    using namespace AVControl;
    wchar_t executable[MAX_PATH]{};
    Check(GetModuleFileNameW(nullptr, executable, MAX_PATH) != 0, "broker fixture location");
    const auto helper = std::filesystem::path(executable).parent_path() / L"Plugins" / L"AVControlBroker.exe";
    Broker broker;
    wil::unique_event_nothrow cancel;
    Check(SUCCEEDED(cancel.create(wil::EventOptions::ManualReset)), "broker cancellation event");
    auto reply = std::make_unique<BrokerReply>();
    auto start = [&] {
        cancel.ResetEvent();
        Check(SUCCEEDED(broker.Start(helper.native(), true)), "start explicitly synthetic owned helper");
        Check(broker.Running() && broker.ChangeEvent(), "helper has owned process and notification event");
        Check(broker.Execute({}, *reply, cancel.get(), 3000) == S_OK, "synthetic helper observes without hardware access");
        Check(reply->inventory.outputCount == 2 && reply->inventory.inputCount == 2 && reply->inventory.cameraCount == 2,
              "bounded complete synthetic inventory across IPC");
    };
    start();
    auto preferences = std::make_unique<InventoryPreferences>();
    preferences->outputs.count = MaximumOutputs + 1;
    Check(broker.SetPreferences(*preferences) == E_INVALIDARG, "malformed preference union rejected before shared publication");
    *preferences = {};
    preferences->truncated = 1;
    Check(broker.SetPreferences(*preferences) == S_OK && broker.Execute({}, *reply, cancel.get(), 3000) == S_OK && reply->inventory.truncated,
        "profile union overflow crosses the actual helper handshake");
    preferences->truncated = 0;
    Check(broker.SetPreferences(*preferences) == S_OK && broker.Execute({}, *reply, cancel.get(), 3000) == S_OK && !reply->inventory.truncated,
        "replacing preferences clears the stale overflow state");
    const HANDLE stableChangeEvent = broker.ChangeEvent();
    wil::unique_handle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, broker.ProcessId()));
    Check(bool(process), "retain owned helper handle for lifecycle proof");
    const auto cpu = CpuTime(process.get());
    Sleep(120);
    Check(CpuTime(process.get()) - cpu < 50'000, "idle helper blocks on events (less than 5 ms CPU in 120 ms)");
    BrokerCommand command;
    command.operation = BrokerOperation::SetLevel; command.device = DeviceKind::Microphone;
    command.id = reply->inventory.state.microphone.id; command.expectedGeneration = reply->inventory.state.microphone.generation;
    command.expectedRevision = reply->inventory.state.microphone.levelRevision; command.value = 37;
    Check(broker.Execute(command, *reply, cancel.get(), 3000) == S_OK, "level command traverses bounded IPC");
    Check(reply->inventory.state.microphone.level == 37 && reply->inventory.state.microphone.muted,
        "level mutation readback preserves existing microphone mute");
    command.value = 82;
    Check(broker.Execute(command, *reply, cancel.get(), 3000) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH) &&
        reply->inventory.state.microphone.level == 37, "stale endpoint revision is rejected with actual state");
    const auto confirmedRevision = reply->inventory.state.revision;
    Check(broker.Execute({}, *reply, nullptr, 3000) == E_INVALIDARG && broker.Execute({}, *reply, cancel.get(), 0) == E_INVALIDARG,
        "invalid cancellation or deadline rejected without helper work");
    Check(reply->inventory.state.revision == confirmedRevision, "invalid request leaves confirmed reply intact");
    command = {}; command.operation = BrokerOperation::FixtureHang;
    const auto began = GetTickCount64();
    Check(broker.Execute(command, *reply, cancel.get(), 500) == HRESULT_FROM_WIN32(ERROR_TIMEOUT), "stalled device operation times out");
    Check(GetTickCount64() - began < 750 && !broker.Running(), "timeout kills only owned helper within bounded latency");
    Check(WaitForSingleObject(process.get(), 500) == WAIT_OBJECT_0, "timed-out helper has exited");
    Check(reply->inventory.state.revision == confirmedRevision, "timeout preserves last confirmed reply");
    start();
    {
        std::jthread cancellation([&] { Sleep(60); cancel.SetEvent(); });
        const auto when = GetTickCount64();
        Check(broker.Execute(command, *reply, cancel.get(), 3000) == HRESULT_FROM_WIN32(ERROR_CANCELLED), "cancel interrupts stalled helper");
        Check(GetTickCount64() - when < 750 && !broker.Running(), "cancel drains owned child promptly");
    }
    start(); command.operation = BrokerOperation::FixtureExit;
    Check(broker.ChangeEvent() == stableChangeEvent, "notification handle survives connection restart for parent wait safety");
    Check(broker.Execute(command, *reply, cancel.get(), 3000) == HRESULT_FROM_WIN32(ERROR_PROCESS_ABORTED), "helper crash produces local failure");
    start(); command.operation = BrokerOperation::FixtureMalformedReply;
    Check(broker.Execute(command, *reply, cancel.get(), 3000) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA) && !broker.Running(),
        "malformed child reply is rejected and quarantined");
    for (uint32_t corruption = 1; corruption <= 13; ++corruption)
    {
        start(); const auto preserved = reply->inventory.state.revision;
        command.operation = BrokerOperation::FixtureMalformedReply; command.value = corruption;
        Check(broker.Execute(command, *reply, cancel.get(), 3000) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA) && !broker.Running(),
            "invalid ID, Unicode, bool, enum, level, capability or duplicate identity is quarantined before use");
        Check(reply->inventory.state.revision == preserved, "invalid payload never replaces confirmed state");
    }
    start();
    process.reset(OpenProcess(SYNCHRONIZE, FALSE, broker.ProcessId()));
    Check(bool(process), "retain final helper handle");
    broker.Stop();
    Check(!broker.Running() && WaitForSingleObject(process.get(), 500) == WAIT_OBJECT_0, "normal shutdown leaves no orphan helper");
    Check(FAILED(broker.Start((helper.parent_path() / L"does-not-exist.exe").native(), true)) && !broker.Running(), "startup failure cleans partial resources");
    Check(broker.Start(L"bad\"path", true) == E_INVALIDARG, "helper executable quoting cannot inject arguments");
    return brokerChecks;
}
