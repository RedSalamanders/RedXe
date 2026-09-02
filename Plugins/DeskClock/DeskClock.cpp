#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Widget.h"

#include "DeskClockBackgroundPixelShader.h"
#include "DeskClockBackgroundVertexShader.h"
#include "DeskClockPixelShader.h"
#include "DeskClockTestContract.h"
#include "DeskClockVertexShader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dwrite.h>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr char kPluginId[] = "builtin.desk-clock";
constexpr char kWidgetTypeId[] = "desk-clock";
constexpr char kSettingsSchema[] =
    R"json({"type":"object","additionalProperties":false,"properties":{"flipDurationMilliseconds":{"type":"integer","minimum":250,"maximum":800},"backgroundColor":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},"cardColor":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},"digitColor":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},"dateColor":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"}}})json";
constexpr char kSettingsDefaults[] =
    R"json({"flipDurationMilliseconds":420,"backgroundColor":"#000000","cardColor":"#FF3B43","digitColor":"#FFFFFF","dateColor":"#D8D8D8"})json";
constexpr RedXePluginSettingsContract kSettingsContract{
    sizeof(RedXePluginSettingsContract), kSettingsSchema, sizeof(kSettingsSchema) - 1, kSettingsDefaults,
    sizeof(kSettingsDefaults) - 1,
};

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        kPluginId,
        L"Desk Clock",
        L"A clean local-time split-flap clock with a synchronized Direct3D 11 transition.",
        L"RedXe",
        L"1.0.0",
        RedXePluginCapabilityWidgetProvider,
    },
};

constexpr std::array kWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        kWidgetTypeId,
        L"Desk Clock",
        L"A scheduled 24-hour split-flap clock with seconds and an invariant-English date.",
        1600.0f,
        600.0f,
        320.0f,
        120.0f,
        RedXeWidgetFlagNone,
    },
};

struct DeskClockConfiguration final
{
    uint32_t flipDurationMilliseconds = 420;
    uint32_t backgroundColor = 0x000000;
    uint32_t cardColor = 0xFF3B43;
    uint32_t digitColor = 0xFFFFFF;
    uint32_t dateColor = 0xD8D8D8;
};

enum ConfigurationMember : uint32_t
{
    ConfigurationDuration = 1U << 0U,
    ConfigurationBackground = 1U << 1U,
    ConfigurationCard = 1U << 2U,
    ConfigurationDigit = 1U << 3U,
    ConfigurationDate = 1U << 4U,
};

inline constexpr uint32_t kAllConfigurationMembers = (1U << 5U) - 1U;
inline constexpr uint32_t kDateGlyphCapacity = 10;
inline constexpr uint32_t kNoGlyph = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t kTimeGlyphCount = 10;
inline constexpr uint32_t kDateDigitOffset = kTimeGlyphCount;
inline constexpr uint32_t kDateGlyphCount = 62;
inline constexpr uint32_t kGlyphCount = kDateDigitOffset + kDateGlyphCount;
inline constexpr uint32_t kGlyphAtlasSize = 1024;
inline constexpr uint32_t kGlyphAtlasBytes = kGlyphAtlasSize * kGlyphAtlasSize;
inline constexpr uint32_t kTimeCellWidth = 192;
inline constexpr uint32_t kTimeCellHeight = 288;
inline constexpr uint32_t kTimeCellColumns = 5;
inline constexpr uint32_t kDateCellSize = 64;
inline constexpr uint32_t kDateCellColumns = 16;
inline constexpr uint32_t kDateAtlasTop = 640;
inline constexpr float kTimeFontEmSize = 270.0f;
inline constexpr float kDateFontEmSize = 64.0f;
inline constexpr float kDateBaselineX = 4.0f;

std::atomic<uint32_t> gLiveProviderCount{0};
std::atomic<uint32_t> gLiveWidgetCount{0};
std::atomic<uint32_t> gLiveDeviceResourceSetCount{0};
std::atomic<uint64_t> gTimeSampleCount{0};
std::atomic<uint64_t> gConstantUploadCount{0};
std::atomic<uint64_t> gDrawCallCount{0};
std::atomic<uint64_t> gScheduleQueryCount{0};
std::atomic<uint64_t> gTypographyBuildCount{0};
std::atomic<uint64_t> gTestTime{0};

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
    if (key == "flipDurationMilliseconds")
    {
        return ConfigurationDuration;
    }
    if (key == "backgroundColor")
    {
        return ConfigurationBackground;
    }
    if (key == "cardColor")
    {
        return ConfigurationCard;
    }
    if (key == "digitColor")
    {
        return ConfigurationDigit;
    }
    if (key == "dateColor")
    {
        return ConfigurationDate;
    }
    return 0;
}

