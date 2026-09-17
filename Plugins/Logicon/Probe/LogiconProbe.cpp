// LogiconProbe: the Phase 0 discovery tool for the MX Creative Console. Not shipped, not a plugin. It lists every
// Logitech HID collection, resolves HID++ features on one device, dumps its feature set, and prints every input
// report while you turn the dial, press keys, or move the roller.
//
//   LogiconProbe list                      every present 046D collection
//   LogiconProbe watch <pid> [seconds]     resolve features on that PID and echo its input reports (default 60 s)
//   LogiconProbe divert <pid> [seconds]    same, after diverting every 0x1B04 control (restored on exit)
//   LogiconProbe raw <pid> [seconds]       echo the PID's mouse-collection packets through Raw Input (wheels)
//   LogiconProbe send <pid> <feature-hex> <fn> [param-hex ...] [-- seconds]
//                                          send one HID++ command to a feature id, print the answer, then echo
//                                          input reports (default 10 s); for exploring 0x4610 on the dialpad

#include "../LogiconHid.h"
#include "../LogiconProtocol.h"
#include "../LogiconRawInput.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>
#include <new>
#include <vector>
#include <windows.h>

namespace
{
using namespace Logicon;

constexpr uint32_t kProbeCollections = 64;

void PrintCollection(uint32_t index, const HidCollectionInfo& info) noexcept
{
    std::wprintf(L"[%2u] pid %04X page 0x%04X usage 0x%04X in %4u out %4u feature %3u in-ids 0x%08X out-ids 0x%08X "
                 L"feature-ids 0x%08X\n     %s\n",
                 index, info.productId, info.usagePage, info.usage, info.inputReportBytes, info.outputReportBytes,
                 info.featureReportBytes, info.inputReportMask, info.outputReportMask, info.featureReportMask,
                 info.path.data());
}

int List() noexcept
{
    std::vector<HidCollectionInfo> items(kProbeCollections);
    uint32_t count = 0;
    const HRESULT result = EnumerateVendorCollections(kVendorId, 0, 0, items.data(), kProbeCollections, count);
    if (FAILED(result))
    {
        std::wprintf(L"enumeration failed: 0x%08X\n", static_cast<unsigned>(result));
        return 1;
    }
    std::wprintf(L"%u Logitech collection(s)\n", count);
    for (uint32_t index = 0; index < count; ++index)
    {
        PrintCollection(index, items[index]);
    }
    return 0;
}

void PrintReport(const wchar_t* prefix, const uint8_t* report, uint32_t bytes) noexcept
{
    std::wprintf(L"%s %3u:", prefix, bytes);
    for (uint32_t index = 0; index < bytes && index < 40; ++index)
    {
        std::wprintf(L" %02X", report[index]);
    }
    std::wprintf(L"%s\n", bytes > 40 ? L" ..." : L"");
}

struct Device final
{
    std::vector<std::unique_ptr<WindowsHidPort>> ports;
    uint8_t deviceIndex = kDeviceIndexWired;

    [[nodiscard]] WindowsHidPort* PortForOutput(uint8_t reportId) noexcept
    {
        for (auto& port : ports)
        {
            if (port->Info().SupportsOutput(reportId))
            {
                return port.get();
            }
        }
        for (auto& port : ports)
        {
            if (port->Info().outputReportBytes >= kLongReportBytes)
            {
                return port.get();
            }
        }
        return nullptr;
    }

