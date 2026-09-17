#pragma once

// The headless Logicon service: parses the effective settings, runs the device lane (discovery, HID++ connect, key
// faces, input dispatch, the dialpad's buttons and wheels), turns key presses into host actions, and exposes a
// bounded snapshot for the Debug monitor tile and the test contract. One instance per process.

#include "LogiconDevice.h"
#include "LogiconFaces.h"
#include "LogiconRawInput.h"
#include "LogiconSettings.h"
#include "LogiconSynthetic.h"
#include "LogiconSystemData.h"
#include "PlugInterfaces/Action.h"
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Service.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <windows.h>

namespace Logicon
{
inline constexpr uint32_t kGridPixels = kGridSize * kGridSize;
inline constexpr uint32_t kMaximumInjectedControls = 16;
inline constexpr uint32_t kReconnectBackoffSteps = 4;
// Raw Input reports classic wheels in WHEEL_DELTA units: one detent of the dial or the roller is 120.
inline constexpr uint32_t kWheelDetentUnits = 120;
// Requests for the published "logicon" namespace arriving from other owners through IRedXeActionPack::Execute.
inline constexpr uint32_t kMaximumLocalRequests = 4;
using ActionName = std::array<char, kMaximumActionBytes + 1>;

enum class OverrideKind : uint8_t
{
    None = 0,
    // Solid color face.
    Color,
    // Generated test picture (gradient with a slot number).
    Picture,
};

struct FaceOverride final
{
    OverrideKind kind = OverrideKind::None;
    uint32_t colorRgb = 0;
};

// Bounded copy of the last host state the service received.
struct HostStateCopy final
{
    uint32_t flags = 0;
    uint32_t pageIndex = 0;
    uint32_t pageCount = 0;
    uint32_t widgetCount = 0;
    uint32_t raisedWidgetOrdinal = 0;
    std::array<char, 129> pageId{};
    std::array<wchar_t, 129> pageName{};
    uint32_t pageNameLength = 0;
};

struct InjectedControl final
{
    // 0 = LCD key slot (index 0..8), 1 = page button (index 0 previous, 1 next), 2 = dialpad button (index 0..3).
    uint8_t kind = 0;
    uint8_t index = 0;
    bool down = false;
};

inline constexpr uint32_t kInjectKindKey = 0;
inline constexpr uint32_t kInjectKindPageButton = 1;
inline constexpr uint32_t kInjectKindDialButton = 2;

// What the lane knows about the dialpad: its HID++ vendor collection (buttons) and the raw-input wheels.
struct DialpadSnapshot final
{
    bool connected = false;
    bool wheelsListening = false;
    uint32_t buttons = 0;
    // Bit n = button n is diverted (bound); the others keep their native behavior.
    uint32_t divertedButtons = 0;
    uint32_t portCount = 0;
    uint32_t connectAttempts = 0;
    uint32_t buttonPresses = 0;
    // Detents dispatched to a dial or roller action.
    uint32_t wheelSteps = 0;
    HRESULT lastFailure = S_OK;
    DeviceFeatures features{};
    DeviceCounters counters{};
    WheelState wheels{};
    // The action bound to each turn, indexed control * 2 + direction; empty when the turn does nothing.
    std::array<ActionName, kDialpadTurns> turns{};
    std::array<TraceEntry, kTraceSlots> trace{};
    uint32_t traceCount = 0;
};

// Everything the Debug monitor and the test contract read. Copied under the service lock.
struct MonitorSnapshot final
{
    bool laneRunning = false;
    bool deviceAccess = false;
    bool connected = false;
    bool synthetic = false;
    bool rendererReady = false;
    bool iconFont = false;
    bool competingWriter = false;
    uint32_t keys = 0;
    uint32_t pageButtons = 0;
    uint32_t keyPage = 0;
    uint32_t keyPageCount = 1;
    uint32_t brightness = kDefaultBrightness;
    uint32_t portCount = 0;
    uint32_t faceGeneration = 0;
    uint32_t facesWritten = 0;
    uint32_t actionsRequested = 0;
    uint32_t localExecuted = 0;
    ActionName lastAction{};
    uint32_t connectAttempts = 0;
    HRESULT lastFailure = S_OK;
    DeviceFeatures features{};
    DeviceCounters counters{};
    HostStateCopy host{};
    std::array<ActionName, kKeyCount> actions{};
    std::array<KeyFace, kKeyCount> faces{};
    std::array<bool, kKeyCount> bound{};
    std::array<bool, kKeyCount> invalid{};
    std::array<FaceOverride, kKeyCount> overrides{};
    std::array<TraceEntry, kTraceSlots> trace{};
    uint32_t traceCount = 0;
    DialpadSnapshot dialpad{};
    // System Data faces: whether the feed is subscribed and its last reduced values.
    bool systemFeed = false;
    SystemValues system{};
};

class LogiconService final : public RedXeComObject<LogiconService, IRedXeService, IRedXeDeviceWorker, IRedXeActionPack>,
                             public SystemValuesListener
{
  public:
    LogiconService(IRedXeHost* host, uint32_t backgroundRgb) noexcept;
    ~LogiconService();

    // SystemValuesListener (acquisition worker): copies the values and wakes the lane.
    void OnSystemValues(const SystemValues& values) noexcept override;

    // Parses the {"plugin":{},"instance":{...}} factory envelope. Fails without retaining anything on error.
    [[nodiscard]] HRESULT ParseConfiguration(const char* jsonUtf8, uint32_t bytes) noexcept;

    // IRedXeService (UI thread).
    HRESULT STDMETHODCALLTYPE Start(const RedXeServiceStartContext* context) noexcept override;
    HRESULT STDMETHODCALLTYPE ApplySettings(const char* settingsJsonUtf8, uint32_t settingsBytes) noexcept override;
    HRESULT STDMETHODCALLTYPE OnHostState(const RedXeHostState* state) noexcept override;
    HRESULT STDMETHODCALLTYPE Stop() noexcept override;

    // IRedXeDeviceWorker (device lane).
    HRESULT STDMETHODCALLTYPE RunDeviceWork(HANDLE stopEvent, HANDLE wakeEvent) noexcept override;

    // IRedXeActionPack (UI thread): the published "logicon" namespace. Copies the request into a bounded slot,
    // wakes the lane, and returns S_FALSE; the lane runs it exactly like a key binding would.
    HRESULT STDMETHODCALLTYPE Execute(const RedXeActionRequest* request) noexcept override;

    // Monitor and test surface; any thread.
    void CopyMonitorSnapshot(MonitorSnapshot& snapshot) const noexcept;
    // Copies the 434×434 composed grid when its generation differs from `generation`; updates generation.
    [[nodiscard]] bool CopyFaceSurface(uint32_t* bgra, uint32_t capacityPixels, uint32_t& generation) const noexcept;
    [[nodiscard]] HRESULT InjectControl(uint32_t kind, uint32_t index, bool down) noexcept;
    [[nodiscard]] HRESULT SetFaceOverride(uint32_t slot, OverrideKind kind, uint32_t colorRgb) noexcept;
    [[nodiscard]] HRESULT SetBrightness(uint32_t percent) noexcept;
    [[nodiscard]] HRESULT SetSynthetic(bool enabled) noexcept;
    [[nodiscard]] HRESULT InjectSyntheticReport(const uint8_t* report, uint32_t bytes) noexcept;
    void AttachMonitor(bool attached) noexcept;
    [[nodiscard]] bool Started() const noexcept;
    // The in-memory keypad the lane uses when synthetic mode is on; its counters are internally locked.
    [[nodiscard]] SyntheticKeypad* Synthetic() noexcept
    {
        return &_synthetic;
    }

    // Module singleton for the monitor tile and the test contract; borrowed, may be null.
    [[nodiscard]] static LogiconService* Current() noexcept;

  private:
    struct SlotState final
    {
        uint64_t signature = 0;
        bool composed = false;
    };

    // Lane-side helpers.
    [[nodiscard]] HRESULT TryOpenDevice(HANDLE stopEvent) noexcept;
    void CloseDevice(HANDLE stopEvent, bool restore) noexcept;
    [[nodiscard]] HRESULT TryOpenDialpad(HANDLE stopEvent) noexcept;
    void CloseDialpad(HANDLE stopEvent, bool restore) noexcept;
    void DispatchEdges(const ControlEdges& edges) noexcept;
    void DispatchSlot(uint32_t slot) noexcept;
    void DispatchDialButton(uint32_t button) noexcept;
    void DispatchBinding(const KeyBinding* binding) noexcept;
    void DispatchPageButton(uint32_t button) noexcept;
    // Folds raw wheel units into detents and runs the action once per detent.
    void DispatchWheel(uint8_t control, int32_t& accumulator, int32_t deltaRaw) noexcept;
    // UI thread: subscribes, activates, pauses, or drops the System Data feed to match the settings.
    void UpdateSystemFeed() noexcept;
    // Forwards a binding to the host (IRedXeHost::RequestAction) from the lane.
    void RequestAction(const KeyBinding& binding) noexcept;
    void RequestNamed(const char* action, const char* target) noexcept;
    // Runs one "logicon" verb on the lane: keyPage.next, keyPage.previous, keyPage.goto, brightness.
    void ExecuteLocal(std::string_view verb, std::string_view target) noexcept;
    // UI thread: asks the host to resolve every binding's action and target and records the result in valid.
    void ValidateBindings(Settings& settings) noexcept;
    void ChangeKeyPage(int direction) noexcept;
    [[nodiscard]] uint64_t SlotSignature(uint32_t slot, const KeyBinding* binding, const FaceOverride& override,
                                         const HostStateCopy& host, uint32_t minuteOfDay) noexcept;
    [[nodiscard]] HRESULT ComposeSlot(uint32_t slot, const KeyBinding* binding, const FaceOverride& override,
                                      const HostStateCopy& host, uint32_t* tile) noexcept;
    [[nodiscard]] HRESULT ComposeAndWrite(HANDLE stopEvent, bool forceAll) noexcept;
    void PublishSnapshot() noexcept;
    // Fills _snapshot from lane state; the caller holds _lock exclusively.
    void FillSnapshot() noexcept;
    [[nodiscard]] bool ClockFaceVisible() const noexcept;
    void DetectCompetingWriter() noexcept;
    void Log(uint32_t level, const char* eventId, const char* message, HRESULT code = S_OK) noexcept;
    void WakeLane() noexcept;

    IRedXeHost* _host;
    uint32_t _backgroundRgb;
    std::atomic<bool> _started{false};
    std::atomic<bool> _laneRunning{false};
    std::atomic<bool> _deviceAccess{true};
    std::atomic<uint32_t> _monitorAttached{0};

    // Shared between the UI thread, the lane, and monitor readers.
    mutable SRWLOCK _lock = SRWLOCK_INIT;
    Settings _settings{};
    uint32_t _settingsGeneration = 0;
    HostStateCopy _hostState{};
    uint32_t _hostGeneration = 0;
    uint32_t _keyPage = 0;
    std::array<FaceOverride, kKeyCount> _overrides{};
    uint32_t _overrideGeneration = 0;
    std::array<InjectedControl, kMaximumInjectedControls> _injected{};
    uint32_t _injectedCount = 0;
    uint32_t _brightnessRequest = 0;
    bool _syntheticRequested = false;
    HANDLE _wakeEvent = nullptr;
    MonitorSnapshot _snapshot{};
    std::unique_ptr<uint32_t[]> _grid;
    uint32_t _faceGeneration = 0;
    uint32_t _facesWritten = 0;
    uint32_t _actionsRequested = 0;
    uint32_t _localExecuted = 0;
    ActionName _lastAction{};
    struct LocalRequest final
    {
        ActionName action{};
        std::array<char, kMaximumTargetBytes + 1> target{};
    };
    std::array<LocalRequest, kMaximumLocalRequests> _pendingLocal{};
    uint32_t _pendingLocalCount = 0;
    SystemValues _systemValues{};
    uint32_t _systemGeneration = 0;
    bool _systemFeedActive = false;

    // UI-thread-owned: the System Data feed lives while the settings bind a system face.
    wil::com_ptr_nothrow<SystemDataFeed> _systemFeed;
    bool _systemFeedFailureLogged = false;

    // Lane-owned.
    Settings _laneSettings{};
    uint32_t _laneSettingsGeneration = 0;
    HostStateCopy _laneHost{};
    uint32_t _laneHostGeneration = 0;
    uint32_t _laneOverrideGeneration = 0;
    uint32_t _laneKeyPage = 0;
    uint32_t _laneMinute = 0xFFFFFFFFU;
    SystemValues _laneSystem{};
    uint32_t _laneSystemGeneration = 0;
    DeviceSession _session;
    DeviceSession _dialpad;
    RawWheelListener _wheels;
    uint32_t _dialpadAttempts = 0;
    uint32_t _dialButtonPresses = 0;
    uint32_t _wheelSteps = 0;
    int32_t _dialAccumulator = 0;
    int32_t _rollerAccumulator = 0;
    HRESULT _lastDialpadFailure = S_OK;
    HRESULT _lastLoggedDialpadFailure = S_OK;
    FaceRenderer _renderer;
    SyntheticKeypad _synthetic;
    bool _syntheticActive = false;
    bool _competingWriter = false;
    std::array<SlotState, kKeyCount> _slots{};
    std::array<uint32_t, kFacePixels> _tile{};
    std::array<uint32_t, kFaceIconPixels * kFaceIconPixels> _iconScratch{};
    std::unique_ptr<uint8_t[]> _jpeg;
    uint32_t _connectAttempts = 0;
    HRESULT _lastConnectFailure = S_OK;
    HRESULT _lastLoggedConnectFailure = S_OK;
    uint32_t _appliedBrightness = 0;
    // Monitor/test brightness override; 0 means the settings value applies. Cleared by a new settings object.
    uint32_t _brightnessOverride = 0;
    uint64_t _publishedHash = 0;
};
} // namespace Logicon
