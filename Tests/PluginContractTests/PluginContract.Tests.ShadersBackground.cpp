#include "PluginContract.Tests.ShadersBackground.h"
#include "Shaders.Tests.Contract.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <new>

namespace
{
// Frozen pre-optimization oracle: it scans every light at every pixel, independently of the production
// row/range algorithm. Keep the original contribution order and arithmetic, including all mip filters.
constexpr uint32_t kBackgroundTextureSize = 512;
constexpr uint32_t kBackgroundMipCount = 10;
struct BokehLight final
{
    float x, y, radius, red, green, blue;
};
[[nodiscard]] uint32_t Hash(uint32_t value) noexcept
{
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

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

} // namespace

HRESULT ValidateShadersBackgroundReference(const uint32_t* actual, uint32_t count) noexcept
{
    if (!actual)
        return E_POINTER;
    if (count != kShadersBackgroundTexelCount)
        return E_INVALIDARG;
    std::unique_ptr<uint32_t[]> expected(new (std::nothrow) uint32_t[count]);
    if (!expected)
        return E_OUTOFMEMORY;
    FillBackgroundBaseLevel(expected.get());
    size_t offset = 0;
    uint32_t size = kBackgroundTextureSize;
    for (uint32_t level = 0; level + 1 < kBackgroundMipCount; ++level)
    {
        const size_t next = offset + static_cast<size_t>(size) * size;
        DownsampleLevel(expected.get() + offset, size, expected.get() + next);
        offset = next;
        size /= 2;
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        if (actual[index] != expected[index])
        {
            std::wprintf(L"Shader background reference differs at texel %u: %08X vs %08X.\n", index, actual[index],
                         expected[index]);
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
    }
    std::wprintf(L"5H4D3R5 background: all %u texels across ten mips match the original reference.\n", count);
    return S_OK;
}
