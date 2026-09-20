#pragma once

// 5H4D3R5 widget settings model shared by the host parser (RedXe/Settings*.cpp) and 5H4D3R5.dll, so the shader
// catalog, the mode names, the ranges, and the defaults exist once. Header-only; no allocation, no exceptions.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Shaders
{
inline constexpr char kPluginId[] = "builtin.5h4d3r5";
inline constexpr char kWidgetTypeId[] = "5h4d3r5";

// How the widget picks the shader it draws.
enum class Mode : uint32_t
{
    Single = 0,    // always `shader`
    Random = 1,    // one shader picked at widget creation, kept until the widget is rebuilt
    Slideshow = 2, // every shader in turn, `intervalSeconds` each, starting at `shader`
};

inline constexpr std::string_view kModeSingle = "single";
inline constexpr std::string_view kModeRandom = "random";
inline constexpr std::string_view kModeSlideshow = "slideshow";

inline constexpr uint32_t kMinimumIntervalSeconds = 10;
inline constexpr uint32_t kMaximumIntervalSeconds = 3600;
inline constexpr uint32_t kMinimumRenderScalePercent = 25;
inline constexpr uint32_t kMaximumRenderScalePercent = 100;

// Seconds a single or random shader runs before its clock restarts through the same fade a slideshow uses. iTime is a
// 32-bit float in every port; restarting keeps its resolution finer than a frame instead of drifting over a long
// uptime.
inline constexpr uint32_t kClockRestartSeconds = 3600;
// Seconds of fade to the dashboard background on either side of a shader change or clock restart.
inline constexpr float kFadeSeconds = 0.75f;

// Bundled shaders. `name` is the value of the `shader` setting (a short lowercase slug of the title); `id` is the
// Shadertoy id (https://www.shadertoy.com/view/<id>) of a port, or nullptr for a shader that is RedXe's own; the rest
// is attribution carried into the docs and the notices file. Every port keeps the author's own header; a Shadertoy
// work with no license of its own is under Shadertoy's default terms (CC BY-NC-SA 3.0 Unported).
struct ShaderInfo final
{
    const char* name;
    const char* id;
    const char* title;
    const char* author;
    const char* license;
    // The shader renders a feedback buffer pass (a Shadertoy "Buffer A" reading itself) before its image pass.
    bool feedbackBuffer;
    // The shader samples the plugin's procedural stand-in for a Shadertoy stock photograph on iChannel0.
    bool backgroundTexture;
};

inline constexpr char kLicenseDefault[] = "CC BY-NC-SA 3.0 Unported (Shadertoy default license)";
inline constexpr char kLicenseStated[] = "CC BY-NC-SA 3.0 Unported (stated in the shader header)";
inline constexpr char kLicenseRedXe[] = "RedXe repository terms (an original RedXe shader, not a Shadertoy work)";
inline constexpr char kLicenseEpicMit[] = "MIT License, Copyright (c) 2020 Epic Games, Inc. (not a Shadertoy work)";

inline constexpr std::array kShaders{
    ShaderInfo{"fluid-solver", "XlsBDf", "Fluid solver", "David A Roberts (davidar)", kLicenseDefault, true, false},
    ShaderInfo{"cineshader-lava", "3sySRK", "CineShader Lava", "Edan Kwan (edankwan)", kLicenseDefault, false, false},
    ShaderInfo{"synthwave-sunset", "tsScRK", "another synthwave sunset thing", "stduhpf", kLicenseDefault, false,
               false},
    ShaderInfo{"seascape", "Ms2SD1", "Seascape", "Alexander Alekseev (TDM)", kLicenseStated, false, false},
    ShaderInfo{"warp-fbm", "tdG3Rd", "Base warp fBM", "trinketMage", kLicenseDefault, false, false},
    ShaderInfo{"fractal-pyramid", "tsXBzS", "fractal pyramid", "bradjamesgrant", kLicenseDefault, false, false},
    ShaderInfo{"octagrams", "tlVGDt", "Octagrams", "whisky_shusuky", kLicenseDefault, false, false},
    ShaderInfo{"heartfelt", "ltffzl", "Heartfelt", "Martijn Steinrucken (BigWings)", kLicenseStated, false, true},
    ShaderInfo{"protean-clouds", "3l23Rh", "Protean clouds", "nimitz", kLicenseStated, false, false},
    ShaderInfo{"drive-home", "MdfBRX", "The Drive Home", "Martijn Steinrucken (BigWings)", kLicenseStated, false,
               false},
    ShaderInfo{"flammes-vortex", "WsccDH", "Flammes 3 - Vortex", "athibaul", kLicenseDefault, true, false},
    ShaderInfo{"neon-pulse", "7csXD4", "Neon Pulse Fractal", "bogdoslav", kLicenseDefault, false, false},
    ShaderInfo{"cosmic-orb", nullptr, "Psychedelic Cosmic Orb", "RedXe", kLicenseRedXe, false, false},
    ShaderInfo{"sky-atmosphere", nullptr, "Sky Atmosphere",
               "Sébastien Hillaire / Epic Games (technique), RedXe (scene)", kLicenseEpicMit, false, false},
};

inline constexpr uint32_t kShaderCount = static_cast<uint32_t>(kShaders.size());
inline constexpr uint32_t kDefaultShaderIndex = 3; // Seascape

struct Configuration final
{
    Mode mode = Mode::Slideshow;
    uint32_t shaderIndex = kDefaultShaderIndex;
    uint32_t intervalSeconds = 120;
    bool shuffle = false;
    uint32_t renderScalePercent = 50;
};

// The compact defaults the host merges under authored keys and the DLL publishes. Keep in step with Configuration.
inline constexpr char kDefaultsJson[] =
    R"json({"mode":"slideshow","shader":"seascape","intervalSeconds":120,"shuffle":false,"renderScalePercent":50})json";

// The published plugin schema (Plugins_API.md subset: closed object, string enums, bounded integers, booleans).
inline constexpr char kSchemaJson[] =
    R"json({"type":"object","additionalProperties":false,"properties":{"mode":{"type":"string","enum":["single","random","slideshow"]},"shader":{"type":"string","enum":["fluid-solver","cineshader-lava","synthwave-sunset","seascape","warp-fbm","fractal-pyramid","octagrams","heartfelt","protean-clouds","drive-home","flammes-vortex","neon-pulse","cosmic-orb","sky-atmosphere"]},"intervalSeconds":{"type":"integer","minimum":10,"maximum":3600},"shuffle":{"type":"boolean"},"renderScalePercent":{"type":"integer","minimum":25,"maximum":100}}})json";

inline constexpr std::array<const char*, 5> kSettingsKeys{"mode", "shader", "intervalSeconds", "shuffle",
                                                          "renderScalePercent"};

[[nodiscard]] constexpr bool TryParseMode(std::string_view text, Mode& mode) noexcept
{
    if (text == kModeSingle)
    {
        mode = Mode::Single;
        return true;
    }
    if (text == kModeRandom)
    {
        mode = Mode::Random;
        return true;
    }
    if (text == kModeSlideshow)
    {
        mode = Mode::Slideshow;
        return true;
    }
    return false;
}

[[nodiscard]] constexpr std::string_view ModeName(Mode mode) noexcept
{
    switch (mode)
    {
    case Mode::Single:
        return kModeSingle;
    case Mode::Random:
        return kModeRandom;
    case Mode::Slideshow:
    default:
        return kModeSlideshow;
    }
}

// Names are exact (lowercase slugs), like every other settings enum.
[[nodiscard]] constexpr bool TryFindShader(std::string_view name, uint32_t& index) noexcept
{
    for (uint32_t candidate = 0; candidate < kShaderCount; ++candidate)
    {
        if (name == kShaders[candidate].name)
        {
            index = candidate;
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr bool IsValidIntervalSeconds(uint64_t value) noexcept
{
    return value >= kMinimumIntervalSeconds && value <= kMaximumIntervalSeconds;
}

[[nodiscard]] constexpr bool IsValidRenderScalePercent(uint64_t value) noexcept
{
    return value >= kMinimumRenderScalePercent && value <= kMaximumRenderScalePercent;
}

consteval bool CatalogIsValid() noexcept
{
    for (uint32_t index = 0; index < kShaderCount; ++index)
    {
        const ShaderInfo& shader = kShaders[index];
        if (!shader.name || shader.name[0] == '\0' || (shader.id && shader.id[0] == '\0') || !shader.title ||
            !shader.author || !shader.license)
        {
            return false;
        }
        for (const char* character = shader.name; *character != '\0'; ++character)
        {
            if (!((*character >= 'a' && *character <= 'z') || (*character >= '0' && *character <= '9') ||
                  *character == '-'))
            {
                return false;
            }
        }
        for (uint32_t previous = 0; previous < index; ++previous)
        {
            const ShaderInfo& earlier = kShaders[previous];
            if ((earlier.id && shader.id && std::string_view(earlier.id) == shader.id) ||
                std::string_view(earlier.name) == shader.name)
            {
                return false;
            }
        }
    }
    uint32_t defaultIndex = 0;
    return kDefaultShaderIndex < kShaderCount && TryFindShader("seascape", defaultIndex) &&
           defaultIndex == kDefaultShaderIndex;
}

static_assert(CatalogIsValid());
} // namespace Shaders
