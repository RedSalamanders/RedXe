// Logicon tests that need no hardware: HID++ framing and the 0x19A1 image stream against the reference byte layout,
// the shared settings model, key-face composition and JPEG round trips, the device session driven by the synthetic
// keypad, and the shipped DLL's factory, contract, service lifetime, device lane, and test exports.

#include "Actions/ActionTargets.h"
#include "LogiconDevice.h"
#include "LogiconFaces.h"
#include "LogiconProtocol.h"
#include "LogiconRawInput.h"
#include "LogiconSettings.h"
#include "LogiconSynthetic.h"
#include "LogiconSystemData.h"
#include "LogiconTestContract.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Service.h"
#include "PlugInterfaces/Widget.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <wincodec.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr HRESULT kTestFailure = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

#define LOGICON_CHECK(condition, message)                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::wprintf(L"FAIL %S (%S:%d)\n", message, __FILE__, __LINE__);                                           \
            return kTestFailure;                                                                                       \
        }                                                                                                              \
    } while (false)

template <typename Function> [[nodiscard]] Function Resolve(HMODULE module, const char* name) noexcept
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

[[nodiscard]] HRESULT BuildSiblingPath(const wchar_t* relativePath, std::array<wchar_t, 1024>& path) noexcept
{
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return HRESULT_FROM_WIN32(length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);
    }
    wchar_t* separator = std::wcsrchr(path.data(), L'\\');
    if (!separator)
    {
        return E_UNEXPECTED;
    }
    ++separator;
    const size_t prefix = static_cast<size_t>(separator - path.data());
    const size_t suffix = std::wcslen(relativePath);
    if (prefix + suffix + 1 > path.size())
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    std::memcpy(separator, relativePath, (suffix + 1) * sizeof(wchar_t));
    return S_OK;
}

// ---------------------------------------------------------------------------------------------------------------
// Protocol
// ---------------------------------------------------------------------------------------------------------------

