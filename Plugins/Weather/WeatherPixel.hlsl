Texture2D<float> glyphAtlas : register(t0);
SamplerState glyphSampler : register(s0);

struct WeatherInput
{
    float4 position : SV_Position;
    float2 localUv : TEXCOORD0;
    float2 atlasUv : TEXCOORD1;
    nointerpolation float4 color : COLOR0;
    nointerpolation float4 params : TEXCOORD2;
};

float RoundedCoverage(float2 uv, float2 size, float radius)
{
    const float2 halfSize = size * 0.5f;
    const float limited = min(radius, min(halfSize.x, halfSize.y));
    const float2 q = abs(uv * size - halfSize) - (halfSize - limited);
    const float distance = length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - limited;
    const float width = max(fwidth(distance), 0.75f);
    return 1.0f - smoothstep(-width, width, distance);
}

float RingCoverage(float2 uv, float2 size, float innerRadius)
{
    const float2 center = size * 0.5f;
    const float outer = min(center.x, center.y);
    const float inner = clamp(innerRadius, 0.0f, outer - 1.0f);
    const float distance = length(uv * size - center);
    const float width = max(fwidth(distance), 0.75f);
    const float outerMask = 1.0f - smoothstep(outer - width, outer + width, distance);
    const float innerMask = smoothstep(inner - width, inner + width, distance);
    return outerMask * innerMask;
}

float4 PixelMain(WeatherInput input) : SV_Target
{
    const uint kind = uint(input.params.x + 0.5f);
    const float2 size = float2(1.0f / max(fwidth(input.localUv.x), 1e-5f),
                               1.0f / max(fwidth(input.localUv.y), 1e-5f));
    float coverage = 0.0f;
    if (kind == 3U)
    {
        coverage = glyphAtlas.Sample(glyphSampler, input.atlasUv);
    }
    else if (kind == 2U)
    {
        coverage = RingCoverage(input.localUv, size, input.params.z);
    }
    else
    {
        coverage = RoundedCoverage(input.localUv, size, max(input.params.y, 0.5f));
        if (kind == 1U)
        {
            const float inner = RoundedCoverage(input.localUv, size, max(input.params.y - input.params.z, 0.0f));
            coverage = saturate(coverage - inner);
        }
        if (kind == 4U)
        {
            coverage *= coverage;
        }
    }

    coverage *= input.color.a;
    if (coverage <= 0.001f)
    {
        discard;
    }
    return float4(input.color.rgb, coverage);
}
