#define REDXE_PLUGIN_EXPORTS
#include "PlugInterfaces/FactoryImpl.h"
#include "PlugInterfaces/Widget.h"

#include "ShadersSettings.h"
#include "ShadersTestContract.h"

// Build-time Shader Model 5.0 blobs (FxCompile in 5H4D3R5.vcxproj). One vertex shader, one blit shader, and one
// image shader per catalog entry plus a feedback-buffer shader for the two multi-pass ports.
#include "ShadersBaseWarpFbmPixelShader.h"
#include "ShadersBlitPixelShader.h"
#include "ShadersCineShaderLavaPixelShader.h"
#include "ShadersCosmicOrbPixelShader.h"
#include "ShadersFlammesVortexBufferAPixelShader.h"
#include "ShadersFlammesVortexImagePixelShader.h"
#include "ShadersFluidSolverBufferAPixelShader.h"
#include "ShadersFluidSolverImagePixelShader.h"
#include "ShadersFractalPyramidPixelShader.h"
#include "ShadersHeartfeltPixelShader.h"
#include "ShadersNeonPulseFractalPixelShader.h"
#include "ShadersOctagramsPixelShader.h"
#include "ShadersProteanCloudsPixelShader.h"
#include "ShadersSeascapePixelShader.h"
#include "ShadersSkyAtmosphereMultiScatteringLutPixelShader.h"
#include "ShadersSkyAtmospherePixelShader.h"
#include "ShadersSkyAtmosphereTransmittanceLutPixelShader.h"
#include "ShadersSynthwaveSunsetPixelShader.h"
#include "ShadersTheDriveHomePixelShader.h"
#include "ShadersVertexShader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#pragma warning(pop)