[[nodiscard]] HRESULT TestProtocolFraming() noexcept
{
    using namespace Logicon;
    std::array<uint8_t, 64> report{};
    LOGICON_CHECK(BuildGetFeatureReport(kDeviceIndexWired, kFeatureContextualDisplay, report.data(),
                                        static_cast<uint32_t>(report.size())) == kLongReportBytes,
                  "getFeature report length");
    const uint8_t expectedGetFeature[] = {0x11, 0xFF, 0x00, 0x0B, 0x19, 0xA1, 0x00};
    LOGICON_CHECK(std::memcmp(report.data(), expectedGetFeature, sizeof(expectedGetFeature)) == 0,
                  "getFeature(0x19A1) bytes");
    for (uint32_t index = 7; index < kLongReportBytes; ++index)
    {
        LOGICON_CHECK(report[index] == 0, "long report zero padding");
    }

    LOGICON_CHECK(BuildSetCidReportingReport(kDeviceIndexWired, 0x0B, kControlPagePrevious, 0x03, 0, 0, report.data(),
                                             static_cast<uint32_t>(report.size())) == kLongReportBytes,
                  "setCidReporting length");
    const uint8_t expectedDivert[] = {0x11, 0xFF, 0x0B, 0x3B, 0x01, 0xA1, 0x03, 0x00, 0x00, 0x00};
    LOGICON_CHECK(std::memcmp(report.data(), expectedDivert, sizeof(expectedDivert)) == 0,
                  "setCidReporting matches the reference init write");

    LOGICON_CHECK(BuildSetBrightnessReport(kDeviceIndexWired, 0x0F, 70, report.data(),
                                           static_cast<uint32_t>(report.size())) == kLongReportBytes,
                  "brightness length");
    const uint8_t expectedBrightness[] = {0x11, 0xFF, 0x0F, 0x2B, 0x00, 0x46};
    LOGICON_CHECK(std::memcmp(report.data(), expectedBrightness, sizeof(expectedBrightness)) == 0,
                  "brightness matches the reference write");
    LOGICON_CHECK(BuildSetBrightnessReport(kDeviceIndexWired, 0x0F, 0, report.data(),
                                           static_cast<uint32_t>(report.size())) == kLongReportBytes &&
                      report[5] == 1,
                  "brightness 0 clamps to 1 (0 resets the device)");
    LOGICON_CHECK(BuildLongReport(kDeviceIndexWired, 0x02, 0x10, nullptr, 0, report.data(),
                                  static_cast<uint32_t>(report.size())) == 0,
                  "function above 15 is rejected");

    // Responses, errors, and acks.
    const uint8_t response[] = {0x11, 0xFF, 0x00, 0x0B, 0x02, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    HidppFrame frame{};
    LOGICON_CHECK(ParseHidppFrame(response, sizeof(response), frame) && !frame.error && frame.featureIndex == 0 &&
                      frame.function == 0 && frame.softwareId == kSoftwareId && frame.params[0] == 0x02,
                  "root response parses");
    LOGICON_CHECK(FrameAnswers(frame, 0x00, 0), "response answers getFeature");
    const uint8_t error[] = {0x11, 0xFF, 0xFF, 0x0B, 0x3B, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    LOGICON_CHECK(ParseHidppFrame(error, sizeof(error), frame) && frame.error && frame.featureIndex == 0x0B &&
                      frame.function == 3 && frame.errorCode == 5,
                  "error frame parses");
    LOGICON_CHECK(FrameAnswers(frame, 0x0B, 3), "error answers the failing command");
    LOGICON_CHECK(!FrameAnswers(frame, 0x02, 3), "error does not answer another feature");

    // 0x19A1 key event: keys 1, 5 and 9 held.
    uint8_t keys[kVlpControlReportBytes]{};
    keys[0] = kReportVlpControl;
    keys[1] = 0xFF;
    keys[2] = 0x02;
    keys[3] = 0x00;
    keys[5] = 0x01;
    keys[6] = 1;
    keys[7] = 5;
    keys[8] = 9;
    InputEvent event{};
    LOGICON_CHECK(ParseInputEvent(keys, sizeof(keys), 0x02, 0x0B, event) && event.kind == InputKind::DisplayKeys &&
                      event.keyCount == 3 && event.mask == ((1U << 0) | (1U << 4) | (1U << 8)),
                  "display key event parses");
    keys[3] = 0x2B; // an ack for a set-image command, not an event
    LOGICON_CHECK(!ParseInputEvent(keys, sizeof(keys), 0x02, 0x0B, event), "display ack is not an input event");
    keys[3] = 0x00;
    LOGICON_CHECK(!ParseInputEvent(keys, sizeof(keys), 0x07, 0x0B, event), "wrong feature index is ignored");

    // 0x1B04 diverted buttons: next page held.
    uint8_t buttons[kLongReportBytes]{};
    buttons[0] = kReportLong;
    buttons[1] = 0xFF;
    buttons[2] = 0x0B;
    buttons[3] = 0x00;
    buttons[4] = 0x01;
    buttons[5] = 0xA2;
    LOGICON_CHECK(ParseInputEvent(buttons, sizeof(buttons), 0x02, 0x0B, event) &&
                      event.kind == InputKind::DivertedButtons && event.controlCount == 1 &&
                      event.controls[0] == kControlPageNext && event.mask == 2U,
                  "diverted page button parses");
    buttons[4] = 0;
    buttons[5] = 0;
    LOGICON_CHECK(ParseInputEvent(buttons, sizeof(buttons), 0x02, 0x0B, event) && event.controlCount == 0 &&
                      event.mask == 0,
                  "release parses as an empty list");

    // 0x1B04 getCidReporting response.
    const uint8_t reportingResponse[] = {0x11, 0xFF, 0x0B, 0x2B, 0x01, 0xA1, 0x00, 0x00, 0x00, 0x00,
                                         0,    0,    0,    0,    0,    0,    0,    0,    0,    0};
    CidReporting reporting{};
    LOGICON_CHECK(ParseHidppFrame(reportingResponse, sizeof(reportingResponse), frame) &&
                      ParseCidReporting(frame, reporting) && reporting.controlId == kControlPagePrevious &&
                      reporting.flags == 0,
                  "cid reporting parses");
    return S_OK;
}

[[nodiscard]] HRESULT TestGeometryAndVlp() noexcept
{
    using namespace Logicon;
    const ImageRegion keyFirst = KeyRegion(0);
    const ImageRegion center = KeyRegion(4);
    const ImageRegion keyLast = KeyRegion(8);
    LOGICON_CHECK(keyFirst.x == 23 && keyFirst.y == 6 && keyFirst.width == 118 && keyFirst.height == 118,
                  "key 0 region");
    LOGICON_CHECK(center.x == 181 && center.y == 164, "key 4 region");
    LOGICON_CHECK(keyLast.x == 339 && keyLast.y == 322 && keyLast.x + keyLast.width == 457 &&
                      keyLast.y + keyLast.height == 440,
                  "key 8 region");
    const ImageRegion grid = GridRegion();
    LOGICON_CHECK(grid.x == 23 && grid.y == 6 && grid.width == 434 && grid.height == 434, "grid region");

    LOGICON_CHECK(VlpPacketCount(0) == 0, "no packets for an empty image");
    LOGICON_CHECK(VlpPacketCount(1) == 1, "one packet for one byte");
    LOGICON_CHECK(VlpPacketCount(4075) == 1, "4075 bytes fill the first packet");
    LOGICON_CHECK(VlpPacketCount(4076) == 2, "4076 bytes need a continuation");
    LOGICON_CHECK(VlpPacketCount(20000) == 5, "20000 bytes need five packets");

    LOGICON_CHECK(VlpSequenceByte(1, true, false) == 0xA1, "first sequence byte");
    LOGICON_CHECK(VlpSequenceByte(2, false, false) == 0x22, "middle sequence byte");
    LOGICON_CHECK(VlpSequenceByte(3, false, true) == 0x63, "last sequence byte");
    LOGICON_CHECK(VlpSequenceByte(1, true, true) == 0xE1, "single-packet sequence byte");

    // A 20000-byte pseudo image: byte n = n & 0xFF.
    std::unique_ptr<uint8_t[]> image{new (std::nothrow) uint8_t[20000]};
    LOGICON_CHECK(image != nullptr, "image allocation");
    for (uint32_t index = 0; index < 20000; ++index)
    {
        image[index] = static_cast<uint8_t>(index & 0xFFU);
    }
    VlpImageStream stream{};
    LOGICON_CHECK(BeginVlpImageStream(0x02, KeyRegion(0), false, image.get(), 20000, stream), "stream begins");
    std::array<uint8_t, kVlpImageReportBytes> packet{};
    uint32_t packets = 0;
    uint32_t consumed = 0;
    while (NextVlpPacket(stream, packet.data(), static_cast<uint32_t>(packet.size())))
    {
        ++packets;
        LOGICON_CHECK(packet[0] == 0x14 && packet[1] == 0xFF && packet[2] == 0x02 && packet[3] == 0x2B,
                      "packet prefix");
        const bool first = packets == 1;
        const bool last = packets == 5;
        LOGICON_CHECK(packet[4] == VlpSequenceByte(packets, first, last), "packet sequence byte");
        const uint32_t header = first ? kVlpFirstHeaderBytes : kVlpContinuationHeaderBytes;
        if (first)
        {
            const uint8_t expectedHeader[] = {0x14, 0xFF, 0x02, 0x2B, 0xA1, 0x01, 0x00, 0x01, 0x00, 0x00,
                                              0x17, 0x00, 0x06, 0x00, 0x76, 0x00, 0x76, 0x00, 0x4E, 0x20};
            LOGICON_CHECK(std::memcmp(packet.data(), expectedHeader, sizeof(expectedHeader)) == 0,
                          "first packet header: display 1, no defer, 1 JPEG, x 23, y 6, 118x118, 20000 bytes");
        }
        const uint32_t payload = std::min<uint32_t>(20000 - consumed, kVlpImageReportBytes - header);
        LOGICON_CHECK(std::memcmp(packet.data() + header, image.get() + consumed, payload) == 0, "packet payload");
        consumed += payload;
    }
    LOGICON_CHECK(packets == 5 && consumed == 20000, "five packets carry the whole image");
    LOGICON_CHECK(!NextVlpPacket(stream, packet.data(), static_cast<uint32_t>(packet.size())), "stream ends");

    // Deferred update and a region that leaves the panel.
    LOGICON_CHECK(BeginVlpImageStream(0x02, GridRegion(), true, image.get(), 10, stream) &&
                      NextVlpPacket(stream, packet.data(), static_cast<uint32_t>(packet.size())) && packet[6] == 1 &&
                      packet[4] == 0xE1,
                  "defer flag and single packet");
    ImageRegion outside{};
    outside.x = 400;
    outside.y = 0;
    outside.width = 118;
    outside.height = 118;
    LOGICON_CHECK(!BeginVlpImageStream(0x02, outside, false, image.get(), 10, stream),
                  "a region past the panel is rejected");
    return S_OK;
}

// ---------------------------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------------------------

[[nodiscard]] HRESULT TestSettings() noexcept
{
    using namespace Logicon;
    Settings settings{};
    std::array<char, 160> diagnostic{};
    LOGICON_CHECK(SUCCEEDED(ParseSettingsJson(kSettingsDefaults, settings, diagnostic.data(), diagnostic.size())),
                  "defaults parse");
    LOGICON_CHECK(settings.brightness == 70 && settings.restoreLogoOnExit && settings.keyCount == 0 &&
                      settings.pageButtons == PageButtons::KeyPages && settings.KeyPageCount() == 1,
                  "default values");

    constexpr std::string_view authored =
        R"json({"brightness":40,"restoreLogoOnExit":false,"pageButtons":"dashboardPages",
      "keys":[
        {"page":0,"slot":0,"action":"page.previous","label":"Prev","icon":"ChevronLeft"},
        {"page":0,"slot":1,"action":"page.next","label":"Next","icon":"ChevronRight","color":"#1E88E5"},
        {"page":1,"slot":4,"action":"widget.toggle","target":"System/2","label":"Pulse"},
        {"page":1,"slot":5,"action":"system.launch","target":"C:\\Tools\\Code.exe","icon":"png:C:\\Icons\\code.png"},
        {"page":3,"slot":8,"face":"clock"},
        {"slot":7,"action":"keys.media","target":"mute","label":"Mute","icon":"Mute"},
        {"slot":6,"action":"logicon.keyPage.next","label":"More"}
      ]})json";
    LOGICON_CHECK(SUCCEEDED(ParseSettingsJson(authored, settings, diagnostic.data(), diagnostic.size())),
                  "authored settings parse");
    LOGICON_CHECK(settings.brightness == 40 && !settings.restoreLogoOnExit &&
                      settings.pageButtons == PageButtons::DashboardPages && settings.keyCount == 7 &&
                      settings.KeyPageCount() == 4,
                  "authored scalar values and page count");
    const KeyBinding* next = settings.Find(0, 1);
    LOGICON_CHECK(next && next->Action() == "page.next" && next->hasColor && next->colorRgb == 0x1E88E5 &&
                      next->Label() == "Next" && next->Icon() == "ChevronRight" && next->valid && !next->IsLocal(),
                  "binding fields");
    const KeyBinding* toggle = settings.Find(1, 4);
    LOGICON_CHECK(toggle && toggle->Action() == "widget.toggle" && toggle->Target() == "System/2", "widget target");
    std::string_view pageId;
    uint32_t ordinal = 0;
    LOGICON_CHECK(ParseWidgetTarget(toggle->Target(), pageId, ordinal) && pageId == "System" && ordinal == 2,
                  "widget target parse");
    LOGICON_CHECK(ParseWidgetTarget("3", pageId, ordinal) && pageId.empty() && ordinal == 3, "bare ordinal");
    LOGICON_CHECK(!ParseWidgetTarget("/3", pageId, ordinal) && !ParseWidgetTarget("a/b", pageId, ordinal) &&
                      !ParseWidgetTarget("1000", pageId, ordinal),
                  "invalid widget targets");
    const KeyBinding* launch = settings.Find(1, 5);
    LOGICON_CHECK(launch && launch->Action() == "system.launch" && launch->Icon().starts_with("png:"), "launch target");
    const KeyBinding* mute = settings.Find(0, 7);
    LOGICON_CHECK(mute && mute->page == 0 && mute->Action() == "keys.media" && mute->Target() == "mute",
                  "omitted page defaults to 0");
    const KeyBinding* local = settings.Find(0, 6);
    LOGICON_CHECK(local && local->IsLocal() && local->Action() == "logicon.keyPage.next",
                  "the published logicon namespace is local to the service");
    LOGICON_CHECK(settings.Find(2, 0) == nullptr, "blank slot");
    LOGICON_CHECK(!settings.UsesSystemFaces() && settings.dialpad.turnCount == 0 && settings.dialpad.buttonCount == 0 &&
                      !settings.dialpad.Turn(kControlDial, kDirectionForward),
                  "no system faces and an unbound dialpad when the document has neither");

    // System faces and the dialpad object.
    constexpr std::string_view dialpadAuthored =
        R"json({"keys":[{"slot":0,"face":"cpu"},{"slot":1,"face":"memory","label":"RAM"},{"slot":2,"face":"gpu"}],
      "dialpad":{"turns":[
        {"control":"dial","direction":"cw","action":"page.next"},
        {"control":"dial","direction":"ccw","action":"page.previous"},
        {"control":"roller","direction":"up","action":"keys.media","target":"volume-up"},
        {"control":"roller","direction":"down","action":"logicon.brightness","target":"-5"}],
      "buttons":[
        {"button":0,"action":"page.previous"},{"button":3,"action":"system.launch","target":"https://example.org"},
        {"button":2}]}})json";
    LOGICON_CHECK(SUCCEEDED(ParseSettingsJson(dialpadAuthored, settings, diagnostic.data(), diagnostic.size())),
                  "system faces and dialpad parse");
    LOGICON_CHECK(settings.UsesSystemFaces() && settings.Find(0, 0)->face == KeyFace::Cpu &&
                      settings.Find(0, 1)->face == KeyFace::Memory && settings.Find(0, 2)->face == KeyFace::Gpu &&
                      IsSystemFace(KeyFace::Gpu) && !IsSystemFace(KeyFace::Clock),
                  "system face names");
    const KeyBinding* clockwise = settings.dialpad.Turn(kControlDial, kDirectionForward);
    const KeyBinding* rollerDown = settings.dialpad.Turn(kControlRoller, kDirectionBackward);
    LOGICON_CHECK(settings.dialpad.turnCount == 4 && clockwise && clockwise->Action() == "page.next" && rollerDown &&
                      rollerDown->Action() == "logicon.brightness" && rollerDown->Target() == "-5" &&
                      rollerDown->IsLocal() && settings.dialpad.buttonCount == 3,
                  "turn bindings by control and direction");
    const KeyBinding* back = settings.dialpad.Button(0);
    const KeyBinding* button7 = settings.dialpad.Button(3);
    LOGICON_CHECK(back && back->Action() == "page.previous" && button7 && button7->Action() == "system.launch" &&
                      settings.dialpad.Button(2) && !settings.dialpad.Button(2)->HasAction() &&
                      !settings.dialpad.Button(1),
                  "dial button bindings by button index");
    LOGICON_CHECK(std::string_view(ControlName(kControlRoller)) == "roller" &&
                      std::string_view(DirectionName(kControlDial, kDirectionBackward)) == "ccw" &&
                      std::string_view(DirectionName(kControlRoller, kDirectionForward)) == "up" &&
                      std::string_view(FaceName(KeyFace::Memory)) == "memory",
                  "enum names round-trip");

    // Target grammars are the host's shared parsers; the model only checks the action-name grammar.
    LOGICON_CHECK(RedXeActions::IsPathOrUri("https://example.org") &&
                      RedXeActions::IsPathOrUri("\\\\server\\share\\x") && !RedXeActions::IsPathOrUri("notepad.exe"),
                  "shared launch target grammar");
    LOGICON_CHECK(RedXeIsActionNameSyntax("zoom.mute") && RedXeIsActionNameSyntax("logicon.keyPage.goto") &&
                      !RedXeIsActionNameSyntax("launch") && !RedXeIsActionNameSyntax("Page.next") &&
                      !RedXeIsActionNameSyntax("page..next") && !RedXeIsActionNameSyntax("page.next."),
                  "action-name grammar");

    // Rejections.
    LOGICON_CHECK(
        FAILED(ParseSettingsJson(R"json({"brightness":0})json", settings, diagnostic.data(), diagnostic.size())),
        "brightness 0 rejected");
    LOGICON_CHECK(
        FAILED(ParseSettingsJson(R"json({"keys":[{"slot":9}]})json", settings, diagnostic.data(), diagnostic.size())),
        "slot 9 rejected");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"keys":[{"page":4,"slot":0}]})json", settings, diagnostic.data(),
                                           diagnostic.size())),
                  "page 4 rejected");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"keys":[{"slot":0},{"slot":0}]})json", settings, diagnostic.data(),
                                           diagnostic.size())),
                  "duplicate slot rejected");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"dialpad":{"turns":[{"control":"dial","direction":"up"}]}})json",
                                           settings, diagnostic.data(), diagnostic.size())),
                  "a roller direction on the dial is rejected");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"dialpad":{"dial":"page"}})json", settings, diagnostic.data(),
                                           diagnostic.size())),
                  "the dial preset member no longer exists");
    LOGICON_CHECK(
        FAILED(ParseSettingsJson(
            R"json({"dialpad":{"turns":[{"control":"dial","direction":"cw"},{"control":"dial","direction":"cw"}]}})json",
            settings, diagnostic.data(), diagnostic.size())),
        "duplicate turn rejected");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"keys":[{"slot":0,"action":"launch"}]})json", settings,
                                           diagnostic.data(), diagnostic.size())),
                  "the former launch name is not an action name");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"keys":[{"slot":0,"action":"keys.down","target":"A"}]})json",
                                           settings, diagnostic.data(), diagnostic.size())),
                  "press-only keys cannot hold a keyboard chord");
    LOGICON_CHECK(FAILED(ParseSettingsJson(
                      R"json({"dialpad":{"buttons":[{"button":1,"action":"mouse.down","target":"left"}]}})json",
                      settings, diagnostic.data(), diagnostic.size())),
                  "press-only dialpad buttons cannot hold a mouse button");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"dialpad":{"buttons":[{"button":4}]}})json", settings,
                                           diagnostic.data(), diagnostic.size())),
                  "dial button 4 rejected");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"dialpad":{"buttons":[{"button":1},{"button":1}]}})json", settings,
                                           diagnostic.data(), diagnostic.size())),
                  "duplicate dial button rejected");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"dialpad":{"buttons":[{"button":1,"label":"x"}]}})json", settings,
                                           diagnostic.data(), diagnostic.size())),
                  "dial button label rejected (no display)");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"dialpad":{"wheel":"page"}})json", settings, diagnostic.data(),
                                           diagnostic.size())),
                  "unknown dialpad member rejected");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"keys":[{"slot":0,"action":"reboot"}]})json", settings,
                                           diagnostic.data(), diagnostic.size())),
                  "unknown action rejected");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"keys":[{"slot":0,"color":"red"}]})json", settings,
                                           diagnostic.data(), diagnostic.size())),
                  "malformed color rejected");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"keys":[{"slot":0,"label":"0123456789ABCDEFG"}]})json", settings,
                                           diagnostic.data(), diagnostic.size())),
                  "17-code-point label rejected");
    LOGICON_CHECK(
        FAILED(ParseSettingsJson(R"json({"keys":[{"page":0}]})json", settings, diagnostic.data(), diagnostic.size())),
        "missing slot rejected");
    LOGICON_CHECK(FAILED(ParseSettingsJson(R"json({"extra":1})json", settings, diagnostic.data(), diagnostic.size())),
                  "unknown member rejected");
    std::string many = R"json({"keys":[)json";
    for (uint32_t index = 0; index < 37; ++index)
    {
        char item[48]{};
        (void)sprintf_s(item, sizeof(item), "%s{\"page\":%u,\"slot\":%u}", index == 0 ? "" : ",", index / 9, index % 9);
        many += item;
    }
    many += "]}";
    LOGICON_CHECK(FAILED(ParseSettingsJson(many, settings, diagnostic.data(), diagnostic.size())), "37 keys rejected");
    LOGICON_CHECK(std::strlen(kSettingsSchema) < 4096 && std::strlen(kSettingsDefaults) < 4096,
                  "published contract stays under the 4096-byte cap");
    return S_OK;
}

