#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Widget.h"

#include "MatrixRainBackgroundPixelShader.h"
#include "MatrixRainBackgroundVertexShader.h"
#include "MatrixRainGlyphAtlas.h"
#include "MatrixRainGlyphPixelShader.h"
#include "MatrixRainGlyphVertexShader.h"
#include "MatrixRainTestContract.h"

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
constexpr char kPluginId[] = "builtin.matrix-rain";
constexpr char kWidgetTypeId[] = "matrix-rain";
constexpr char kSettingsSchema[] =
    R"json({"type":"object","additionalProperties":false,"properties":{"seed":{"type":"integer","minimum":0,"maximum":4294967295},"glyphHeightDips":{"type":"integer","minimum":12,"maximum":48},"densityPercent":{"type":"integer","minimum":10,"maximum":100},"speedPercent":{"type":"integer","minimum":25,"maximum":300},"trailLengthGlyphs":{"type":"integer","minimum":6,"maximum":48},"mutationPerSecond":{"type":"integer","minimum":0,"maximum":30},"headColor":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},"trailColor":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},"backgroundColor":{"type":"string","pattern":"^#[0-9A-Fa-f]{6}$"},"glowPercent":{"type":"integer","minimum":0,"maximum":100}}})json";
constexpr char kSettingsDefaults[] =
    R"json({"seed":1999,"glyphHeightDips":18,"densityPercent":70,"speedPercent":100,"trailLengthGlyphs":18,"mutationPerSecond":8,"headColor":"#D8FFE5","trailColor":"#00E65C","backgroundColor":"#010502","glowPercent":35})json";
constexpr RedXePluginSettingsContract kSettingsContract{
    sizeof(RedXePluginSettingsContract), kSettingsSchema, sizeof(kSettingsSchema) - 1, kSettingsDefaults,
    sizeof(kSettingsDefaults) - 1,
};
constexpr uint32_t kMaximumGlyphInstances = 65'536;

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        kPluginId,
        L"Matrix Rain",
        L"Low-resource deterministic digital-glyph rain for the XENEON EDGE dashboard.",
        L"RedXe",
        L"1.0.0",
        RedXePluginCapabilityWidgetProvider,
    },
};

constexpr std::array kWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        kWidgetTypeId,
        L"Matrix Rain",
        L"A continuously animated Direct3D 11 digital-rain field.",
        2560.0f,
        720.0f,
        160.0f,
        90.0f,
        RedXeWidgetFlagContinuousAnimation,
    },
};

struct MatrixRainConfiguration final
{
    uint32_t seed = 1999;
    uint32_t glyphHeightDips = 18;
    uint32_t densityPercent = 70;
    uint32_t speedPercent = 100;
    uint32_t trailLengthGlyphs = 18;
    uint32_t mutationPerSecond = 8;
    uint32_t headColor = 0xD8FFE5;
    uint32_t trailColor = 0x00E65C;
    uint32_t backgroundColor = 0x010502;
    uint32_t glowPercent = 35;
};

enum ConfigurationMember : uint32_t
{
    ConfigurationSeed = 1U << 0U,
    ConfigurationGlyphHeight = 1U << 1U,
    ConfigurationDensity = 1U << 2U,
    ConfigurationSpeed = 1U << 3U,
    ConfigurationTrailLength = 1U << 4U,
    ConfigurationMutation = 1U << 5U,
    ConfigurationHeadColor = 1U << 6U,
    ConfigurationTrailColor = 1U << 7U,
    ConfigurationBackgroundColor = 1U << 8U,
    ConfigurationGlow = 1U << 9U,
};

inline constexpr uint32_t kAllConfigurationMembers = (1U << 10U) - 1U;

