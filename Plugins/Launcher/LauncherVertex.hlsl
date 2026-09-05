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
    const float2 halfSize = iconRect[instanceId].zw;
    const float2 local = (corner * 2.0f - 1.0f) * halfSize;
    const float tilt = iconMotion[instanceId].x;
    const float zOffset = iconMotion[instanceId].y;
    const float cosine = cos(tilt);
    const float sine = sin(tilt);
    const float3 rotated = float3(local.x * cosine, local.y, -local.x * sine + zOffset);
    const float perspective = 1.0f / max(1.0f + rotated.z * 0.0035f, 0.15f);
    const float2 pixel = iconRect[instanceId].xy + rotated.xy * perspective;
    const float2 size = max(viewportSize, float2(1.0f, 1.0f));
    output.position = float4(pixel.x / size.x * 2.0f - 1.0f, 1.0f - pixel.y / size.y * 2.0f, 0.0f, 1.0f);
    output.uv = corner;
    output.dim = iconMotion[instanceId].z;
    output.slice = iconMotion[instanceId].w;
    return output;
}