// ---------------------------------------------------------------------------------------------------------------
// Faces
// ---------------------------------------------------------------------------------------------------------------

[[nodiscard]] HRESULT DecodeJpegSize(IWICImagingFactory* factory, const uint8_t* jpeg, uint32_t bytes, uint32_t& width,
                                     uint32_t& height) noexcept
{
    width = 0;
    height = 0;
    wil::com_ptr_nothrow<IWICStream> stream;
    HRESULT result = factory->CreateStream(stream.put());
    if (SUCCEEDED(result))
    {
        result = stream->InitializeFromMemory(const_cast<uint8_t*>(jpeg), bytes);
    }
    wil::com_ptr_nothrow<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(result))
    {
        result = factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
    }
    wil::com_ptr_nothrow<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(result))
    {
        result = decoder->GetFrame(0, frame.put());
    }
    UINT decodedWidth = 0;
    UINT decodedHeight = 0;
    if (SUCCEEDED(result))
    {
        result = frame->GetSize(&decodedWidth, &decodedHeight);
    }
    width = decodedWidth;
    height = decodedHeight;
    return result;
}

[[nodiscard]] HRESULT TestFaces() noexcept
{
    using namespace Logicon;
    LOGICON_CHECK(FluentGlyphFromName("ChevronRight", 12) == L'\xE76C', "glyph table lookup");
    LOGICON_CHECK(FluentGlyphFromName("Nope", 4) == 0, "unknown glyph name");
    LOGICON_CHECK(FluentGlyphNameCount() >= 60 && FluentGlyphNameAt(0) != nullptr, "glyph table size");

    FaceRenderer renderer;
    HRESULT result = renderer.Initialize();
    LOGICON_CHECK(SUCCEEDED(result) && renderer.Ready(), "face renderer initializes");

    std::unique_ptr<uint32_t[]> tile{new (std::nothrow) uint32_t[kFacePixels]};
    LOGICON_CHECK(tile != nullptr, "tile allocation");
    FaceSpec spec{};
    spec.backgroundRgb = 0x102030;
    spec.foregroundRgb = 0xFFFFFF;
    spec.icon = FluentGlyphFromName("Home", 4);
    spec.label = L"Home page";
    spec.labelLength = 9;
    result = renderer.ComposeKey(spec, tile.get());
    LOGICON_CHECK(SUCCEEDED(result), "compose icon and label");
    LOGICON_CHECK(tile[0] == 0xFF102030, "corner keeps the background");
    uint32_t inked = 0;
    for (uint32_t index = 0; index < kFacePixels; ++index)
    {
        inked += tile[index] != 0xFF102030 ? 1U : 0U;
    }
    LOGICON_CHECK(inked > 100, "label and icon left ink on the face");

    FaceSpec longLabel{};
    longLabel.label = L"ABCDEFGHIJKLMNOP";
    longLabel.labelLength = 16;
    LOGICON_CHECK(SUCCEEDED(renderer.ComposeKey(longLabel, tile.get())), "16-character label ellipsizes");
    FaceSpec ring{};
    ring.accentRing = true;
    ring.accentRgb = 0xFF0000;
    ring.big = L"2/3";
    ring.bigLength = 3;
    LOGICON_CHECK(SUCCEEDED(renderer.ComposeKey(ring, tile.get())) && tile[1] == 0xFFFF0000,
                  "accent ring paints the border");
    FaceSpec invalid{};
    invalid.invalid = true;
    LOGICON_CHECK(SUCCEEDED(renderer.ComposeKey(invalid, tile.get())) && tile[0] == 0xFF8B1A1A, "invalid face is red");

    std::unique_ptr<uint8_t[]> jpeg{new (std::nothrow) uint8_t[kMaximumJpegBytes]};
    LOGICON_CHECK(jpeg != nullptr, "jpeg allocation");
    uint32_t bytes = 0;
    result = renderer.EncodeJpeg(tile.get(), kFaceSize, kFaceSize, kFaceSize, jpeg.get(), kMaximumJpegBytes, bytes);
    LOGICON_CHECK(SUCCEEDED(result) && bytes > 100 && bytes < kMaximumJpegBytes, "tile encodes");
    LOGICON_CHECK(jpeg[0] == 0xFF && jpeg[1] == 0xD8 && jpeg[bytes - 2] == 0xFF && jpeg[bytes - 1] == 0xD9,
                  "JPEG SOI/EOI markers");
    wil::com_ptr_nothrow<IWICImagingFactory> wic;
    LOGICON_CHECK(
        SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put()))),
        "WIC factory for the round trip");
    uint32_t width = 0;
    uint32_t height = 0;
    LOGICON_CHECK(SUCCEEDED(DecodeJpegSize(wic.get(), jpeg.get(), bytes, width, height)) && width == kFaceSize &&
                      height == kFaceSize,
                  "tile JPEG decodes at 118x118");

    // A 434x434 grid composed of nine tiles encodes and decodes at full size.
    std::unique_ptr<uint32_t[]> grid{new (std::nothrow) uint32_t[kGridSize * kGridSize]};
    LOGICON_CHECK(grid != nullptr, "grid allocation");
    std::fill_n(grid.get(), kGridSize * kGridSize, 0xFF000000U);
    for (uint32_t slot = 0; slot < kKeyCount; ++slot)
    {
        FaceSpec face{};
        face.backgroundRgb = 0x202020 + slot * 0x101010;
        wchar_t label[4]{};
        (void)swprintf_s(label, L"K%u", slot + 1U);
        face.label = label;
        face.labelLength = 2;
        LOGICON_CHECK(SUCCEEDED(renderer.ComposeKey(face, tile.get())), "grid tile compose");
        const uint32_t originX = KeyColumn(slot) * (kKeySize + kKeyGap);
        const uint32_t originY = KeyRow(slot) * (kKeySize + kKeyGap);
        for (uint32_t y = 0; y < kKeySize; ++y)
        {
            std::memcpy(grid.get() + static_cast<size_t>(originY + y) * kGridSize + originX,
                        tile.get() + static_cast<size_t>(y) * kKeySize, kKeySize * sizeof(uint32_t));
        }
    }
    result = renderer.EncodeJpeg(grid.get(), kGridSize, kGridSize, kGridSize, jpeg.get(), kMaximumJpegBytes, bytes);
    LOGICON_CHECK(SUCCEEDED(result) && bytes < kMaximumJpegBytes, "grid encodes within the JPEG budget");
    LOGICON_CHECK(SUCCEEDED(DecodeJpegSize(wic.get(), jpeg.get(), bytes, width, height)) && width == kGridSize &&
                      height == kGridSize,
                  "grid JPEG decodes at 434x434");
    // Encoding a sub-rectangle of the grid with the grid stride yields a key-sized image.
    result = renderer.EncodeJpeg(grid.get() + static_cast<size_t>(158) * kGridSize + 158, kKeySize, kKeySize, kGridSize,
                                 jpeg.get(), kMaximumJpegBytes, bytes);
    LOGICON_CHECK(SUCCEEDED(result) && SUCCEEDED(DecodeJpegSize(wic.get(), jpeg.get(), bytes, width, height)) &&
                      width == kKeySize && height == kKeySize,
                  "strided sub-rectangle encodes at 118x118");
    LOGICON_CHECK(renderer.EncodeJpeg(tile.get(), kFaceSize, kFaceSize, kFaceSize, jpeg.get(), 64, bytes) ==
                          STG_E_MEDIUMFULL ||
                      FAILED(renderer.EncodeJpeg(tile.get(), kFaceSize, kFaceSize, kFaceSize, jpeg.get(), 64, bytes)),
                  "a too-small buffer fails cleanly");
    return S_OK;
}

