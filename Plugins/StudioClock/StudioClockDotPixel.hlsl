struct PixelInput
{
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    float4 color : COLOR0;
};

float4 PixelMain(PixelInput input) : SV_Target
{
    const float distanceFromCenter = length(input.local);
    const float antialiasWidth = max(fwidth(distanceFromCenter), 0.001);
    const float coverage = 1.0 - smoothstep(1.0 - antialiasWidth, 1.0, distanceFromCenter);
    return float4(input.color.rgb, input.color.a * coverage);
}
