// Host chrome quad: a flat wash, or a glyph cut out of the single-channel atlas.
cbuffer HostChromeConstants : register(b0)
{
    float4 rectPixels;
    float4 color;
    float4 uvRect;
    float4 viewportGlyph;
};

Texture2D<float> glyphAtlas : register(t0);
SamplerState glyphSampler : register(s0);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 PixelMain(PixelInput input) : SV_TARGET
{
    const float coverage = viewportGlyph.z > 0.5f ? glyphAtlas.Sample(glyphSampler, input.uv) : 1.0f;
    return float4(color.rgb, color.a * coverage);
}
