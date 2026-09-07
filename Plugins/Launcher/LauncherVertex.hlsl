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

struct LauncherIconInstance
{
    float4 rect;
    float4 motion;
};

StructuredBuffer<LauncherIconInstance> iconInstances : register(t1);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float dim : TEXCOORD1;
    nointerpolation float slice : TEXCOORD2;
    nointerpolation float mode : TEXCOORD3;
};

PixelInput VertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    PixelInput output;
    output.mode = drawMode;
    output.dim = 1.0f;
    output.slice = 0.0f;
    if (drawMode < 0.5f)
    {
        const float2 positions[3] = {
            float2(-1.0f, -1.0f),
            float2(-1.0f, 3.0f),
            float2(3.0f, -1.0f),
        };
        output.position = float4(positions[vertexId], 0.0f, 1.0f);
        output.uv = positions[vertexId] * float2(0.5f, -0.5f) + 0.5f;
        return output;
    }

    const float2 corners[6] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
        float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
    };
    const float2 corner = corners[vertexId];
    const LauncherIconInstance icon = iconInstances[instanceId];
    const float2 halfSize = icon.rect.zw;
    const float2 local = (corner * 2.0f - 1.0f) * halfSize;
    const float tilt = icon.motion.x;
    const float zOffset = icon.motion.y;
    const float cosine = cos(tilt);
    const float sine = sin(tilt);
    const float3 rotated = float3(local.x * cosine, local.y, -local.x * sine + zOffset);
    const float perspective = 1.0f / max(1.0f + rotated.z * 0.0035f, 0.15f);
    const float2 pixel = icon.rect.xy + rotated.xy * perspective;
    const float2 size = max(viewportSize, float2(1.0f, 1.0f));
    output.position = float4(pixel.x / size.x * 2.0f - 1.0f, 1.0f - pixel.y / size.y * 2.0f, 0.0f, 1.0f);
    output.uv = corner;
    output.dim = icon.motion.z;
    output.slice = icon.motion.w;
    return output;
}
