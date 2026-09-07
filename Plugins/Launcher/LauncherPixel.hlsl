cbuffer LauncherConstants : register(b0)
{
    float2 viewportSize;
    float drawMode;
    float hint;
    float4 backgroundColor;
    float4 hintColor;
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
        float4 color = lerp(backgroundColor, hintColor, plus);
        if (iconPad.x >= 2)
        {
            const float2 pixel = input.uv * max(viewportSize, float2(1.0f, 1.0f));
            const uint pages = iconPad.x;
            const uint selected = iconPad.y;
            const float scale = max(float(iconPad.z), 96.0f) / 96.0f;
            const float radius = 3.0f * scale;
            const float selectedRadius = 4.0f * scale;
            const float gap = 14.0f * scale;
            const float strip = 20.0f * scale;
            const float total = gap * float(pages - 1);
            const float originX = viewportSize.x * 0.5f - total * 0.5f;
            const float originY = viewportSize.y - strip * 0.5f;
            [loop]
            for (uint i = 0; i < 32; ++i)
            {
                if (i >= pages)
                {
                    break;
                }
                const float2 center = float2(originX + gap * float(i), originY);
                const float r = (i == selected) ? selectedRadius : radius;
                const float d = length(pixel - center);
                if (d < r)
                {
                    const float4 dotColor = (i == selected) ? float4(0.92f, 0.94f, 1.00f, 1.0f)
                                                            : float4(0.52f, 0.54f, 0.60f, 1.0f);
                    const float aa = saturate((r - d) / max(r * 0.25f, 0.75f));
                    color = lerp(color, dotColor, aa);
                }
            }
        }
        return color;
    }

    const float4 color = icons.Sample(iconSampler, float3(input.uv, input.slice));
    if (color.a < 0.01f)
    {
        discard;
    }
    return float4(color.rgb * input.dim, color.a);
}