    // Sends one long command and waits up to timeoutMs for the answer; other reports are printed as they pass.
    [[nodiscard]] HRESULT Command(uint8_t featureIndex, uint8_t function, const uint8_t* params, uint32_t paramBytes,
                                  uint8_t* answer, uint32_t answerCapacity, uint32_t& answerBytes,
                                  uint32_t timeoutMs = 1500) noexcept
    {
        answerBytes = 0;
        std::array<uint8_t, kLongReportBytes> report{};
        if (BuildLongReport(deviceIndex, featureIndex, function, params, paramBytes, report.data(),
                            static_cast<uint32_t>(report.size())) == 0)
        {
            return E_INVALIDARG;
        }
        WindowsHidPort* port = PortForOutput(kReportLong);
        if (!port)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        HRESULT result = port->Write(report.data(), static_cast<uint32_t>(report.size()), nullptr, 1000);
        if (FAILED(result))
        {
            return result;
        }
        const uint64_t deadline = GetTickCount64() + timeoutMs;
        std::array<uint8_t, kMaximumHidReportBytes> buffer{};
        for (;;)
        {
            for (auto& candidate : ports)
            {
                for (int drained = 0; drained < 32; ++drained)
                {
                    uint32_t bytes = 0;
                    result = candidate->TakeReport(buffer.data(), static_cast<uint32_t>(buffer.size()), bytes);
                    if (result != S_OK)
                    {
                        break;
                    }
                    HidppFrame frame{};
                    if (ParseHidppFrame(buffer.data(), bytes, frame) && FrameAnswers(frame, featureIndex, function))
                    {
                        answerBytes = bytes < answerCapacity ? bytes : answerCapacity;
                        std::memcpy(answer, buffer.data(), answerBytes);
                        return frame.error ? HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION) : S_OK;
                    }
                    PrintReport(L"  (in)", buffer.data(), bytes);
                }
            }
            const uint64_t now = GetTickCount64();
            if (now >= deadline)
            {
                return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            }
            std::array<HANDLE, 16> handles{};
            uint32_t handleCount = 0;
            for (auto& candidate : ports)
            {
                if (candidate->ReadEvent() && handleCount < handles.size())
                {
                    handles[handleCount++] = candidate->ReadEvent();
                }
            }
            (void)WaitForMultipleObjects(handleCount, handles.data(), FALSE, static_cast<DWORD>(deadline - now));
        }
    }
};

[[nodiscard]] bool OpenDevice(uint16_t productId, Device& device) noexcept
{
    std::vector<HidCollectionInfo> items(kProbeCollections);
    uint32_t count = 0;
    if (FAILED(EnumerateVendorCollections(kVendorId, productId, 0, items.data(), kProbeCollections, count)))
    {
        return false;
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        PrintCollection(index, items[index]);
        std::unique_ptr<WindowsHidPort> port{new (std::nothrow) WindowsHidPort()};
        if (!port)
        {
            return false;
        }
        const HRESULT opened = port->Open(items[index]);
        if (FAILED(opened))
        {
            std::wprintf(L"     open failed 0x%08X (skipped)\n", static_cast<unsigned>(opened));
            continue;
        }
        device.ports.push_back(std::move(port));
    }
    return !device.ports.empty();
}

void DumpFeatures(Device& device, std::array<uint8_t, 256>& featureIdsByIndex) noexcept
{
    std::array<uint8_t, kMaximumHidReportBytes> answer{};
    uint32_t answerBytes = 0;
    const uint16_t wanted[] = {kFeatureSet,
                               kFeatureContextualDisplay,
                               kFeatureReprogControls,
                               kFeatureBrightness,
                               0x2121,
                               0x2150,
                               0x2110,
                               0x1E00,
                               0x0005,
                               0x0003,
                               0x1D4B,
                               0x1B04,
                               0x8100,
                               0x1DF3,
                               0x1F20,
                               0x9001,
                               0x8060,
                               0x1982};
    for (const uint16_t featureId : wanted)
    {
        uint8_t params[3] = {static_cast<uint8_t>(featureId >> 8U), static_cast<uint8_t>(featureId & 0xFFU), 0};
        const HRESULT result = device.Command(0x00, 0, params, sizeof(params), answer.data(),
                                              static_cast<uint32_t>(answer.size()), answerBytes);
        if (SUCCEEDED(result) && answerBytes >= 7)
        {
            std::wprintf(L"root getFeature(0x%04X) -> index 0x%02X type 0x%02X version %u\n", featureId, answer[4],
                         answer[5], answer[6]);
        }
        else
        {
            std::wprintf(L"root getFeature(0x%04X) -> 0x%08X\n", featureId, static_cast<unsigned>(result));
        }
    }
    // Walk the feature set.
    uint8_t setParams[3] = {0x00, 0x01, 0x00};
    if (SUCCEEDED(device.Command(0x00, 0, setParams, sizeof(setParams), answer.data(),
                                 static_cast<uint32_t>(answer.size()), answerBytes)) &&
        answerBytes >= 5 && answer[4] != 0)
    {
        const uint8_t setIndex = answer[4];
        if (SUCCEEDED(device.Command(setIndex, 0, nullptr, 0, answer.data(), static_cast<uint32_t>(answer.size()),
                                     answerBytes)) &&
            answerBytes >= 5)
        {
            const uint32_t total = answer[4];
            std::wprintf(L"feature set: %u feature(s)\n", total);
            for (uint32_t index = 1; index <= total && index < 256; ++index)
            {
                uint8_t query[1] = {static_cast<uint8_t>(index)};
                if (SUCCEEDED(device.Command(setIndex, 1, query, 1, answer.data(), static_cast<uint32_t>(answer.size()),
                                             answerBytes)) &&
                    answerBytes >= 7)
                {
                    std::wprintf(L"  [0x%02X] feature 0x%02X%02X type 0x%02X version %u\n", index, answer[4], answer[5],
                                 answer[6], answerBytes >= 8 ? answer[7] : 0);
                    featureIdsByIndex[index] = answer[4] == 0x1B && answer[5] == 0x04 ? 1 : 0;
                }
            }
        }
    }
}