[[nodiscard]] bool ParseSettingsObject(JsonCursor& cursor, DeskClockConfiguration& configuration) noexcept
{
    if (!cursor.Consume('{'))
    {
        return false;
    }
    DeskClockConfiguration parsed{};
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

        uint32_t number = 0;
        std::string_view text;
        switch (member)
        {
        case ConfigurationDuration:
            if (!cursor.ReadUnsigned(number) || number < 250 || number > 800)
            {
                return false;
            }
            parsed.flipDurationMilliseconds = number;
            break;
        case ConfigurationBackground:
            if (!cursor.ReadString(text) || !ParseColor(text, parsed.backgroundColor))
            {
                return false;
            }
            break;
        case ConfigurationCard:
            if (!cursor.ReadString(text) || !ParseColor(text, parsed.cardColor))
            {
                return false;
            }
            break;
        case ConfigurationDigit:
            if (!cursor.ReadString(text) || !ParseColor(text, parsed.digitColor))
            {
                return false;
            }
            break;
        case ConfigurationDate:
            if (!cursor.ReadString(text) || !ParseColor(text, parsed.dateColor))
            {
                return false;
            }
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

[[nodiscard]] bool ParseNormalizedConfiguration(std::string_view json, DeskClockConfiguration& configuration) noexcept
{
    JsonCursor cursor(json);
    if (!cursor.Consume('{'))
    {
        return false;
    }
    constexpr uint32_t pluginSeen = 1U << 0U;
    constexpr uint32_t instanceSeen = 1U << 1U;
    uint32_t seen = 0;
    DeskClockConfiguration parsed{};
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
                                               DeskClockConfiguration& configuration) noexcept
{
    configuration = DeskClockConfiguration{};
    if (!options)
    {
        return S_OK;
    }
    if (options->sizeBytes != sizeof(RedXeFactoryOptions))
    {
        return E_INVALIDARG;
    }
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
    DeskClockConfiguration parsed{};
    const std::string_view text(json, bytes);
    if (!ParseNormalizedConfiguration(text, parsed))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    configuration = parsed;
    return S_OK;
}

[[nodiscard]] std::array<float, 4> ColorToFloat(uint32_t color) noexcept
{
    constexpr float scale = 1.0f / 255.0f;
    return {
        static_cast<float>((color >> 16U) & 0xFFU) * scale,
        static_cast<float>((color >> 8U) & 0xFFU) * scale,
        static_cast<float>(color & 0xFFU) * scale,
        1.0f,
    };
}

struct alignas(16) DeskClockConstants final
{
    float targetSize[4];
    float layout[4];
    float metrics[4];
    float backgroundColor[4];
    float cardColor[4];
    float digitColor[4];
    float dateColor[4];
    uint32_t oldDigits0[4];
    uint32_t oldDigits1[4];
    uint32_t targetDigits0[4];
    uint32_t targetDigits1[4];
    uint32_t state[4];
    float animation[4];
    uint32_t oldDate0[4];
    uint32_t oldDate1[4];
    uint32_t oldDate2[4];
    uint32_t targetDate0[4];
    uint32_t targetDate1[4];
    uint32_t targetDate2[4];
    float oldDatePositions0[4];
    float oldDatePositions1[4];
    float oldDatePositions2[4];
    float targetDatePositions0[4];
    float targetDatePositions1[4];
    float targetDatePositions2[4];
};

static_assert(sizeof(DeskClockConstants) == 400);
static_assert(sizeof(DeskClockConstants) <= 512);

struct GlyphAtlasBuildResult final
{
    std::unique_ptr<std::uint8_t[]> pixels;
    std::array<float, kDateGlyphCount> dateAdvances{};
    float dateSpaceAdvance = 0.0f;
};

[[nodiscard]] HRESULT CreateClockFontFace(IDWriteFactory& factory, wil::com_ptr_nothrow<IDWriteFontFace>& face) noexcept
{
    wil::com_ptr_nothrow<IDWriteFontCollection> collection;
    HRESULT result = factory.GetSystemFontCollection(collection.put(), FALSE);
    if (FAILED(result))
    {
        return result;
    }

    constexpr std::array<const wchar_t*, 2> families{L"Bahnschrift", L"Arial"};
    for (const wchar_t* familyName : families)
    {
        UINT32 familyIndex = 0;
        BOOL exists = FALSE;
        result = collection->FindFamilyName(familyName, &familyIndex, &exists);
        if (FAILED(result))
        {
            return result;
        }
        if (!exists)
        {
            continue;
        }

        wil::com_ptr_nothrow<IDWriteFontFamily> family;
        result = collection->GetFontFamily(familyIndex, family.put());
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<IDWriteFont> font;
        result = family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STRETCH_CONDENSED,
                                              DWRITE_FONT_STYLE_NORMAL, font.put());
        if (FAILED(result))
        {
            return result;
        }
        result = font->CreateFontFace(face.put());
        if (SUCCEEDED(result))
        {
            return S_OK;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

[[nodiscard]] HRESULT ReadGlyphMetrics(IDWriteFontFace& face, wchar_t character, DWRITE_GLYPH_METRICS& metrics,
                                       UINT16& glyphIndex) noexcept
{
    const UINT32 codePoint = static_cast<UINT32>(character);
    HRESULT result = face.GetGlyphIndices(&codePoint, 1, &glyphIndex);
    if (FAILED(result))
    {
        return result;
    }
    if (glyphIndex == 0)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    return face.GetDesignGlyphMetrics(&glyphIndex, 1, &metrics, FALSE);
}

[[nodiscard]] HRESULT RasterizeGlyph(IDWriteFactory& factory, IDWriteFontFace& face, wchar_t character,
                                     float fontEmSize, uint32_t cellWidth, uint32_t cellHeight, uint32_t atlasX,
                                     uint32_t atlasY, bool fixedDateOrigin, std::uint8_t* atlasPixels,
                                     std::uint8_t* scratch, uint32_t scratchBytes) noexcept
{
    if (!atlasPixels || !scratch || atlasX + cellWidth > kGlyphAtlasSize || atlasY + cellHeight > kGlyphAtlasSize)
    {
        return E_INVALIDARG;
    }

    UINT16 glyphIndex = 0;
    DWRITE_GLYPH_METRICS glyphMetrics{};
    HRESULT result = ReadGlyphMetrics(face, character, glyphMetrics, glyphIndex);
    if (FAILED(result))
    {
        return result;
    }
    DWRITE_FONT_METRICS fontMetrics{};
    face.GetMetrics(&fontMetrics);
    if (fontMetrics.designUnitsPerEm == 0)
    {
        return E_UNEXPECTED;
    }

    const float designScale = fontEmSize / static_cast<float>(fontMetrics.designUnitsPerEm);
    const float advance = static_cast<float>(glyphMetrics.advanceWidth) * designScale;
    const float inkWidth = static_cast<float>(static_cast<std::int64_t>(glyphMetrics.advanceWidth) -
                                              glyphMetrics.leftSideBearing - glyphMetrics.rightSideBearing) *
                           designScale;
    const float baselineX = fixedDateOrigin ? kDateBaselineX
                                            : (static_cast<float>(cellWidth) - inkWidth) * 0.5f -
                                                  static_cast<float>(glyphMetrics.leftSideBearing) * designScale;
    const float descent = fixedDateOrigin ? static_cast<float>(fontMetrics.descent) * designScale : 0.0f;
    const float capHeight = static_cast<float>(fontMetrics.capHeight) * designScale;
    const float baselineY = (static_cast<float>(cellHeight) - capHeight - descent) * 0.5f + capHeight;

    const DWRITE_GLYPH_RUN run{
        &face, fontEmSize, 1, &glyphIndex, &advance, nullptr, FALSE, 0,
    };
    wil::com_ptr_nothrow<IDWriteGlyphRunAnalysis> analysis;
    result = factory.CreateGlyphRunAnalysis(&run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                            DWRITE_MEASURING_MODE_NATURAL, baselineX, baselineY, analysis.put());
    if (FAILED(result))
    {
        return result;
    }

    RECT bounds{};
    result = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
    if (FAILED(result))
    {
        return result;
    }
    if (bounds.left < 0 || bounds.top < 0 || bounds.right > static_cast<LONG>(cellWidth) ||
        bounds.bottom > static_cast<LONG>(cellHeight) || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    const uint32_t width = static_cast<uint32_t>(bounds.right - bounds.left);
    const uint32_t height = static_cast<uint32_t>(bounds.bottom - bounds.top);
    const uint64_t requiredBytes = static_cast<uint64_t>(width) * height * 3U;
    if (requiredBytes > scratchBytes)
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    result = analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, scratch,
                                          static_cast<UINT32>(requiredBytes));
    if (FAILED(result))
    {
        return result;
    }

    for (uint32_t y = 0; y < height; ++y)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            const size_t source = (static_cast<size_t>(y) * width + x) * 3U;
            const uint32_t coverage =
                static_cast<uint32_t>(scratch[source]) + scratch[source + 1U] + scratch[source + 2U];
            const size_t destination =
                static_cast<size_t>(atlasY + static_cast<uint32_t>(bounds.top) + y) * kGlyphAtlasSize + atlasX +
                static_cast<uint32_t>(bounds.left) + x;
            atlasPixels[destination] = static_cast<std::uint8_t>((coverage + 1U) / 3U);
        }
    }
    return S_OK;
}

[[nodiscard]] HRESULT BuildGlyphAtlas(GlyphAtlasBuildResult& atlas) noexcept
{
    GlyphAtlasBuildResult built{};
    built.pixels.reset(new (std::nothrow) std::uint8_t[kGlyphAtlasBytes]{});
    constexpr uint32_t scratchBytes = kTimeCellWidth * kTimeCellHeight * 3U;
    std::unique_ptr<std::uint8_t[]> scratch(new (std::nothrow) std::uint8_t[scratchBytes]);
    if (!built.pixels || !scratch)
    {
        return E_OUTOFMEMORY;
    }

    wil::unique_hmodule directWrite{LoadLibraryExW(L"dwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    if (!directWrite)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    using DWriteCreateFactoryFn = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
    const auto createFactory =
        reinterpret_cast<DWriteCreateFactoryFn>(GetProcAddress(directWrite.get(), "DWriteCreateFactory"));
    if (!createFactory)
    {
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    wil::com_ptr_nothrow<IDWriteFactory> factory;
    HRESULT result = createFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(factory.put()));
    if (FAILED(result))
    {
        return result;
    }
    wil::com_ptr_nothrow<IDWriteFontFace> face;
    result = CreateClockFontFace(*factory, face);
    if (FAILED(result))
    {
        return result;
    }

    for (uint32_t digit = 0; digit < kTimeGlyphCount; ++digit)
    {
        const uint32_t atlasX = (digit % kTimeCellColumns) * kTimeCellWidth;
        const uint32_t atlasY = (digit / kTimeCellColumns) * kTimeCellHeight;
        result =
            RasterizeGlyph(*factory, *face, static_cast<wchar_t>(L'0' + digit), kTimeFontEmSize, kTimeCellWidth,
                           kTimeCellHeight, atlasX, atlasY, false, built.pixels.get(), scratch.get(), scratchBytes);
        if (FAILED(result))
        {
            return result;
        }
    }

    constexpr std::wstring_view dateCharacters = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static_assert(dateCharacters.size() == kDateGlyphCount);
    DWRITE_FONT_METRICS fontMetrics{};
    face->GetMetrics(&fontMetrics);
    if (fontMetrics.designUnitsPerEm == 0)
    {
        return E_UNEXPECTED;
    }
    const float inverseEm = 1.0f / static_cast<float>(fontMetrics.designUnitsPerEm);
    for (uint32_t index = 0; index < kDateGlyphCount; ++index)
    {
        UINT16 glyphIndex = 0;
        DWRITE_GLYPH_METRICS metrics{};
        result = ReadGlyphMetrics(*face, dateCharacters[index], metrics, glyphIndex);
        if (FAILED(result))
        {
            return result;
        }
        built.dateAdvances[index] = static_cast<float>(metrics.advanceWidth) * inverseEm;
        const uint32_t atlasX = (index % kDateCellColumns) * kDateCellSize;
        const uint32_t atlasY = kDateAtlasTop + (index / kDateCellColumns) * kDateCellSize;
        result = RasterizeGlyph(*factory, *face, dateCharacters[index], kDateFontEmSize, kDateCellSize, kDateCellSize,
                                atlasX, atlasY, true, built.pixels.get(), scratch.get(), scratchBytes);
        if (FAILED(result))
        {
            return result;
        }
    }

    UINT16 spaceGlyph = 0;
    DWRITE_GLYPH_METRICS spaceMetrics{};
    result = ReadGlyphMetrics(*face, L' ', spaceMetrics, spaceGlyph);
    if (FAILED(result))
    {
        return result;
    }
    built.dateSpaceAdvance = static_cast<float>(spaceMetrics.advanceWidth) * inverseEm;
    atlas = std::move(built);
    gTypographyBuildCount.fetch_add(1, std::memory_order_relaxed);
    return S_OK;
}

class DeskClockDeviceResources final
{
  public:
    ~DeskClockDeviceResources()
    {
        Reset();
    }

    [[nodiscard]] HRESULT Initialize(ID3D11Device* device) noexcept
    {
        if (!device)
        {
            return E_POINTER;
        }
        if (_deviceIdentity == device && _constantBuffer)
        {
            return S_OK;
        }

        GlyphAtlasBuildResult glyphAtlas;
        HRESULT result = BuildGlyphAtlas(glyphAtlas);
        if (FAILED(result))
        {
            return result;
        }

        wil::com_ptr_nothrow<ID3D11VertexShader> backgroundVertexShader;
        result =
            device->CreateVertexShader(g_DeskClockBackgroundVertexShader, sizeof(g_DeskClockBackgroundVertexShader),
                                       nullptr, backgroundVertexShader.put());
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<ID3D11PixelShader> backgroundPixelShader;
        result = device->CreatePixelShader(g_DeskClockBackgroundPixelShader, sizeof(g_DeskClockBackgroundPixelShader),
                                           nullptr, backgroundPixelShader.put());
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<ID3D11VertexShader> clockVertexShader;
        result = device->CreateVertexShader(g_DeskClockVertexShader, sizeof(g_DeskClockVertexShader), nullptr,
                                            clockVertexShader.put());
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<ID3D11PixelShader> clockPixelShader;
        result = device->CreatePixelShader(g_DeskClockPixelShader, sizeof(g_DeskClockPixelShader), nullptr,
                                           clockPixelShader.put());
        if (FAILED(result))
        {
            return result;
        }
        D3D11_BUFFER_DESC drawBufferDescription{};
        drawBufferDescription.ByteWidth = 16;
        drawBufferDescription.Usage = D3D11_USAGE_IMMUTABLE;
        drawBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constexpr std::array<std::array<uint32_t, 4>, 3> drawOffsets{{{0, 0, 0, 0}, {16, 0, 0, 0}, {32, 0, 0, 0}}};
        std::array<wil::com_ptr_nothrow<ID3D11Buffer>, 3> drawBuffers;
        for (size_t index = 0; index < drawBuffers.size(); ++index)
        {
            D3D11_SUBRESOURCE_DATA drawData{};
            drawData.pSysMem = drawOffsets[index].data();
            result = device->CreateBuffer(&drawBufferDescription, &drawData, drawBuffers[index].put());
            if (FAILED(result))
            {
                return result;
            }
        }

        D3D11_TEXTURE2D_DESC atlasDescription{};
        atlasDescription.Width = kGlyphAtlasSize;
        atlasDescription.Height = kGlyphAtlasSize;
        atlasDescription.MipLevels = 1;
        atlasDescription.ArraySize = 1;
        atlasDescription.Format = DXGI_FORMAT_R8_UNORM;
        atlasDescription.SampleDesc.Count = 1;
        atlasDescription.Usage = D3D11_USAGE_IMMUTABLE;
        atlasDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA atlasData{};
        atlasData.pSysMem = glyphAtlas.pixels.get();
        atlasData.SysMemPitch = kGlyphAtlasSize;
        wil::com_ptr_nothrow<ID3D11Texture2D> atlas;
        result = device->CreateTexture2D(&atlasDescription, &atlasData, atlas.put());
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> atlasView;
        result = device->CreateShaderResourceView(atlas.get(), nullptr, atlasView.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        wil::com_ptr_nothrow<ID3D11SamplerState> sampler;
        result = device->CreateSamplerState(&samplerDescription, sampler.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_RASTERIZER_DESC rasterizerDescription{};
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_NONE;
        rasterizerDescription.DepthClipEnable = TRUE;
        wil::com_ptr_nothrow<ID3D11RasterizerState> rasterizer;
        result = device->CreateRasterizerState(&rasterizerDescription, rasterizer.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_DEPTH_STENCIL_DESC depthDescription{};
        depthDescription.DepthEnable = FALSE;
        depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
        wil::com_ptr_nothrow<ID3D11DepthStencilState> depthState;
        result = device->CreateDepthStencilState(&depthDescription, depthState.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_BLEND_DESC opaqueDescription{};
        opaqueDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        wil::com_ptr_nothrow<ID3D11BlendState> opaqueBlend;
        result = device->CreateBlendState(&opaqueDescription, opaqueBlend.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_BLEND_DESC alphaDescription{};
        D3D11_RENDER_TARGET_BLEND_DESC& alphaTarget = alphaDescription.RenderTarget[0];
        alphaTarget.BlendEnable = TRUE;
        alphaTarget.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        alphaTarget.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        alphaTarget.BlendOp = D3D11_BLEND_OP_ADD;
        alphaTarget.SrcBlendAlpha = D3D11_BLEND_ONE;
        alphaTarget.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        alphaTarget.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        alphaTarget.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        wil::com_ptr_nothrow<ID3D11BlendState> alphaBlend;
        result = device->CreateBlendState(&alphaDescription, alphaBlend.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_BUFFER_DESC constantDescription{};
        constantDescription.ByteWidth = sizeof(DeskClockConstants);
        constantDescription.Usage = D3D11_USAGE_DYNAMIC;
        constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        wil::com_ptr_nothrow<ID3D11Buffer> constantBuffer;
        result = device->CreateBuffer(&constantDescription, nullptr, constantBuffer.put());
        if (FAILED(result))
        {
            return result;
        }

        Reset();
        _deviceIdentity = device;
        _backgroundVertexShader = std::move(backgroundVertexShader);
        _backgroundPixelShader = std::move(backgroundPixelShader);
        _clockVertexShader = std::move(clockVertexShader);
        _clockPixelShader = std::move(clockPixelShader);
        _drawBuffers = std::move(drawBuffers);
        _atlas = std::move(atlas);
        _atlasView = std::move(atlasView);
        _sampler = std::move(sampler);
        _rasterizer = std::move(rasterizer);
        _depthState = std::move(depthState);
        _opaqueBlend = std::move(opaqueBlend);
        _alphaBlend = std::move(alphaBlend);
        _constantBuffer = std::move(constantBuffer);
        _dateAdvances = glyphAtlas.dateAdvances;
        _dateSpaceAdvance = glyphAtlas.dateSpaceAdvance;
        gLiveDeviceResourceSetCount.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
    }

    [[nodiscard]] float DateAdvance(uint32_t glyph) const noexcept
    {
        if (glyph < kDateDigitOffset || glyph >= kGlyphCount)
        {
            return _dateSpaceAdvance;
        }
        return _dateAdvances[glyph - kDateDigitOffset];
    }

    [[nodiscard]] float DateSpaceAdvance() const noexcept
    {
        return _dateSpaceAdvance;
    }

    void Reset() noexcept
    {
        if (_deviceIdentity)
        {
            gLiveDeviceResourceSetCount.fetch_sub(1, std::memory_order_relaxed);
        }
        _constantBuffer.reset();
        _alphaBlend.reset();
        _opaqueBlend.reset();
        _depthState.reset();
        _rasterizer.reset();
        _sampler.reset();
        _atlasView.reset();
        _atlas.reset();
        _clockPixelShader.reset();
        for (auto& drawBuffer : _drawBuffers)
        {
            drawBuffer.reset();
        }
        _clockVertexShader.reset();
        _backgroundPixelShader.reset();
        _backgroundVertexShader.reset();
        _dateAdvances.fill(0.0f);
        _dateSpaceAdvance = 0.0f;
        _deviceIdentity = nullptr;
        _lastOwner = nullptr;
        _lastVersion = 0;
    }

    [[nodiscard]] HRESULT Render(ID3D11DeviceContext* context, const void* owner, uint64_t version,
                                 const DeskClockConstants& constants, bool transitionActive) noexcept
    {
        if (!context || !_constantBuffer)
        {
            return E_UNEXPECTED;
        }
        if (_lastOwner != owner || _lastVersion != version)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            HRESULT result = context->Map(_constantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(result))
            {
                return result;
            }
            std::memcpy(mapped.pData, &constants, sizeof(constants));
            context->Unmap(_constantBuffer.get(), 0);
            _lastOwner = owner;
            _lastVersion = version;
            gConstantUploadCount.fetch_add(1, std::memory_order_relaxed);
        }

        ID3D11Buffer* constantBuffer = _constantBuffer.get();
        ID3D11ShaderResourceView* atlasView = _atlasView.get();
        ID3D11SamplerState* sampler = _sampler.get();
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->RSSetState(_rasterizer.get());
        context->OMSetDepthStencilState(_depthState.get(), 0);
        context->VSSetConstantBuffers(0, 1, &constantBuffer);
        context->PSSetConstantBuffers(0, 1, &constantBuffer);
        context->PSSetShaderResources(0, 1, &atlasView);
        context->PSSetSamplers(0, 1, &sampler);

        context->OMSetBlendState(_opaqueBlend.get(), nullptr, 0xFFFFFFFFU);
        context->VSSetShader(_backgroundVertexShader.get(), nullptr, 0);
        context->PSSetShader(_backgroundPixelShader.get(), nullptr, 0);
        context->Draw(3, 0);

        context->OMSetBlendState(_alphaBlend.get(), nullptr, 0xFFFFFFFFU);
        context->VSSetShader(_clockVertexShader.get(), nullptr, 0);
        context->PSSetShader(_clockPixelShader.get(), nullptr, 0);
        ID3D11Buffer* drawBuffer = _drawBuffers[0].get();
        context->VSSetConstantBuffers(1, 1, &drawBuffer);
        context->DrawInstanced(6, 12, 0, 0);
        uint64_t draws = 2;
        if (transitionActive)
        {
            drawBuffer = _drawBuffers[1].get();
            context->VSSetConstantBuffers(1, 1, &drawBuffer);
            context->DrawInstanced(6, 6, 0, 0);
            ++draws;
        }
        drawBuffer = _drawBuffers[2].get();
        context->VSSetConstantBuffers(1, 1, &drawBuffer);
        context->DrawInstanced(6, 28, 0, 0);
        ++draws;

        gDrawCallCount.fetch_add(draws, std::memory_order_relaxed);
        return S_OK;
    }

  private:
    ID3D11Device* _deviceIdentity = nullptr;
    const void* _lastOwner = nullptr;
    uint64_t _lastVersion = 0;
    wil::com_ptr_nothrow<ID3D11VertexShader> _backgroundVertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _backgroundPixelShader;
    wil::com_ptr_nothrow<ID3D11VertexShader> _clockVertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _clockPixelShader;
    std::array<wil::com_ptr_nothrow<ID3D11Buffer>, 3> _drawBuffers;
    wil::com_ptr_nothrow<ID3D11Texture2D> _atlas;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _atlasView;
    wil::com_ptr_nothrow<ID3D11SamplerState> _sampler;
    wil::com_ptr_nothrow<ID3D11RasterizerState> _rasterizer;
    wil::com_ptr_nothrow<ID3D11DepthStencilState> _depthState;
    wil::com_ptr_nothrow<ID3D11BlendState> _opaqueBlend;
    wil::com_ptr_nothrow<ID3D11BlendState> _alphaBlend;
    wil::com_ptr_nothrow<ID3D11Buffer> _constantBuffer;
    std::array<float, kDateGlyphCount> _dateAdvances{};
    float _dateSpaceAdvance = 0.0f;
};

[[nodiscard]] uint64_t PackTestTime(const DeskClockTestTime& time) noexcept
{
    return (1ULL << 63U) | static_cast<uint64_t>(time.milliseconds) | (static_cast<uint64_t>(time.second) << 10U) |
           (static_cast<uint64_t>(time.minute) << 16U) | (static_cast<uint64_t>(time.hour) << 22U) |
           (static_cast<uint64_t>(time.day) << 27U) | (static_cast<uint64_t>(time.dayOfWeek) << 32U) |
           (static_cast<uint64_t>(time.month) << 35U) | (static_cast<uint64_t>(time.year) << 39U);
}

void ReadLocalClock(SYSTEMTIME& time) noexcept
{
    const uint64_t packed = gTestTime.load(std::memory_order_acquire);
    if ((packed & (1ULL << 63U)) == 0)
    {
        GetLocalTime(&time);
    }
    else
    {
        time = {};
        time.wMilliseconds = static_cast<WORD>(packed & 0x3FFU);
        time.wSecond = static_cast<WORD>((packed >> 10U) & 0x3FU);
        time.wMinute = static_cast<WORD>((packed >> 16U) & 0x3FU);
        time.wHour = static_cast<WORD>((packed >> 22U) & 0x1FU);
        time.wDay = static_cast<WORD>((packed >> 27U) & 0x1FU);
        time.wDayOfWeek = static_cast<WORD>((packed >> 32U) & 0x7U);
        time.wMonth = static_cast<WORD>((packed >> 35U) & 0xFU);
        time.wYear = static_cast<WORD>((packed >> 39U) & 0xFFFU);
    }
    gTimeSampleCount.fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] uint32_t DateGlyphIndex(char character) noexcept
{
    if (character >= '0' && character <= '9')
    {
        return kDateDigitOffset + static_cast<uint32_t>(character - '0');
    }
    if (character >= 'A' && character <= 'Z')
    {
        return kDateDigitOffset + 10U + static_cast<uint32_t>(character - 'A');
    }
    if (character >= 'a' && character <= 'z')
    {
        return kDateDigitOffset + 36U + static_cast<uint32_t>(character - 'a');
    }
    return kNoGlyph;
}

[[nodiscard]] std::array<uint32_t, 8> DigitsForTime(const SYSTEMTIME& time) noexcept
{
    return {
        time.wHour / 10U,
        time.wHour % 10U,
        time.wMinute / 10U,
        time.wMinute % 10U,
        time.wSecond / 10U,
        time.wSecond % 10U,
        0U,
        0U,
    };
}

[[nodiscard]] std::array<uint32_t, 12> GlyphsForDate(const SYSTEMTIME& time) noexcept
{
    constexpr std::array<std::string_view, 7> weekdays{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    constexpr std::array<std::string_view, 12> months{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::string_view weekday = weekdays[std::min<size_t>(time.wDayOfWeek, weekdays.size() - 1)];
    const size_t monthIndex = time.wMonth > 0 ? static_cast<size_t>(time.wMonth - 1) : 0;
    const std::string_view month = months[std::min(monthIndex, months.size() - 1)];
    std::array<uint32_t, 12> glyphs{};
    glyphs.fill(kNoGlyph);
    uint32_t index = 0;
    const auto append = [&](char character) noexcept { glyphs[index++] = DateGlyphIndex(character); };
    append(weekday[0]);
    append(weekday[1]);
    append(weekday[2]);
    ++index;
    if (time.wDay >= 10U)
    {
        append(static_cast<char>('0' + time.wDay / 10U));
    }
    append(static_cast<char>('0' + time.wDay % 10U));
    ++index;
    append(month[0]);
    append(month[1]);
    append(month[2]);
    glyphs[11] = index;
    return glyphs;
}

[[nodiscard]] float SmootherStep(float value) noexcept
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return clamped * clamped * clamped * (clamped * (clamped * 6.0f - 15.0f) + 10.0f);
}

class DeskClockWidget final : public IRedXeWidget,
                              public IRedXeGpuWidget,
                              public IRedXeScheduledWidget,
                              public IRedXeRaisedWidget
{
  public:
    DeskClockWidget(wil::com_ptr_nothrow<IRedXeWidgetProvider>&& providerOwner, DeskClockDeviceResources& resources,
                    const DeskClockConfiguration& configuration) noexcept
        : _providerOwner(std::move(providerOwner)), _resources(&resources), _configuration(configuration),
          _backgroundColor(ColorToFloat(configuration.backgroundColor)),
          _cardColor(ColorToFloat(configuration.cardColor)), _digitColor(ColorToFloat(configuration.digitColor)),
          _dateColor(ColorToFloat(configuration.dateColor))
    {
        _oldDate.fill(kNoGlyph);
        _targetDate.fill(kNoGlyph);
        gLiveWidgetCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~DeskClockWidget()
    {
        gLiveWidgetCount.fetch_sub(1, std::memory_order_relaxed);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeWidget))
        {
            *result = static_cast<IRedXeWidget*>(this);
        }
        else if (interfaceId == __uuidof(IRedXeGpuWidget))
        {
            *result = static_cast<IRedXeGpuWidget*>(this);
        }
        else if (interfaceId == __uuidof(IRedXeScheduledWidget))
        {
            *result = static_cast<IRedXeScheduledWidget*>(this);
        }
        else if (interfaceId == __uuidof(IRedXeRaisedWidget))
        {
            *result = static_cast<IRedXeRaisedWidget*>(this);
        }
        else
        {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return ++_references;
    }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        const ULONG references = --_references;
        if (references == 0)
        {
            delete this;
        }
        return references;
    }

    HRESULT STDMETHODCALLTYPE SetVisible(BOOL) noexcept override
    {
        return S_OK;
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
        if (context->featureLevel < D3D_FEATURE_LEVEL_11_0 || context->targetFormat == DXGI_FORMAT_UNKNOWN)
        {
            return DXGI_ERROR_UNSUPPORTED;
        }
        const HRESULT result = _resources->Initialize(context->device);
        if (SUCCEEDED(result))
        {
            _initialized = false;
            ++_visualVersion;
        }
        return result;
    }

    void STDMETHODCALLTYPE OnDeviceLost() noexcept override
    {
        _resources->Reset();
        _initialized = false;
        _transitionActive = false;
        _changedMask = 0;
        _scheduleUsesCachedSample = false;
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
            !std::isfinite(context->viewport.MaxDepth) || context->viewport.TopLeftX < 0.0f ||
            context->viewport.TopLeftY < 0.0f || context->viewport.Width < 0.0f || context->viewport.Height < 0.0f ||
            context->viewport.MinDepth < 0.0f || context->viewport.MaxDepth > 1.0f ||
            context->viewport.MinDepth > context->viewport.MaxDepth)
        {
            return E_INVALIDARG;
        }
        if (frame.widthPixels == 0 || frame.heightPixels == 0 || context->viewport.Width == 0.0f ||
            context->viewport.Height == 0.0f)
        {
            return S_OK;
        }

        const double deltaMilliseconds = static_cast<double>(frame.deltaSeconds) * 1000.0;
        if (!_initialized || deltaMilliseconds > 2'000.0)
        {
            SampleAndSnap();
        }
        else
        {
            AdvanceTransition(deltaMilliseconds);
            _millisecondsUntilSample -= deltaMilliseconds;
            if (_millisecondsUntilSample <= 0.0)
            {
                SampleAndTransition();
            }
        }
        UpdateLayout(frame);

        DeskClockConstants constants{};
        BuildConstants(frame, constants);
        return _resources->Render(context->deviceContext, this, _visualVersion, constants, _transitionActive);
    }

    HRESULT STDMETHODCALLTYPE GetNextFrameDelayMilliseconds(uint32_t* delayMilliseconds) noexcept override
    {
        if (delayMilliseconds)
        {
            *delayMilliseconds = 0;
        }
        if (!delayMilliseconds)
        {
            return E_POINTER;
        }
        gScheduleQueryCount.fetch_add(1, std::memory_order_relaxed);
        if (!_initialized || _transitionActive)
        {
            *delayMilliseconds = 1;
            return S_OK;
        }
        if (_scheduleUsesCachedSample)
        {
            _scheduleUsesCachedSample = false;
        }
        else
        {
            SYSTEMTIME sample{};
            ReadLocalClock(sample);
            _millisecondsUntilSample = std::max(1.0, 1000.0 - static_cast<double>(sample.wMilliseconds));
            BeginTransition(sample);
            if (_transitionActive)
            {
                *delayMilliseconds = 1;
                return S_OK;
            }
        }
        if (!std::isfinite(_millisecondsUntilSample) || _millisecondsUntilSample <= 1.0)
        {
            *delayMilliseconds = 1;
            return S_OK;
        }
        const double rounded = std::ceil(_millisecondsUntilSample);
        *delayMilliseconds = static_cast<uint32_t>(
            std::clamp(rounded, 1.0, static_cast<double>(kRedXeMaximumScheduledFrameDelayMilliseconds)));
        return S_OK;
    }

  private:
    void BuildDatePositions(const std::array<uint32_t, 12>& glyphs, std::array<float, 12>& positions) const noexcept
    {
        positions.fill(0.0f);
        const uint32_t count = std::min(glyphs[11], kDateGlyphCapacity);
        float totalAdvance = 0.0f;
        for (uint32_t index = 0; index < count; ++index)
        {
            totalAdvance +=
                glyphs[index] == kNoGlyph ? _resources->DateSpaceAdvance() : _resources->DateAdvance(glyphs[index]);
        }
        float pen = totalAdvance * -0.5f;
        for (uint32_t index = 0; index < count; ++index)
        {
            positions[index] = pen;
            pen += glyphs[index] == kNoGlyph ? _resources->DateSpaceAdvance() : _resources->DateAdvance(glyphs[index]);
        }
    }

    void SampleAndSnap() noexcept
    {
        SYSTEMTIME sample{};
        ReadLocalClock(sample);
        _oldDigits = DigitsForTime(sample);
        _targetDigits = _oldDigits;
        _oldDate = GlyphsForDate(sample);
        _targetDate = _oldDate;
        BuildDatePositions(_oldDate, _oldDatePositions);
        _targetDatePositions = _oldDatePositions;
        _changedMask = 0;
        _dateChanged = false;
        _transitionActive = false;
        _transitionElapsedMilliseconds = 0.0;
        _millisecondsUntilSample = std::max(1.0, 1000.0 - static_cast<double>(sample.wMilliseconds));
        _initialized = true;
        _scheduleUsesCachedSample = true;
        ++_visualVersion;
    }

    void SampleAndTransition() noexcept
    {
        SYSTEMTIME sample{};
        ReadLocalClock(sample);
        _millisecondsUntilSample = std::max(1.0, 1000.0 - static_cast<double>(sample.wMilliseconds));
        _scheduleUsesCachedSample = true;
        BeginTransition(sample);
    }

    void BeginTransition(const SYSTEMTIME& sample) noexcept
    {
        const auto digits = DigitsForTime(sample);
        const auto date = GlyphsForDate(sample);
        if (digits == _targetDigits && date == _targetDate)
        {
            return;
        }

        _oldDigits = _targetDigits;
        _oldDate = _targetDate;
        _oldDatePositions = _targetDatePositions;
        _targetDigits = digits;
        _targetDate = date;
        BuildDatePositions(_targetDate, _targetDatePositions);
        _changedMask = 0;
        for (uint32_t index = 0; index < 6; ++index)
        {
            if (_oldDigits[index] != _targetDigits[index])
            {
                _changedMask |= 1U << index;
            }
        }
        _dateChanged = _oldDate != _targetDate;
        _transitionActive = _changedMask != 0 || _dateChanged;
        _transitionElapsedMilliseconds = 0.0;
        ++_visualVersion;
    }

    void AdvanceTransition(double deltaMilliseconds) noexcept
    {
        if (!_transitionActive)
        {
            return;
        }
        _transitionElapsedMilliseconds += deltaMilliseconds;
        if (_transitionElapsedMilliseconds >= static_cast<double>(_configuration.flipDurationMilliseconds))
        {
            _transitionElapsedMilliseconds = static_cast<double>(_configuration.flipDurationMilliseconds);
            _oldDigits = _targetDigits;
            _oldDate = _targetDate;
            _oldDatePositions = _targetDatePositions;
            _changedMask = 0;
            _dateChanged = false;
            _transitionActive = false;
            _scheduleUsesCachedSample = true;
        }
        ++_visualVersion;
    }

    void UpdateLayout(const RedXeWidgetFrameContext& frame) noexcept
    {
        if (_layoutWidth == frame.widthPixels && _layoutHeight == frame.heightPixels && _layoutDpi == frame.dpi)
        {
            return;
        }
        _layoutWidth = frame.widthPixels;
        _layoutHeight = frame.heightPixels;
        _layoutDpi = frame.dpi;
        const float width = static_cast<float>(frame.widthPixels);
        const float height = static_cast<float>(frame.heightPixels);
        _scale = std::min(width / 1450.0f, height / 600.0f);
        _cardWidth = 195.0f * _scale;
        _cardHeight = 310.0f * _scale;
        _pairGap = 10.0f * _scale;
        _groupGap = 65.0f * _scale;
        const float totalWidth = 6.0f * _cardWidth + 3.0f * _pairGap + 2.0f * _groupGap;
        _originX = (width - totalWidth) * 0.5f;
        const float contentHeight = 430.0f * _scale;
        _originY = (height - contentHeight) * 0.5f;
        _dateY = _originY + _cardHeight + 54.0f * _scale;
        ++_visualVersion;
    }

    void BuildConstants(const RedXeWidgetFrameContext& frame, DeskClockConstants& constants) const noexcept
    {
        constants.targetSize[0] = static_cast<float>(frame.widthPixels);
        constants.targetSize[1] = static_cast<float>(frame.heightPixels);
        constants.targetSize[2] = static_cast<float>(frame.dpi);
        constants.layout[0] = _originX;
        constants.layout[1] = _originY;
        constants.layout[2] = _cardWidth;
        constants.layout[3] = _cardHeight;
        constants.metrics[0] = _pairGap;
        constants.metrics[1] = _groupGap;
        constants.metrics[2] = _dateY;
        constants.metrics[3] = _scale;
        std::copy(_backgroundColor.begin(), _backgroundColor.end(), constants.backgroundColor);
        std::copy(_cardColor.begin(), _cardColor.end(), constants.cardColor);
        std::copy(_digitColor.begin(), _digitColor.end(), constants.digitColor);
        std::copy(_dateColor.begin(), _dateColor.end(), constants.dateColor);
        std::copy_n(_oldDigits.begin(), 4, constants.oldDigits0);
        std::copy_n(_oldDigits.begin() + 4, 4, constants.oldDigits1);
        std::copy_n(_targetDigits.begin(), 4, constants.targetDigits0);
        std::copy_n(_targetDigits.begin() + 4, 4, constants.targetDigits1);

        float overallProgress = 1.0f;
        uint32_t phase = 0;
        float phaseProgress = 1.0f;
        if (_transitionActive)
        {
            overallProgress = static_cast<float>(_transitionElapsedMilliseconds /
                                                 static_cast<double>(_configuration.flipDurationMilliseconds));
            if (overallProgress < 0.5f)
            {
                phase = 1;
                phaseProgress = SmootherStep(overallProgress * 2.0f);
            }
            else
            {
                phase = 2;
                phaseProgress = SmootherStep((overallProgress - 0.5f) * 2.0f);
            }
        }
        constants.state[0] = _changedMask;
        constants.state[1] = phase;
        constants.state[2] = _dateChanged ? 1U : 0U;
        constants.state[3] = kDateGlyphCapacity;
        constants.animation[0] = phaseProgress;
        constants.animation[1] = SmootherStep(overallProgress);
        constants.animation[2] = _cardWidth > 0.0f ? std::min(0.08f, 12.0f * _scale / _cardWidth) : 0.02f;
        constants.animation[3] = _cardHeight > 0.0f ? std::max(0.002f, 2.0f * _scale / _cardHeight) : 0.004f;
        std::copy_n(_oldDate.begin(), 4, constants.oldDate0);
        std::copy_n(_oldDate.begin() + 4, 4, constants.oldDate1);
        std::copy_n(_oldDate.begin() + 8, 4, constants.oldDate2);
        std::copy_n(_targetDate.begin(), 4, constants.targetDate0);
        std::copy_n(_targetDate.begin() + 4, 4, constants.targetDate1);
        std::copy_n(_targetDate.begin() + 8, 4, constants.targetDate2);
        std::copy_n(_oldDatePositions.begin(), 4, constants.oldDatePositions0);
        std::copy_n(_oldDatePositions.begin() + 4, 4, constants.oldDatePositions1);
        std::copy_n(_oldDatePositions.begin() + 8, 4, constants.oldDatePositions2);
        std::copy_n(_targetDatePositions.begin(), 4, constants.targetDatePositions0);
        std::copy_n(_targetDatePositions.begin() + 4, 4, constants.targetDatePositions1);
        std::copy_n(_targetDatePositions.begin() + 8, 4, constants.targetDatePositions2);
    }

    std::atomic<ULONG> _references{1};
    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    DeskClockDeviceResources* _resources;
    DeskClockConfiguration _configuration;
    std::array<float, 4> _backgroundColor;
    std::array<float, 4> _cardColor;
    std::array<float, 4> _digitColor;
    std::array<float, 4> _dateColor;
    std::array<uint32_t, 8> _oldDigits{};
    std::array<uint32_t, 8> _targetDigits{};
    std::array<uint32_t, 12> _oldDate{};
    std::array<uint32_t, 12> _targetDate{};
    std::array<float, 12> _oldDatePositions{};
    std::array<float, 12> _targetDatePositions{};
    bool _initialized = false;
    bool _transitionActive = false;
    bool _dateChanged = false;
    bool _scheduleUsesCachedSample = false;
    uint32_t _changedMask = 0;
    double _millisecondsUntilSample = 1.0;
    double _transitionElapsedMilliseconds = 0.0;
    uint64_t _visualVersion = 1;
    uint32_t _layoutWidth = 0;
    uint32_t _layoutHeight = 0;
    uint32_t _layoutDpi = 0;
    float _scale = 1.0f;
    float _cardWidth = 210.0f;
    float _cardHeight = 360.0f;
    float _pairGap = 14.0f;
    float _groupGap = 70.0f;
    float _originX = 0.0f;
    float _originY = 0.0f;
    float _dateY = 0.0f;
};

class DeskClockProvider final : public IRedXeWidgetProvider
{
  public:
    explicit DeskClockProvider(const DeskClockConfiguration& configuration) noexcept : _configuration(configuration)
    {
        gLiveProviderCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~DeskClockProvider()
    {
        gLiveProviderCount.fetch_sub(1, std::memory_order_relaxed);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeWidgetProvider))
        {
            *result = static_cast<IRedXeWidgetProvider*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return ++_references;
    }

    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        const ULONG references = --_references;
        if (references == 0)
        {
            delete this;
        }
        return references;
    }

    HRESULT STDMETHODCALLTYPE GetWidgetTypes(const RedXeWidgetTypeDescriptor** descriptors,
                                             uint32_t* count) noexcept override
    {
        if (descriptors)
        {
            *descriptors = nullptr;
        }
        if (count)
        {
            *count = 0;
        }
        if (!descriptors || !count)
        {
            return E_POINTER;
        }
        *descriptors = kWidgetTypes.data();
        *count = static_cast<uint32_t>(kWidgetTypes.size());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateWidget(const char* typeId, const char* instanceId,
                                           IRedXeWidget** widget) noexcept override
    {
        if (!widget)
        {
            return E_POINTER;
        }
        *widget = nullptr;
        if (!typeId || !RedXeIsValidMachineId(instanceId))
        {
            return E_INVALIDARG;
        }
        if (!RedXeAsciiEqualsIgnoreCase(typeId, kWidgetTypeId))
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        wil::com_ptr_nothrow<IRedXeWidgetProvider> providerOwner;
        HRESULT result = QueryInterface(__uuidof(IRedXeWidgetProvider), reinterpret_cast<void**>(providerOwner.put()));
        if (FAILED(result))
        {
            return result;
        }
        auto* created = new (std::nothrow) DeskClockWidget(std::move(providerOwner), _resources, _configuration);
        if (!created)
        {
            return E_OUTOFMEMORY;
        }
        *widget = static_cast<IRedXeWidget*>(created);
        return S_OK;
    }

  private:
    std::atomic<ULONG> _references{1};
    DeskClockConfiguration _configuration;
    DeskClockDeviceResources _resources;
};

static_assert(kGlyphAtlasBytes == 1U * 1024U * 1024U);
static_assert(sizeof(DeskClockDeviceResources) < 32U * 1024U);
static_assert(sizeof(DeskClockProvider) < 32U * 1024U);
static_assert(sizeof(DeskClockWidget) < 4U * 1024U);

HRESULT CreateDeskClockProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost*,
                                void** result) noexcept
{
    if (interfaceId != __uuidof(IRedXeWidgetProvider))
    {
        return E_NOINTERFACE;
    }
    DeskClockConfiguration configuration{};
    const HRESULT createResult = ReadFactoryConfiguration(options, configuration);
    if (FAILED(createResult))
    {
        return createResult;
    }
    auto* provider = new (std::nothrow) DeskClockProvider(configuration);
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = provider;
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateDeskClockProvider},
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

extern "C" void __stdcall RedXePluginShutdown() noexcept {}

extern "C" HRESULT __stdcall RedXeDeskClockSetTestTime(const DeskClockTestTime* time) noexcept
{
    if (!time)
    {
        gTestTime.store(0, std::memory_order_release);
        return S_OK;
    }
    if (time->sizeBytes != sizeof(DeskClockTestTime) || time->year < 1601 || time->year > 4095 || time->month < 1 ||
        time->month > 12 || time->dayOfWeek > 6 || time->day < 1 || time->day > 31 || time->hour > 23 ||
        time->minute > 59 || time->second > 59 || time->milliseconds > 999)
    {
        return E_INVALIDARG;
    }
    gTestTime.store(PackTestTime(*time), std::memory_order_release);
    return S_OK;
}

extern "C" HRESULT __stdcall RedXeDeskClockGetTestDiagnostics(DeskClockTestDiagnostics* diagnostics) noexcept
{
    if (!diagnostics)
    {
        return E_POINTER;
    }
    if (diagnostics->sizeBytes != sizeof(DeskClockTestDiagnostics))
    {
        return E_INVALIDARG;
    }
    diagnostics->liveProviders = gLiveProviderCount.load(std::memory_order_relaxed);
    diagnostics->liveWidgets = gLiveWidgetCount.load(std::memory_order_relaxed);
    diagnostics->liveDeviceResourceSets = gLiveDeviceResourceSetCount.load(std::memory_order_relaxed);
    diagnostics->timeSamples = gTimeSampleCount.load(std::memory_order_relaxed);
    diagnostics->constantUploads = gConstantUploadCount.load(std::memory_order_relaxed);
    diagnostics->drawCalls = gDrawCallCount.load(std::memory_order_relaxed);
    diagnostics->scheduleQueries = gScheduleQueryCount.load(std::memory_order_relaxed);
    diagnostics->typographyBuilds = gTypographyBuildCount.load(std::memory_order_relaxed);
    diagnostics->atlasBytes = kGlyphAtlasBytes;
    diagnostics->constantBytes = sizeof(DeskClockConstants);
    return S_OK;
}
