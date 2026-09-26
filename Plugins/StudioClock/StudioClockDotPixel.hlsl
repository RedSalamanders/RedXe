struct PixelInput
{
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation float2 halo : TEXCOORD1;
};

// The output is premultiplied. local is measured in LED radii, so the lit disc is the unit circle on core and halo
// quads alike.
float4 PixelMain(PixelInput input) : SV_Target
{
    const float distanceFromCenter = length(input.local);
    const float antialiasWidth = max(fwidth(distanceFromCenter), 0.001);
    const float coverage = 1.0 - smoothstep(1.0 - antialiasWidth, 1.0, distanceFromCenter);
    if (input.halo.x > 0.0)
    {
        // Additive light (zero alpha) with a compact Gaussian-like falloff that reaches zero at the quad edge. It stays
        // out of the disc so a dimmed LED keeps its own brightness under the core pass.
        const float falloff = saturate(1.0 - distanceFromCenter * distanceFromCenter * input.halo.y);
        const float falloffSquared = falloff * falloff;
        const float light = input.halo.x * falloffSquared * falloffSquared * (1.0 - coverage);
        return float4(input.color.rgb * light, 0.0);
    }
    const float alpha = input.color.a * coverage;
    return float4(input.color.rgb * alpha, alpha);
}
