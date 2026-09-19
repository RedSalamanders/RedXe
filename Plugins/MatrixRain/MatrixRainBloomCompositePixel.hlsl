cbuffer MatrixRainConstants : register(b0)
{
    uint4 targetAndSeed;
    uint4 grid;
    uint4 stream;
    float4 geometryAndTime;
    float4 headColor;
    float4 trailColor;
    float4 backgroundColor;
    float4 effect;
};

Texture2D<float4> bloomTexture : register(t0);
SamplerState bloomSampler : register(s0);

struct BloomInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Adds the blurred glyph light back over the tile. effect.zw maps the viewport UV onto the region of the bloom
// target this frame drew into; effect.x is glowPercent, scaled so the default reads as a phosphor monitor.
float4 PixelMain(BloomInput input) : SV_Target
{
    const float3 bloom = bloomTexture.Sample(bloomSampler, input.uv * effect.zw).rgb;
    return float4(bloom * (effect.x * 4.0f), 1.0f);
}