struct DivertedControl final
{
    uint16_t cid = 0;
    uint8_t flags = 0;
    uint16_t remap = 0;
    uint8_t flags2 = 0;
};

void DivertAll(Device& device, uint8_t reprogIndex, std::vector<DivertedControl>& saved) noexcept
{
    std::array<uint8_t, kMaximumHidReportBytes> answer{};
    uint32_t answerBytes = 0;
    if (FAILED(device.Command(reprogIndex, 0, nullptr, 0, answer.data(), static_cast<uint32_t>(answer.size()),
                              answerBytes)) ||
        answerBytes < 5)
    {
        std::wprintf(L"0x1B04 getCount failed\n");
        return;
    }
    const uint32_t count = answer[4];
    std::wprintf(L"0x1B04: %u control(s)\n", count);
    for (uint32_t index = 0; index < count; ++index)
    {
        uint8_t query[1] = {static_cast<uint8_t>(index)};
        if (FAILED(device.Command(reprogIndex, 1, query, 1, answer.data(), static_cast<uint32_t>(answer.size()),
                                  answerBytes)) ||
            answerBytes < 12)
        {
            continue;
        }
        const uint16_t cid = static_cast<uint16_t>((answer[4] << 8U) | answer[5]);
        const uint16_t task = static_cast<uint16_t>((answer[6] << 8U) | answer[7]);
        std::wprintf(L"  control %u: cid 0x%04X task 0x%04X flags 0x%02X pos %u group 0x%02X gmask 0x%02X "
                     L"flags2 0x%02X\n",
                     index, cid, task, answer[8], answer[9], answer[10], answer[11], answerBytes > 12 ? answer[12] : 0);
        uint8_t cidParams[2] = {answer[4], answer[5]};
        if (FAILED(device.Command(reprogIndex, 2, cidParams, 2, answer.data(), static_cast<uint32_t>(answer.size()),
                                  answerBytes)) ||
            answerBytes < 10)
        {
            continue;
        }
        DivertedControl control{};
        control.cid = cid;
        control.flags = answer[6];
        control.remap = static_cast<uint16_t>((answer[7] << 8U) | answer[8]);
        control.flags2 = answer[9];
        saved.push_back(control);
        uint8_t divert[6] = {answer[4], answer[5], static_cast<uint8_t>(control.flags | 0x03),
                             answer[7], answer[8], answer[9]};
        const HRESULT result = device.Command(reprogIndex, 3, divert, sizeof(divert), answer.data(),
                                              static_cast<uint32_t>(answer.size()), answerBytes);
        std::wprintf(L"    divert -> 0x%08X\n", static_cast<unsigned>(result));
    }
}

void RestoreAll(Device& device, uint8_t reprogIndex, const std::vector<DivertedControl>& saved) noexcept
{
    std::array<uint8_t, kMaximumHidReportBytes> answer{};
    uint32_t answerBytes = 0;
    for (const DivertedControl& control : saved)
    {
        uint8_t params[6] = {
            static_cast<uint8_t>(control.cid >> 8U),   static_cast<uint8_t>(control.cid & 0xFFU),   control.flags,
            static_cast<uint8_t>(control.remap >> 8U), static_cast<uint8_t>(control.remap & 0xFFU), control.flags2};
        (void)device.Command(reprogIndex, 3, params, sizeof(params), answer.data(),
                             static_cast<uint32_t>(answer.size()), answerBytes);
    }
}

