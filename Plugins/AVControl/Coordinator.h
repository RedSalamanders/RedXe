#pragma once
#include "../../Common/PlugInterfaces/FactoryImpl.h"
#include "../../Common/PlugInterfaces/Widget.h"
#include "AVControlBroker.h"
#include "AVControlView.h"
#include <optional>

namespace AVControl
{
// One per loaded AV module. Public methods/acknowledged state are UI-thread affine; Run alone owns the broker.
// The host retains this object through its control completion and drains the lane before module shutdown.
class Coordinator final : public RedXeComObject<Coordinator, IRedXeControlWork>
{
  public:
    Coordinator(IRedXeHost* host, std::wstring executable, bool synthetic = false);
    ~Coordinator();
    HRESULT Initialize() noexcept;
    HRESULT RegisterConfiguration(const Configuration* configuration) noexcept;
    void UnregisterConfiguration(const Configuration* configuration) noexcept;
    void ConfigurationChanged() noexcept;
    void SetSubscriberVisible(bool visible) noexcept;
    void Prepare() noexcept;
    HRESULT Submit(const ViewCommand& command) noexcept;
    HRESULT SubmitProfile(const Profile& profile) noexcept;
    void Refresh() noexcept;
    [[nodiscard]] const Inventory& Current() const noexcept
    {
        return *_current;
    }
    [[nodiscard]] uint64_t Revision() const noexcept
    {
        return _revision;
    }
    [[nodiscard]] uint64_t DeviceRevision() const noexcept
    {
        return _deviceRevision;
    }
    [[nodiscard]] uint32_t PendingMask() const noexcept;
    [[nodiscard]] bool ProfilePending() const noexcept
    {
        return _profilePending;
    }
    [[nodiscard]] HRESULT LastResult() const noexcept
    {
        return _lastResult;
    }
    [[nodiscard]] const ApplyOutcome& LastApply() const noexcept
    {
        return _lastApply;
    }
    [[nodiscard]] IRedXeHost* Host() const noexcept
    {
        return _host;
    }
    HRESULT STDMETHODCALLTYPE Run(HANDLE cancelEvent, uint32_t timeoutMilliseconds) noexcept override;
    void STDMETHODCALLTYPE Complete(HRESULT result) noexcept override;

  private:
    struct Command final
    {
        ViewCommand value;
        uint64_t acceptedAt = 0;
        bool followRoute = false;
    };
    IRedXeHost* _host; // Borrowed until runtime shutdown, after control-lane drain.
    std::wstring _executable;
    bool _synthetic = false;
    Broker _broker;
    std::unique_ptr<Inventory> _current;
    std::unique_ptr<BrokerReply> _reply;
    // Two simultaneously loaded dashboard pages, each bounded to 32 widgets. Pointers are UI-thread borrowed;
    // worker snapshots contain IDs only and never retain a widget or its configuration.
    std::array<const Configuration*, 64> _configurations{};
    std::unique_ptr<InventoryPreferences> _preferences, _workPreferences;
    uint64_t _preferenceRevision = 1, _workPreferenceRevision = 0;
    uint64_t _brokerPreferenceRevision = 0;
    mutable SRWLOCK _lock = SRWLOCK_INIT;
    std::array<std::optional<Command>, 5> _commands;
    std::optional<Profile> _profile;
    uint64_t _profileAcceptedAt = 0;
    SafetyState _safety;
    wil::unique_threadpool_wait _wait;
    std::atomic<bool> _dirty{true};
    std::atomic<bool> _observing{false};
    uint32_t _visible = 0;
    uint32_t _runningMask = 0;
    bool _inFlight = false, _profilePending = false, _observeRequested = false, _stopRequested = false;
    uint64_t _revision = 1, _deviceRevision = 1;
    HRESULT _lastResult = S_OK;
    ApplyOutcome _lastApply;
    // Worker receipt, read only after Complete is delivered by the host's synchronized completion queue.
    bool _ranProfile = false, _stateKnown = false;
    uint32_t _controlledRole = 0;
    ApplyOutcome _workApply;
    static void CALLBACK Changed(PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT) noexcept;
    void Disarm() noexcept;
    void Arm() noexcept;
    HRESULT Schedule() noexcept;
    [[nodiscard]] bool HasPending() noexcept;
    void Invalidate() noexcept;
};
} // namespace AVControl