// ---------------------------------------------------------------------------------------------------------------
// Device session over the synthetic keypad
// ---------------------------------------------------------------------------------------------------------------

[[nodiscard]] HRESULT TestDeviceSession() noexcept
{
    using namespace Logicon;
    SyntheticKeypad keypad;
    LOGICON_CHECK(SUCCEEDED(keypad.Initialize()), "synthetic keypad initializes");
    DeviceSession session;
    std::unique_ptr<HidPort> port{new (std::nothrow) SyntheticHidPort(keypad)};
    LOGICON_CHECK(port != nullptr && SUCCEEDED(session.AttachPort(std::move(port))), "port attaches");
    LOGICON_CHECK(session.HasPorts() && !session.Connected(), "attached but not connected");
    wil::unique_event_nothrow stop;
    LOGICON_CHECK(SUCCEEDED(stop.create(wil::EventOptions::ManualReset)), "stop event");

    HRESULT result = session.Connect(stop.get());
    LOGICON_CHECK(SUCCEEDED(result), "connect resolves features and diverts page buttons");
    LOGICON_CHECK(session.Connected() && session.Features().display == 0x02 &&
                      session.Features().reprogControls == 0x0B && session.Features().brightness == 0x0F,
                  "feature indexes resolved through root lookups");
    LOGICON_CHECK(keypad.PageButtonsDiverted(), "page buttons diverted with flags 0x03");
    LOGICON_CHECK(keypad.CommandsReceived() >= 7, "root lookups and reporting commands were sent");

    LOGICON_CHECK(SUCCEEDED(session.SetBrightness(55, stop.get())), "brightness write");
    LOGICON_CHECK(keypad.BrightnessPercent() == 55, "synthetic keypad saw brightness 55");

    std::array<uint8_t, 9000> jpeg{};
    jpeg[0] = 0xFF;
    jpeg[1] = 0xD8;
    LOGICON_CHECK(
        SUCCEEDED(session.WriteImage(KeyRegion(4), jpeg.data(), static_cast<uint32_t>(jpeg.size()), false, stop.get())),
        "image write");
    LOGICON_CHECK(session.Counters().imagePackets == 3 && session.Counters().imagesWritten == 1,
                  "9000 bytes stream as three packets");
    SyntheticImageRecord records[4]{};
    LOGICON_CHECK(keypad.CopyImageRecords(records, 4) == 1 && records[0].region.x == 181 &&
                      records[0].region.y == 164 && records[0].region.width == 118 && records[0].bytes == 9000 &&
                      !records[0].deferUpdate && records[0].head[0] == 0xFF && records[0].head[1] == 0xD8,
                  "synthetic keypad reassembled the image record");

    // Key events arrive through Pump.
    bool changed = false;
    ControlEdges edges{};
    LOGICON_CHECK(SUCCEEDED(keypad.InjectKeys((1U << 2) | (1U << 6))), "inject keys");
    LOGICON_CHECK(WaitForSingleObject(keypad.ReadEvent(), 100) == WAIT_OBJECT_0, "read event signaled");
    LOGICON_CHECK(SUCCEEDED(session.Pump(changed, edges)) && changed &&
                      session.Controls().keys == ((1U << 2) | (1U << 6)) && edges.keysDown == ((1U << 2) | (1U << 6)),
                  "pump applies pressed keys and reports their edges");
    LOGICON_CHECK(SUCCEEDED(keypad.InjectKeys(0)) && SUCCEEDED(session.Pump(changed, edges)) && changed &&
                      session.Controls().keys == 0 && edges.keysDown == 0,
                  "release clears the mask without an edge");
    LOGICON_CHECK(SUCCEEDED(keypad.InjectPageButtons(2U)) && SUCCEEDED(session.Pump(changed, edges)) && changed &&
                      session.Controls().pageButtons == 2U && edges.pageButtonsDown == 2U,
                  "page button event");
    LOGICON_CHECK(SUCCEEDED(session.Pump(changed, edges)) && !changed && edges.keysDown == 0 &&
                      edges.pageButtonsDown == 0,
                  "an idle pump reports no change");
    // A tap whose press and release are both buffered before one pump still yields a press edge.
    LOGICON_CHECK(SUCCEEDED(keypad.InjectKeys(1U << 8)) && SUCCEEDED(keypad.InjectKeys(0)) &&
                      SUCCEEDED(session.Pump(changed, edges)) && session.Controls().keys == 0 &&
                      edges.keysDown == (1U << 8),
                  "press and release in one drain report the press edge");
    TraceEntry trace[kTraceSlots]{};
    LOGICON_CHECK(session.CopyTrace(trace, kTraceSlots) > 0, "trace records frames");

    LOGICON_CHECK(SUCCEEDED(session.Restore(stop.get(), true)), "restore");
    LOGICON_CHECK(!keypad.PageButtonsDiverted() && keypad.ResetToLogoSeen(), "flags restored and splash reset sent");
    LOGICON_CHECK(SUCCEEDED(session.Restore(stop.get(), true)), "restore is idempotent");
    session.Detach();
    LOGICON_CHECK(!session.HasPorts() && session.Disconnected(), "detached");

    // Shipping firmware hides 0x19A1 from the root; the feature-set walk still finds it at its real index.
    keypad.Reset();
    keypad.HideDisplayFromRoot(true);
    std::unique_ptr<HidPort> hiddenPort{new (std::nothrow) SyntheticHidPort(keypad)};
    LOGICON_CHECK(hiddenPort != nullptr && SUCCEEDED(session.AttachPort(std::move(hiddenPort))), "port re-attaches");
    LOGICON_CHECK(SUCCEEDED(session.Connect(stop.get())) && session.Features().display == 0x02 &&
                      !session.Features().displayAssumed,
                  "a root-hidden display feature is resolved through the feature set");
    LOGICON_CHECK(SUCCEEDED(session.Restore(stop.get(), false)), "restore after the hidden lookup");
    session.Detach();
    keypad.HideDisplayFromRoot(false);
    return S_OK;
}

// ---------------------------------------------------------------------------------------------------------------
// Dialpad: the four diverted buttons over the synthetic vendor collection, and the raw-input wheel helpers
// ---------------------------------------------------------------------------------------------------------------

