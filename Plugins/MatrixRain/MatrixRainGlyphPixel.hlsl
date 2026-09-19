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

float4 PixelMain(GlyphInput input) : SV_Target
{
    const float distance = glyphAtlas.Sample(glyphSampler, input.atlasUv);
    // Threshold the distance field over about one screen pixel whatever the glyph size, so edges stay smooth from a
    // 12-DIP tile glyph to a raised 48-DIP one instead of showing the atlas texels.
    const float edgeWidth = clamp(fwidth(distance) * 0.7f, 0.004f, 0.12f);
    const float coverage = smoothstep(0.5f - edgeWidth, 0.5f + edgeWidth, distance);
    const float halo = saturate(smoothstep(0.18f, 0.49f, distance) - coverage) * effect.x;
    const float intensity = saturate(input.trailIntensity + input.headIntensity * 0.55f);
    const float alpha = saturate(coverage * intensity + halo * intensity * 0.42f);
    const float3 color = lerp(trailColor.rgb, headColor.rgb, saturate(input.headIntensity));
    return float4(color, alpha);
}
