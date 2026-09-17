cbuffer MonitorConstants : register(b0)
{
    float4 rect;       // x, y, width, height in target pixels
    float4 targetSize; // width, height, unused, unused
};

struct MonitorOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

MonitorOutput VertexMain(uint vertexId : SV_VertexID)
{
    const float2 corners[6] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
        float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
    };
    const float2 corner = corners[vertexId];
    const float2 pixel = float2(rect.x + corner.x * rect.z, rect.y + corner.y * rect.w);
    MonitorOutput output;
    output.position = float4(pixel.x * (2.0f / max(targetSize.x, 1.0f)) - 1.0f,
                             1.0f - pixel.y * (2.0f / max(targetSize.y, 1.0f)), 0.0f, 1.0f);
    output.uv = corner;
    return output;
}