[[nodiscard]] HRESULT TestDialpad() noexcept
{
    using namespace Logicon;
    // The captured 0x1B04 event for "Button 6" (cid 0x0059) and Back (0x0053) held together.
    const uint8_t held[kLongReportBytes] = {0x11, 0xFF, 0x0A, 0x00, 0x00, 0x59, 0x00, 0x53, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    InputEvent event{};
    LOGICON_CHECK(ParseInputEvent(held, sizeof(held), 0x00, 0x0A, event) && event.kind == InputKind::DivertedButtons &&
                      event.controlCount == 2 && event.mask == 0,
                  "dialpad button event parses as diverted buttons without page bits");
    LOGICON_CHECK(DialpadButtonMask(event) == ((1U << 2) | (1U << 0)), "dial button mask follows kDialpadControls");
    InputEvent none{};
    LOGICON_CHECK(DialpadButtonMask(none) == 0, "no event, no buttons");

    // Raw-input device names in the Bluetooth and USB spellings.
    LOGICON_CHECK(RawWheelListener::DeviceNameMatches(
                      L"\\\\?\\HID#{00001812-0000-1000-8000-00805f9b34fb}_Dev_VID&02046d_PID&bc00_REV&0016_"
                      L"d31f4fa36724&Col01#e&27bf0ac3&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}",
                      kVendorId, kDialpadProductId),
                  "the dialpad's Bluetooth mouse collection matches");
    LOGICON_CHECK(RawWheelListener::DeviceNameMatches(L"\\\\?\\HID#VID_046D&PID_BC00&MI_01&Col01#7&1&0&0000#{...}",
                                                      kVendorId, kDialpadProductId),
                  "a USB spelling matches");
    LOGICON_CHECK(!RawWheelListener::DeviceNameMatches(
                      L"\\\\?\\HID#{00001812-0000-1000-8000-00805f9b34fb}_Dev_VID&02046d_PID&b034&REV&0016_"
                      L"aabbcc&Col01#e&1&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}",
                      kVendorId, kDialpadProductId),
                  "another Logitech PID does not match");
    LOGICON_CHECK(!RawWheelListener::DeviceNameMatches(nullptr, kVendorId, kDialpadProductId) &&
                      !RawWheelListener::DeviceNameMatches(L"", kVendorId, kDialpadProductId),
                  "empty names never match");

    // Raw mouse packets fold into the wheel state: horizontal wheel = dial, vertical wheel = roller, buttons 4/5.
    WheelState state{};
    WheelDeltas pending{};
    RAWMOUSE mouse{};
    mouse.usButtonFlags = RI_MOUSE_HWHEEL;
    mouse.usButtonData = static_cast<USHORT>(static_cast<int16_t>(-120));
    RawWheelListener::FoldMouse(mouse, state, pending);
    mouse.usButtonFlags = RI_MOUSE_WHEEL;
    mouse.usButtonData = 120;
    RawWheelListener::FoldMouse(mouse, state, pending);
    RawWheelListener::FoldMouse(mouse, state, pending);
    LOGICON_CHECK(state.dialRaw == -120 && state.dialEvents == 1 && state.rollerRaw == 240 && state.rollerEvents == 2 &&
                      state.reports == 3 && state.motionEvents == 0 && pending.dial == -120 && pending.roller == 240,
                  "wheel deltas accumulate per axis");
    mouse = RAWMOUSE{};
    mouse.usButtonFlags = RI_MOUSE_BUTTON_4_DOWN;
    RawWheelListener::FoldMouse(mouse, state, pending);
    LOGICON_CHECK(state.buttonMask == (1U << 3), "button 4 down sets bit 3");
    mouse.usButtonFlags = RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_DOWN;
    mouse.lLastX = 3;
    RawWheelListener::FoldMouse(mouse, state, pending);
    LOGICON_CHECK(state.buttonMask == (1U << 4) && state.motionEvents == 1, "button 4 up, 5 down, motion counted");

    // The listener itself needs a thread with a window; Start/Stop must succeed in a plain process.
    RawWheelListener listener;
    LOGICON_CHECK(SUCCEEDED(listener.Start(kVendorId, kDialpadProductId)) && listener.Running(),
                  "raw input listener starts");
    (void)listener.Pump();
    listener.Stop();
    LOGICON_CHECK(!listener.Running(), "raw input listener stops");
    LOGICON_CHECK(SUCCEEDED(listener.Start(kVendorId, kDialpadProductId)), "raw input listener restarts");
    listener.Stop();

    // The dialpad session over the synthetic vendor collection: 0x1B04 resolved at 0x0A, four controls diverted.
    SyntheticKeypad dialpad;
    dialpad.ActAsDialpad(true);
    LOGICON_CHECK(SUCCEEDED(dialpad.Initialize()), "synthetic dialpad initializes");
    DeviceSession session;
    std::unique_ptr<HidPort> port{new (std::nothrow) SyntheticHidPort(dialpad)};
    LOGICON_CHECK(port != nullptr && port->Info().productId == kDialpadProductId &&
                      port->Info().outputReportBytes == kLongReportBytes,
                  "synthetic dialpad port describes a 20-byte 0x11 collection");
    LOGICON_CHECK(SUCCEEDED(session.AttachPort(std::move(port))), "dialpad port attaches");
    wil::unique_event_nothrow stop;
    LOGICON_CHECK(SUCCEEDED(stop.create(wil::EventOptions::ManualReset)), "stop event");
    LOGICON_CHECK(SUCCEEDED(session.ConnectDialpad(stop.get(), 0x0F)) && session.Connected() &&
                      session.Role() == DeviceRole::Dialpad && session.Features().reprogControls == 0x0A &&
                      session.Features().display == 0 && session.DivertedDialButtons() == 0x0F,
                  "dialpad connect resolves 0x1B04 only");
    LOGICON_CHECK(dialpad.DialButtonsDiverted(), "all four dial controls diverted with flags 0x03");
    LOGICON_CHECK(dialpad.CommandsReceived() == 9, "one root lookup and four get/set reporting pairs");

    bool changed = false;
    ControlEdges edges{};
    LOGICON_CHECK(SUCCEEDED(dialpad.InjectDialButtons(1U << 3)) && SUCCEEDED(session.Pump(changed, edges)) && changed &&
                      session.Controls().dialButtons == (1U << 3) && edges.dialButtonsDown == (1U << 3) &&
                      edges.pageButtonsDown == 0 && session.Controls().pageButtons == 0,
                  "button 7 press reaches the dial buttons, not the page buttons");
    LOGICON_CHECK(SUCCEEDED(dialpad.InjectDialButtons(0)) && SUCCEEDED(dialpad.InjectDialButtons(1U << 1)) &&
                      SUCCEEDED(dialpad.InjectDialButtons(0)) && SUCCEEDED(session.Pump(changed, edges)) &&
                      session.Controls().dialButtons == 0 && edges.dialButtonsDown == (1U << 1),
                  "a tap drained in one pump still reports its press edge");
    LOGICON_CHECK(SUCCEEDED(session.Restore(stop.get(), false)) && !dialpad.DialButtonsDiverted(),
                  "restore puts the four controls back");
    session.Detach();

    // Only bound buttons are diverted: Forward alone leaves Back and the two others native.
    dialpad.Reset();
    std::unique_ptr<HidPort> partialPort{new (std::nothrow) SyntheticHidPort(dialpad)};
    LOGICON_CHECK(partialPort != nullptr && SUCCEEDED(session.AttachPort(std::move(partialPort))), "port re-attaches");
    LOGICON_CHECK(SUCCEEDED(session.ConnectDialpad(stop.get(), 1U << 1)) &&
                      session.DivertedDialButtons() == (1U << 1) && !dialpad.DialButtonsDiverted() &&
                      dialpad.CommandsReceived() == 3,
                  "one bound button means one get/set pair");
    LOGICON_CHECK(SUCCEEDED(session.Restore(stop.get(), false)), "partial restore");
    session.Detach();

    // The keypad persona is unchanged by the dialpad additions: a dial control is rejected there.
    SyntheticKeypad keypad;
    LOGICON_CHECK(SUCCEEDED(keypad.Initialize()), "synthetic keypad initializes");
    std::unique_ptr<HidPort> keypadPort{new (std::nothrow) SyntheticHidPort(keypad)};
    LOGICON_CHECK(keypadPort != nullptr && SUCCEEDED(session.AttachPort(std::move(keypadPort))), "keypad attaches");
    LOGICON_CHECK(SUCCEEDED(session.Connect(stop.get())) && session.Role() == DeviceRole::Keypad,
                  "keypad connect keeps the keypad role");
    LOGICON_CHECK(SUCCEEDED(keypad.InjectPageButtons(1U)) && SUCCEEDED(session.Pump(changed, edges)) &&
                      session.Controls().pageButtons == 1U && session.Controls().dialButtons == 0,
                  "page buttons still land in the page mask on the keypad");
    LOGICON_CHECK(SUCCEEDED(session.Restore(stop.get(), false)), "keypad restore");
    session.Detach();

    // System Data reduction: cpu rounds, memory is used/total, gpu prefers the discrete hardware adapter.
    std::array<int32_t, 8> columns{};
    columns.fill(-1);
    columns[0] = 0; // cpu totalPercent
    columns[1] = 0; // memory totalPhysicalBytes
    columns[2] = 2; // memory usedPhysicalBytes
    columns[3] = 3; // gpu utilizationPercent
    columns[4] = 1; // gpu software
    columns[5] = 2; // gpu integrated
    SystemValues values{};
    RedXeDataValue cpuValue{sizeof(RedXeDataValue), RedXeDataValueTypeFloat64, RedXeDataQualityGood, {}, 0};
    cpuValue.float64Value = 37.49;
    RedXeDataRow cpuRow{sizeof(RedXeDataRow), &cpuValue, 1};
    RedXeDataSnapshot cpu{};
    cpu.sizeBytes = sizeof(cpu);
    cpu.dataSetId = "cpu.summary";
    cpu.rows = &cpuRow;
    cpu.rowCount = 1;
    cpu.columnCount = 1;
    SystemDataFeed::Reduce(cpu, columns, values);
    LOGICON_CHECK(values.cpuPercent == 37 && values.memoryPercent == -1, "cpu percent rounds down from 37.49");
    RedXeDataValue memoryValues[3]{};
    for (RedXeDataValue& value : memoryValues)
    {
        value = RedXeDataValue{sizeof(RedXeDataValue), RedXeDataValueTypeUInt64, RedXeDataQualityGood, {}, 0};
    }
    memoryValues[0].uint64Value = 32ULL << 30U;
    memoryValues[2].uint64Value = 12ULL << 30U;
    RedXeDataRow memoryRow{sizeof(RedXeDataRow), memoryValues, 3};
    RedXeDataSnapshot memory = cpu;
    memory.dataSetId = "memory.summary";
    memory.rows = &memoryRow;
    memory.columnCount = 3;
    SystemDataFeed::Reduce(memory, columns, values);
    LOGICON_CHECK(values.memoryPercent == 38 && values.cpuPercent == 37, "memory percent is used over total");
    // Two adapters: an integrated one at 80% and a discrete one at 15%; the discrete one wins.
    RedXeDataValue gpuValues[2][4]{};
    for (uint32_t row = 0; row < 2; ++row)
    {
        gpuValues[row][0] =
            RedXeDataValue{sizeof(RedXeDataValue), RedXeDataValueTypeUtf16, RedXeDataQualityGood, {}, 0};
        gpuValues[row][1] =
            RedXeDataValue{sizeof(RedXeDataValue), RedXeDataValueTypeUInt64, RedXeDataQualityGood, {}, 0};
        gpuValues[row][2] =
            RedXeDataValue{sizeof(RedXeDataValue), RedXeDataValueTypeUInt64, RedXeDataQualityGood, {}, 0};
        gpuValues[row][3] =
            RedXeDataValue{sizeof(RedXeDataValue), RedXeDataValueTypeFloat64, RedXeDataQualityGood, {}, 0};
    }
    gpuValues[0][2].uint64Value = 1;
    gpuValues[0][3].float64Value = 80.0;
    gpuValues[1][3].float64Value = 15.2;
    RedXeDataRow gpuRows[2] = {{sizeof(RedXeDataRow), gpuValues[0], 4}, {sizeof(RedXeDataRow), gpuValues[1], 4}};
    RedXeDataSnapshot gpu = cpu;
    gpu.dataSetId = "gpu.adapter";
    gpu.rows = gpuRows;
    gpu.rowCount = 2;
    gpu.columnCount = 4;
    SystemDataFeed::Reduce(gpu, columns, values);
    LOGICON_CHECK(values.gpuPercent == 15, "the discrete adapter's utilization is chosen");
    cpuValue.quality = RedXeDataQualityUnavailable;
    SystemDataFeed::Reduce(cpu, columns, values);
    LOGICON_CHECK(values.cpuPercent == -1, "an unavailable value reads as unknown");
    RedXeDataSnapshot unknown = cpu;
    unknown.dataSetId = "thread.list";
    SystemDataFeed::Reduce(unknown, columns, values);
    LOGICON_CHECK(values.memoryPercent == 38 && values.gpuPercent == 15, "unknown data sets are ignored");
    return S_OK;
}

// ---------------------------------------------------------------------------------------------------------------
// The shipped DLL: factory, contract, service lifetime, device lane, and test exports
// ---------------------------------------------------------------------------------------------------------------

// A stand-in for builtin.system-data: the three data sets Logicon reads with the columns it looks up, one
// subscription slot per data set, and a Push that delivers a snapshot to the sink the way the acquisition worker
// would.
class FakeSystemData final : public IRedXeDataProvider
{
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeDataProvider))
        {
            *result = static_cast<IRedXeDataProvider*>(this);
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return 2;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        return 1;
    }
    HRESULT STDMETHODCALLTYPE GetDataSets(const RedXeDataSetDescriptor** descriptors, uint32_t* count) noexcept override
    {
        if (!descriptors || !count)
        {
            return E_POINTER;
        }
        *descriptors = kDataSets;
        *count = 3;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Subscribe(const RedXeDataSubscriptionOptions* options, IRedXeDataSink* sink,
                                        IRedXeDataSubscription** subscription) noexcept override
    {
        if (!options || !sink || !subscription || options->sizeBytes != sizeof(RedXeDataSubscriptionOptions))
        {
            return E_INVALIDARG;
        }
        *subscription = nullptr;
        for (uint32_t index = 0; index < 3; ++index)
        {
            if (std::strcmp(options->dataSetId, kDataSets[index].dataSetId) == 0)
            {
                slots[index].sink = sink;
                sink->AddRef();
                slots[index].interval = options->requestedIntervalMilliseconds;
                *subscription = &slots[index];
                subscribed.fetch_add(1, std::memory_order_relaxed);
                return S_OK;
            }
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    // Delivers one row of cpu.summary / memory.summary / gpu.adapter (index 0..2) to its sink when active.
    [[nodiscard]] HRESULT Push(uint32_t dataSet, const RedXeDataValue* values, uint32_t valueCount) noexcept
    {
        Slot& slot = slots[dataSet];
        if (!slot.sink || !slot.active)
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_READY);
        }
        RedXeDataRow row{sizeof(RedXeDataRow), values, valueCount};
        RedXeDataSnapshot snapshot{};
        snapshot.sizeBytes = sizeof(snapshot);
        snapshot.dataSetId = kDataSets[dataSet].dataSetId;
        snapshot.sequence = ++sequence;
        snapshot.rows = &row;
        snapshot.rowCount = 1;
        snapshot.columnCount = valueCount;
        return slot.sink->OnDataSnapshot(&snapshot);
    }

    [[nodiscard]] bool AllActive() const noexcept
    {
        return slots[0].active && slots[1].active && slots[2].active;
    }
    [[nodiscard]] bool AnyActive() const noexcept
    {
        return slots[0].active || slots[1].active || slots[2].active;
    }
    [[nodiscard]] bool AnySink() const noexcept
    {
        return slots[0].sink || slots[1].sink || slots[2].sink;
    }

    struct Slot final : IRedXeDataSubscription
    {
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
        {
            if (!result)
            {
                return E_POINTER;
            }
            *result = nullptr;
            if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeDataSubscription))
            {
                *result = static_cast<IRedXeDataSubscription*>(this);
                ++references;
                return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() noexcept override
        {
            return ++references;
        }
        ULONG STDMETHODCALLTYPE Release() noexcept override
        {
            // The last release drops the sink, as the host's subscription release ends callback access.
            const ULONG remaining = --references;
            if (remaining == 0 && sink)
            {
                active = false;
                sink->Release();
                sink = nullptr;
            }
            return remaining;
        }
        HRESULT STDMETHODCALLTYPE SetActive(BOOL wanted) noexcept override
        {
            active = wanted != FALSE;
            return S_OK;
        }

        IRedXeDataSink* sink = nullptr;
        ULONG references = 1;
        uint32_t interval = 0;
        bool active = false;
    };

    static constexpr RedXeDataColumnDescriptor kCpuColumns[] = {
        {sizeof(RedXeDataColumnDescriptor), "totalPercent", L"Total CPU", L"percent", RedXeDataValueTypeFloat64},
    };
    static constexpr RedXeDataColumnDescriptor kMemoryColumns[] = {
        {sizeof(RedXeDataColumnDescriptor), "totalPhysicalBytes", L"Physical", L"bytes", RedXeDataValueTypeUInt64},
        {sizeof(RedXeDataColumnDescriptor), "availablePhysicalBytes", L"Available", L"bytes", RedXeDataValueTypeUInt64},
        {sizeof(RedXeDataColumnDescriptor), "usedPhysicalBytes", L"Used", L"bytes", RedXeDataValueTypeUInt64},
    };
    static constexpr RedXeDataColumnDescriptor kGpuColumns[] = {
        {sizeof(RedXeDataColumnDescriptor), "displayName", L"Name", nullptr, RedXeDataValueTypeUtf16},
        {sizeof(RedXeDataColumnDescriptor), "software", L"Software", nullptr, RedXeDataValueTypeUInt64},
        {sizeof(RedXeDataColumnDescriptor), "integrated", L"Integrated", nullptr, RedXeDataValueTypeUInt64},
        {sizeof(RedXeDataColumnDescriptor), "utilizationPercent", L"Utilization", L"percent",
         RedXeDataValueTypeFloat64},
    };
    static constexpr RedXeDataSetDescriptor kDataSets[] = {
        {sizeof(RedXeDataSetDescriptor), "cpu.summary", L"CPU", L"", kCpuColumns, 1, 1, 1000, 0},
        {sizeof(RedXeDataSetDescriptor), "memory.summary", L"Memory", L"", kMemoryColumns, 3, 1, 1000, 0},
        {sizeof(RedXeDataSetDescriptor), "gpu.adapter", L"GPU", L"", kGpuColumns, 4, 8, 1000, 0},
    };

    std::array<Slot, 3> slots{};
    std::atomic<uint32_t> subscribed{0};
    uint64_t sequence = 0;
};

