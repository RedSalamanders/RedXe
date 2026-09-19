Texture2D<float4> bloomSource : register(t0);
SamplerState bloomSampler : register(s0);

struct BloomInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// One direction of a separable nine-tap Gaussian (sigma two texels) over the quarter-resolution bloom target. The
// texel size comes from the texture itself, so the pass needs no constants; samples past the drawn region read the
// cleared black border.
float4 PixelMain(BloomInput input) : SV_Target
{
    static const float weights[5] = {0.2042f, 0.1802f, 0.1238f, 0.0663f, 0.0276f};
    float width;
    float height;
    bloomSource.GetDimensions(width, height);
    const float2 texel = input.position.xy / float2(width, height);
    const float2 step = float2(1.0f / width, 0.0f);
    float3 sum = bloomSource.Sample(bloomSampler, texel).rgb * weights[0];
    [unroll]
    for (int tap = 1; tap < 5; ++tap)
    {
        const float2 offset = step * float(tap);
        sum += bloomSource.Sample(bloomSampler, texel + offset).rgb * weights[tap];
        sum += bloomSource.Sample(bloomSampler, texel - offset).rgb * weights[tap];
    }
    return float4(sum, 1.0f);
}
