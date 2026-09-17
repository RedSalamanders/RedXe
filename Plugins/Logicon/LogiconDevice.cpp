#include "LogiconDevice.h"

#include <algorithm>
#include <cstring>

namespace Logicon
{
namespace
{
constexpr uint32_t kInputBufferBytes = kMaximumHidReportBytes;

[[nodiscard]] HRESULT CommandFailure(const HidppFrame& frame) noexcept
{
    // HID++ 2.0 error codes: 1 invalid argument/function, 5 unsupported, 6 invalid feature index, 7 busy.
    switch (frame.errorCode)
    {
    case 5:
    case 6:
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    case 7:
        return HRESULT_FROM_WIN32(ERROR_BUSY);
    default:
        return HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION);
    }
}
} // namespace

HRESULT DeviceSession::AttachPort(std::unique_ptr<HidPort> port) noexcept
{
    if (!port)
    {
        return E_POINTER;
    }
    if (_portCount >= _ports.size())
    {
        return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
    }
    _ports[_portCount++] = std::move(port);
    return S_OK;
}

void DeviceSession::Detach() noexcept
{
    for (auto& port : _ports)
    {
        port.reset();
    }
    _portCount = 0;
    _role = DeviceRole::Keypad;
    _features = DeviceFeatures{};
    _controls = ControlState{};
    _edges = ControlEdges{};
    _savedControlCount = 0;
    _divertedDialButtons = 0;
    _connected = false;
    _restored = true;
}

bool DeviceSession::HasPorts() const noexcept
{
    return _portCount != 0;
}

bool DeviceSession::Connected() const noexcept
{
    return _connected && !Disconnected();
}

bool DeviceSession::Disconnected() const noexcept
{
    if (_portCount == 0)
    {
        return true;
    }
    for (uint32_t index = 0; index < _portCount; ++index)
    {
        if (_ports[index]->Disconnected())
        {
            return true;
        }
    }
    return false;
}

HidPort* DeviceSession::PortForOutput(uint8_t reportId) const noexcept
{
    for (uint32_t index = 0; index < _portCount; ++index)
    {
        if (_ports[index]->Info().SupportsOutput(reportId))
        {
            return _ports[index].get();
        }
    }
    // A descriptor that declares no per-id caps still routes to the widest collection.
    HidPort* widest = nullptr;
    for (uint32_t index = 0; index < _portCount; ++index)
    {
        if (!widest || _ports[index]->Info().outputReportBytes > widest->Info().outputReportBytes)
        {
            widest = _ports[index].get();
        }
    }
    return widest;
}

HidPort* DeviceSession::PortForFeature(uint8_t reportId) const noexcept
{
    for (uint32_t index = 0; index < _portCount; ++index)
    {
        if (_ports[index]->Info().SupportsFeature(reportId))
        {
            return _ports[index].get();
        }
    }
    for (uint32_t index = 0; index < _portCount; ++index)
    {
        if (_ports[index]->Info().featureReportBytes != 0)
        {
            return _ports[index].get();
        }
    }
    return nullptr;
}

void DeviceSession::Trace(const uint8_t* report, uint32_t bytes, bool outbound) noexcept
{
    TraceEntry& entry = _trace[(_traceHead + _traceCount) % kTraceSlots];
    if (_traceCount == kTraceSlots)
    {
        _traceHead = (_traceHead + 1) % kTraceSlots;
    }
    else
    {
        ++_traceCount;
    }
    entry = TraceEntry{};
    entry.tick = GetTickCount();
    entry.outbound = outbound;
    entry.length = bytes;
    std::memcpy(entry.bytes.data(), report, std::min<uint32_t>(bytes, kTraceBytes));
}

uint32_t DeviceSession::CopyTrace(TraceEntry* entries, uint32_t capacity) const noexcept
{
    if (!entries || capacity == 0)
    {
        return 0;
    }
    const uint32_t count = std::min(capacity, _traceCount);
    const uint32_t skip = _traceCount - count;
    for (uint32_t index = 0; index < count; ++index)
    {
        entries[index] = _trace[(_traceHead + skip + index) % kTraceSlots];
    }
    return count;
}