int Watch(uint16_t productId, uint32_t seconds, bool divert) noexcept
{
    Device device;
    if (!OpenDevice(productId, device))
    {
        std::wprintf(L"no openable collection for pid %04X\n", productId);
        return 1;
    }
    std::wprintf(L"%zu port(s) open\n", device.ports.size());
    std::array<uint8_t, 256> featureIdsByIndex{};
    DumpFeatures(device, featureIdsByIndex);

    std::vector<DivertedControl> saved;
    uint8_t reprogIndex = 0;
    {
        std::array<uint8_t, kMaximumHidReportBytes> answer{};
        uint32_t answerBytes = 0;
        uint8_t params[3] = {0x1B, 0x04, 0x00};
        if (SUCCEEDED(device.Command(0x00, 0, params, sizeof(params), answer.data(),
                                     static_cast<uint32_t>(answer.size()), answerBytes)) &&
            answerBytes >= 5)
        {
            reprogIndex = answer[4];
        }
    }
    if (divert && reprogIndex != 0)
    {
        DivertAll(device, reprogIndex, saved);
    }

    // 0x4610 looks like the dial feature (crown-style). Read its info/mode, then ask for diverted rotation events
    // the way the 0x4600 crown does (setMode bit 0 = divert); restored on exit.
    uint8_t dialIndex = 0;
    uint8_t dialMode = 0;
    bool dialModeKnown = false;
    {
        std::array<uint8_t, kMaximumHidReportBytes> answer{};
        uint32_t answerBytes = 0;
        uint8_t params[3] = {0x46, 0x10, 0x00};
        if (SUCCEEDED(device.Command(0x00, 0, params, sizeof(params), answer.data(),
                                     static_cast<uint32_t>(answer.size()), answerBytes)) &&
            answerBytes >= 5)
        {
            dialIndex = answer[4];
        }
        if (dialIndex != 0)
        {
            for (uint8_t function = 0; function < 4; ++function)
            {
                const HRESULT result = device.Command(dialIndex, function, nullptr, 0, answer.data(),
                                                      static_cast<uint32_t>(answer.size()), answerBytes);
                std::wprintf(L"0x4610 fn %u -> 0x%08X", function, static_cast<unsigned>(result));
                if (SUCCEEDED(result))
                {
                    PrintReport(L"", answer.data(), answerBytes);
                    if (function == 1 && answerBytes >= 5)
                    {
                        dialMode = answer[4];
                        dialModeKnown = true;
                    }
                }
                else
                {
                    std::wprintf(L"\n");
                }
            }
            if (divert)
            {
                uint8_t modeParams[1] = {static_cast<uint8_t>(dialMode | 0x01)};
                const HRESULT result = device.Command(dialIndex, 2, modeParams, sizeof(modeParams), answer.data(),
                                                      static_cast<uint32_t>(answer.size()), answerBytes);
                std::wprintf(L"0x4610 setMode(0x%02X) -> 0x%08X\n", modeParams[0], static_cast<unsigned>(result));
            }
        }
    }

    std::wprintf(L"listening for %u s: turn the dial, the roller, and press every button...\n", seconds);
    const uint64_t deadline = GetTickCount64() + static_cast<uint64_t>(seconds) * 1000ULL;
    std::array<uint8_t, kMaximumHidReportBytes> buffer{};
    uint32_t total = 0;
    for (;;)
    {
        for (auto& port : device.ports)
        {
            for (int drained = 0; drained < 32; ++drained)
            {
                uint32_t bytes = 0;
                const HRESULT result = port->TakeReport(buffer.data(), static_cast<uint32_t>(buffer.size()), bytes);
                if (result != S_OK)
                {
                    break;
                }
                ++total;
                wchar_t prefix[32]{};
                (void)swprintf_s(prefix, L"%6llu 0x%04X", GetTickCount64() % 1000000ULL, port->Info().usage);
                PrintReport(prefix, buffer.data(), bytes);
            }
        }
        const uint64_t now = GetTickCount64();
        if (now >= deadline)
        {
            break;
        }
        std::array<HANDLE, 16> handles{};
        uint32_t handleCount = 0;
        for (auto& port : device.ports)
        {
            if (port->ReadEvent() && handleCount < handles.size())
            {
                handles[handleCount++] = port->ReadEvent();
            }
        }
        (void)WaitForMultipleObjects(handleCount, handles.data(), FALSE,
                                     static_cast<DWORD>(deadline - now > 1000 ? 1000 : deadline - now));
    }
    std::wprintf(L"%u report(s)\n", total);
    if (divert && reprogIndex != 0)
    {
        RestoreAll(device, reprogIndex, saved);
        std::wprintf(L"controls restored\n");
    }
    if (divert && dialIndex != 0 && dialModeKnown)
    {
        std::array<uint8_t, kMaximumHidReportBytes> answer{};
        uint32_t answerBytes = 0;
        uint8_t modeParams[1] = {dialMode};
        (void)device.Command(dialIndex, 2, modeParams, sizeof(modeParams), answer.data(),
                             static_cast<uint32_t>(answer.size()), answerBytes);
        std::wprintf(L"dial mode restored\n");
    }
    return 0;
}
} // namespace

