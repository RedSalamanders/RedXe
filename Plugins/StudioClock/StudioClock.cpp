#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Widget.h"

#include "StudioClockBackgroundPixelShader.h"
#include "StudioClockBackgroundVertexShader.h"
#include "StudioClockDotPixelShader.h"
#include "StudioClockDotVertexShader.h"
#include "StudioClockTestContract.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
constexpr char kPluginId[] = "builtin.studio-clock";
constexpr char kWidgetTypeId[] = "studio-clock";
constexpr char kSettingsSchema[] =
    R"json({"type":"object","additionalProperties":false,"properties":{"showSecondProgress":{"type":"boolean"},"externalDotsAlwaysOn":{"type":"boolean"},"showSeconds":{"type":"boolean"},"secondsColor":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},"showDate":{"type":"boolean"},"dateFormat":{"type":"string","enum":["dd-mm-yyyy","mm-dd-yyyy","yyyy-mm-dd"]},"timeColor":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},"glowPercent":{"type":"integer","minimum":0,"maximum":100}}})json";
constexpr char kSettingsDefaults[] =
    R"json({"showSecondProgress":true,"externalDotsAlwaysOn":true,"showSeconds":true,"secondsColor":"#FF1616","showDate":false,"dateFormat":"dd-mm-yyyy","timeColor":"#FF1616","glowPercent":35})json";
constexpr RedXePluginSettingsContract kSettingsContract{
    sizeof(RedXePluginSettingsContract), kSettingsSchema, sizeof(kSettingsSchema) - 1, kSettingsDefaults,
    sizeof(kSettingsDefaults) - 1,
};
constexpr uint32_t kTimeDotInstances = 114;
constexpr uint32_t kSecondsDotInstances = 42;
constexpr uint32_t kDateDotInstances = 174;
constexpr uint32_t kProgressDotInstances = 72;
constexpr uint32_t kMaximumDotInstances =
    kTimeDotInstances + kSecondsDotInstances + kDateDotInstances + kProgressDotInstances;
// A nonzero glow submits every dot twice in the same draw: one additive halo, then the LED core.
constexpr uint32_t kMaximumSubmittedInstances = kMaximumDotInstances * 2U;
constexpr uint32_t kMaximumGlowPercent = 100;
// Halo light at glowPercent 100, relative to the LED color, before the falloff. The default 35 keeps the gaps between
// the dots of a segment clearly darker than the dots, as on a real display; 100 merges each segment into a glowing bar.
constexpr float kFullGlowStrength = 0.6f;
// The halo quad spans this many LED radii from the dot center; the falloff reaches zero at its edge.
constexpr float kGlowExtentRadii = 4.0f;
constexpr float kDateCompositionHeightScale = 10.0f / 9.0f;
constexpr ULONGLONG kMaximumClockSampleIntervalMilliseconds = 1000;
constexpr ULONGLONG kLowCadenceFrameThresholdMilliseconds = 500;

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        kPluginId,
        L"Studio Clock",
        L"Low-wake dot-matrix local-time clock with configurable seconds progress and date.",
        L"RedXe",
        L"1.0.0",
        RedXePluginCapabilityWidgetProvider,
    },
};

constexpr std::array kSquareWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        kWidgetTypeId,
        L"Studio Clock",
        L"A scheduled Direct3D 11 dot-matrix studio clock.",
        720.0f,
        720.0f,
        160.0f,
        160.0f,
        RedXeWidgetFlagNone,
    },
};

constexpr std::array kDatedWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        kWidgetTypeId,
        L"Studio Clock",
        L"A scheduled Direct3D 11 dot-matrix studio clock.",
        720.0f,
        800.0f,
        160.0f,
        178.0f,
        RedXeWidgetFlagNone,
    },
};

enum class DateFormat : uint32_t
{
    DayMonthYear,
    MonthDayYear,
    YearMonthDay,
};

struct StudioClockConfiguration final
{
    bool showSecondProgress = true;
    bool externalDotsAlwaysOn = true;
    bool showSeconds = true;
    uint32_t secondsColor = 0xFF1616;
    bool showDate = false;
    DateFormat dateFormat = DateFormat::DayMonthYear;
    uint32_t timeColor = 0xFF1616;
    uint32_t glowPercent = 35;
    // Host-resolved dashboard background from RedXeFactoryOptions, never a settings member of this plugin.
    uint32_t backgroundColor = kRedXeDefaultBackgroundColor & 0x00FFFFFFu;
};

enum ConfigurationMember : uint32_t
{
    ConfigurationShowSecondProgress = 1U << 0U,
    ConfigurationExternalDotsAlwaysOn = 1U << 1U,
    ConfigurationShowSeconds = 1U << 2U,
    ConfigurationSecondsColor = 1U << 3U,
    ConfigurationShowDate = 1U << 4U,
    ConfigurationDateFormat = 1U << 5U,
    ConfigurationTimeColor = 1U << 6U,
    ConfigurationGlowPercent = 1U << 7U,
};

inline constexpr uint32_t kAllConfigurationMembers = (1U << 8U) - 1U;

std::atomic<uint32_t> gLiveProviderCount{0};
std::atomic<uint32_t> gLiveWidgetCount{0};
std::atomic<uint32_t> gLiveSharedDeviceResourceSetCount{0};
std::atomic<uint32_t> gLiveConstantBufferCount{0};
std::atomic<uint32_t> gLastMapCount{0};
std::atomic<uint32_t> gLastDrawCount{0};
std::atomic<uint32_t> gLastInstanceCount{0};
std::atomic<uint64_t> gTimeSampleCount{0};
std::atomic<uint64_t> gTestTimePacked{0};
std::atomic<uint64_t> gTestTimeRevision{0};