namespace
{
using Shaders::Configuration;
using Shaders::kShaderCount;
using Shaders::kShaders;
using Shaders::Mode;

constexpr RedXePluginSettingsContract kSettingsContract{
    sizeof(RedXePluginSettingsContract), Shaders::kSchemaJson, sizeof(Shaders::kSchemaJson) - 1, Shaders::kDefaultsJson,
    sizeof(Shaders::kDefaultsJson) - 1,
};

constexpr std::array kMetadata{
    RedXePluginMetadata{
        sizeof(RedXePluginMetadata),
        Shaders::kPluginId,
        L"5H4D3R5",
        L"Bundled build-time HLSL shaders and demos (twelve Shadertoy ports and a RedXe orb): one, a random one, or "
        L"a fading slideshow.",
        L"RedXe",
        L"1.0.0",
        RedXePluginCapabilityWidgetProvider,
    },
};

constexpr std::array kWidgetTypes{
    RedXeWidgetTypeDescriptor{
        sizeof(RedXeWidgetTypeDescriptor),
        Shaders::kWidgetTypeId,
        L"5H4D3R5",
        L"A continuously animated full-screen pixel shader from the bundled catalog.",
        2560.0f,
        720.0f,
        160.0f,
        90.0f,
        RedXeWidgetFlagContinuousAnimation,
    },
};

struct ShaderBlob final
{
    const BYTE* data = nullptr;
    size_t bytes = 0;
};

// A lookup table an entry builds once per device before its first frame: a fixed-size texture drawn by one pass.
// The second table's pass reads the first on iChannel0; the image pass reads them on iChannel0 and iChannel1.
struct LookupTablePass final
{
    ShaderBlob shader;
    uint32_t width = 0;
    uint32_t height = 0;
};

constexpr uint32_t kLookupTableSlots = 2;

// Image shader and optional feedback-buffer or lookup-table shaders per catalog entry, in catalog order (checked by
// name). An entry uses feedback buffers or lookup tables, never both.
struct ShaderProgram final
{
    const char* name;
    ShaderBlob image;
    ShaderBlob buffer;
    // Sampler the feedback buffer is read with, following the channel settings of the source (Shadertoy wrap mode).
    bool bufferWraps;
    std::array<LookupTablePass, kLookupTableSlots> lookupTables;
};

#define SHADERS_BLOB(symbol)                                                                                           \
    ShaderBlob                                                                                                         \
    {                                                                                                                  \
        symbol, sizeof(symbol)                                                                                         \
    }

constexpr std::array<ShaderProgram, kShaderCount> kPrograms{
    ShaderProgram{"fluid-solver", SHADERS_BLOB(g_ShadersFluidSolverImagePixelShader),
                  SHADERS_BLOB(g_ShadersFluidSolverBufferAPixelShader), true},
    ShaderProgram{"cineshader-lava", SHADERS_BLOB(g_ShadersCineShaderLavaPixelShader), {}, false},
    ShaderProgram{"synthwave-sunset", SHADERS_BLOB(g_ShadersSynthwaveSunsetPixelShader), {}, false},
    ShaderProgram{"seascape", SHADERS_BLOB(g_ShadersSeascapePixelShader), {}, false},
    ShaderProgram{"warp-fbm", SHADERS_BLOB(g_ShadersBaseWarpFbmPixelShader), {}, false},
    ShaderProgram{"fractal-pyramid", SHADERS_BLOB(g_ShadersFractalPyramidPixelShader), {}, false},
    ShaderProgram{"octagrams", SHADERS_BLOB(g_ShadersOctagramsPixelShader), {}, false},
    ShaderProgram{"heartfelt", SHADERS_BLOB(g_ShadersHeartfeltPixelShader), {}, false},
    ShaderProgram{"protean-clouds", SHADERS_BLOB(g_ShadersProteanCloudsPixelShader), {}, false},
    ShaderProgram{"drive-home", SHADERS_BLOB(g_ShadersTheDriveHomePixelShader), {}, false},
    ShaderProgram{"flammes-vortex", SHADERS_BLOB(g_ShadersFlammesVortexImagePixelShader),
                  SHADERS_BLOB(g_ShadersFlammesVortexBufferAPixelShader), false},
    ShaderProgram{"neon-pulse", SHADERS_BLOB(g_ShadersNeonPulseFractalPixelShader), {}, false},
    ShaderProgram{"cosmic-orb", SHADERS_BLOB(g_ShadersCosmicOrbPixelShader), {}, false},
    ShaderProgram{"sky-atmosphere",
                  SHADERS_BLOB(g_ShadersSkyAtmospherePixelShader),
                  {},
                  false,
                  {LookupTablePass{SHADERS_BLOB(g_ShadersSkyAtmosphereTransmittanceLutPixelShader), 256, 64},
                   LookupTablePass{SHADERS_BLOB(g_ShadersSkyAtmosphereMultiScatteringLutPixelShader), 32, 32}}},
};

#undef SHADERS_BLOB

consteval bool ProgramsMatchCatalog() noexcept
{
    for (uint32_t index = 0; index < kShaderCount; ++index)
    {
        const ShaderProgram& program = kPrograms[index];
        if (std::string_view(program.name) != kShaders[index].name || !program.image.data || program.image.bytes == 0 ||
            (kShaders[index].feedbackBuffer != (program.buffer.data != nullptr)))
        {
            return false;
        }
        const bool tables = program.lookupTables[0].shader.data != nullptr;
        if ((tables && program.buffer.data) ||
            (program.lookupTables[1].shader.data && !program.lookupTables[0].shader.data))
        {
            return false;
        }
        for (const LookupTablePass& table : program.lookupTables)
        {
            if (table.shader.data && (table.width == 0 || table.height == 0))
            {
                return false;
            }
        }
    }
    return true;
}

static_assert(ProgramsMatchCatalog());

// Mirrors cbuffer ShadersConstants in Shaders/ShadersCommon.hlsli.
struct alignas(16) ShadersConstants final
{
    float resolution[3];
    float time;
    float mouse[4];
    float timeDelta;
    float frameRate;
    int32_t frame;
    float fade;
    float viewportOrigin[2];
    float blitScale[2];
    float date[4];
    float background[4];
};

static_assert(sizeof(ShadersConstants) == 96);

constexpr uint32_t kBackgroundTextureSize = 512;
constexpr uint32_t kBackgroundMipCount = 10;
static_assert((1U << (kBackgroundMipCount - 1)) == kBackgroundTextureSize);

std::atomic<uint32_t> gLiveProviderCount{0};
std::atomic<uint32_t> gLiveWidgetCount{0};
std::atomic<uint32_t> gLiveSharedResourceSetCount{0};
std::atomic<uint32_t> gLastShaderIndex{0};
std::atomic<uint32_t> gLastShaderFrame{0};

[[nodiscard]] uint32_t Hash(uint32_t value) noexcept
{
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

// Host-resolved dashboard background as the float color the fade passes through (RedXeFactoryOptions rgb).
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

    [[nodiscard]] bool ReadBool(bool& value) noexcept
    {
        SkipWhitespace();
        if (_text.substr(_offset, 4) == "true")
        {
            _offset += 4;
            value = true;
            return true;
        }
        if (_text.substr(_offset, 5) == "false")
        {
            _offset += 5;
            value = false;
            return true;
        }
        return false;
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

enum ConfigurationMember : uint32_t
{
    ConfigurationMode = 1U << 0U,
    ConfigurationShader = 1U << 1U,
    ConfigurationInterval = 1U << 2U,
    ConfigurationShuffle = 1U << 3U,
    ConfigurationRenderScale = 1U << 4U,
};

inline constexpr uint32_t kAllConfigurationMembers = (1U << 5U) - 1U;

[[nodiscard]] uint32_t ConfigurationMemberForKey(std::string_view key) noexcept
{
    constexpr std::array members{
        std::pair<std::string_view, uint32_t>{"mode", ConfigurationMode},
        std::pair<std::string_view, uint32_t>{"shader", ConfigurationShader},
        std::pair<std::string_view, uint32_t>{"intervalSeconds", ConfigurationInterval},
        std::pair<std::string_view, uint32_t>{"shuffle", ConfigurationShuffle},
        std::pair<std::string_view, uint32_t>{"renderScalePercent", ConfigurationRenderScale},
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

// The instance object of the normalized envelope: every member present exactly once, values inside the published
// schema. The host merges the defaults before it hands the object over, so a missing member is a host defect.
[[nodiscard]] bool ParseInstanceObject(JsonCursor& cursor, Configuration& configuration) noexcept
{
    if (!cursor.Consume('{'))
    {
        return false;
    }

    Configuration parsed{};
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
        case ConfigurationMode:
            if (!cursor.ReadString(text) || !Shaders::TryParseMode(text, parsed.mode))
            {
                return false;
            }
            break;
        case ConfigurationShader:
            if (!cursor.ReadString(text) || !Shaders::TryFindShader(text, parsed.shaderIndex))
            {
                return false;
            }
            break;
        case ConfigurationInterval:
            if (!cursor.ReadUnsigned(value) || !Shaders::IsValidIntervalSeconds(value))
            {
                return false;
            }
            parsed.intervalSeconds = value;
            break;
        case ConfigurationShuffle:
            if (!cursor.ReadBool(parsed.shuffle))
            {
                return false;
            }
            break;
        case ConfigurationRenderScale:
            if (!cursor.ReadUnsigned(value) || !Shaders::IsValidRenderScalePercent(value))
            {
                return false;
            }
            parsed.renderScalePercent = value;
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

[[nodiscard]] bool ParseNormalizedConfiguration(std::string_view json, Configuration& configuration) noexcept
{
    JsonCursor cursor(json);
    if (!cursor.Consume('{'))
    {
        return false;
    }

    constexpr uint32_t pluginSeen = 1U << 0U;
    constexpr uint32_t instanceSeen = 1U << 1U;
    uint32_t seen = 0;
    Configuration parsed{};
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
            if ((seen & instanceSeen) != 0 || !ParseInstanceObject(cursor, parsed))
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

[[nodiscard]] HRESULT ReadFactoryConfiguration(const RedXeFactoryOptions* options, Configuration& configuration,
                                               uint32_t& backgroundRgb) noexcept
{
    configuration = Configuration{};
    backgroundRgb = kRedXeDefaultBackgroundColor & 0x00FFFFFFu;
    if (!options)
    {
        return S_OK;
    }
    if (options->sizeBytes != sizeof(RedXeFactoryOptions))
    {
        return E_INVALIDARG;
    }
    backgroundRgb = RedXeBackgroundRgb(options);

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

    Configuration parsed{};
    if (!ParseNormalizedConfiguration(std::string_view(json, bytes), parsed))
    {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    configuration = parsed;
    return S_OK;
}

// Whether the widget can ever draw a port that needs feedback buffers, decided once so the buffers are allocated
// only for widgets that will use them.
[[nodiscard]] bool MayUseFeedbackBuffers(const Configuration& configuration, uint32_t randomShaderIndex) noexcept
{
    switch (configuration.mode)
    {
    case Mode::Single:
        return kShaders[configuration.shaderIndex].feedbackBuffer;
    case Mode::Random:
        return kShaders[randomShaderIndex].feedbackBuffer;
    case Mode::Slideshow:
    default:
        return true;
    }
}

// The procedural stand-in for the photograph Heartfelt samples on Shadertoy: a night street behind wet glass, dark
// blue above a warm horizon with soft bokeh lights in headlight, sodium, tail-light, and sign colors. Generated once
// per device with a full mip chain (the shader blurs by sampling coarse mips), never from Render.
struct BokehLight final
{
    float x;
    float y;
    float radius;
    float red;
    float green;
    float blue;
};

[[nodiscard]] float Smoothstep(float edge0, float edge1, float x) noexcept
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void FillBackgroundBaseLevel(uint32_t* pixels) noexcept
{
    constexpr uint32_t lightCount = 56;
    std::array<BokehLight, lightCount> lights{};
    uint32_t state = 0x5EA5C4FEU;
    const auto next = [&state]() noexcept
    {
        state = Hash(state + 0x9E3779B9U);
        return static_cast<float>(state & 0xFFFFFFU) / static_cast<float>(0x1000000U);
    };
    for (BokehLight& light : lights)
    {
        light.x = next();
        // Lights gather around the street: headlights and tail lights low, signs and lamps higher up.
        const float band = next();
        light.y = band < 0.55f ? 0.42f + next() * 0.30f : 0.18f + next() * 0.32f;
        light.radius = 0.012f + next() * next() * 0.05f;
        const float palette = next();
        const float brightness = 0.35f + next() * 0.65f;
        // Sodium street lamp, headlight, tail light, or sign.
        const std::array<float, 3> color = palette < 0.34f   ? std::array{1.0f, 0.72f, 0.35f}
                                           : palette < 0.62f ? std::array{0.80f, 0.88f, 1.0f}
                                           : palette < 0.85f ? std::array{1.0f, 0.22f, 0.16f}
                                                             : std::array{0.30f, 0.95f, 0.75f};
        light.red = color[0] * brightness;
        light.green = color[1] * brightness;
        light.blue = color[2] * brightness;
    }

    constexpr float scale = 1.0f / static_cast<float>(kBackgroundTextureSize);
    for (uint32_t y = 0; y < kBackgroundTextureSize; ++y)
    {
        const float v = (static_cast<float>(y) + 0.5f) * scale;
        for (uint32_t x = 0; x < kBackgroundTextureSize; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) * scale;
            // Sky to wet asphalt with a warm haze along the horizon.
            float red = 0.03f + v * 0.09f;
            float green = 0.04f + v * 0.08f;
            float blue = 0.09f + v * 0.06f;
            const float horizon = std::exp(-((v - 0.52f) * (v - 0.52f)) / 0.02f);
            red += horizon * 0.16f;
            green += horizon * 0.12f;
            blue += horizon * 0.09f;
            for (const BokehLight& light : lights)
            {
                const float dx = (u - light.x) * 1.35f;
                const float dy = v - light.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance >= light.radius * 1.9f)
                {
                    continue;
                }
                const float disk = Smoothstep(light.radius, light.radius * 0.55f, distance);
                const float halo = Smoothstep(light.radius * 1.9f, light.radius, distance) * 0.25f;
                const float weight = disk + halo;
                red += light.red * weight;
                green += light.green * weight;
                blue += light.blue * weight;
                // A faint reflection streak below every light on the wet road.
                if (dy > 0.0f && dy < light.radius * 6.0f && std::fabs(dx) < light.radius * 0.6f)
                {
                    const float streak = (1.0f - dy / (light.radius * 6.0f)) * 0.12f;
                    red += light.red * streak;
                    green += light.green * streak;
                    blue += light.blue * streak;
                }
            }
            const auto channel = [](float value) noexcept
            { return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
            pixels[static_cast<size_t>(y) * kBackgroundTextureSize + x] =
                0xFF000000U | (channel(blue) << 16U) | (channel(green) << 8U) | channel(red);
        }
    }
}

// Box-filters level `size` into level `size / 2`.
void DownsampleLevel(const uint32_t* source, uint32_t size, uint32_t* destination) noexcept
{
    const uint32_t half = size / 2;
    for (uint32_t y = 0; y < half; ++y)
    {
        for (uint32_t x = 0; x < half; ++x)
        {
            uint32_t sums[4] = {0, 0, 0, 0};
            for (uint32_t dy = 0; dy < 2; ++dy)
            {
                for (uint32_t dx = 0; dx < 2; ++dx)
                {
                    const uint32_t texel = source[static_cast<size_t>(y * 2 + dy) * size + (x * 2 + dx)];
                    sums[0] += texel & 0xFFU;
                    sums[1] += (texel >> 8U) & 0xFFU;
                    sums[2] += (texel >> 16U) & 0xFFU;
                    sums[3] += (texel >> 24U) & 0xFFU;
                }
            }
            destination[static_cast<size_t>(y) * half + x] = ((sums[3] + 2U) / 4U) << 24U |
                                                             ((sums[2] + 2U) / 4U) << 16U |
                                                             ((sums[1] + 2U) / 4U) << 8U | ((sums[0] + 2U) / 4U);
        }
    }
}

[[nodiscard]] HRESULT CreateBackgroundTexture(ID3D11Device* device,
                                              wil::com_ptr_nothrow<ID3D11ShaderResourceView>& view) noexcept
{
    // Every mip level in one heap block: 512² + 256² + ... + 1² texels.
    constexpr size_t totalTexels =
        (static_cast<size_t>(kBackgroundTextureSize) * kBackgroundTextureSize * 4U) / 3U + 1U;
    std::unique_ptr<uint32_t[]> texels(new (std::nothrow) uint32_t[totalTexels]);
    if (!texels)
    {
        return E_OUTOFMEMORY;
    }

    std::array<D3D11_SUBRESOURCE_DATA, kBackgroundMipCount> levels{};
    size_t offset = 0;
    uint32_t size = kBackgroundTextureSize;
    FillBackgroundBaseLevel(texels.get());
    for (uint32_t level = 0; level < kBackgroundMipCount; ++level)
    {
        levels[level].pSysMem = texels.get() + offset;
        levels[level].SysMemPitch = size * sizeof(uint32_t);
        if (level + 1 < kBackgroundMipCount)
        {
            const size_t nextOffset = offset + static_cast<size_t>(size) * size;
            DownsampleLevel(texels.get() + offset, size, texels.get() + nextOffset);
            offset = nextOffset;
            size /= 2;
        }
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = kBackgroundTextureSize;
    description.Height = kBackgroundTextureSize;
    description.MipLevels = kBackgroundMipCount;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    HRESULT result = device->CreateTexture2D(&description, levels.data(), texture.put());
    if (FAILED(result))
    {
        return result;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
    viewDescription.Format = description.Format;
    viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDescription.Texture2D.MipLevels = kBackgroundMipCount;
    return device->CreateShaderResourceView(texture.get(), &viewDescription, view.put());
}

// A texture drawn by one pass and sampled by the next.
struct RenderTexture final
{
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    wil::com_ptr_nothrow<ID3D11RenderTargetView> target;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> view;
    uint32_t width = 0;
    uint32_t height = 0;

    void Reset() noexcept
    {
        view.reset();
        target.reset();
        texture.reset();
        width = 0;
        height = 0;
    }

    [[nodiscard]] HRESULT Create(ID3D11Device* device, uint32_t textureWidth, uint32_t textureHeight,
                                 DXGI_FORMAT format) noexcept
    {
        Reset();
        D3D11_TEXTURE2D_DESC description{};
        description.Width = textureWidth;
        description.Height = textureHeight;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = format;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT result = device->CreateTexture2D(&description, nullptr, texture.put());
        if (SUCCEEDED(result))
        {
            result = device->CreateRenderTargetView(texture.get(), nullptr, target.put());
        }
        if (SUCCEEDED(result))
        {
            result = device->CreateShaderResourceView(texture.get(), nullptr, view.put());
        }
        if (FAILED(result))
        {
            Reset();
            return result;
        }
        width = textureWidth;
        height = textureHeight;
        return S_OK;
    }
};

// Pointer travel between Down and Up that still counts as a tap, in DIPs.
constexpr float kTapSlopDips = 24.0f;

// Immutable device resources every widget of a provider shares: the shader objects, samplers, pipeline states, and
// the Heartfelt background. Built by the first OnDeviceCreated for a device, released by any OnDeviceLost.
class SharedDeviceResources final
{
  public:
    SharedDeviceResources() = default;
    ~SharedDeviceResources()
    {
        Reset();
    }
    SharedDeviceResources(const SharedDeviceResources&) = delete;
    SharedDeviceResources& operator=(const SharedDeviceResources&) = delete;

    [[nodiscard]] HRESULT Initialize(ID3D11Device* device) noexcept
    {
        if (!device)
        {
            return E_POINTER;
        }
        if (_deviceIdentity == device && _vertexShader)
        {
            return S_OK;
        }

        wil::com_ptr_nothrow<ID3D11VertexShader> vertexShader;
        HRESULT result = device->CreateVertexShader(g_ShadersVertexShader, sizeof(g_ShadersVertexShader), nullptr,
                                                    vertexShader.put());
        if (FAILED(result))
        {
            return result;
        }
        wil::com_ptr_nothrow<ID3D11PixelShader> blitShader;
        result = device->CreatePixelShader(g_ShadersBlitPixelShader, sizeof(g_ShadersBlitPixelShader), nullptr,
                                           blitShader.put());
        if (FAILED(result))
        {
            return result;
        }
        std::array<wil::com_ptr_nothrow<ID3D11PixelShader>, kShaderCount> imageShaders;
        std::array<wil::com_ptr_nothrow<ID3D11PixelShader>, kShaderCount> bufferShaders;
        for (uint32_t index = 0; index < kShaderCount; ++index)
        {
            const ShaderProgram& program = kPrograms[index];
            result =
                device->CreatePixelShader(program.image.data, program.image.bytes, nullptr, imageShaders[index].put());
            if (FAILED(result))
            {
                return result;
            }
            if (program.buffer.data)
            {
                result = device->CreatePixelShader(program.buffer.data, program.buffer.bytes, nullptr,
                                                   bufferShaders[index].put());
                if (FAILED(result))
                {
                    return result;
                }
            }
        }
        // Lookup tables: their shaders and their (empty) textures. The tables are drawn by the first widget frame
        // that needs them, never here, because device creation has no immediate context.
        std::array<LookupTableSet, kShaderCount> lookupTables;
        for (uint32_t index = 0; index < kShaderCount; ++index)
        {
            const ShaderProgram& program = kPrograms[index];
            for (uint32_t slot = 0; slot < kLookupTableSlots; ++slot)
            {
                const LookupTablePass& pass = program.lookupTables[slot];
                if (!pass.shader.data)
                {
                    continue;
                }
                result = device->CreatePixelShader(pass.shader.data, pass.shader.bytes, nullptr,
                                                   lookupTables[index].shaders[slot].put());
                if (SUCCEEDED(result))
                {
                    result = lookupTables[index].textures[slot].Create(device, pass.width, pass.height,
                                                                       DXGI_FORMAT_R32G32B32A32_FLOAT);
                }
                if (FAILED(result))
                {
                    return result;
                }
            }
        }

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        wil::com_ptr_nothrow<ID3D11SamplerState> clampSampler;
        result = device->CreateSamplerState(&samplerDescription, clampSampler.put());
        if (FAILED(result))
        {
            return result;
        }
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        wil::com_ptr_nothrow<ID3D11SamplerState> wrapSampler;
        result = device->CreateSamplerState(&samplerDescription, wrapSampler.put());
        if (FAILED(result))
        {
            return result;
        }
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        wil::com_ptr_nothrow<ID3D11SamplerState> wrapMipSampler;
        result = device->CreateSamplerState(&samplerDescription, wrapMipSampler.put());
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

        D3D11_BLEND_DESC blendDescription{};
        blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        wil::com_ptr_nothrow<ID3D11BlendState> opaqueBlend;
        result = device->CreateBlendState(&blendDescription, opaqueBlend.put());
        if (FAILED(result))
        {
            return result;
        }

        wil::com_ptr_nothrow<ID3D11ShaderResourceView> background;
        result = CreateBackgroundTexture(device, background);
        if (FAILED(result))
        {
            return result;
        }

        Reset();
        _deviceIdentity = device;
        _vertexShader = std::move(vertexShader);
        _blitShader = std::move(blitShader);
        _imageShaders = std::move(imageShaders);
        _bufferShaders = std::move(bufferShaders);
        _clampSampler = std::move(clampSampler);
        _wrapSampler = std::move(wrapSampler);
        _wrapMipSampler = std::move(wrapMipSampler);
        _rasterizer = std::move(rasterizer);
        _depthState = std::move(depthState);
        _opaqueBlend = std::move(opaqueBlend);
        _background = std::move(background);
        _lookupTables = std::move(lookupTables);
        gLiveSharedResourceSetCount.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
    }

    void Reset() noexcept
    {
        if (_deviceIdentity)
        {
            gLiveSharedResourceSetCount.fetch_sub(1, std::memory_order_relaxed);
        }
        for (LookupTableSet& tables : _lookupTables)
        {
            tables.Reset();
        }
        _background.reset();
        _opaqueBlend.reset();
        _depthState.reset();
        _rasterizer.reset();
        _wrapMipSampler.reset();
        _wrapSampler.reset();
        _clampSampler.reset();
        for (auto& shader : _bufferShaders)
        {
            shader.reset();
        }
        for (auto& shader : _imageShaders)
        {
            shader.reset();
        }
        _blitShader.reset();
        _vertexShader.reset();
        _deviceIdentity = nullptr;
    }

    [[nodiscard]] bool IsReady(ID3D11Device* device) const noexcept
    {
        return _deviceIdentity == device && _vertexShader;
    }

    [[nodiscard]] ID3D11VertexShader* VertexShader() const noexcept
    {
        return _vertexShader.get();
    }
    [[nodiscard]] ID3D11PixelShader* BlitShader() const noexcept
    {
        return _blitShader.get();
    }
    [[nodiscard]] ID3D11PixelShader* ImageShader(uint32_t index) const noexcept
    {
        return _imageShaders[index].get();
    }
    [[nodiscard]] ID3D11PixelShader* BufferShader(uint32_t index) const noexcept
    {
        return _bufferShaders[index].get();
    }
    [[nodiscard]] ID3D11SamplerState* ClampSampler() const noexcept
    {
        return _clampSampler.get();
    }
    [[nodiscard]] ID3D11SamplerState* WrapSampler() const noexcept
    {
        return _wrapSampler.get();
    }
    [[nodiscard]] ID3D11SamplerState* WrapMipSampler() const noexcept
    {
        return _wrapMipSampler.get();
    }
    [[nodiscard]] ID3D11RasterizerState* Rasterizer() const noexcept
    {
        return _rasterizer.get();
    }
    [[nodiscard]] ID3D11DepthStencilState* DepthState() const noexcept
    {
        return _depthState.get();
    }
    [[nodiscard]] ID3D11BlendState* OpaqueBlend() const noexcept
    {
        return _opaqueBlend.get();
    }
    [[nodiscard]] ID3D11ShaderResourceView* Background() const noexcept
    {
        return _background.get();
    }

    // The lookup-table passes and textures of one entry (empty members for an entry without tables).
    struct LookupTableSet final
    {
        std::array<wil::com_ptr_nothrow<ID3D11PixelShader>, kLookupTableSlots> shaders;
        std::array<RenderTexture, kLookupTableSlots> textures;
        bool built = false;

        void Reset() noexcept
        {
            for (uint32_t slot = 0; slot < kLookupTableSlots; ++slot)
            {
                textures[slot].Reset();
                shaders[slot].reset();
            }
            built = false;
        }
    };

    [[nodiscard]] LookupTableSet& LookupTables(uint32_t index) noexcept
    {
        return _lookupTables[index];
    }

  private:
    ID3D11Device* _deviceIdentity = nullptr;
    wil::com_ptr_nothrow<ID3D11VertexShader> _vertexShader;
    wil::com_ptr_nothrow<ID3D11PixelShader> _blitShader;
    std::array<wil::com_ptr_nothrow<ID3D11PixelShader>, kShaderCount> _imageShaders;
    std::array<wil::com_ptr_nothrow<ID3D11PixelShader>, kShaderCount> _bufferShaders;
    wil::com_ptr_nothrow<ID3D11SamplerState> _clampSampler;
    wil::com_ptr_nothrow<ID3D11SamplerState> _wrapSampler;
    wil::com_ptr_nothrow<ID3D11SamplerState> _wrapMipSampler;
    wil::com_ptr_nothrow<ID3D11RasterizerState> _rasterizer;
    wil::com_ptr_nothrow<ID3D11DepthStencilState> _depthState;
    wil::com_ptr_nothrow<ID3D11BlendState> _opaqueBlend;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> _background;
    std::array<LookupTableSet, kShaderCount> _lookupTables;
};

// What the widget shows at one instant: which catalog entry, how far into its run, and how visible it is.
struct ShowState final
{
    uint32_t shaderIndex = 0;
    uint32_t cycle = 0;
    float shaderTime = 0.0f;
    float fade = 1.0f;
};

class ShadersWidget final
    : public RedXeComObject<ShadersWidget, IRedXeWidget, IRedXeGpuWidget, IRedXeRaisedWidget, IRedXeInteractiveWidget>
{
  public:
    ShadersWidget(wil::com_ptr_nothrow<IRedXeWidgetProvider>&& providerOwner, SharedDeviceResources& shared,
                  const Configuration& configuration, const std::array<float, 4>& background,
                  uint32_t randomSeed) noexcept
        : _providerOwner(std::move(providerOwner)), _shared(&shared), _configuration(configuration),
          _background(background), _randomSeed(randomSeed), _randomShaderIndex(Hash(randomSeed) % kShaderCount),
          _mayUseFeedbackBuffers(MayUseFeedbackBuffers(configuration, _randomShaderIndex))
    {
        gLiveWidgetCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~ShadersWidget()
    {
        ReleaseSizedResources();
        _constantBuffer.reset();
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
        *extent = RedXeRaisedExtentFull;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetRaised(BOOL) noexcept override
    {
        return S_OK;
    }

    // A slideshow skips to its next entry on a click or tap: Down is left to the host (S_FALSE), a committed Up
    // that stayed within the tap slop is consumed and pushes the virtual clock to the fade before the next cycle
    // boundary, so the current entry fades out and the next fades in exactly as a timed change does. Drags, wheel
    // samples (host paging), and every other mode leave the host's behavior untouched.
    HRESULT STDMETHODCALLTYPE OnPointer(const RedXePointerEvent* event) noexcept override
    {
        if (!event)
        {
            return E_POINTER;
        }
        if (event->sizeBytes != sizeof(RedXePointerEvent))
        {
            return E_INVALIDARG;
        }
        switch (event->phase)
        {
        case RedXePointerPhaseDown:
            _pointerDown = true;
            _pointerStartX = event->x;
            _pointerStartY = event->y;
            return S_FALSE;
        case RedXePointerPhaseUp:
        {
            const bool wasDown = _pointerDown;
            _pointerDown = false;
            if (!wasDown || _configuration.mode != Mode::Slideshow || !std::isfinite(event->x) ||
                !std::isfinite(event->y))
            {
                return S_FALSE;
            }
            const float slop = kTapSlopDips * static_cast<float>(event->dpi ? event->dpi : USER_DEFAULT_SCREEN_DPI) /
                               static_cast<float>(USER_DEFAULT_SCREEN_DPI);
            if (std::fabs(event->x - _pointerStartX) > slop || std::fabs(event->y - _pointerStartY) > slop)
            {
                return S_FALSE;
            }
            AdvanceSlideshow();
            return S_OK;
        }
        case RedXePointerPhaseCancel:
            _pointerDown = false;
            return S_FALSE;
        case RedXePointerPhaseMove:
        case RedXePointerPhaseWheel:
        case RedXePointerPhaseHorizontalWheel:
        default:
            return S_FALSE;
        }
    }

    HRESULT STDMETHODCALLTYPE OnDragOver(float, float) noexcept override
    {
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE OnDragLeave() noexcept override
    {
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE OnDrop(const RedXeDropEvent*) noexcept override
    {
        return S_FALSE;
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
        HRESULT result = _shared->Initialize(context->device);
        if (FAILED(result))
        {
            return result;
        }

        D3D11_BUFFER_DESC constantDescription{};
        constantDescription.ByteWidth = sizeof(ShadersConstants);
        constantDescription.Usage = D3D11_USAGE_DYNAMIC;
        constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        wil::com_ptr_nothrow<ID3D11Buffer> constantBuffer;
        result = context->device->CreateBuffer(&constantDescription, nullptr, constantBuffer.put());
        if (FAILED(result))
        {
            return result;
        }

        ReleaseSizedResources();
        _constantBuffer = std::move(constantBuffer);
        _device = context->device;
        _targetFormat = context->targetFormat;
        _shaderFrame = 0;
        return S_OK;
    }

    void STDMETHODCALLTYPE OnDeviceLost() noexcept override
    {
        ReleaseSizedResources();
        _constantBuffer.reset();
        _device = nullptr;
        _shared->Reset();
    }

    HRESULT STDMETHODCALLTYPE OnTargetSizeChanged(const RedXeGpuTargetSizeContext* context) noexcept override
    {
        if (!context || context->sizeBytes != sizeof(RedXeGpuTargetSizeContext))
        {
            return E_INVALIDARG;
        }
        if (!_device || context->widthPixels == 0 || context->heightPixels == 0)
        {
            return S_OK;
        }
        const uint32_t width = ScaledExtent(context->widthPixels);
        const uint32_t height = ScaledExtent(context->heightPixels);
        const bool wantOffscreen = _configuration.renderScalePercent < Shaders::kMaximumRenderScalePercent;
        const bool sizeUnchanged = width == _sizedWidth && height == _sizedHeight;
        if (sizeUnchanged && (wantOffscreen == static_cast<bool>(_offscreen.texture)) &&
            (_mayUseFeedbackBuffers == static_cast<bool>(_feedback[0].texture)))
        {
            return S_OK;
        }

        // Build the replacement set completely before swapping it in, so a failure keeps the previous resources.
        RenderTexture offscreen;
        std::array<RenderTexture, 2> feedback;
        HRESULT result = S_OK;
        if (wantOffscreen)
        {
            result = offscreen.Create(_device, width, height, _targetFormat);
        }
        if (SUCCEEDED(result) && _mayUseFeedbackBuffers)
        {
            result = feedback[0].Create(_device, width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
            if (SUCCEEDED(result))
            {
                result = feedback[1].Create(_device, width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
            }
        }
        if (FAILED(result))
        {
            return result;
        }
        ReleaseSizedResources();
        _offscreen = std::move(offscreen);
        _feedback = std::move(feedback);
        _sizedWidth = width;
        _sizedHeight = height;
        // New buffers hold no simulation state: restart the shader's frame count so it re-initializes.
        _shaderFrame = 0;
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

        const RedXeWidgetFrameContext& widget = *context->widget;
        const D3D11_VIEWPORT& viewport = context->viewport;
        if (widget.sizeBytes != sizeof(RedXeWidgetFrameContext) || widget.dpi == 0 ||
            !std::isfinite(widget.elapsedSeconds) || !std::isfinite(widget.deltaSeconds) ||
            widget.elapsedSeconds < 0.0f || widget.deltaSeconds < 0.0f || !std::isfinite(viewport.TopLeftX) ||
            !std::isfinite(viewport.TopLeftY) || !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height) ||
            !std::isfinite(viewport.MinDepth) || !std::isfinite(viewport.MaxDepth) || viewport.Width < 0.0f ||
            viewport.Height < 0.0f || viewport.MinDepth < 0.0f || viewport.MaxDepth > 1.0f ||
            viewport.MinDepth > viewport.MaxDepth)
        {
            return E_INVALIDARG;
        }
        if (widget.widthPixels == 0 || widget.heightPixels == 0 || viewport.Width < 1.0f || viewport.Height < 1.0f)
        {
            return S_OK;
        }
        if (!_constantBuffer || !_shared->IsReady(_device))
        {
            return E_UNEXPECTED;
        }

        ID3D11DeviceContext* const deviceContext = context->deviceContext;
        _lastElapsedSeconds = widget.elapsedSeconds;
        const ShowState show = ComputeShow(static_cast<double>(widget.elapsedSeconds) + _clockOffsetSeconds);
        if (show.cycle != _shownCycle || show.shaderIndex != _shownShaderIndex)
        {
            _shownCycle = show.cycle;
            _shownShaderIndex = show.shaderIndex;
            _shaderFrame = 0;
        }
        const ShaderProgram& program = kPrograms[show.shaderIndex];
        const Shaders::ShaderInfo& info = kShaders[show.shaderIndex];

        // The host bound only its render target and viewport; take a reference to both so the offscreen passes can
        // hand them back. COM references, no heap.
        wil::com_ptr_nothrow<ID3D11RenderTargetView> hostTarget;
        wil::com_ptr_nothrow<ID3D11DepthStencilView> hostDepth;
        deviceContext->OMGetRenderTargets(1, hostTarget.put(), hostDepth.put());
        if (!hostTarget)
        {
            return E_UNEXPECTED;
        }

        BindCommonState(deviceContext);

        ShadersConstants constants{};
        constants.timeDelta = widget.deltaSeconds;
        constants.frameRate = widget.deltaSeconds > 0.0f ? 1.0f / widget.deltaSeconds : 0.0f;
        constants.frame = static_cast<int32_t>(std::min<uint32_t>(_shaderFrame, 0x7FFFFFFFU));
        constants.time = show.shaderTime;
        constants.fade = show.fade;
        std::memcpy(constants.background, _background.data(), sizeof(constants.background));
        FillDate(constants);

        // Feedback pass: once per presented frame at the buffer's own resolution, whatever size this draw has.
        const bool feedbackPass = info.feedbackBuffer && _feedback[0].texture && _feedback[1].texture;
        HRESULT result = S_OK;
        if (feedbackPass && _lastSimulatedElapsed != widget.elapsedSeconds)
        {
            const RenderTexture& read = _feedback[_feedbackRead];
            const RenderTexture& write = _feedback[_feedbackRead ^ 1U];
            constants.resolution[0] = static_cast<float>(write.width);
            constants.resolution[1] = static_cast<float>(write.height);
            constants.resolution[2] = 1.0f;
            constants.viewportOrigin[0] = 0.0f;
            constants.viewportOrigin[1] = 0.0f;
            result = UploadConstants(deviceContext, constants);
            if (FAILED(result))
            {
                return result;
            }
            ID3D11ShaderResourceView* views[] = {read.view.get()};
            ID3D11SamplerState* samplers[] = {program.bufferWraps ? _shared->WrapSampler() : _shared->ClampSampler()};
            ID3D11RenderTargetView* targets[] = {write.target.get()};
            deviceContext->OMSetRenderTargets(1, targets, nullptr);
            SetViewport(deviceContext, 0.0f, 0.0f, static_cast<float>(write.width), static_cast<float>(write.height));
            deviceContext->PSSetShaderResources(0, 1, views);
            deviceContext->PSSetSamplers(0, 1, samplers);
            deviceContext->PSSetShader(_shared->BufferShader(show.shaderIndex), nullptr, 0);
            deviceContext->Draw(3, 0);
            UnbindShaderResource(deviceContext);
            _feedbackRead ^= 1U;
            _lastSimulatedElapsed = widget.elapsedSeconds;
        }

        // Lookup tables: drawn once per device by the first frame of any widget that shows this entry, at their own
        // sizes, each pass reading the previous table on iChannel0. Small draws with no per-frame cost afterwards.
        SharedDeviceResources::LookupTableSet& tables = _shared->LookupTables(show.shaderIndex);
        if (program.lookupTables[0].shader.data && !tables.built)
        {
            ShadersConstants tableConstants = constants;
            tableConstants.viewportOrigin[0] = 0.0f;
            tableConstants.viewportOrigin[1] = 0.0f;
            for (uint32_t slot = 0; slot < kLookupTableSlots; ++slot)
            {
                if (!tables.shaders[slot])
                {
                    continue;
                }
                const RenderTexture& table = tables.textures[slot];
                tableConstants.resolution[0] = static_cast<float>(table.width);
                tableConstants.resolution[1] = static_cast<float>(table.height);
                tableConstants.resolution[2] = 1.0f;
                result = UploadConstants(deviceContext, tableConstants);
                if (FAILED(result))
                {
                    return result;
                }
                ID3D11ShaderResourceView* previous[] = {slot > 0 ? tables.textures[slot - 1].view.get() : nullptr};
                ID3D11SamplerState* previousSamplers[] = {_shared->ClampSampler()};
                ID3D11RenderTargetView* targets[] = {table.target.get()};
                deviceContext->OMSetRenderTargets(1, targets, nullptr);
                SetViewport(deviceContext, 0.0f, 0.0f, static_cast<float>(table.width),
                            static_cast<float>(table.height));
                deviceContext->PSSetShaderResources(0, 1, previous);
                deviceContext->PSSetSamplers(0, 1, previousSamplers);
                deviceContext->PSSetShader(tables.shaders[slot].get(), nullptr, 0);
                deviceContext->Draw(3, 0);
                UnbindShaderResource(deviceContext);
            }
            tables.built = true;
            // The host target must be rebound below even at 100 % scale; the offscreen branch does so itself.
            ID3D11RenderTargetView* hostTargets[] = {hostTarget.get()};
            deviceContext->OMSetRenderTargets(1, hostTargets, hostDepth.get());
            deviceContext->RSSetViewports(1, &viewport);
        }

        ID3D11ShaderResourceView* channels[kLookupTableSlots] = {nullptr, nullptr};
        ID3D11SamplerState* channelSamplers[kLookupTableSlots] = {_shared->ClampSampler(), _shared->ClampSampler()};
        if (feedbackPass)
        {
            channels[0] = _feedback[_feedbackRead].view.get();
        }
        else if (info.backgroundTexture)
        {
            channels[0] = _shared->Background();
            channelSamplers[0] = _shared->WrapMipSampler();
        }
        else if (tables.built)
        {
            channels[0] = tables.textures[0].view.get();
            channels[1] = tables.textures[1].view.get();
        }

        const float viewWidth = std::floor(viewport.Width + 0.5f);
        const float viewHeight = std::floor(viewport.Height + 0.5f);
        if (_offscreen.texture)
        {
            // Image pass into the top-left of the offscreen texture at the scaled size, then a stretch into the tile.
            const uint32_t renderWidth =
                std::clamp(ScaledExtent(static_cast<uint32_t>(viewWidth)), 1U, _offscreen.width);
            const uint32_t renderHeight =
                std::clamp(ScaledExtent(static_cast<uint32_t>(viewHeight)), 1U, _offscreen.height);
            constants.resolution[0] = static_cast<float>(renderWidth);
            constants.resolution[1] = static_cast<float>(renderHeight);
            constants.resolution[2] = 1.0f;
            constants.viewportOrigin[0] = 0.0f;
            constants.viewportOrigin[1] = 0.0f;
            result = UploadConstants(deviceContext, constants);
            if (FAILED(result))
            {
                return result;
            }
            ID3D11RenderTargetView* targets[] = {_offscreen.target.get()};
            deviceContext->OMSetRenderTargets(1, targets, nullptr);
            SetViewport(deviceContext, 0.0f, 0.0f, static_cast<float>(renderWidth), static_cast<float>(renderHeight));
            deviceContext->PSSetShaderResources(0, kLookupTableSlots, channels);
            deviceContext->PSSetSamplers(0, kLookupTableSlots, channelSamplers);
            deviceContext->PSSetShader(_shared->ImageShader(show.shaderIndex), nullptr, 0);
            deviceContext->Draw(3, 0);
            UnbindShaderResource(deviceContext);

            ID3D11RenderTargetView* hostTargets[] = {hostTarget.get()};
            deviceContext->OMSetRenderTargets(1, hostTargets, hostDepth.get());
            deviceContext->RSSetViewports(1, &viewport);
            constants.resolution[0] = viewWidth;
            constants.resolution[1] = viewHeight;
            constants.viewportOrigin[0] = viewport.TopLeftX;
            constants.viewportOrigin[1] = viewport.TopLeftY;
            constants.blitScale[0] = static_cast<float>(renderWidth) / static_cast<float>(_offscreen.width);
            constants.blitScale[1] = static_cast<float>(renderHeight) / static_cast<float>(_offscreen.height);
            result = UploadConstants(deviceContext, constants);
            if (FAILED(result))
            {
                return result;
            }
            ID3D11ShaderResourceView* offscreenViews[] = {_offscreen.view.get()};
            ID3D11SamplerState* offscreenSamplers[] = {_shared->ClampSampler()};
            deviceContext->PSSetShaderResources(0, 1, offscreenViews);
            deviceContext->PSSetSamplers(0, 1, offscreenSamplers);
            deviceContext->PSSetShader(_shared->BlitShader(), nullptr, 0);
            deviceContext->Draw(3, 0);
            UnbindShaderResource(deviceContext);
        }
        else
        {
            constants.resolution[0] = viewWidth;
            constants.resolution[1] = viewHeight;
            constants.resolution[2] = 1.0f;
            constants.viewportOrigin[0] = viewport.TopLeftX;
            constants.viewportOrigin[1] = viewport.TopLeftY;
            result = UploadConstants(deviceContext, constants);
            if (FAILED(result))
            {
                return result;
            }
            if (feedbackPass)
            {
                ID3D11RenderTargetView* hostTargets[] = {hostTarget.get()};
                deviceContext->OMSetRenderTargets(1, hostTargets, hostDepth.get());
                deviceContext->RSSetViewports(1, &viewport);
            }
            deviceContext->PSSetShaderResources(0, kLookupTableSlots, channels);
            deviceContext->PSSetSamplers(0, kLookupTableSlots, channelSamplers);
            deviceContext->PSSetShader(_shared->ImageShader(show.shaderIndex), nullptr, 0);
            deviceContext->Draw(3, 0);
            UnbindShaderResource(deviceContext);
        }

        ++_shaderFrame;
        gLastShaderIndex.store(show.shaderIndex, std::memory_order_relaxed);
        gLastShaderFrame.store(_shaderFrame, std::memory_order_relaxed);
        return S_OK;
    }

  private:
    [[nodiscard]] uint32_t ScaledExtent(uint32_t pixels) const noexcept
    {
        const uint64_t scaled = (static_cast<uint64_t>(pixels) * _configuration.renderScalePercent + 99U) / 100U;
        return static_cast<uint32_t>(std::max<uint64_t>(scaled, 1U));
    }

    void ReleaseSizedResources() noexcept
    {
        _offscreen.Reset();
        _feedback[0].Reset();
        _feedback[1].Reset();
        _sizedWidth = 0;
        _sizedHeight = 0;
        _feedbackRead = 0;
        _lastSimulatedElapsed = -1.0f;
    }

    // Slideshow order for one round of `kShaderCount` slides when shuffling: a Fisher-Yates permutation seeded by
    // the widget's random seed and the round, so every shader shows once per round and no round repeats the last.
    [[nodiscard]] uint32_t ShuffledShader(uint32_t cycle) const noexcept
    {
        const uint32_t round = cycle / kShaderCount;
        const uint32_t position = cycle % kShaderCount;
        std::array<uint32_t, kShaderCount> order{};
        for (uint32_t index = 0; index < kShaderCount; ++index)
        {
            order[index] = index;
        }
        uint32_t state = Hash(_randomSeed ^ (round * 0x9E3779B9U));
        for (uint32_t index = kShaderCount - 1; index > 0; --index)
        {
            state = Hash(state + index);
            std::swap(order[index], order[state % (index + 1)]);
        }
        return order[position];
    }

    [[nodiscard]] double CyclePeriodSeconds() const noexcept
    {
        return static_cast<double>(_configuration.mode == Mode::Slideshow ? _configuration.intervalSeconds
                                                                          : Shaders::kClockRestartSeconds);
    }

    // Moves the virtual clock (host elapsed time plus the offset every tap has added) to kFadeSeconds before the
    // next cycle boundary. Inside that final fade already, the change is imminent and nothing moves.
    void AdvanceSlideshow() noexcept
    {
        const double period = CyclePeriodSeconds();
        const double now = static_cast<double>(_lastElapsedSeconds) + _clockOffsetSeconds;
        const double boundary = (std::floor(now / period) + 1.0) * period;
        const double target = boundary - static_cast<double>(Shaders::kFadeSeconds);
        if (target > now)
        {
            _clockOffsetSeconds += target - now;
        }
    }

    // The clock is the host's elapsed seconds plus the offset taps have added, in double so a long uptime keeps
    // sub-frame resolution; every value below is a pure function of it.
    [[nodiscard]] ShowState ComputeShow(double clockSeconds) const noexcept
    {
        ShowState state{};
        const double period = CyclePeriodSeconds();
        const double cycles = std::floor(clockSeconds / period);
        state.cycle = cycles >= 4294967040.0 ? std::numeric_limits<uint32_t>::max() : static_cast<uint32_t>(cycles);
        state.shaderTime = static_cast<float>(std::max(clockSeconds - cycles * period, 0.0));
        const float remaining = static_cast<float>(period) - state.shaderTime;
        state.fade = std::clamp(std::min(state.shaderTime, remaining) / Shaders::kFadeSeconds, 0.0f, 1.0f);
        switch (_configuration.mode)
        {
        case Mode::Single:
            state.shaderIndex = _configuration.shaderIndex;
            break;
        case Mode::Random:
            state.shaderIndex = _randomShaderIndex;
            break;
        case Mode::Slideshow:
        default:
            state.shaderIndex = _configuration.shuffle
                                    ? ShuffledShader(state.cycle)
                                    : (_configuration.shaderIndex + state.cycle % kShaderCount) % kShaderCount;
            break;
        }
        return state;
    }

    static void FillDate(ShadersConstants& constants) noexcept
    {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        constants.date[0] = static_cast<float>(now.wYear);
        constants.date[1] = static_cast<float>(now.wMonth - 1);
        constants.date[2] = static_cast<float>(now.wDay);
        constants.date[3] = static_cast<float>(now.wHour) * 3600.0f + static_cast<float>(now.wMinute) * 60.0f +
                            static_cast<float>(now.wSecond) + static_cast<float>(now.wMilliseconds) * 0.001f;
    }

    void BindCommonState(ID3D11DeviceContext* deviceContext) const noexcept
    {
        constexpr std::array blendFactor{0.0f, 0.0f, 0.0f, 0.0f};
        ID3D11Buffer* constantBuffers[] = {_constantBuffer.get()};
        deviceContext->IASetInputLayout(nullptr);
        deviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
        deviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        deviceContext->GSSetShader(nullptr, nullptr, 0);
        deviceContext->HSSetShader(nullptr, nullptr, 0);
        deviceContext->DSSetShader(nullptr, nullptr, 0);
        deviceContext->CSSetShader(nullptr, nullptr, 0);
        deviceContext->RSSetState(_shared->Rasterizer());
        deviceContext->OMSetDepthStencilState(_shared->DepthState(), 0);
        deviceContext->OMSetBlendState(_shared->OpaqueBlend(), blendFactor.data(), UINT_MAX);
        deviceContext->VSSetShader(_shared->VertexShader(), nullptr, 0);
        deviceContext->VSSetConstantBuffers(0, 1, constantBuffers);
        deviceContext->PSSetConstantBuffers(0, 1, constantBuffers);
    }

    [[nodiscard]] HRESULT UploadConstants(ID3D11DeviceContext* deviceContext,
                                          const ShadersConstants& constants) const noexcept
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result = deviceContext->Map(_constantBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result))
        {
            return result;
        }
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        deviceContext->Unmap(_constantBuffer.get(), 0);
        return S_OK;
    }

    static void SetViewport(ID3D11DeviceContext* deviceContext, float left, float top, float width,
                            float height) noexcept
    {
        D3D11_VIEWPORT viewport{};
        viewport.TopLeftX = left;
        viewport.TopLeftY = top;
        viewport.Width = width;
        viewport.Height = height;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        deviceContext->RSSetViewports(1, &viewport);
    }

    static void UnbindShaderResource(ID3D11DeviceContext* deviceContext) noexcept
    {
        ID3D11ShaderResourceView* noViews[kLookupTableSlots] = {nullptr, nullptr};
        deviceContext->PSSetShaderResources(0, kLookupTableSlots, noViews);
    }

    wil::com_ptr_nothrow<IRedXeWidgetProvider> _providerOwner;
    SharedDeviceResources* _shared;
    Configuration _configuration;
    std::array<float, 4> _background;
    uint32_t _randomSeed;
    uint32_t _randomShaderIndex;
    bool _mayUseFeedbackBuffers;
    ID3D11Device* _device = nullptr;
    DXGI_FORMAT _targetFormat = DXGI_FORMAT_UNKNOWN;
    wil::com_ptr_nothrow<ID3D11Buffer> _constantBuffer;
    RenderTexture _offscreen;
    std::array<RenderTexture, 2> _feedback;
    uint32_t _sizedWidth = 0;
    uint32_t _sizedHeight = 0;
    uint32_t _feedbackRead = 0;
    float _lastSimulatedElapsed = -1.0f;
    uint32_t _shownCycle = std::numeric_limits<uint32_t>::max();
    uint32_t _shownShaderIndex = std::numeric_limits<uint32_t>::max();
    uint32_t _shaderFrame = 0;
    float _lastElapsedSeconds = 0.0f;
    double _clockOffsetSeconds = 0.0;
    bool _pointerDown = false;
    float _pointerStartX = 0.0f;
    float _pointerStartY = 0.0f;
};

class ShadersProvider final : public RedXeComObject<ShadersProvider, IRedXeWidgetProvider>
{
  public:
    ShadersProvider(const Configuration& configuration, uint32_t backgroundRgb) noexcept
        : _configuration(configuration), _background(ColorToFloat(backgroundRgb))
    {
        gLiveProviderCount.fetch_add(1, std::memory_order_relaxed);
    }

    ~ShadersProvider()
    {
        gLiveProviderCount.fetch_sub(1, std::memory_order_relaxed);
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
        if (!RedXeAsciiEqualsIgnoreCase(typeId, Shaders::kWidgetTypeId))
        {
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }

        wil::com_ptr_nothrow<IRedXeWidgetProvider> providerOwner;
        const HRESULT result =
            QueryInterface(__uuidof(IRedXeWidgetProvider), reinterpret_cast<void**>(providerOwner.put()));
        if (FAILED(result))
        {
            return result;
        }

        // Random mode and shuffled slideshows differ per launch and per instance: the seed mixes the clock with the
        // instance id, never a fixed constant.
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        uint32_t seed = Hash(static_cast<uint32_t>(counter.QuadPart) ^ static_cast<uint32_t>(counter.QuadPart >> 32U));
        for (const char* character = instanceId; *character != '\0'; ++character)
        {
            seed = Hash(seed ^ static_cast<uint32_t>(static_cast<unsigned char>(*character)));
        }
        seed = Hash(seed ^ GetCurrentProcessId());

        auto* created =
            new (std::nothrow) ShadersWidget(std::move(providerOwner), _shared, _configuration, _background, seed);
        if (!created)
        {
            return E_OUTOFMEMORY;
        }
        *widget = static_cast<IRedXeWidget*>(created);
        return S_OK;
    }

  private:
    Configuration _configuration;
    std::array<float, 4> _background;
    SharedDeviceResources _shared;
};

static_assert(sizeof(SharedDeviceResources) < 4U * 1024U);
static_assert(sizeof(ShadersProvider) < 4U * 1024U);
static_assert(sizeof(ShadersWidget) < 4U * 1024U);

HRESULT CreateShadersProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost*,
                              void** result) noexcept
{
    if (interfaceId != __uuidof(IRedXeWidgetProvider))
    {
        return E_NOINTERFACE;
    }

    Configuration configuration{};
    uint32_t backgroundRgb = 0;
    const HRESULT createResult = ReadFactoryConfiguration(options, configuration, backgroundRgb);
    if (FAILED(createResult))
    {
        return createResult;
    }

    auto* provider = new (std::nothrow) ShadersProvider(configuration, backgroundRgb);
    if (!provider)
    {
        return E_OUTOFMEMORY;
    }
    *result = provider;
    return S_OK;
}

constexpr std::array kFactoryEntries{
    RedXeFactoryEntry{&kMetadata[0], CreateShadersProvider},
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
    return RedXeGetStaticPluginSettingsContract(Shaders::kPluginId, pluginId, &kSettingsContract, contract);
}

extern "C" void __stdcall RedXePluginShutdown() noexcept {}

extern "C" HRESULT __stdcall RedXeShadersGetTestDiagnostics(ShadersTestDiagnostics* diagnostics) noexcept
{
    if (!diagnostics)
    {
        return E_POINTER;
    }
    if (diagnostics->sizeBytes != sizeof(ShadersTestDiagnostics))
    {
        return E_INVALIDARG;
    }

    diagnostics->liveProviderCount = gLiveProviderCount.load(std::memory_order_relaxed);
    diagnostics->liveWidgetCount = gLiveWidgetCount.load(std::memory_order_relaxed);
    diagnostics->liveSharedResourceSetCount = gLiveSharedResourceSetCount.load(std::memory_order_relaxed);
    diagnostics->lastShaderIndex = gLastShaderIndex.load(std::memory_order_relaxed);
    diagnostics->lastShaderFrame = gLastShaderFrame.load(std::memory_order_relaxed);
    return S_OK;
}