class TestHost final : public IRedXeHost
{
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeHost))
        {
            *result = static_cast<IRedXeHost*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return 2;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        return 1;
    }
    HRESULT STDMETHODCALLTYPE GetDataProvider(const char* providerId, IRedXeDataProvider** provider) noexcept override
    {
        if (!provider)
        {
            return E_POINTER;
        }
        *provider = nullptr;
        if (!providerId || std::strcmp(providerId, "builtin.system-data") != 0)
        {
            return E_NOINTERFACE;
        }
        providerLookups.fetch_add(1, std::memory_order_relaxed);
        *provider = &systemData;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestFrame() noexcept override
    {
        frames.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReportWidgetStatus(const char*, const RedXeWidgetStatusReport*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PersistWidgetSettings(const char*, const char*, uint32_t) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord* record) noexcept override
    {
        if (!record || record->sizeBytes != sizeof(RedXeLogRecord))
        {
            return E_INVALIDARG;
        }
        logs.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueueControlWork(IRedXeControlWork*) noexcept override
    {
        return E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE RequestAction(const RedXeActionRequest* request) noexcept override
    {
        if (!request || request->sizeBytes != sizeof(RedXeActionRequest) || !request->actionUtf8)
        {
            return E_INVALIDARG;
        }
        {
            const auto guard = wil::AcquireSRWLockExclusive(&actionLock);
            strncpy_s(lastActionName, request->actionUtf8, _TRUNCATE);
            lastTarget[0] = '\0';
            if (request->targetUtf8)
            {
                strncpy_s(lastTarget, request->targetUtf8, _TRUNCATE);
            }
        }
        actions.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ExecuteAction(const RedXeActionRequest* request) noexcept override
    {
        return RequestAction(request);
    }
    // Every name resolves and every target is accepted, except the one name tests use to prove the invalid face.
    HRESULT STDMETHODCALLTYPE ValidateAction(const RedXeActionRequest* request,
                                             const RedXeActionDescriptor** descriptor) noexcept override
    {
        if (descriptor)
        {
            *descriptor = nullptr;
        }
        if (!request || request->sizeBytes != sizeof(RedXeActionRequest) || !request->actionUtf8)
        {
            return E_INVALIDARG;
        }
        validations.fetch_add(1, std::memory_order_relaxed);
        return std::strcmp(request->actionUtf8, "system.nonexistent") == 0 ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND) : S_OK;
    }
    // The last action name the service requested, copied under the host lock.
    bool LastActionIs(const char* name) noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&actionLock);
        return std::strcmp(lastActionName, name) == 0;
    }
    bool LastTargetIs(const char* target) noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&actionLock);
        return std::strcmp(lastTarget, target) == 0;
    }

    std::atomic<uint32_t> frames{0};
    std::atomic<uint32_t> logs{0};
    std::atomic<uint32_t> actions{0};
    std::atomic<uint32_t> validations{0};
    SRWLOCK actionLock = SRWLOCK_INIT;
    char lastActionName[65]{};
    std::atomic<uint32_t> providerLookups{0};
    char lastTarget[513]{};
    FakeSystemData systemData;
};

template <typename Predicate> [[nodiscard]] bool WaitUntil(Predicate predicate, DWORD milliseconds) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    while (GetTickCount64() < deadline)
    {
        if (predicate())
        {
            return true;
        }
        Sleep(10);
    }
    return predicate();
}