// Echoes the mouse-collection packets of one PID through Raw Input: which wheel moves, by how much, which buttons.
int Raw(uint16_t productId, uint32_t seconds) noexcept
{
    RawWheelListener listener;
    const HRESULT started = listener.Start(kVendorId, productId);
    if (FAILED(started))
    {
        std::wprintf(L"raw input registration failed: 0x%08X\n", static_cast<unsigned>(started));
        return 1;
    }
    std::wprintf(L"listening for %u s through raw input: turn the dial and the roller, press the buttons...\n",
                 seconds);
    const uint64_t deadline = GetTickCount64() + static_cast<uint64_t>(seconds) * 1000ULL;
    WheelState previous{};
    for (;;)
    {
        const uint64_t now = GetTickCount64();
        if (now >= deadline)
        {
            break;
        }
        (void)MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(deadline - now), QS_ALLINPUT,
                                          MWMO_INPUTAVAILABLE);
        if (!listener.Pump())
        {
            continue;
        }
        const WheelState& state = listener.State();
        const WheelDeltas deltas = listener.TakeDeltas();
        std::wprintf(L"%6llu hwheel %+d (sum %+d, %u ev)  wheel %+d (sum %+d, %u ev)  buttons 0x%02X  motion %u\n",
                     GetTickCount64() % 1000000ULL, deltas.dial, state.dialRaw, state.dialEvents, deltas.roller,
                     state.rollerRaw, state.rollerEvents, state.buttonMask, state.motionEvents - previous.motionEvents);
        previous = state;
    }
    const WheelState& state = listener.State();
    std::wprintf(L"%u matched packet(s), %u from other mice; hwheel sum %+d roller sum %+d\n", state.reports,
                 state.otherReports, state.dialRaw, state.rollerRaw);
    listener.Stop();
    return 0;
}

