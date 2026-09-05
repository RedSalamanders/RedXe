cbuffer LauncherConstants : register(b0)
{
    float2 viewportSize;
    float drawMode;
    float hint;
    float4 backgroundColor;
    float4 hintColor;
    float4 iconRect[8];
    float4 iconMotion[8];
    uint iconCount;
    uint3 iconPad;
};

Texture2DArray icons : register(t0);
SamplerState iconSampler : register(s0);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float dim : TEXCOORD1;
    nointerpolation float slice : TEXCOORD2;
    nointerpolation float mode : TEXCOORD3;
};

float4 PixelMain(PixelInput input) : SV_Target
{
    if (input.mode < 0.5f)
    {
        float plus = 0.0f;
        if (hint > 0.5f)
        {
            const float2 delta = abs(input.uv - 0.5f) * max(viewportSize, float2(1.0f, 1.0f));
            const float arm = min(min(viewportSize.x, viewportSize.y) * 0.08f, 36.0f);
            const float thickness = max(arm * 0.14f, 3.0f);
            plus = ((delta.x < thickness && delta.y < arm) || (delta.y < thickness && delta.x < arm)) ? 1.0f : 0.0f;
        }
        return lerp(backgroundColor, hintColor, plus);
    }

    const float4 color = icons.Sample(iconSampler, float3(input.uv, input.slice));
    if (color.a < 0.01f)
    {
        discard;
    }
    return float4(color.rgb * input.dim, color.a);
}