class JsonCursor final
{
  public:
    explicit JsonCursor(std::string_view text) noexcept : _text(text) {}

    void SkipWhitespace() noexcept
    {
        while (_offset < _text.size())
        {
            const char value = _text[_offset];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
            {
                break;
            }
            ++_offset;
        }
    }

    [[nodiscard]] bool Consume(char expected) noexcept
    {
        SkipWhitespace();
        if (_offset >= _text.size() || _text[_offset] != expected)
        {
            return false;
        }
        ++_offset;
        return true;
    }

    [[nodiscard]] bool ReadString(std::string_view& value) noexcept
    {
        SkipWhitespace();
        if (_offset >= _text.size() || _text[_offset] != '"')
        {
            return false;
        }
        const size_t start = ++_offset;
        while (_offset < _text.size() && _text[_offset] != '"')
        {
            const unsigned char character = static_cast<unsigned char>(_text[_offset]);
            if (character < 0x20U || character == '\\')
            {
                return false;
            }
            ++_offset;
        }
        if (_offset >= _text.size())
        {
            return false;
        }
        value = _text.substr(start, _offset - start);
        ++_offset;
        return true;
    }

    [[nodiscard]] bool ReadBoolean(bool& value) noexcept
    {
        SkipWhitespace();
        constexpr std::string_view trueText = "true";
        constexpr std::string_view falseText = "false";
        if (_text.substr(_offset, trueText.size()) == trueText)
        {
            _offset += trueText.size();
            value = true;
            return true;
        }
        if (_text.substr(_offset, falseText.size()) == falseText)
        {
            _offset += falseText.size();
            value = false;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool ReadUnsigned(uint32_t& value) noexcept
    {
        SkipWhitespace();
        if (_offset >= _text.size() || _text[_offset] < '0' || _text[_offset] > '9')
        {
            return false;
        }
        const bool leadingZero = _text[_offset] == '0';
        uint64_t parsed = 0;
        size_t digits = 0;
        while (_offset < _text.size() && _text[_offset] >= '0' && _text[_offset] <= '9')
        {
            parsed = parsed * 10U + static_cast<uint64_t>(_text[_offset] - '0');
            if (parsed > std::numeric_limits<uint32_t>::max())
            {
                return false;
            }
            ++_offset;
            ++digits;
        }
        if (leadingZero && digits != 1)
        {
            return false;
        }
        value = static_cast<uint32_t>(parsed);
        return true;
    }

    [[nodiscard]] bool AtEnd() noexcept
    {
        SkipWhitespace();
        return _offset == _text.size();
    }

  private:
    std::string_view _text;
    size_t _offset = 0;
};

[[nodiscard]] int HexDigitValue(char value) noexcept
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    return -1;
}

[[nodiscard]] bool ParseColor(std::string_view text, uint32_t& color) noexcept
{
    if (text.size() != 7 || text[0] != '#')
    {
        return false;
    }
    uint32_t parsed = 0;
    for (size_t index = 1; index < text.size(); ++index)
    {
        const int digit = HexDigitValue(text[index]);
        if (digit < 0)
        {
            return false;
        }
        parsed = (parsed << 4U) | static_cast<uint32_t>(digit);
    }
    color = parsed;
    return true;
}

[[nodiscard]] uint32_t ConfigurationMemberForKey(std::string_view key) noexcept
{
    constexpr std::array members{
        std::pair<std::string_view, uint32_t>{"showSecondProgress", ConfigurationShowSecondProgress},
        std::pair<std::string_view, uint32_t>{"externalDotsAlwaysOn", ConfigurationExternalDotsAlwaysOn},
        std::pair<std::string_view, uint32_t>{"showSeconds", ConfigurationShowSeconds},
        std::pair<std::string_view, uint32_t>{"secondsColor", ConfigurationSecondsColor},
        std::pair<std::string_view, uint32_t>{"showDate", ConfigurationShowDate},
        std::pair<std::string_view, uint32_t>{"dateFormat", ConfigurationDateFormat},
        std::pair<std::string_view, uint32_t>{"timeColor", ConfigurationTimeColor},
        std::pair<std::string_view, uint32_t>{"glowPercent", ConfigurationGlowPercent},
    };
    for (const auto& member : members)
    {
        if (member.first == key)
        {
            return member.second;
        }
    }
    return 0;
}

[[nodiscard]] bool ParseDateFormat(std::string_view value, DateFormat& format) noexcept
{
    if (value == "dd-mm-yyyy")
    {
        format = DateFormat::DayMonthYear;
        return true;
    }
    if (value == "mm-dd-yyyy")
    {
        format = DateFormat::MonthDayYear;
        return true;
    }
    if (value == "yyyy-mm-dd")
    {
        format = DateFormat::YearMonthDay;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseSettingsObject(JsonCursor& cursor, StudioClockConfiguration& configuration) noexcept
{
    if (!cursor.Consume('{'))
    {
        return false;
    }

    StudioClockConfiguration parsed{};
    uint32_t seen = 0;
    for (;;)
    {
        std::string_view key;
        if (!cursor.ReadString(key) || !cursor.Consume(':'))
        {
            return false;
        }
        const uint32_t member = ConfigurationMemberForKey(key);
        if (member == 0 || (seen & member) != 0)
        {
            return false;
        }
        seen |= member;

        bool booleanValue = false;
        uint32_t number = 0;
        std::string_view text;
        switch (member)
        {
        case ConfigurationShowSecondProgress:
            if (!cursor.ReadBoolean(booleanValue))
                return false;
            parsed.showSecondProgress = booleanValue;
            break;
        case ConfigurationExternalDotsAlwaysOn:
            if (!cursor.ReadBoolean(booleanValue))
                return false;
            parsed.externalDotsAlwaysOn = booleanValue;
            break;
        case ConfigurationShowSeconds:
            if (!cursor.ReadBoolean(booleanValue))
                return false;
            parsed.showSeconds = booleanValue;
            break;
        case ConfigurationSecondsColor:
            if (!cursor.ReadString(text) || !ParseColor(text, parsed.secondsColor))
                return false;
            break;
        case ConfigurationShowDate:
            if (!cursor.ReadBoolean(booleanValue))
                return false;
            parsed.showDate = booleanValue;
            break;
        case ConfigurationDateFormat:
            if (!cursor.ReadString(text) || !ParseDateFormat(text, parsed.dateFormat))
                return false;
            break;
        case ConfigurationTimeColor:
            if (!cursor.ReadString(text) || !ParseColor(text, parsed.timeColor))
                return false;
            break;
        case ConfigurationGlowPercent:
            if (!cursor.ReadUnsigned(number) || number > kMaximumGlowPercent)
                return false;
            parsed.glowPercent = number;
            break;
        default:
            return false;
        }

        if (cursor.Consume('}'))
        {
            break;
        }
        if (!cursor.Consume(','))
        {
            return false;
        }
    }

    if (seen != kAllConfigurationMembers)
    {
        return false;
    }
    configuration = parsed;
    return true;
}

[[nodiscard]] bool ParseNormalizedConfiguration(std::string_view json, StudioClockConfiguration& configuration) noexcept
{
    JsonCursor cursor(json);
    if (!cursor.Consume('{'))
    {
        return false;
    }

    constexpr uint32_t pluginSeen = 1U << 0U;
    constexpr uint32_t instanceSeen = 1U << 1U;
    uint32_t seen = 0;
    StudioClockConfiguration parsed{};
    for (;;)
    {
        std::string_view key;
        if (!cursor.ReadString(key) || !cursor.Consume(':'))
        {
            return false;
        }
        if (key == "plugin")
        {
            if ((seen & pluginSeen) != 0 || !cursor.Consume('{') || !cursor.Consume('}'))
            {
                return false;
            }
            seen |= pluginSeen;
        }
        else if (key == "instance")
        {
            if ((seen & instanceSeen) != 0 || !ParseSettingsObject(cursor, parsed))
            {
                return false;
            }
            seen |= instanceSeen;
        }
        else
        {
            return false;
        }

        if (cursor.Consume('}'))
        {
            break;
        }
        if (!cursor.Consume(','))
        {
            return false;
        }
    }
    if (seen != (pluginSeen | instanceSeen) || !cursor.AtEnd())
    {
        return false;
    }
    configuration = parsed;
    return true;
}

[[nodiscard]] HRESULT ReadFactoryConfiguration(const RedXeFactoryOptions* options,
                                               StudioClockConfiguration& configuration) noexcept
{
    configuration = StudioClockConfiguration{};
    if (!options)
    {
        return S_OK;
    }
    if (options->sizeBytes != sizeof(RedXeFactoryOptions))
    {
        return E_INVALIDARG;
    }
    configuration.backgroundColor = RedXeBackgroundRgb(options);
    const char* json = options->configurationJsonUtf8;
    const uint32_t bytes = options->configurationBytes;
    if (!json && bytes == 0)
    {
        return S_OK;
    }
    if (!json || bytes == 0 || bytes > kRedXeMaximumFactoryConfigurationBytes)
    {
        return E_INVALIDARG;
    }

    StudioClockConfiguration parsed{};
    const std::string_view text(json, bytes);
    if (!ParseNormalizedConfiguration(text, parsed))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    parsed.backgroundColor = configuration.backgroundColor;
    configuration = parsed;
    return S_OK;
}

[[nodiscard]] std::array<float, 4> ConvertColor(uint32_t color, float alpha = 1.0f) noexcept
{
    return {
        static_cast<float>((color >> 16U) & 0xFFU) / 255.0f,
        static_cast<float>((color >> 8U) & 0xFFU) / 255.0f,
        static_cast<float>(color & 0xFFU) / 255.0f,
        alpha,
    };
}

[[nodiscard]] uint64_t PackTime(const SYSTEMTIME& time) noexcept
{
    return static_cast<uint64_t>(time.wYear) | (static_cast<uint64_t>(time.wMonth) << 14U) |
           (static_cast<uint64_t>(time.wDay) << 18U) | (static_cast<uint64_t>(time.wHour) << 23U) |
           (static_cast<uint64_t>(time.wMinute) << 28U) | (static_cast<uint64_t>(time.wSecond) << 34U) |
           (static_cast<uint64_t>(time.wMilliseconds) << 40U);
}

[[nodiscard]] SYSTEMTIME UnpackTime(uint64_t packed) noexcept
{
    SYSTEMTIME time{};
    time.wYear = static_cast<WORD>(packed & 0x3FFFU);
    time.wMonth = static_cast<WORD>((packed >> 14U) & 0xFU);
    time.wDay = static_cast<WORD>((packed >> 18U) & 0x1FU);
    time.wHour = static_cast<WORD>((packed >> 23U) & 0x1FU);
    time.wMinute = static_cast<WORD>((packed >> 28U) & 0x3FU);
    time.wSecond = static_cast<WORD>((packed >> 34U) & 0x3FU);
    time.wMilliseconds = static_cast<WORD>((packed >> 40U) & 0x3FFU);
    return time;
}

[[nodiscard]] SYSTEMTIME ReadClockTime() noexcept
{
    gTimeSampleCount.fetch_add(1, std::memory_order_relaxed);
    const uint64_t packed = gTestTimePacked.load(std::memory_order_acquire);
    if (packed != 0)
    {
        return UnpackTime(packed);
    }
    SYSTEMTIME time{};
    GetLocalTime(&time);
    return time;
}

[[nodiscard]] uint32_t DelayToNextBoundary(const SYSTEMTIME& time, bool showSecondDetail) noexcept
{
    const uint32_t milliseconds = time.wMilliseconds < 1000 ? time.wMilliseconds : 999U;
    if (showSecondDetail)
    {
        return 1001U - milliseconds;
    }
    const uint32_t second = time.wSecond < 60 ? time.wSecond : 59U;
    return (60U - second) * 1000U - milliseconds + 1U;
}

struct alignas(16) StudioClockConstants final
{
    float backgroundColor[4];
    float timeColor[4];
    float secondsColor[4];
    float viewportAndOrigin[4];
    // Square size in pixels, glow strength, halo extent in LED radii (zero skips the halo pass), unused.
    float geometry[4];
    uint32_t timeDigits[4];
    uint32_t secondsAndFlags[4];
    uint32_t dateDigits0[4];
    uint32_t dateDigits1[4];
    uint32_t segmentCounts[4];
};

static_assert(sizeof(StudioClockConstants) == 160);
static_assert(sizeof(StudioClockConstants) <= 256);
static_assert(kMaximumDotInstances == 402);
static_assert(kMaximumSubmittedInstances == 804);

class StudioClockSharedResources final
{
  public:
    ~StudioClockSharedResources()
    {
        Reset();
    }

    [[nodiscard]] HRESULT Acquire(ID3D11Device* device) noexcept
    {
        if (!device)
        {
            return E_POINTER;
        }
        if (_deviceIdentity)
        {
            if (_deviceIdentity != device)
            {
                return E_UNEXPECTED;
            }
            ++_users;
            return S_OK;
        }

        wil::com_ptr_nothrow<ID3D11VertexShader> backgroundVertexShader;
        HRESULT result =
            device->CreateVertexShader(g_StudioClockBackgroundVertexShader, sizeof(g_StudioClockBackgroundVertexShader),
                                       nullptr, backgroundVertexShader.put());
        if (FAILED(result))
            return result;
        wil::com_ptr_nothrow<ID3D11PixelShader> backgroundPixelShader;
        result =
            device->CreatePixelShader(g_StudioClockBackgroundPixelShader, sizeof(g_StudioClockBackgroundPixelShader),
                                      nullptr, backgroundPixelShader.put());
        if (FAILED(result))
            return result;
        wil::com_ptr_nothrow<ID3D11VertexShader> dotVertexShader;
        result = device->CreateVertexShader(g_StudioClockDotVertexShader, sizeof(g_StudioClockDotVertexShader), nullptr,
                                            dotVertexShader.put());
        if (FAILED(result))
            return result;
        wil::com_ptr_nothrow<ID3D11PixelShader> dotPixelShader;
        result = device->CreatePixelShader(g_StudioClockDotPixelShader, sizeof(g_StudioClockDotPixelShader), nullptr,
                                           dotPixelShader.put());
        if (FAILED(result))
            return result;

        D3D11_RASTERIZER_DESC rasterizerDescription{};
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_NONE;
        rasterizerDescription.DepthClipEnable = TRUE;
        wil::com_ptr_nothrow<ID3D11RasterizerState> rasterizer;
        result = device->CreateRasterizerState(&rasterizerDescription, rasterizer.put());
        if (FAILED(result))
            return result;

        D3D11_DEPTH_STENCIL_DESC depthDescription{};
        depthDescription.DepthEnable = FALSE;
        depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
        wil::com_ptr_nothrow<ID3D11DepthStencilState> depthState;
        result = device->CreateDepthStencilState(&depthDescription, depthState.put());
        if (FAILED(result))
            return result;

        D3D11_BLEND_DESC opaqueDescription{};
        opaqueDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        wil::com_ptr_nothrow<ID3D11BlendState> opaqueBlend;
        result = device->CreateBlendState(&opaqueDescription, opaqueBlend.put());
        if (FAILED(result))
            return result;

        // Premultiplied: a core composites over what is below it, and a halo (zero alpha) adds light to it.
        D3D11_BLEND_DESC alphaDescription{};
        D3D11_RENDER_TARGET_BLEND_DESC& target = alphaDescription.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D11_BLEND_ONE;
        target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOp = D3D11_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D11_BLEND_ONE;
        target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        wil::com_ptr_nothrow<ID3D11BlendState> alphaBlend;
        result = device->CreateBlendState(&alphaDescription, alphaBlend.put());
        if (FAILED(result))
            return result;

        _deviceIdentity = device;
        _backgroundVertexShader = std::move(backgroundVertexShader);
        _backgroundPixelShader = std::move(backgroundPixelShader);
        _dotVertexShader = std::move(dotVertexShader);
        _dotPixelShader = std::move(dotPixelShader);
        _rasterizer = std::move(rasterizer);
        _depthState = std::move(depthState);
        _opaqueBlend = std::move(opaqueBlend);
        _alphaBlend = std::move(alphaBlend);
        _users = 1;
        gLiveSharedDeviceResourceSetCount.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
    }

    void Release() noexcept
    {
        if (_users == 0)
        {
            return;
        }
        --_users;
        if (_users == 0)
        {
            Reset();
        }
    }

    [[nodiscard]] HRESULT Render(ID3D11DeviceContext* context, ID3D11Buffer* constantBuffer,
                                 const StudioClockConstants& constants, bool uploadConstants,
                                 uint32_t instanceCount) noexcept
    {
        if (!context || !constantBuffer || !_deviceIdentity)
        {
            return E_UNEXPECTED;
        }

        uint32_t mapCount = 0;
        if (uploadConstants)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const HRESULT result = context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(result))
            {
                return result;
            }
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            context->Unmap(constantBuffer, 0);
            mapCount = 1;
        }

        constexpr std::array blendFactor{0.0f, 0.0f, 0.0f, 0.0f};
        ID3D11Buffer* constantBuffers[] = {constantBuffer};
        context->IASetInputLayout(nullptr);
        context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
        context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->CSSetShader(nullptr, nullptr, 0);
        context->RSSetState(_rasterizer.get());
        context->OMSetDepthStencilState(_depthState.get(), 0);
        context->VSSetConstantBuffers(0, 1, constantBuffers);
        context->PSSetConstantBuffers(0, 1, constantBuffers);

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(_backgroundVertexShader.get(), nullptr, 0);
        context->PSSetShader(_backgroundPixelShader.get(), nullptr, 0);
        context->OMSetBlendState(_opaqueBlend.get(), blendFactor.data(), UINT_MAX);
        context->Draw(3, 0);

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(_dotVertexShader.get(), nullptr, 0);
        context->PSSetShader(_dotPixelShader.get(), nullptr, 0);
        context->OMSetBlendState(_alphaBlend.get(), blendFactor.data(), UINT_MAX);
        context->DrawInstanced(4, instanceCount, 0, 0);

        gLastMapCount.store(mapCount, std::memory_order_relaxed);
        gLastDrawCount.store(2, std::memory_order_relaxed);
        gLastInstanceCount.store(instanceCount, std::memory_order_relaxed);
        return S_OK;
    }

  private:
    void Reset() noexcept
    {
        if (_deviceIdentity)
        {
            gLiveSharedDeviceResourceSetCount.fetch_sub(1, std::memory_order_relaxed);
        }
        _alphaBlend.reset();
        _opaqueBlend.reset();
        _depthState.reset();
        _rasterizer.reset();
        _dotPixelShader.reset();
        _dotVertexShader.reset();
        _backgroundPixelShader.reset();
        _backgroundVertexShader.reset();
        _deviceIdentity = nullptr;
        _users = 0;
    }

    ID3D11Device* _deviceIdentity = nullptr;
    uint32_t _users = 0;
    wil::com_ptr_nothrow<ID3D11VertexShader> _backgroundVertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _backgroundPixelShader;
    wil::com_ptr_nothrow<ID3D11VertexShader> _dotVertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _dotPixelShader;
    wil::com_ptr_nothrow<ID3D11RasterizerState> _rasterizer;
    wil::com_ptr_nothrow<ID3D11DepthStencilState> _depthState;
    wil::com_ptr_nothrow<ID3D11BlendState> _opaqueBlend;
    wil::com_ptr_nothrow<ID3D11BlendState> _alphaBlend;
};

// The three COM base subobjects precede the 16-byte-aligned HLSL constant record, so MSVC reports intentional
// object padding even though the record itself has an exact asserted layout.
#pragma warning(push)
#pragma warning(disable : 4324)
class StudioClockWidget final
    : public RedXeComObject<StudioClockWidget, IRedXeWidget, IRedXeGpuWidget, IRedXeScheduledWidget, IRedXeRaisedWidget>
{
  public:
    StudioClockWidget(wil::com_ptr_nothrow<IRedXeWidgetProvider>&& providerOwner,
                      const StudioClockConfiguration& configuration, StudioClockSharedResources& resources) noexcept
        : _providerOwner(std::move(providerOwner)), _configuration(configuration), _resources(&resources),
          _timeColor(ConvertColor(configuration.timeColor)), _secondsColor(ConvertColor(configuration.secondsColor)),
          _backgroundColor(ConvertColor(configuration.backgroundColor)),
          _glowStrength(kFullGlowStrength * static_cast<float>(configuration.glowPercent) /
                        static_cast<float>(kMaximumGlowPercent)),
          _glowExtent(configuration.glowPercent != 0 ? kGlowExtentRadii : 0.0f)
    {
        gLiveWidgetCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~StudioClockWidget()
    {
        OnDeviceLost();
        gLiveWidgetCount.fetch_sub(1, std::memory_order_relaxed);
    }

    HRESULT STDMETHODCALLTYPE SetVisible(BOOL) noexcept override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CollectPersistentSettings(char* jsonUtf8, uint32_t capacityBytes,
                                                        uint32_t* writtenBytes) noexcept override
    {
        return RedXeCollectNoPersistentSettings(jsonUtf8, capacityBytes, writtenBytes);
    }

    HRESULT STDMETHODCALLTYPE GetRaisedExtent(RedXeRaisedExtent* extent) noexcept override
    {
        if (!extent)
        {
            return E_POINTER;
        }
        *extent = RedXeRaisedExtentHalf;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetRaised(BOOL) noexcept override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceCreated(const RedXeGpuDeviceContext* context) noexcept override
    {
        if (!context)
        {
            return E_POINTER;
        }
        if (context->sizeBytes != sizeof(RedXeGpuDeviceContext))
        {
            return E_INVALIDARG;
        }
        if (!context->device)
        {
            return E_POINTER;
        }
        if (_deviceAttached)
        {
            return E_UNEXPECTED;
        }
        if (context->featureLevel < D3D_FEATURE_LEVEL_11_0 || context->targetFormat == DXGI_FORMAT_UNKNOWN)
        {
            return DXGI_ERROR_UNSUPPORTED;
        }

        HRESULT result = _resources->Acquire(context->device);
        if (FAILED(result))
        {
            return result;
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof(StudioClockConstants);
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        wil::com_ptr_nothrow<ID3D11Buffer> constantBuffer;
        result = context->device->CreateBuffer(&description, nullptr, constantBuffer.put());
        if (FAILED(result))
        {
            _resources->Release();
            return result;
        }
        _constantBuffer = std::move(constantBuffer);
        _deviceAttached = true;
        _constantsDirty = true;
        gLiveConstantBufferCount.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
    }

    void STDMETHODCALLTYPE OnDeviceLost() noexcept override
    {
        if (!_deviceAttached)
        {
            return;
        }
        _constantBuffer.reset();
        gLiveConstantBufferCount.fetch_sub(1, std::memory_order_relaxed);
        _resources->Release();
        _deviceAttached = false;
        _constantsDirty = true;
    }

    HRESULT STDMETHODCALLTYPE OnTargetSizeChanged(const RedXeGpuTargetSizeContext* context) noexcept override
    {
        if (!context || context->sizeBytes != sizeof(RedXeGpuTargetSizeContext))
        {
            return E_INVALIDARG;
        }
        // Dots and segments are generated geometry scaled per frame from the frame context; there is no glyph atlas or
        // other resolution-dependent resource to rebuild.
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Render(const RedXeGpuFrameContext* context) noexcept override
    {
        if (!context)
        {
            return E_POINTER;
        }
        if (context->sizeBytes != sizeof(RedXeGpuFrameContext))
        {
            return E_INVALIDARG;
        }
        if (!context->widget || !context->deviceContext)
        {
            return E_POINTER;
        }
        const RedXeWidgetFrameContext& frame = *context->widget;
        if (frame.sizeBytes != sizeof(RedXeWidgetFrameContext) || frame.dpi == 0 ||
            !std::isfinite(frame.elapsedSeconds) || !std::isfinite(frame.deltaSeconds) || frame.elapsedSeconds < 0.0f ||
            frame.deltaSeconds < 0.0f || !std::isfinite(context->viewport.TopLeftX) ||
            !std::isfinite(context->viewport.TopLeftY) || !std::isfinite(context->viewport.Width) ||
            !std::isfinite(context->viewport.Height) || !std::isfinite(context->viewport.MinDepth) ||
            !std::isfinite(context->viewport.MaxDepth) || context->viewport.Width < 0.0f ||
            context->viewport.Height < 0.0f || context->viewport.MinDepth < 0.0f || context->viewport.MaxDepth > 1.0f ||
            context->viewport.MinDepth > context->viewport.MaxDepth)
        {
            return E_INVALIDARG;
        }
        if (frame.widthPixels == 0 || frame.heightPixels == 0 || context->viewport.Width == 0.0f ||
            context->viewport.Height == 0.0f)
        {
            return S_OK;
        }
        if (!_deviceAttached || !_constantBuffer)
        {
            return E_UNEXPECTED;
        }

        const ULONGLONG renderTick = GetTickCount64();
        const bool lowCadenceFrame =
            _lastRenderTick == 0 || renderTick - _lastRenderTick >= kLowCadenceFrameThresholdMilliseconds;
        _lastRenderTick = renderTick;
        SampleTime(renderTick, lowCadenceFrame);
        if (_width != frame.widthPixels || _height != frame.heightPixels || _dpi != frame.dpi)
        {
            _width = frame.widthPixels;
            _height = frame.heightPixels;
            _dpi = frame.dpi;
            _constantsDirty = true;
        }
        if (_constantsDirty)
        {
            BuildConstants();
        }
        const bool upload = _constantsDirty;
        const HRESULT result =
            _resources->Render(context->deviceContext, _constantBuffer.get(), _constants, upload, _instanceCount);
        if (SUCCEEDED(result))
        {
            _constantsDirty = false;
        }
        return result;
    }

    HRESULT STDMETHODCALLTYPE GetNextFrameDelayMilliseconds(uint32_t* delayMilliseconds) noexcept override
    {
        if (!delayMilliseconds)
        {
            return E_POINTER;
        }
        *delayMilliseconds = 0;
        const SYSTEMTIME time = ReadClockTime();
        if (_hasTime && VisualTimeChanged(time))
        {
            _time = time;
            _hasTime = true;
            _constantsDirty = true;
            _nextSampleTick = GetTickCount64();
            *delayMilliseconds = 1;
            return S_OK;
        }
        *delayMilliseconds = DelayToNextBoundary(time, ShowsSecondDetail());
        return S_OK;
    }

  private:
    [[nodiscard]] bool ShowsSecondDetail() const noexcept
    {
        return _configuration.showSeconds || _configuration.showSecondProgress;
    }

    [[nodiscard]] bool VisualTimeChanged(const SYSTEMTIME& value) const noexcept
    {
        if (!_hasTime || value.wHour != _time.wHour || value.wMinute != _time.wMinute)
        {
            return true;
        }
        if (ShowsSecondDetail() && value.wSecond != _time.wSecond)
        {
            return true;
        }
        return _configuration.showDate &&
               (value.wYear != _time.wYear || value.wMonth != _time.wMonth || value.wDay != _time.wDay);
    }

    void SampleTime(ULONGLONG now, bool forceSample) noexcept
    {
        const uint64_t testTimeRevision = gTestTimeRevision.load(std::memory_order_acquire);
        const bool testTimeChanged = testTimeRevision != _lastTestTimeRevision;
        const bool sampleDue = forceSample || !_hasTime || now >= _nextSampleTick;
        if (!testTimeChanged && !sampleDue)
        {
            return;
        }

        const SYSTEMTIME sampled = ReadClockTime();
        if (VisualTimeChanged(sampled))
        {
            _constantsDirty = true;
        }
        _time = sampled;
        _hasTime = true;
        _lastTestTimeRevision = testTimeRevision;
        const uint32_t boundary = DelayToNextBoundary(sampled, ShowsSecondDetail());
        const uint32_t sampleInterval = boundary > 1 ? boundary - 1 : 1;
        _nextSampleTick =
            now + std::min(sampleInterval, static_cast<uint32_t>(kMaximumClockSampleIntervalMilliseconds));
    }

    static void SplitTwoDigits(uint32_t value, uint32_t& first, uint32_t& second) noexcept
    {
        first = value / 10U;
        second = value % 10U;
    }

    void BuildDateDigits(std::array<uint32_t, 8>& digits) const noexcept
    {
        std::array<uint32_t, 2> day{};
        std::array<uint32_t, 2> month{};
        SplitTwoDigits(_time.wDay, day[0], day[1]);
        SplitTwoDigits(_time.wMonth, month[0], month[1]);
        const std::array year{
            static_cast<uint32_t>(_time.wYear / 1000U),
            static_cast<uint32_t>((_time.wYear / 100U) % 10U),
            static_cast<uint32_t>((_time.wYear / 10U) % 10U),
            static_cast<uint32_t>(_time.wYear % 10U),
        };
        if (_configuration.dateFormat == DateFormat::YearMonthDay)
        {
            digits = {year[0], year[1], year[2], year[3], month[0], month[1], day[0], day[1]};
        }
        else if (_configuration.dateFormat == DateFormat::MonthDayYear)
        {
            digits = {month[0], month[1], day[0], day[1], year[0], year[1], year[2], year[3]};
        }
        else
        {
            digits = {day[0], day[1], month[0], month[1], year[0], year[1], year[2], year[3]};
        }
    }

    void BuildConstants() noexcept
    {
        std::array<uint32_t, 4> timeDigits{};
        SplitTwoDigits(_time.wHour, timeDigits[0], timeDigits[1]);
        SplitTwoDigits(_time.wMinute, timeDigits[2], timeDigits[3]);
        std::array<uint32_t, 2> secondsDigits{};
        SplitTwoDigits(_time.wSecond, secondsDigits[0], secondsDigits[1]);
        std::array<uint32_t, 8> dateDigits{};
        BuildDateDigits(dateDigits);

        const float viewportWidth = static_cast<float>(_width);
        const float viewportHeight = static_cast<float>(_height);
        const float heightLimitedSquare =
            _configuration.showDate ? viewportHeight / kDateCompositionHeightScale : viewportHeight;
        const float squareSize = viewportWidth < heightLimitedSquare ? viewportWidth : heightLimitedSquare;
        const float compositionHeight = squareSize * (_configuration.showDate ? kDateCompositionHeightScale : 1.0f);
        const float originX = (static_cast<float>(_width) - squareSize) * 0.5f;
        const float originY = (viewportHeight - compositionHeight) * 0.5f;
        const uint32_t secondsCount = _configuration.showSeconds ? kSecondsDotInstances : 0;
        const uint32_t dateCount = _configuration.showDate ? kDateDotInstances : 0;
        const uint32_t progressCount = _configuration.showSecondProgress ? kProgressDotInstances : 0;

        _constants = StudioClockConstants{
            {_backgroundColor[0], _backgroundColor[1], _backgroundColor[2], _backgroundColor[3]},
            {_timeColor[0], _timeColor[1], _timeColor[2], _timeColor[3]},
            {_secondsColor[0], _secondsColor[1], _secondsColor[2], _secondsColor[3]},
            {static_cast<float>(_width), static_cast<float>(_height), originX, originY},
            {squareSize, _glowStrength, _glowExtent, 0.0f},
            {timeDigits[0], timeDigits[1], timeDigits[2], timeDigits[3]},
            {secondsDigits[0], secondsDigits[1], _time.wSecond, _configuration.externalDotsAlwaysOn ? 1U : 0U},
            {dateDigits[0], dateDigits[1], dateDigits[2], dateDigits[3]},
            {dateDigits[4], dateDigits[5], dateDigits[6], dateDigits[7]},
            {kTimeDotInstances, secondsCount, dateCount, progressCount},
        };
        const uint32_t dotCount = kTimeDotInstances + secondsCount + dateCount + progressCount;
        _instanceCount = _glowExtent > 0.0f ? dotCount * 2U : dotCount;
    }

    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    StudioClockConfiguration _configuration;
    StudioClockSharedResources* _resources;
    std::array<float, 4> _timeColor;
    std::array<float, 4> _secondsColor;
    std::array<float, 4> _backgroundColor;
    float _glowStrength;
    float _glowExtent;
    wil::com_ptr_nothrow<ID3D11Buffer> _constantBuffer;
    StudioClockConstants _constants{};
    SYSTEMTIME _time{};
    ULONGLONG _nextSampleTick = 0;
    ULONGLONG _lastRenderTick = 0;
    uint64_t _lastTestTimeRevision = 0;
    uint32_t _width = 0;
    uint32_t _height = 0;
    uint32_t _dpi = 0;
    uint32_t _instanceCount = 0;
    bool _hasTime = false;
    bool _deviceAttached = false;
    bool _constantsDirty = true;
};
#pragma warning(pop)

class StudioClockProvider final : public RedXeComObject<StudioClockProvider, IRedXeWidgetProvider>
{
  public:
    explicit StudioClockProvider(const StudioClockConfiguration& configuration) noexcept : _configuration(configuration)
    {
        gLiveProviderCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~StudioClockProvider()
    {
        gLiveProviderCount.fetch_sub(1, std::memory_order_relaxed);
    }

    HRESULT STDMETHODCALLTYPE GetWidgetTypes(const RedXeWidgetTypeDescriptor** descriptors,
                                             uint32_t* count) noexcept override
    {
        if (descriptors)
            *descriptors = nullptr;
        if (count)
            *count = 0;
        if (!descriptors || !count)
            return E_POINTER;
        const auto& widgetTypes = _configuration.showDate ? kDatedWidgetTypes : kSquareWidgetTypes;
        *descriptors = widgetTypes.data();
        *count = static_cast<uint32_t>(widgetTypes.size());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateWidget(const char* typeId, const char* instanceId,
                                           IRedXeWidget** widget) noexcept override
    {
        if (!widget)
            return E_POINTER;
        *widget = nullptr;
        if (!typeId || !instanceId || instanceId[0] == '\0')
            return E_INVALIDARG;
        if (!RedXeAsciiEqualsIgnoreCase(typeId, kWidgetTypeId))
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

        wil::com_ptr_nothrow<IRedXeWidgetProvider> providerOwner;
        HRESULT result = QueryInterface(__uuidof(IRedXeWidgetProvider), reinterpret_cast<void**>(providerOwner.put()));
        if (FAILED(result))
            return result;
        auto* created = new (std::nothrow) StudioClockWidget(std::move(providerOwner), _configuration, _resources);
        if (!created)
            return E_OUTOFMEMORY;
        *widget = static_cast<IRedXeWidget*>(created);
        return S_OK;
    }

  private:
    StudioClockConfiguration _configuration;
    StudioClockSharedResources _resources;
};

static_assert(sizeof(StudioClockSharedResources) < 16U * 1024U);
static_assert(sizeof(StudioClockProvider) < 16U * 1024U);
static_assert(sizeof(StudioClockWidget) < 2U * 1024U);

HRESULT CreateStudioClockProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost*,
                                  void** result) noexcept
{
    if (interfaceId != __uuidof(IRedXeWidgetProvider))
    {
        return E_NOINTERFACE;
    }
    StudioClockConfiguration configuration{};
    const HRESULT configurationResult = ReadFactoryConfiguration(options, configuration);
    if (FAILED(configurationResult))
    {
        return configurationResult;
    }
    auto* provider = new (std::nothrow) StudioClockProvider(configuration);
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = provider;
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateStudioClockProvider},
};
} // namespace

extern "C" HRESULT __stdcall RedXeCreate(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                         const char* pluginId, void** result) noexcept
{
    return RedXeCreateFromFactoryEntries(kFactoryEntries.data(), static_cast<uint32_t>(kFactoryEntries.size()),
                                         interfaceId, options, host, pluginId, result);
}

extern "C" HRESULT __stdcall RedXeEnumeratePlugins(const RedXePluginMetadata** metadata, uint32_t* count) noexcept
{
    return RedXeEnumerateFactoryMetadata(kMetadata.data(), static_cast<uint32_t>(kMetadata.size()), metadata, count);
}

extern "C" HRESULT __stdcall RedXeGetPluginSettingsContract(const char* pluginId,
                                                            const RedXePluginSettingsContract** contract) noexcept
{
    return RedXeGetStaticPluginSettingsContract(kPluginId, pluginId, &kSettingsContract, contract);
}

extern "C" void __stdcall RedXePluginShutdown() noexcept
{
    gTestTimePacked.store(0, std::memory_order_release);
    gTestTimeRevision.fetch_add(1, std::memory_order_release);
}

extern "C" HRESULT __stdcall RedXeStudioClockSetTestTime(const StudioClockTestTime* testTime) noexcept
{
    if (!testTime)
    {
        gTestTimePacked.store(0, std::memory_order_release);
        gTestTimeRevision.fetch_add(1, std::memory_order_release);
        return S_OK;
    }
    if (testTime->sizeBytes != sizeof(StudioClockTestTime))
    {
        return E_INVALIDARG;
    }
    SYSTEMTIME time{};
    time.wYear = testTime->year;
    time.wMonth = testTime->month;
    time.wDay = testTime->day;
    time.wHour = testTime->hour;
    time.wMinute = testTime->minute;
    time.wSecond = testTime->second;
    time.wMilliseconds = testTime->milliseconds;
    FILETIME fileTime{};
    if (!SystemTimeToFileTime(&time, &fileTime) || time.wSecond > 59 || time.wMilliseconds > 999)
    {
        return E_INVALIDARG;
    }
    gTestTimePacked.store(PackTime(time), std::memory_order_release);
    gTestTimeRevision.fetch_add(1, std::memory_order_release);
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeStudioClockGetTestDiagnostics(StudioClockTestDiagnostics* diagnostics) noexcept
{
    if (!diagnostics)
    {
        return E_POINTER;
    }
    if (diagnostics->sizeBytes != sizeof(StudioClockTestDiagnostics))
    {
        return E_INVALIDARG;
    }
    diagnostics->liveProviderCount = gLiveProviderCount.load(std::memory_order_relaxed);
    diagnostics->liveWidgetCount = gLiveWidgetCount.load(std::memory_order_relaxed);
    diagnostics->liveSharedDeviceResourceSetCount = gLiveSharedDeviceResourceSetCount.load(std::memory_order_relaxed);
    diagnostics->liveConstantBufferCount = gLiveConstantBufferCount.load(std::memory_order_relaxed);
    diagnostics->lastMapCount = gLastMapCount.load(std::memory_order_relaxed);
    diagnostics->lastDrawCount = gLastDrawCount.load(std::memory_order_relaxed);
    diagnostics->lastInstanceCount = gLastInstanceCount.load(std::memory_order_relaxed);
    diagnostics->timeSampleCount = gTimeSampleCount.load(std::memory_order_relaxed);
    return S_OK;
}
