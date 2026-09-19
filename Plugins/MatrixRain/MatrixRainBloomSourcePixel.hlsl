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

Texture2D<float> glyphAtlas : register(t0);
SamplerState glyphSampler : register(s0);

struct GlyphInput
{
    float4 position : SV_Position;
    float2 atlasUv : TEXCOORD0;
    float trailIntensity : TEXCOORD1;
    float headIntensity : TEXCOORD2;
};

// The light a glyph gives off, drawn additively into the quarter-resolution bloom target: the same coverage as the
// visible glyph, no halo, with the head counted extra so its phosphor bloom dominates the field.
float4 PixelMain(GlyphInput input) : SV_Target
{
    const float distance = glyphAtlas.Sample(glyphSampler, input.atlasUv);
    const float edgeWidth = clamp(fwidth(distance) * 0.7f, 0.004f, 0.12f);
    const float coverage = smoothstep(0.5f - edgeWidth, 0.5f + edgeWidth, distance);
    const float light = coverage * (input.trailIntensity * 0.55f + input.headIntensity * 2.4f);
    const float3 color = lerp(trailColor.rgb, headColor.rgb, saturate(input.headIntensity));
    return float4(color * light, 1.0f);
}