HRESULT DeviceSession::WriteReport(const uint8_t* report, uint32_t bytes, HANDLE stopEvent) noexcept
{
    if (!report || bytes == 0)
    {
        return E_POINTER;
    }
    HidPort* port = PortForOutput(report[0]);
    if (!port)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    const HRESULT result = port->Write(report, bytes, stopEvent, kWriteTimeoutMilliseconds);
    if (FAILED(result))
    {
        _counters.lastFailure = result;
        return result;
    }
    ++_counters.reportsOut;
    if (report[0] != kReportVlpImage || (report[4] & 0x80U) != 0)
    {
        Trace(report, bytes, true);
    }
    return S_OK;
}

void DeviceSession::HandleInput(const uint8_t* report, uint32_t bytes, bool& changed) noexcept
{
    ++_counters.reportsIn;
    Trace(report, bytes, false);
    InputEvent event{};
    if (!ParseInputEvent(report, bytes, _features.display, _features.reprogControls, event))
    {
        return;
    }
    if (event.kind == InputKind::DisplayKeys)
    {
        changed = changed || _controls.keys != event.mask;
        _edges.keysDown |= event.mask & ~_controls.keys;
        _controls.keys = event.mask;
    }
    else if (event.kind == InputKind::DivertedButtons && _role == DeviceRole::Dialpad)
    {
        const uint32_t mask = DialpadButtonMask(event);
        changed = changed || _controls.dialButtons != mask;
        _edges.dialButtonsDown |= mask & ~_controls.dialButtons;
        _controls.dialButtons = mask;
    }
    else if (event.kind == InputKind::DivertedButtons)
    {
        changed = changed || _controls.pageButtons != event.mask;
        _edges.pageButtonsDown |= event.mask & ~_controls.pageButtons;
        _controls.pageButtons = event.mask;
    }
}

HRESULT DeviceSession::Pump(bool& changed, ControlEdges& edges) noexcept
{
    changed = false;
    // Presses seen while a command waited for its answer are reported by this pump.
    const auto flush = wil::scope_exit(
        [this, &edges]() noexcept
        {
            edges = _edges;
            _edges = ControlEdges{};
        });
    std::array<uint8_t, kInputBufferBytes> buffer{};
    for (uint32_t index = 0; index < _portCount; ++index)
    {
        for (uint32_t drained = 0; drained < 64; ++drained)
        {
            uint32_t bytes = 0;
            const HRESULT result =
                _ports[index]->TakeReport(buffer.data(), static_cast<uint32_t>(buffer.size()), bytes);
            if (result == S_FALSE)
            {
                break;
            }
            if (FAILED(result))
            {
                _counters.lastFailure = result;
                return result;
            }
            HandleInput(buffer.data(), bytes, changed);
        }
    }
    return S_OK;
}