std::atomic<uint32_t> gLiveProviderCount{0};
std::atomic<uint32_t> gLiveWidgetCount{0};
std::atomic<uint32_t> gLiveDeviceResourceSetCount{0};

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
    constexpr std::array members{
        std::pair<std::string_view, uint32_t>{"seed", ConfigurationSeed},
        std::pair<std::string_view, uint32_t>{"glyphHeightDips", ConfigurationGlyphHeight},
        std::pair<std::string_view, uint32_t>{"densityPercent", ConfigurationDensity},
        std::pair<std::string_view, uint32_t>{"speedPercent", ConfigurationSpeed},
        std::pair<std::string_view, uint32_t>{"trailLengthGlyphs", ConfigurationTrailLength},
        std::pair<std::string_view, uint32_t>{"mutationPerSecond", ConfigurationMutation},
        std::pair<std::string_view, uint32_t>{"headColor", ConfigurationHeadColor},
        std::pair<std::string_view, uint32_t>{"trailColor", ConfigurationTrailColor},
        std::pair<std::string_view, uint32_t>{"backgroundColor", ConfigurationBackgroundColor},
        std::pair<std::string_view, uint32_t>{"glowPercent", ConfigurationGlow},
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

[[nodiscard]] bool IsInRange(uint32_t value, uint32_t minimum, uint32_t maximum) noexcept
{
    return value >= minimum && value <= maximum;
}

[[nodiscard]] bool ParseMatrixSettingsObject(JsonCursor& cursor, MatrixRainConfiguration& configuration) noexcept
{
    if (!cursor.Consume('{'))
    {
        return false;
    }

    MatrixRainConfiguration parsed{};
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

        uint32_t value = 0;
        std::string_view text;
        switch (member)
        {
        case ConfigurationSeed:
            if (!cursor.ReadUnsigned(parsed.seed))
            {
                return false;
            }
            break;
        case ConfigurationGlyphHeight:
            if (!cursor.ReadUnsigned(value) || !IsInRange(value, 12, 48))
            {
                return false;
            }
            parsed.glyphHeightDips = value;
            break;
        case ConfigurationDensity:
            if (!cursor.ReadUnsigned(value) || !IsInRange(value, 10, 100))
            {
                return false;
            }
            parsed.densityPercent = value;
            break;
        case ConfigurationSpeed:
            if (!cursor.ReadUnsigned(value) || !IsInRange(value, 25, 300))
            {
                return false;
            }
            parsed.speedPercent = value;
            break;
        case ConfigurationTrailLength:
            if (!cursor.ReadUnsigned(value) || !IsInRange(value, 6, 48))
            {
                return false;
            }
            parsed.trailLengthGlyphs = value;
            break;
        case ConfigurationMutation:
            if (!cursor.ReadUnsigned(value) || value > 30)
            {
                return false;
            }
            parsed.mutationPerSecond = value;
            break;
        case ConfigurationHeadColor:
            if (!cursor.ReadString(text) || !ParseColor(text, parsed.headColor))
            {
                return false;
            }
            break;
        case ConfigurationTrailColor:
            if (!cursor.ReadString(text) || !ParseColor(text, parsed.trailColor))
            {
                return false;
            }
            break;
        case ConfigurationBackgroundColor:
            if (!cursor.ReadString(text) || !ParseColor(text, parsed.backgroundColor))
            {
                return false;
            }
            break;
        case ConfigurationGlow:
            if (!cursor.ReadUnsigned(value) || value > 100)
            {
                return false;
            }
            parsed.glowPercent = value;
            break;
        default:
            return false;
        }

        cursor.SkipWhitespace();
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

[[nodiscard]] bool ParseNormalizedConfiguration(std::string_view json, MatrixRainConfiguration& configuration) noexcept
{
    JsonCursor cursor(json);
    if (!cursor.Consume('{'))
    {
        return false;
    }

    constexpr uint32_t pluginSeen = 1U << 0U;
    constexpr uint32_t instanceSeen = 1U << 1U;
    uint32_t seen = 0;
    MatrixRainConfiguration parsed{};
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
            if ((seen & instanceSeen) != 0 || !ParseMatrixSettingsObject(cursor, parsed))
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
                                               MatrixRainConfiguration& configuration) noexcept
{
    configuration = MatrixRainConfiguration{};
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

    MatrixRainConfiguration parsed{};
    if (!ParseNormalizedConfiguration(std::string_view(json, bytes), parsed))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    configuration = parsed;
    return S_OK;
}

struct alignas(16) MatrixRainConstants final
{
    uint32_t targetAndSeed[4];
    uint32_t grid[4];
    uint32_t stream[4];
    float geometryAndTime[4];
    float headColor[4];
    float trailColor[4];
    float backgroundColor[4];
    float effect[4];
};

static_assert(sizeof(MatrixRainConstants) == 128);
static_assert(sizeof(MatrixRainConstants) <= 256);

struct GridCache final
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t dpi = 0;
    uint32_t columns = 0;
    uint32_t rows = 0;
    uint32_t activeColumns = 0;
    uint32_t instanceCount = 0;
    uint32_t permutationMultiplier = 0;
    uint32_t permutationOffset = 0;
    float cellWidth = 0.0f;
    float cellHeight = 0.0f;
    float horizontalMargin = 0.0f;
};

[[nodiscard]] uint32_t Hash(uint32_t value) noexcept
{
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

[[nodiscard]] uint32_t GreatestCommonDivisor(uint32_t left, uint32_t right) noexcept
{
    while (right != 0)
    {
        const uint32_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
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

class MatrixRainDeviceResources final
{
  public:
    explicit MatrixRainDeviceResources(const MatrixRainConfiguration& configuration) noexcept
        : _configuration(configuration), _headColor(ColorToFloat(configuration.headColor)),
          _trailColor(ColorToFloat(configuration.trailColor)),
          _backgroundColor(ColorToFloat(configuration.backgroundColor))
    {
    }

    ~MatrixRainDeviceResources()
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

        wil::com_ptr_nothrow<ID3D11VertexShader> backgroundVertexShader;
        HRESULT result =
            device->CreateVertexShader(g_MatrixRainBackgroundVertexShader, sizeof(g_MatrixRainBackgroundVertexShader),
                                       nullptr, backgroundVertexShader.put());
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<ID3D11PixelShader> backgroundPixelShader;
        result = device->CreatePixelShader(g_MatrixRainBackgroundPixelShader, sizeof(g_MatrixRainBackgroundPixelShader),
                                           nullptr, backgroundPixelShader.put());
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<ID3D11VertexShader> glyphVertexShader;
        result = device->CreateVertexShader(g_MatrixRainGlyphVertexShader, sizeof(g_MatrixRainGlyphVertexShader),
                                            nullptr, glyphVertexShader.put());
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<ID3D11PixelShader> glyphPixelShader;
        result = device->CreatePixelShader(g_MatrixRainGlyphPixelShader, sizeof(g_MatrixRainGlyphPixelShader), nullptr,
                                           glyphPixelShader.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_TEXTURE2D_DESC atlasDescription{};
        atlasDescription.Width = kMatrixRainGlyphAtlasSize;
        atlasDescription.Height = kMatrixRainGlyphAtlasSize;
        atlasDescription.MipLevels = 1;
        atlasDescription.ArraySize = 1;
        atlasDescription.Format = DXGI_FORMAT_R8_UNORM;
        atlasDescription.SampleDesc.Count = 1;
        atlasDescription.Usage = D3D11_USAGE_IMMUTABLE;
        atlasDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA atlasData{};
        atlasData.pSysMem = kMatrixRainGlyphAtlas.data();
        atlasData.SysMemPitch = kMatrixRainGlyphAtlasSize;
        wil::com_ptr_nothrow<ID3D11Texture2D> atlas;
        result = device->CreateTexture2D(&atlasDescription, &atlasData, atlas.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC atlasViewDescription{};
        atlasViewDescription.Format = atlasDescription.Format;
        atlasViewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        atlasViewDescription.Texture2D.MipLevels = 1;
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> atlasView;
        result = device->CreateShaderResourceView(atlas.get(), &atlasViewDescription, atlasView.put());
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

        D3D11_BLEND_DESC opaqueBlendDescription{};
        opaqueBlendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        wil::com_ptr_nothrow<ID3D11BlendState> opaqueBlend;
        result = device->CreateBlendState(&opaqueBlendDescription, opaqueBlend.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_BLEND_DESC alphaBlendDescription{};
        D3D11_RENDER_TARGET_BLEND_DESC& alphaTarget = alphaBlendDescription.RenderTarget[0];
        alphaTarget.BlendEnable = TRUE;
        alphaTarget.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        alphaTarget.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        alphaTarget.BlendOp = D3D11_BLEND_OP_ADD;
        alphaTarget.SrcBlendAlpha = D3D11_BLEND_ONE;
        alphaTarget.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        alphaTarget.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        alphaTarget.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        wil::com_ptr_nothrow<ID3D11BlendState> alphaBlend;
        result = device->CreateBlendState(&alphaBlendDescription, alphaBlend.put());
        if (FAILED(result))
        {
            return result;
        }

        D3D11_BUFFER_DESC constantDescription{};
        constantDescription.ByteWidth = sizeof(MatrixRainConstants);
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
        _glyphVertexShader = std::move(glyphVertexShader);
        _glyphPixelShader = std::move(glyphPixelShader);
        _atlas = std::move(atlas);
        _atlasView = std::move(atlasView);
        _sampler = std::move(sampler);
        _rasterizer = std::move(rasterizer);
        _depthState = std::move(depthState);
        _opaqueBlend = std::move(opaqueBlend);
        _alphaBlend = std::move(alphaBlend);
        _constantBuffer = std::move(constantBuffer);
        gLiveDeviceResourceSetCount.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
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
        _glyphPixelShader.reset();
        _glyphVertexShader.reset();
        _backgroundPixelShader.reset();
        _backgroundVertexShader.reset();
        _deviceIdentity = nullptr;
        _grid = {};
    }

    [[nodiscard]] HRESULT Render(ID3D11DeviceContext* context, const RedXeWidgetFrameContext& frame) noexcept
    {
        if (!context || !_constantBuffer)
        {
            return E_UNEXPECTED;
        }

        UpdateGrid(frame);
        const MatrixRainConstants constants{
            {frame.widthPixels, frame.heightPixels, _configuration.seed, _grid.rows},
            {_grid.columns, _grid.activeColumns, _grid.permutationMultiplier, _grid.permutationOffset},
            {_configuration.trailLengthGlyphs, _configuration.mutationPerSecond, 8, 8},
            {_grid.cellWidth, _grid.cellHeight, frame.elapsedSeconds,
             7.2f * static_cast<float>(_configuration.speedPercent) / 100.0f},
            {_headColor[0], _headColor[1], _headColor[2], _headColor[3]},
            {_trailColor[0], _trailColor[1], _trailColor[2], _trailColor[3]},
            {_backgroundColor[0], _backgroundColor[1], _backgroundColor[2], _backgroundColor[3]},
            {static_cast<float>(_configuration.glowPercent) / 100.0f, _grid.horizontalMargin, 0.0f, 0.0f},
        };

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT result = context->Map(_constantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result))
        {
            return result;
        }
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        context->Unmap(_constantBuffer.get(), 0);

        constexpr std::array blendFactor{0.0f, 0.0f, 0.0f, 0.0f};
        ID3D11Buffer* constantBuffers[] = {_constantBuffer.get()};
        ID3D11SamplerState* samplers[] = {_sampler.get()};
        ID3D11ShaderResourceView* atlasViews[] = {_atlasView.get()};
        ID3D11ShaderResourceView* noViews[] = {nullptr};

        context->IASetInputLayout(nullptr);
        context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
        context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->CSSetShader(nullptr, nullptr, 0);
        context->RSSetState(_rasterizer.get());
        context->OMSetDepthStencilState(_depthState.get(), 0);
        context->VSSetConstantBuffers(0, 1, constantBuffers);
        context->PSSetConstantBuffers(0, 1, constantBuffers);

        context->VSSetShader(_backgroundVertexShader.get(), nullptr, 0);
        context->PSSetShader(_backgroundPixelShader.get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, noViews);
        context->OMSetBlendState(_opaqueBlend.get(), blendFactor.data(), UINT_MAX);
        context->Draw(3, 0);

        if (_grid.instanceCount != 0)
        {
            context->VSSetShader(_glyphVertexShader.get(), nullptr, 0);
            context->PSSetShader(_glyphPixelShader.get(), nullptr, 0);
            context->PSSetShaderResources(0, 1, atlasViews);
            context->PSSetSamplers(0, 1, samplers);
            context->OMSetBlendState(_alphaBlend.get(), blendFactor.data(), UINT_MAX);
            context->DrawInstanced(6, _grid.instanceCount, 0, 0);
            context->PSSetShaderResources(0, 1, noViews);
        }
        return S_OK;
    }

  private:
    void UpdateGrid(const RedXeWidgetFrameContext& frame) noexcept
    {
        if (_grid.width == frame.widthPixels && _grid.height == frame.heightPixels && _grid.dpi == frame.dpi)
        {
            return;
        }

        GridCache grid{};
        grid.width = frame.widthPixels;
        grid.height = frame.heightPixels;
        grid.dpi = frame.dpi;
        grid.cellHeight = static_cast<float>(_configuration.glyphHeightDips) * static_cast<float>(frame.dpi) /
                          static_cast<float>(USER_DEFAULT_SCREEN_DPI);
        if (grid.cellHeight < 1.0f)
        {
            grid.cellHeight = 1.0f;
        }
        grid.cellWidth = grid.cellHeight * 0.625f;
        if (grid.cellWidth < 1.0f)
        {
            grid.cellWidth = 1.0f;
        }

        const double columns = std::floor(static_cast<double>(frame.widthPixels) / grid.cellWidth);
        grid.columns = columns < 1.0 ? 1U : static_cast<uint32_t>(columns);
        const double rows = std::ceil(static_cast<double>(frame.heightPixels) / grid.cellHeight) + 1.0;
        grid.rows = rows >= static_cast<double>(std::numeric_limits<uint32_t>::max())
                        ? std::numeric_limits<uint32_t>::max()
                        : static_cast<uint32_t>(rows);

        const uint64_t desiredActive =
            (static_cast<uint64_t>(grid.columns) * _configuration.densityPercent + 99U) / 100U;
        const uint32_t maximumActive =
            grid.rows == 0 ? 0 : static_cast<uint32_t>(kMaximumGlyphInstances / grid.rows);
        grid.activeColumns = static_cast<uint32_t>(desiredActive > maximumActive ? maximumActive : desiredActive);
        grid.instanceCount = grid.activeColumns * grid.rows;

        if (grid.columns > 1)
        {
            uint32_t multiplier = (Hash(_configuration.seed ^ grid.columns) | 1U) % grid.columns;
            if (multiplier == 0)
            {
                multiplier = 1;
            }
            while (GreatestCommonDivisor(multiplier, grid.columns) != 1)
            {
                ++multiplier;
                if (multiplier >= grid.columns)
                {
                    multiplier = 1;
                }
            }
            grid.permutationMultiplier = multiplier;
            grid.permutationOffset = Hash(_configuration.seed ^ 0xA511E9B3U) % grid.columns;
        }

        const float contentWidth = static_cast<float>(grid.columns) * grid.cellWidth;
        grid.horizontalMargin = contentWidth < static_cast<float>(frame.widthPixels)
                                    ? (static_cast<float>(frame.widthPixels) - contentWidth) * 0.5f
                                    : 0.0f;
        _grid = grid;
    }

    MatrixRainConfiguration _configuration;
    std::array<float, 4> _headColor;
    std::array<float, 4> _trailColor;
    std::array<float, 4> _backgroundColor;
    ID3D11Device* _deviceIdentity = nullptr;
    GridCache _grid{};
    wil::com_ptr_nothrow<ID3D11VertexShader> _backgroundVertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _backgroundPixelShader;
    wil::com_ptr_nothrow<ID3D11VertexShader> _glyphVertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _glyphPixelShader;
    wil::com_ptr_nothrow<ID3D11Texture2D> _atlas;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _atlasView;
    wil::com_ptr_nothrow<ID3D11SamplerState> _sampler;
    wil::com_ptr_nothrow<ID3D11RasterizerState> _rasterizer;
    wil::com_ptr_nothrow<ID3D11DepthStencilState> _depthState;
    wil::com_ptr_nothrow<ID3D11BlendState> _opaqueBlend;
    wil::com_ptr_nothrow<ID3D11BlendState> _alphaBlend;
    wil::com_ptr_nothrow<ID3D11Buffer> _constantBuffer;
};

class MatrixRainWidget final : public IRedXeWidget, public IRedXeGpuWidget
{
  public:
    MatrixRainWidget(wil::com_ptr_nothrow<IRedXeWidgetProvider>&& providerOwner, MatrixRainDeviceResources& resources,
                     std::atomic<bool>& widgetClaim) noexcept
        : _providerOwner(std::move(providerOwner)), _resources(&resources), _widgetClaim(&widgetClaim)
    {
        gLiveWidgetCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~MatrixRainWidget()
    {
        _widgetClaim->store(false, std::memory_order_release);
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
        return _resources->Initialize(context->device);
    }

    void STDMETHODCALLTYPE OnDeviceLost() noexcept override
    {
        _resources->Reset();
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

        const RedXeWidgetFrameContext& widget = *context->widget;
        if (widget.sizeBytes != sizeof(RedXeWidgetFrameContext) || widget.dpi == 0 ||
            !std::isfinite(widget.elapsedSeconds) || !std::isfinite(widget.deltaSeconds) ||
            widget.elapsedSeconds < 0.0f || widget.deltaSeconds < 0.0f || !std::isfinite(context->viewport.TopLeftX) ||
            !std::isfinite(context->viewport.TopLeftY) || !std::isfinite(context->viewport.Width) ||
            !std::isfinite(context->viewport.Height) || !std::isfinite(context->viewport.MinDepth) ||
            !std::isfinite(context->viewport.MaxDepth) || context->viewport.TopLeftX < 0.0f ||
            context->viewport.TopLeftY < 0.0f || context->viewport.Width < 0.0f || context->viewport.Height < 0.0f ||
            context->viewport.MinDepth < 0.0f || context->viewport.MaxDepth > 1.0f ||
            context->viewport.MinDepth > context->viewport.MaxDepth)
        {
            return E_INVALIDARG;
        }
        if (widget.widthPixels == 0 || widget.heightPixels == 0 || context->viewport.Width == 0.0f ||
            context->viewport.Height == 0.0f)
        {
            return S_OK;
        }
        return _resources->Render(context->deviceContext, widget);
    }

  private:
    std::atomic<ULONG> _references{1};
    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    MatrixRainDeviceResources* _resources;
    std::atomic<bool>* _widgetClaim;
};

class MatrixRainProvider final : public IRedXeWidgetProvider
{
  public:
    explicit MatrixRainProvider(const MatrixRainConfiguration& configuration) noexcept : _resources(configuration)
    {
        gLiveProviderCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~MatrixRainProvider()
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

        bool expected = false;
        if (!_widgetClaim.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES);
        }

        wil::com_ptr_nothrow<IRedXeWidgetProvider> providerOwner;
        HRESULT result = QueryInterface(__uuidof(IRedXeWidgetProvider), reinterpret_cast<void**>(providerOwner.put()));
        if (FAILED(result))
        {
            _widgetClaim.store(false, std::memory_order_release);
            return result;
        }

        auto* created = new (std::nothrow) MatrixRainWidget(std::move(providerOwner), _resources, _widgetClaim);
        if (!created)
        {
            _widgetClaim.store(false, std::memory_order_release);
            return E_OUTOFMEMORY;
        }
        *widget = static_cast<IRedXeWidget*>(created);
        return S_OK;
    }

  private:
    std::atomic<ULONG> _references{1};
    std::atomic<bool> _widgetClaim{false};
    MatrixRainDeviceResources _resources;
};

static_assert(kMatrixRainGlyphAtlas.size() <= 128U * 1024U);
static_assert(sizeof(MatrixRainDeviceResources) < 64U * 1024U);
static_assert(sizeof(MatrixRainProvider) < 64U * 1024U);
static_assert(sizeof(MatrixRainWidget) < 4U * 1024U);

HRESULT CreateMatrixRainProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost*,
                                 void** result) noexcept
{
    if (interfaceId != __uuidof(IRedXeWidgetProvider))
    {
        return E_NOINTERFACE;
    }

    MatrixRainConfiguration configuration{};
    HRESULT createResult = ReadFactoryConfiguration(options, configuration);
    if (FAILED(createResult))
    {
        return createResult;
    }

    auto* provider = new (std::nothrow) MatrixRainProvider(configuration);
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = provider;
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateMatrixRainProvider},
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
    return RedXeEnumerateFactoryMetadata(kMetadata.data(), static_cast<uint32_t>(kMetadata.size()), metadata,
                                         count);
}

extern "C" HRESULT __stdcall RedXeGetPluginSettingsContract(const char* pluginId,
                                                            const RedXePluginSettingsContract** contract) noexcept
{
    return RedXeGetStaticPluginSettingsContract(kPluginId, pluginId, &kSettingsContract, contract);
}

extern "C" void __stdcall RedXePluginShutdown() noexcept {}

extern "C" HRESULT __stdcall RedXeMatrixRainGetTestDiagnostics(MatrixRainTestDiagnostics* diagnostics) noexcept
{
    if (!diagnostics)
    {
        return E_POINTER;
    }
    if (diagnostics->sizeBytes != sizeof(MatrixRainTestDiagnostics))
    {
        return E_INVALIDARG;
    }

    diagnostics->liveProviderCount = gLiveProviderCount.load(std::memory_order_relaxed);
    diagnostics->liveWidgetCount = gLiveWidgetCount.load(std::memory_order_relaxed);
    diagnostics->liveDeviceResourceSetCount = gLiveDeviceResourceSetCount.load(std::memory_order_relaxed);
    return S_OK;
}