[[nodiscard]] HRESULT TestShippedModule() noexcept
{
    std::array<wchar_t, 1024> path{};
    HRESULT result = BuildSiblingPath(L"Plugins\\Logicon.dll", path);
    LOGICON_CHECK(SUCCEEDED(result), "plugin path");
    wil::unique_hmodule module{
        LoadLibraryExW(path.data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    LOGICON_CHECK(module != nullptr, "Logicon.dll maps");
    const auto create = Resolve<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const auto enumerate = Resolve<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    const auto contract =
        Resolve<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);
    const auto diagnostics =
        Resolve<RedXeLogiconGetTestDiagnosticsFn>(module.get(), kRedXeLogiconGetTestDiagnosticsExport);
    const auto useSynthetic =
        Resolve<RedXeLogiconUseSyntheticDeviceFn>(module.get(), kRedXeLogiconUseSyntheticDeviceExport);
    const auto inject = Resolve<RedXeLogiconInjectControlFn>(module.get(), kRedXeLogiconInjectControlExport);
    const auto setOverride = Resolve<RedXeLogiconSetFaceOverrideFn>(module.get(), kRedXeLogiconSetFaceOverrideExport);
    const auto setBrightness = Resolve<RedXeLogiconSetBrightnessFn>(module.get(), kRedXeLogiconSetBrightnessExport);
    LOGICON_CHECK(create && enumerate && contract && diagnostics && useSynthetic && inject && setOverride &&
                      setBrightness,
                  "required and test exports resolve");

    const RedXePluginMetadata* metadata = nullptr;
    uint32_t count = 0;
    LOGICON_CHECK(SUCCEEDED(enumerate(&metadata, &count)) && metadata && count >= 1, "metadata enumerates");
    LOGICON_CHECK(RedXeAsciiEqualsIgnoreCase(metadata[0].id, Logicon::kPluginId) &&
                      metadata[0].capabilities == (RedXePluginCapabilityService | RedXePluginCapabilityActions),
                  "builtin.logicon is a service plugin that publishes actions");
    LOGICON_CHECK(count == 2 && RedXeAsciiEqualsIgnoreCase(metadata[1].id, Logicon::kMonitorPluginId) &&
                      metadata[1].capabilities == RedXePluginCapabilityWidgetProvider,
                  "the monitor tile is catalogued in every build");
    {
        // Debug builds construct the monitor; Release builds list the type and refuse the instance so the host
        // draws a placeholder instead of a tile.
        RedXeFactoryOptions monitorOptions{};
        monitorOptions.sizeBytes = sizeof(monitorOptions);
        constexpr char monitorEnvelope[] = R"json({"plugin":{},"instance":{}})json";
        monitorOptions.configurationJsonUtf8 = monitorEnvelope;
        monitorOptions.configurationBytes = static_cast<uint32_t>(sizeof(monitorEnvelope) - 1);
        monitorOptions.backgroundColor = kRedXeDefaultBackgroundColor;
        void* providerObject = nullptr;
        TestHost monitorHost;
        LOGICON_CHECK(SUCCEEDED(create(__uuidof(IRedXeWidgetProvider), &monitorOptions, &monitorHost,
                                       Logicon::kMonitorPluginId, &providerObject)) &&
                          providerObject,
                      "monitor provider creates");
        wil::com_ptr_nothrow<IRedXeWidgetProvider> provider;
        provider.attach(static_cast<IRedXeWidgetProvider*>(providerObject));
        const RedXeWidgetTypeDescriptor* types = nullptr;
        uint32_t typeCount = 0;
        LOGICON_CHECK(SUCCEEDED(provider->GetWidgetTypes(&types, &typeCount)) && typeCount == 1 &&
                          RedXeAsciiEqualsIgnoreCase(types[0].typeId, Logicon::kMonitorWidgetTypeId),
                      "monitor provider lists its type");
        wil::com_ptr_nothrow<IRedXeWidget> widget;
        const HRESULT created = provider->CreateWidget(Logicon::kMonitorWidgetTypeId, "widget.1", widget.put());
#if defined(_DEBUG)
        LOGICON_CHECK(SUCCEEDED(created) && widget, "Debug builds construct the monitor tile");
        wil::com_ptr_nothrow<IRedXeGpuWidget> gpu;
        wil::com_ptr_nothrow<IRedXeInteractiveWidget> interactive;
        LOGICON_CHECK(SUCCEEDED(widget.query_to(gpu.put())) && SUCCEEDED(widget.query_to(interactive.put())),
                      "the monitor is a GPU and interactive widget");
#else
        LOGICON_CHECK(created == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) && !widget,
                      "Release builds refuse the monitor instance");
#endif
    }
    const RedXePluginSettingsContract* published = nullptr;
    LOGICON_CHECK(SUCCEEDED(contract(Logicon::kPluginId, &published)) && published &&
                      published->schemaBytes == std::strlen(Logicon::kSettingsSchema),
                  "service contract publishes");
    LOGICON_CHECK(contract("builtin.nope", &published) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "unknown contract id");

    RedXeLogiconTestDiagnostics report{};
    report.sizeBytes = sizeof(report);
    LOGICON_CHECK(diagnostics(&report) == HRESULT_FROM_WIN32(ERROR_NOT_READY), "no service yet");

    TestHost host;
    constexpr char envelope[] =
        R"json({"plugin":{},"instance":{"brightness":60,"restoreLogoOnExit":true,"pageButtons":"keyPages","keys":[)json"
        R"json({"page":0,"slot":0,"action":"page.next","label":"Next","icon":"ChevronRight"},)json"
        R"json({"page":0,"slot":1,"action":"system.launch","target":"https://example.org","label":"Web"},)json"
        R"json({"page":0,"slot":2,"action":"logicon.keyPage.next","label":"More"},)json"
        R"json({"page":1,"slot":0,"action":"widget.toggle","target":"0","label":"Toggle"},)json"
        R"json({"page":0,"slot":8,"face":"pageIndicator"}]}})json";
    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
    options.configurationJsonUtf8 = envelope;
    options.configurationBytes = static_cast<uint32_t>(sizeof(envelope) - 1);
    options.backgroundColor = kRedXeDefaultBackgroundColor;
    void* object = nullptr;
    LOGICON_CHECK(create(__uuidof(IRedXeWidgetProvider), &options, &host, Logicon::kPluginId, &object) == E_NOINTERFACE,
                  "the service plugin is not a widget provider");
    LOGICON_CHECK(SUCCEEDED(create(__uuidof(IRedXeService), &options, &host, Logicon::kPluginId, &object)) && object,
                  "service creates");
    wil::com_ptr_nothrow<IRedXeService> service;
    service.attach(static_cast<IRedXeService*>(object));
    wil::com_ptr_nothrow<IRedXeDeviceWorker> worker;
    LOGICON_CHECK(SUCCEEDED(service.query_to(worker.put())) && worker, "service exposes the device worker");
    wil::com_ptr_nothrow<IUnknown> identityA;
    wil::com_ptr_nothrow<IUnknown> identityB;
    LOGICON_CHECK(SUCCEEDED(service.query_to(identityA.put())) && SUCCEEDED(worker.query_to(identityB.put())) &&
                      identityA.get() == identityB.get(),
                  "one controlling IUnknown");

    RedXeFactoryOptions bad = options;
    constexpr char badEnvelope[] = R"json({"plugin":{},"instance":{"brightness":500}})json";
    bad.configurationJsonUtf8 = badEnvelope;
    bad.configurationBytes = static_cast<uint32_t>(sizeof(badEnvelope) - 1);
    void* rejected = nullptr;
    LOGICON_CHECK(FAILED(create(__uuidof(IRedXeService), &bad, &host, Logicon::kPluginId, &rejected)) && !rejected,
                  "an invalid envelope is rejected at create");

    LOGICON_CHECK(SUCCEEDED(diagnostics(&report)) && report.serviceStarted == 0 && report.laneRunning == 0,
                  "diagnostics before start");
    RedXeServiceStartContext start{};
    start.sizeBytes = sizeof(start);
    start.flags = RedXeServiceFlagDeviceAccessDisabled;
    LOGICON_CHECK(SUCCEEDED(service->Start(&start)), "service starts without device access");
    LOGICON_CHECK(SUCCEEDED(diagnostics(&report)) && report.serviceStarted == 1 && report.deviceAccess == 0,
                  "diagnostics after start");

    // Drive the lane the way the host does: one thread, host-owned stop and wake events.
    wil::unique_event_nothrow stop;
    wil::unique_event_nothrow wake;
    LOGICON_CHECK(SUCCEEDED(stop.create(wil::EventOptions::ManualReset)) && SUCCEEDED(wake.create()), "lane events");
    std::atomic<HRESULT> laneResult{E_PENDING};
    std::thread lane(
        [&]() noexcept
        {
            (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            laneResult.store(worker->RunDeviceWork(stop.get(), wake.get()), std::memory_order_release);
            CoUninitialize();
        });
    // A failed check returns early; the lane must still be stopped and joined before the service is released.
    const auto stopLane = wil::scope_exit(
        [&]() noexcept
        {
            SetEvent(stop.get());
            if (lane.joinable())
            {
                lane.join();
            }
        });
    LOGICON_CHECK(
        WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.laneRunning == 1; }, 3000),
        "lane reports running");
    LOGICON_CHECK(report.connected == 0, "no device without device access");

    // Host state feeds the page indicator face.
    const wchar_t pageName[] = L"Development";
    RedXeHostState state{};
    state.sizeBytes = sizeof(state);
    state.pageIndex = 1;
    state.pageCount = 3;
    state.pageId = "system";
    state.pageName = pageName;
    state.widgetCount = 4;
    state.flags = RedXeHostStateVisible;
    LOGICON_CHECK(SUCCEEDED(service->OnHostState(&state)), "host state accepted");
    LOGICON_CHECK(
        WaitUntil([&]() noexcept
                  { return SUCCEEDED(diagnostics(&report)) && report.hostPageIndex == 1 && report.hostPageCount == 3; },
                  2000),
        "lane picked up the host state");

    // Synthetic keypad: connect, faces, key presses, actions.
    LOGICON_CHECK(SUCCEEDED(useSynthetic(TRUE)), "synthetic device enabled");
    LOGICON_CHECK(
        WaitUntil([&]() noexcept
                  { return SUCCEEDED(diagnostics(&report)) && report.connected == 1 && report.synthetic == 1; }, 5000),
        "lane connected to the synthetic keypad");
    LOGICON_CHECK(
        WaitUntil(
            [&]() noexcept
            { return SUCCEEDED(diagnostics(&report)) && report.facesWritten >= 1 && report.syntheticImages >= 1; },
            5000),
        "faces were written after connect");
    LOGICON_CHECK(report.syntheticPageButtonsDiverted == 1, "page buttons diverted on the synthetic keypad");
    LOGICON_CHECK(report.syntheticBrightness == 60, "configured brightness applied");
    LOGICON_CHECK(report.rendererReady == 1, "renderer ready on the lane");

    const uint32_t actionsBefore = host.actions.load(std::memory_order_relaxed);
    LOGICON_CHECK(SUCCEEDED(inject(0, 0, TRUE)) && SUCCEEDED(inject(0, 0, FALSE)), "inject key 1 press");
    LOGICON_CHECK(
        WaitUntil([&]() noexcept { return host.actions.load(std::memory_order_relaxed) > actionsBefore; }, 2000) &&
            host.LastActionIs("page.next"),
        "key 1 requested page.next");
    LOGICON_CHECK(SUCCEEDED(inject(0, 1, TRUE)) && SUCCEEDED(inject(0, 1, FALSE)), "inject key 2 press");
    LOGICON_CHECK(WaitUntil([&]() noexcept { return host.LastActionIs("system.launch"); }, 2000) &&
                      host.LastTargetIs("https://example.org"),
                  "key 2 requested launch with its target");
    LOGICON_CHECK(SUCCEEDED(inject(0, 2, TRUE)) && SUCCEEDED(inject(0, 2, FALSE)), "inject key 3 press");
    LOGICON_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.keyPage == 1; }, 2000),
                  "key 3 switched to key page 2");
    const uint32_t imagesBefore = report.syntheticImages;
    LOGICON_CHECK(SUCCEEDED(inject(1, 0, TRUE)) && SUCCEEDED(inject(1, 0, FALSE)), "inject previous page button");
    LOGICON_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.keyPage == 0; }, 2000),
                  "page button went back to key page 1");
    LOGICON_CHECK(WaitUntil([&]() noexcept
                            { return SUCCEEDED(diagnostics(&report)) && report.syntheticImages > imagesBefore; }, 2000),
                  "the key page change repainted the faces");

    // Dialpad buttons are counted (no bindings yet); without device access neither the dialpad nor raw input opens.
    LOGICON_CHECK(SUCCEEDED(diagnostics(&report)) && report.dialpadConnected == 0 && report.wheelsListening == 0 &&
                      report.dialRaw == 0 && report.rollerRaw == 0,
                  "no dialpad and no raw input without device access");
    const uint32_t dialPressesBefore = report.dialButtonPresses;
    LOGICON_CHECK(FAILED(inject(2, 4, TRUE)), "dial button index 4 is rejected");
    LOGICON_CHECK(SUCCEEDED(inject(2, 2, TRUE)) && SUCCEEDED(inject(2, 2, FALSE)), "inject dialpad button 6");
    LOGICON_CHECK(
        WaitUntil([&]() noexcept
                  { return SUCCEEDED(diagnostics(&report)) && report.dialButtonPresses == dialPressesBefore + 1; },
                  2000),
        "the dialpad press was counted once");

    // Raw synthetic input reports drive the same path as injected controls.
    const uint32_t actionsBeforeRaw = host.actions.load(std::memory_order_relaxed);
    const auto injectRaw =
        Resolve<RedXeLogiconInjectSyntheticReportFn>(module.get(), kRedXeLogiconInjectSyntheticReportExport);
    LOGICON_CHECK(injectRaw != nullptr, "raw inject export");
    uint8_t rawDown[Logicon::kVlpControlReportBytes]{};
    rawDown[0] = Logicon::kReportVlpControl;
    rawDown[1] = 0xFF;
    rawDown[2] = 0x02;
    rawDown[5] = 0x01;
    rawDown[6] = 1;
    uint8_t rawUp[Logicon::kVlpControlReportBytes]{};
    std::memcpy(rawUp, rawDown, 6);
    LOGICON_CHECK(SUCCEEDED(injectRaw(rawDown, sizeof(rawDown))) && SUCCEEDED(injectRaw(rawUp, sizeof(rawUp))),
                  "raw reports queued");
    LOGICON_CHECK(
        WaitUntil([&]() noexcept { return host.actions.load(std::memory_order_relaxed) > actionsBeforeRaw; }, 2000) &&
            host.LastActionIs("page.next"),
        "raw key 1 report requested page.next");

    // Face overrides and brightness from the monitor/test surface.
    LOGICON_CHECK(SUCCEEDED(diagnostics(&report)), "diagnostics before override");
    const uint32_t generationBefore = report.faceGeneration;
    LOGICON_CHECK(SUCCEEDED(setOverride(4, 1, 0x00FF00)), "color override");
    LOGICON_CHECK(WaitUntil([&]() noexcept
                            { return SUCCEEDED(diagnostics(&report)) && report.faceGeneration > generationBefore; },
                            2000),
                  "override recomposed the faces");
    LOGICON_CHECK(SUCCEEDED(setOverride(4, 2, 0)) && SUCCEEDED(setOverride(4, 0, 0)), "picture and clear overrides");
    LOGICON_CHECK(setOverride(9, 1, 0) == E_INVALIDARG && setOverride(0, 3, 0) == E_INVALIDARG, "override bounds");
    LOGICON_CHECK(SUCCEEDED(setBrightness(25)), "brightness request");
    LOGICON_CHECK(
        WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.syntheticBrightness == 25; }, 2000),
        "brightness reached the synthetic keypad");
    LOGICON_CHECK(setBrightness(0) == E_INVALIDARG && setBrightness(101) == E_INVALIDARG, "brightness bounds");

    // Live settings apply: a new object re-parses and recomposes.
    constexpr char applied[] =
        R"json({"brightness":90,"keys":[{"slot":0,"action":"page.previous","label":"Back"}]})json";
    LOGICON_CHECK(SUCCEEDED(service->ApplySettings(applied, static_cast<uint32_t>(sizeof(applied) - 1))),
                  "settings apply");
    LOGICON_CHECK(
        WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.syntheticBrightness == 90; }, 2000),
        "applied brightness reached the keypad");
    constexpr char invalidApply[] = R"json({"keys":[{"slot":42}]})json";
    LOGICON_CHECK(FAILED(service->ApplySettings(invalidApply, static_cast<uint32_t>(sizeof(invalidApply) - 1))),
                  "invalid settings are rejected without stopping the service");
    LOGICON_CHECK(host.frames.load(std::memory_order_relaxed) == 0, "no frame requests without a monitor tile");

    // System Data faces: the feed subscribes only once a system face is bound, values reach the faces, and an
    // object without such faces pauses the subscriptions.
    LOGICON_CHECK(host.providerLookups.load(std::memory_order_relaxed) == 0 && !host.systemData.AnySink() &&
                      SUCCEEDED(diagnostics(&report)) && report.systemFeed == 0 && report.cpuPercent == -1,
                  "no provider lookup before a system face is bound");
    constexpr char systemApply[] =
        R"json({"keys":[{"slot":0,"face":"cpu"},{"slot":1,"face":"memory"},{"slot":2,"face":"gpu"}],
      "dialpad":{"turns":[{"control":"dial","direction":"cw","action":"page.next"}],"buttons":[{"button":1,"action":"page.next"}]}})json";
    LOGICON_CHECK(SUCCEEDED(service->ApplySettings(systemApply, static_cast<uint32_t>(sizeof(systemApply) - 1))),
                  "system faces apply");
    LOGICON_CHECK(host.providerLookups.load(std::memory_order_relaxed) == 1 &&
                      host.systemData.subscribed.load(std::memory_order_relaxed) == 3 && host.systemData.AllActive() &&
                      host.systemData.slots[0].interval == 1000,
                  "three subscriptions at one second, active");
    LOGICON_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.systemFeed == 1; }, 2000),
                  "diagnostics report the feed");
    const uint32_t imagesBeforeSystem = report.syntheticImages;
    RedXeDataValue cpuValue{sizeof(RedXeDataValue), RedXeDataValueTypeFloat64, RedXeDataQualityGood, {}, 0};
    cpuValue.float64Value = 42.0;
    LOGICON_CHECK(SUCCEEDED(host.systemData.Push(0, &cpuValue, 1)), "cpu snapshot delivered to the sink");
    LOGICON_CHECK(
        WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.cpuPercent == 42; }, 2000),
        "cpu value reached the service");
    LOGICON_CHECK(WaitUntil([&]() noexcept
                            { return SUCCEEDED(diagnostics(&report)) && report.syntheticImages > imagesBeforeSystem; },
                            2000),
                  "the cpu face repainted");
    LOGICON_CHECK(SUCCEEDED(host.systemData.Push(0, &cpuValue, 1)), "same value again");
    LOGICON_CHECK(SUCCEEDED(diagnostics(&report)) && report.memoryPercent == -1 && report.gpuPercent == -1,
                  "other metrics stay unknown");
    // A dialpad button press runs its binding like a key.
    const uint32_t actionsBeforeDial = host.actions.load(std::memory_order_relaxed);
    LOGICON_CHECK(SUCCEEDED(inject(2, 1, TRUE)) && SUCCEEDED(inject(2, 1, FALSE)), "inject dialpad Forward");
    LOGICON_CHECK(
        WaitUntil([&]() noexcept { return host.actions.load(std::memory_order_relaxed) > actionsBeforeDial; }, 2000) &&
            host.LastActionIs("page.next"),
        "dialpad Forward requested page.next");
    LOGICON_CHECK(SUCCEEDED(service->ApplySettings(applied, static_cast<uint32_t>(sizeof(applied) - 1))),
                  "settings without system faces apply");
    LOGICON_CHECK(!host.systemData.AnyActive() && host.systemData.AnySink(), "feed paused, subscriptions kept");
    LOGICON_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.systemFeed == 0; }, 2000),
                  "diagnostics report the paused feed");

    // Stop: the lane drains well inside the host's 3 s budget and restores the device.
    const ULONGLONG stopStarted = GetTickCount64();
    SetEvent(stop.get());
    lane.join();
    const ULONGLONG stopElapsed = GetTickCount64() - stopStarted;
    LOGICON_CHECK(SUCCEEDED(laneResult.load(std::memory_order_acquire)), "lane returned success");
    LOGICON_CHECK(stopElapsed < 1500, "lane drained within 1.5 s");
    LOGICON_CHECK(SUCCEEDED(diagnostics(&report)) && report.laneRunning == 0 && report.connected == 0 &&
                      report.syntheticResetSeen == 1 && report.syntheticPageButtonsDiverted == 0,
                  "lane stopped, flags restored, splash reset sent");
    LOGICON_CHECK(SUCCEEDED(service->Stop()) && SUCCEEDED(service->Stop()), "stop is idempotent");
    LOGICON_CHECK(!host.systemData.AnySink(), "stop released every System Data subscription");
    LOGICON_CHECK(host.logs.load(std::memory_order_relaxed) > 0, "the service logged lifetime events");
    identityA.reset();
    identityB.reset();
    worker.reset();
    service.reset();
    LOGICON_CHECK(diagnostics(&report) == HRESULT_FROM_WIN32(ERROR_NOT_READY), "service released");
    return S_OK;
}

