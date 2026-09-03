cbuffer DeskClockConstants : register(b0)
{
    float4 targetSize;
    float4 layout;
    float4 metrics;
    float4 backgroundColor;
    float4 cardColor;
    float4 digitColor;
    float4 dateColor;
    uint4 oldDigits0;
    uint4 oldDigits1;
    uint4 targetDigits0;
    uint4 targetDigits1;
    uint4 state;
    float4 animation;
    uint4 oldDate0;
    uint4 oldDate1;
    uint4 oldDate2;
    uint4 targetDate0;
    uint4 targetDate1;
    uint4 targetDate2;
    float4 oldDatePositions0;
    float4 oldDatePositions1;
    float4 oldDatePositions2;
    float4 targetDatePositions0;
    float4 targetDatePositions1;
    float4 targetDatePositions2;
};

Texture2D<float> glyphAtlas : register(t0);
SamplerState glyphSampler : register(s0);

struct ClockInput
{
    float4 position : SV_Position;
    float2 localUv : TEXCOORD0;
    nointerpolation uint kind : TEXCOORD1;
    nointerpolation uint glyph : TEXCOORD2;
    nointerpolation float shade : TEXCOORD3;
    nointerpolation float alpha : TEXCOORD4;
};

float RoundedCardCoverage(float2 uv)
{
    const float radius = max(animation.z, 0.001f);
    const float2 q = abs(uv - 0.5f) - (0.5f - radius);
    const float distance = length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - radius;
    return 1.0f - smoothstep(-fwidth(distance), fwidth(distance), distance);
}

float GlyphCoverage(uint glyph, float2 uv, bool isDate)
{
    // Every cell, offset, and em size scales with the atlas tier, so cell geometry in texels is the base layout
    // times this factor and normalized coordinates are identical across tiers.
    const float atlasEdge = max(targetSize.w, 1.0f);
    const float atlasScale = atlasEdge * (1.0f / 1024.0f);
    const float inverseEdge = 1.0f / atlasEdge;

    if (isDate)
    {
        const uint dateGlyph = glyph - 10U;
        const uint2 cell = uint2(dateGlyph & 15U, dateGlyph >> 4U);
        const float cellSize = 64.0f * atlasScale;
        const float2 origin = float2(float(cell.x) * cellSize, (640.0f * atlasScale) + float(cell.y) * cellSize);
        const float2 atlasPixel = origin + 0.5f + saturate(uv) * (cellSize - 1.0f);
        // Date cells are the small ones, and a filtered mip erases their thin strokes, so read level 0 explicitly.
        // SampleLevel also needs no derivatives, which is the correct choice inside this divergent branch.
        return glyphAtlas.SampleLevel(glyphSampler, atlasPixel * inverseEdge, 0.0f);
    }

    const float2 glyphMinimum = float2(0.11f, 0.09f);
    const float2 glyphMaximum = float2(0.89f, 0.91f);
    const float2 glyphUv = (uv - glyphMinimum) / (glyphMaximum - glyphMinimum);
    if (any(glyphUv < 0.0f) || any(glyphUv > 1.0f))
    {
        return 0.0f;
    }
    const uint2 cell = uint2(glyph % 5U, glyph / 5U);
    const float2 cellSize = float2(192.0f, 288.0f) * atlasScale;
    const float2 origin = float2(float(cell.x), float(cell.y)) * cellSize;
    const float2 atlasPixel = origin + 0.5f + saturate(glyphUv) * (cellSize - 1.0f);
    // Time cells take the mip chain: they are minified on every tile at or below the composition design size.
    return glyphAtlas.Sample(glyphSampler, atlasPixel * inverseEdge);
}

float4 PixelMain(ClockInput input) : SV_Target
{
    if (input.alpha <= 0.0f)
    {
        discard;
    }

    if (input.kind <= 1U)
    {
        const float cardCoverage = RoundedCardCoverage(input.localUv);
        if (cardCoverage <= 0.0f)
        {
            discard;
        }
        float shade = input.shade;
        const float seam = 1.0f - smoothstep(animation.w, animation.w * 2.2f,
                                             abs(input.localUv.y - 0.5f));
        if (input.kind == 0U)
        {
            shade *= 1.0f - seam * 0.08f;
        }
        const float glyph = GlyphCoverage(input.glyph, input.localUv, false);
        const float3 surface = lerp(cardColor.rgb, digitColor.rgb, glyph) * shade;
        return float4(surface, cardCoverage * input.alpha);
    }

    if (input.kind == 2U)
    {
        const float distance = length(input.localUv - 0.5f);
        const float coverage = 1.0f - smoothstep(0.46f, 0.52f, distance);
        if (coverage <= 0.0f)
        {
            discard;
        }
        return float4(digitColor.rgb, coverage * input.alpha);
    }

    const float coverage = GlyphCoverage(input.glyph, input.localUv, true);
    if (coverage <= 0.0f)
    {
        discard;
    }
    return float4(dateColor.rgb, coverage * input.alpha);
}