HRESULT DeviceSession::SendCommand(uint8_t featureIndex, uint8_t function, const uint8_t* params, uint32_t paramBytes,
                                   HANDLE stopEvent, HidppFrame& response, uint8_t* responseBuffer,
                                   uint32_t responseCapacity) noexcept
{
    response = HidppFrame{};
    std::array<uint8_t, kLongReportBytes> report{};
    if (BuildLongReport(kDeviceIndexWired, featureIndex, function, params, paramBytes, report.data(),
                        static_cast<uint32_t>(report.size())) == 0)
    {
        return E_INVALIDARG;
    }
    HRESULT result = WriteReport(report.data(), static_cast<uint32_t>(report.size()), stopEvent);
    if (FAILED(result))
    {
        return result;
    }

    std::array<HANDLE, kMaximumHidCollections + 1> handles{};
    const uint64_t deadline = GetTickCount64() + kCommandTimeoutMilliseconds;
    for (;;)
    {
        // Drain first: the answer may already be buffered.
        for (uint32_t index = 0; index < _portCount; ++index)
        {
            for (uint32_t drained = 0; drained < 64; ++drained)
            {
                uint32_t bytes = 0;
                result = _ports[index]->TakeReport(responseBuffer, responseCapacity, bytes);
                if (result == S_FALSE)
                {
                    break;
                }
                if (FAILED(result))
                {
                    _counters.lastFailure = result;
                    return result;
                }
                HidppFrame frame{};
                if (ParseHidppFrame(responseBuffer, bytes, frame) && FrameAnswers(frame, featureIndex, function))
                {
                    ++_counters.reportsIn;
                    Trace(responseBuffer, bytes, false);
                    response = frame;
                    if (frame.error)
                    {
                        ++_counters.commandErrors;
                        _counters.lastHidppError = frame.errorCode;
                        return CommandFailure(frame);
                    }
                    return S_OK;
                }
                bool changed = false;
                HandleInput(responseBuffer, bytes, changed);
            }
        }
        const uint64_t now = GetTickCount64();
        if (now >= deadline)
        {
            ++_counters.commandErrors;
            _counters.lastFailure = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        uint32_t handleCount = 0;
        if (stopEvent)
        {
            handles[handleCount++] = stopEvent;
        }
        handleCount += ReadEvents(handles.data() + handleCount, static_cast<uint32_t>(handles.size()) - handleCount);
        const DWORD waited =
            WaitForMultipleObjects(handleCount, handles.data(), FALSE, static_cast<DWORD>(deadline - now));
        if (stopEvent && waited == WAIT_OBJECT_0)
        {
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
        if (waited == WAIT_FAILED)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }
}

HRESULT DeviceSession::ResolveFeature(uint16_t featureId, HANDLE stopEvent, uint8_t& index) noexcept
{
    index = 0;
    uint8_t params[3]{};
    params[0] = static_cast<uint8_t>(featureId >> 8U);
    params[1] = static_cast<uint8_t>(featureId & 0xFFU);
    HidppFrame response{};
    std::array<uint8_t, kInputBufferBytes> buffer{};
    const HRESULT result = SendCommand(0x00, 0, params, sizeof(params), stopEvent, response, buffer.data(),
                                       static_cast<uint32_t>(buffer.size()));
    _lastLookup = LookupRecord{};
    _lastLookup.featureId = featureId;
    _lastLookup.result = result;
    if (SUCCEEDED(result) && response.params)
    {
        _lastLookup.paramBytes =
            std::min<uint32_t>(response.paramBytes, static_cast<uint32_t>(_lastLookup.params.size()));
        std::memcpy(_lastLookup.params.data(), response.params, _lastLookup.paramBytes);
    }
    if (FAILED(result))
    {
        return result;
    }
    if (response.paramBytes != 0 && response.params[0] != 0)
    {
        index = response.params[0];
        return S_OK;
    }
    // The root hides some features (0x19A1 answers index 0 on the keypad); the feature set still lists them.
    return ResolveFeatureThroughSet(featureId, stopEvent, index);
}

HRESULT DeviceSession::ResolveFeatureThroughSet(uint16_t featureId, HANDLE stopEvent, uint8_t& index) noexcept
{
    index = 0;
    std::array<uint8_t, kInputBufferBytes> buffer{};
    HidppFrame response{};
    uint8_t setParams[3] = {0x00, 0x01, 0x00};
    HRESULT result = SendCommand(0x00, 0, setParams, sizeof(setParams), stopEvent, response, buffer.data(),
                                 static_cast<uint32_t>(buffer.size()));
    if (FAILED(result))
    {
        return result;
    }
    const uint8_t setIndex = response.paramBytes != 0 ? response.params[0] : 0;
    if (setIndex == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    result =
        SendCommand(setIndex, 0, nullptr, 0, stopEvent, response, buffer.data(), static_cast<uint32_t>(buffer.size()));
    if (FAILED(result))
    {
        return result;
    }
    const uint32_t count = response.paramBytes != 0 ? response.params[0] : 0;
    for (uint32_t candidate = 1; candidate <= count && candidate <= 64; ++candidate)
    {
        uint8_t query[1] = {static_cast<uint8_t>(candidate)};
        result = SendCommand(setIndex, 1, query, sizeof(query), stopEvent, response, buffer.data(),
                             static_cast<uint32_t>(buffer.size()));
        if (FAILED(result))
        {
            return result;
        }
        if (response.paramBytes >= 2 &&
            static_cast<uint16_t>((response.params[0] << 8U) | response.params[1]) == featureId)
        {
            index = static_cast<uint8_t>(candidate);
            return S_OK;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

HRESULT DeviceSession::Connect(HANDLE stopEvent) noexcept
{
    if (_portCount == 0 || !PortForOutput(kReportLong))
    {
        return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    }
    _connected = false;
    _role = DeviceRole::Keypad;
    _features = DeviceFeatures{};
    _controls = ControlState{};
    HRESULT result = ResolveFeature(kFeatureContextualDisplay, stopEvent, _features.display);
    if (result == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED))
    {
        // Neither the root nor the feature set names the display feature; both reference implementations use the
        // index observed on shipping firmware. The assumption is reported through Features().displayAssumed.
        _features.display = kObservedDisplayFeatureIndex;
        _features.displayAssumed = true;
        result = S_OK;
    }
    if (FAILED(result))
    {
        return result;
    }
    result = ResolveFeature(kFeatureReprogControls, stopEvent, _features.reprogControls);
    if (FAILED(result))
    {
        return result;
    }
    // Brightness is optional: an older firmware without 0x8040 still draws faces.
    (void)ResolveFeature(kFeatureBrightness, stopEvent, _features.brightness);

    // Divert the page buttons so they arrive as 0x1B04 events instead of keyboard usages, remembering what the
    // device had so Restore can put it back.
    const uint16_t pageButtons[] = {kControlPagePrevious, kControlPageNext};
    result = DivertControls(pageButtons, 2, stopEvent);
    if (FAILED(result))
    {
        return result;
    }
    _connected = true;
    _restored = false;
    return S_OK;
}

HRESULT DeviceSession::ConnectDialpad(HANDLE stopEvent, uint32_t buttonMask) noexcept
{
    if (_portCount == 0 || !PortForOutput(kReportLong))
    {
        return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    }
    _connected = false;
    _role = DeviceRole::Dialpad;
    _features = DeviceFeatures{};
    _controls = ControlState{};
    _divertedDialButtons = 0;
    HRESULT result = ResolveFeature(kFeatureReprogControls, stopEvent, _features.reprogControls);
    if (FAILED(result))
    {
        return result;
    }
    // Diverted, a button arrives as a 0x1B04 event instead of its mouse button or keyboard usage; only bound
    // buttons are taken so the others keep working as they do without RedXe.
    std::array<uint16_t, kDialpadButtonCount> controls{};
    uint32_t count = 0;
    for (uint32_t button = 0; button < kDialpadButtonCount; ++button)
    {
        if ((buttonMask & (1U << button)) != 0)
        {
            controls[count++] = kDialpadControls[button];
        }
    }
    result = DivertControls(controls.data(), count, stopEvent);
    if (FAILED(result))
    {
        return result;
    }
    _divertedDialButtons = buttonMask & ((1U << kDialpadButtonCount) - 1U);
    _connected = true;
    _restored = false;
    return S_OK;
}

HRESULT DeviceSession::DivertControls(const uint16_t* controls, uint32_t count, HANDLE stopEvent) noexcept
{
    if (!controls || count > kMaximumSavedControls)
    {
        return E_INVALIDARG;
    }
    _savedControlCount = 0;
    std::array<uint8_t, kInputBufferBytes> buffer{};
    for (uint32_t index = 0; index < count; ++index)
    {
        const uint16_t control = controls[index];
        uint8_t params[2] = {static_cast<uint8_t>(control >> 8U), static_cast<uint8_t>(control & 0xFFU)};
        HidppFrame response{};
        HRESULT result = SendCommand(_features.reprogControls, 2, params, sizeof(params), stopEvent, response,
                                     buffer.data(), static_cast<uint32_t>(buffer.size()));
        if (FAILED(result))
        {
            return result;
        }
        CidReporting reporting{};
        if (!ParseCidReporting(response, reporting))
        {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        reporting.controlId = control;
        _savedControls[_savedControlCount++] = reporting;
        uint8_t divert[6] = {static_cast<uint8_t>(control >> 8U),
                             static_cast<uint8_t>(control & 0xFFU),
                             static_cast<uint8_t>(reporting.flags | kCidReportingDivert),
                             static_cast<uint8_t>(reporting.remap >> 8U),
                             static_cast<uint8_t>(reporting.remap & 0xFFU),
                             reporting.flags2};
        result = SendCommand(_features.reprogControls, 3, divert, sizeof(divert), stopEvent, response, buffer.data(),
                             static_cast<uint32_t>(buffer.size()));
        if (FAILED(result))
        {
            return result;
        }
    }
    return S_OK;
}

HRESULT DeviceSession::Restore(HANDLE stopEvent, bool resetToLogo) noexcept
{
    if (_restored || _portCount == 0 || Disconnected())
    {
        _restored = true;
        return S_OK;
    }
    HRESULT first = S_OK;
    std::array<uint8_t, kInputBufferBytes> buffer{};
    for (uint32_t index = 0; index < _savedControlCount; ++index)
    {
        const CidReporting& saved = _savedControls[index];
        uint8_t params[6] = {
            static_cast<uint8_t>(saved.controlId >> 8U), static_cast<uint8_t>(saved.controlId & 0xFFU), saved.flags,
            static_cast<uint8_t>(saved.remap >> 8U),     static_cast<uint8_t>(saved.remap & 0xFFU),     saved.flags2};
        HidppFrame response{};
        const HRESULT result = SendCommand(_features.reprogControls, 3, params, sizeof(params), stopEvent, response,
                                           buffer.data(), static_cast<uint32_t>(buffer.size()));
        if (FAILED(result) && SUCCEEDED(first))
        {
            first = result;
        }
    }
    if (resetToLogo)
    {
        HidPort* port = PortForFeature(0x03);
        if (port)
        {
            std::array<uint8_t, kFeatureResetReportBytes> reset{};
            (void)BuildResetToLogoFeatureReport(reset.data(), static_cast<uint32_t>(reset.size()));
            const HRESULT result = port->SetFeature(reset.data(), static_cast<uint32_t>(reset.size()));
            if (FAILED(result) && SUCCEEDED(first))
            {
                first = result;
            }
        }
    }
    _restored = true;
    _connected = false;
    return first;
}

HRESULT DeviceSession::SetBrightness(uint32_t percent, HANDLE stopEvent) noexcept
{
    if (!_connected)
    {
        return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    }
    if (_features.brightness == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    std::array<uint8_t, kLongReportBytes> report{};
    if (BuildSetBrightnessReport(kDeviceIndexWired, _features.brightness, percent, report.data(),
                                 static_cast<uint32_t>(report.size())) == 0)
    {
        return E_INVALIDARG;
    }
    // Fire-and-forget like the reference: the ack arrives with the next pump and is ignored as a response.
    return WriteReport(report.data(), static_cast<uint32_t>(report.size()), stopEvent);
}

HRESULT DeviceSession::WriteImage(const ImageRegion& region, const uint8_t* jpeg, uint32_t bytes, bool deferUpdate,
                                  HANDLE stopEvent) noexcept
{
    if (!_connected)
    {
        return HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
    }
    VlpImageStream stream{};
    if (!BeginVlpImageStream(_features.display, region, deferUpdate, jpeg, bytes, stream))
    {
        return E_INVALIDARG;
    }
    while (NextVlpPacket(stream, _packet.data(), static_cast<uint32_t>(_packet.size())))
    {
        if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
        {
            return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        }
        const HRESULT result = WriteReport(_packet.data(), static_cast<uint32_t>(_packet.size()), stopEvent);
        if (FAILED(result))
        {
            return result;
        }
        ++_counters.imagePackets;
    }
    ++_counters.imagesWritten;
    return S_OK;
}

uint32_t DeviceSession::ReadEvents(HANDLE* handles, uint32_t capacity) const noexcept
{
    if (!handles)
    {
        return 0;
    }
    uint32_t count = 0;
    for (uint32_t index = 0; index < _portCount && count < capacity; ++index)
    {
        const HANDLE event = _ports[index]->ReadEvent();
        if (event)
        {
            handles[count++] = event;
        }
    }
    return count;
}

const ControlState& DeviceSession::Controls() const noexcept
{
    return _controls;
}

const DeviceFeatures& DeviceSession::Features() const noexcept
{
    return _features;
}

const DeviceCounters& DeviceSession::Counters() const noexcept
{
    return _counters;
}

uint32_t DeviceSession::PortCount() const noexcept
{
    return _portCount;
}

const HidCollectionInfo* DeviceSession::PortInfo(uint32_t index) const noexcept
{
    return index < _portCount ? &_ports[index]->Info() : nullptr;
}
} // namespace Logicon