[[nodiscard]] HRESULT Run() noexcept
{
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment))
    {
        return apartment;
    }
    struct Case final
    {
        const char* name;
        HRESULT (*function)() noexcept;
    };
    const Case cases[] = {
        {"protocol framing", TestProtocolFraming}, {"geometry and VLP stream", TestGeometryAndVlp},
        {"settings model", TestSettings},          {"key faces", TestFaces},
        {"device session", TestDeviceSession},     {"dialpad", TestDialpad},
        {"shipped module", TestShippedModule},
    };
    HRESULT result = S_OK;
    for (const Case& test : cases)
    {
        const HRESULT outcome = test.function();
        std::wprintf(L"%s %S\n", SUCCEEDED(outcome) ? L"PASS" : L"FAIL", test.name);
        if (FAILED(outcome) && SUCCEEDED(result))
        {
            result = outcome;
        }
    }
    CoUninitialize();
    return result;
}
} // namespace

int wmain() noexcept
{
    // Progress lines must reach a redirected console immediately so a hang is attributable to one case.
    (void)setvbuf(stdout, nullptr, _IONBF, 0);
    const HRESULT result = Run();
    if (FAILED(result))
    {
        std::wprintf(L"Logicon tests failed: 0x%08X\n", static_cast<unsigned int>(result));
        return 1;
    }
    std::wprintf(L"Logicon protocol, settings, face, device, dialpad, and module tests passed.\n");
    return 0;
}