// Sends one command to a feature id (resolved through the root) and echoes the answer and later input reports.
int Send(uint16_t productId, uint16_t featureId, uint8_t function, const uint8_t* params, uint32_t paramBytes,
         uint32_t seconds) noexcept
{
    Device device;
    if (!OpenDevice(productId, device))
    {
        std::wprintf(L"no collection of PID %04X could be opened.\n", productId);
        return 1;
    }
    std::array<uint8_t, kMaximumHidReportBytes> answer{};
    uint32_t answerBytes = 0;
    uint8_t lookup[3] = {static_cast<uint8_t>(featureId >> 8U), static_cast<uint8_t>(featureId & 0xFFU), 0x00};
    HRESULT result = device.Command(0x00, 0, lookup, sizeof(lookup), answer.data(),
                                    static_cast<uint32_t>(answer.size()), answerBytes);
    const uint8_t index = SUCCEEDED(result) && answerBytes >= 5 ? answer[4] : 0;
    std::wprintf(L"root getFeature(0x%04X) -> index 0x%02X (hr 0x%08X)\n", featureId, index,
                 static_cast<unsigned>(result));
    if (index == 0)
    {
        return 1;
    }
    result = device.Command(index, function, params, paramBytes, answer.data(), static_cast<uint32_t>(answer.size()),
                            answerBytes);
    std::wprintf(L"0x%04X fn %u -> 0x%08X", featureId, function, static_cast<unsigned>(result));
    if (answerBytes != 0)
    {
        PrintReport(L"", answer.data(), answerBytes);
    }
    else
    {
        std::wprintf(L"\n");
    }
    std::wprintf(L"listening for %u s...\n", seconds);
    const uint64_t deadline = GetTickCount64() + static_cast<uint64_t>(seconds) * 1000ULL;
    std::array<uint8_t, kMaximumHidReportBytes> buffer{};
    for (;;)
    {
        for (auto& port : device.ports)
        {
            for (int drained = 0; drained < 32; ++drained)
            {
                uint32_t bytes = 0;
                if (port->TakeReport(buffer.data(), static_cast<uint32_t>(buffer.size()), bytes) != S_OK)
                {
                    break;
                }
                wchar_t prefix[32]{};
                (void)swprintf_s(prefix, L"%6llu 0x%04X", GetTickCount64() % 1000000ULL, port->Info().usage);
                PrintReport(prefix, buffer.data(), bytes);
            }
        }
        const uint64_t now = GetTickCount64();
        if (now >= deadline)
        {
            break;
        }
        std::array<HANDLE, 16> handles{};
        uint32_t handleCount = 0;
        for (auto& port : device.ports)
        {
            if (handleCount < handles.size() && port->ReadEvent())
            {
                handles[handleCount++] = port->ReadEvent();
            }
        }
        (void)WaitForMultipleObjects(handleCount, handles.data(), FALSE, static_cast<DWORD>(deadline - now));
    }
    return 0;
}

int wmain(int argumentCount, wchar_t** arguments) noexcept
{
    (void)setvbuf(stdout, nullptr, _IONBF, 0);
    if (argumentCount >= 2 && std::wcscmp(arguments[1], L"list") == 0)
    {
        return List();
    }
    if (argumentCount >= 3 && (std::wcscmp(arguments[1], L"watch") == 0 || std::wcscmp(arguments[1], L"divert") == 0))
    {
        const uint16_t productId = static_cast<uint16_t>(std::wcstoul(arguments[2], nullptr, 16));
        const uint32_t seconds =
            argumentCount >= 4 ? static_cast<uint32_t>(std::wcstoul(arguments[3], nullptr, 10)) : 60;
        return Watch(productId, seconds == 0 ? 60 : seconds, std::wcscmp(arguments[1], L"divert") == 0);
    }
    if (argumentCount >= 3 && std::wcscmp(arguments[1], L"raw") == 0)
    {
        const uint16_t productId = static_cast<uint16_t>(std::wcstoul(arguments[2], nullptr, 16));
        const uint32_t seconds =
            argumentCount >= 4 ? static_cast<uint32_t>(std::wcstoul(arguments[3], nullptr, 10)) : 60;
        return Raw(productId, seconds == 0 ? 60 : seconds);
    }
    if (argumentCount >= 5 && std::wcscmp(arguments[1], L"send") == 0)
    {
        const uint16_t productId = static_cast<uint16_t>(std::wcstoul(arguments[2], nullptr, 16));
        const uint16_t featureId = static_cast<uint16_t>(std::wcstoul(arguments[3], nullptr, 16));
        const uint8_t function = static_cast<uint8_t>(std::wcstoul(arguments[4], nullptr, 10));
        std::array<uint8_t, 16> params{};
        uint32_t paramBytes = 0;
        uint32_t seconds = 10;
        for (int argument = 5; argument < argumentCount; ++argument)
        {
            if (std::wcscmp(arguments[argument], L"--") == 0)
            {
                seconds = argument + 1 < argumentCount
                              ? static_cast<uint32_t>(std::wcstoul(arguments[argument + 1], nullptr, 10))
                              : seconds;
                break;
            }
            if (paramBytes < params.size())
            {
                params[paramBytes++] = static_cast<uint8_t>(std::wcstoul(arguments[argument], nullptr, 16));
            }
        }
        return Send(productId, featureId, function, params.data(), paramBytes, seconds);
    }
    std::wprintf(L"usage: LogiconProbe list | watch <pid-hex> [seconds] | divert <pid-hex> [seconds] | "
                 L"raw <pid-hex> [seconds] | send <pid-hex> <feature-hex> <fn> [param-hex ...] [-- seconds]\n");
    return 2;
}
