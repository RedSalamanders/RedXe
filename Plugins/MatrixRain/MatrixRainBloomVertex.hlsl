// Fullscreen triangle for the bloom passes; the UV spans the current viewport so the composite pass can map it onto
// the region of the quarter-resolution bloom target this frame drew into.
struct BloomOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

BloomOutput VertexMain(uint vertexId : SV_VertexID)
{
    static const float2 positions[3] = {
        float2(-1.0f, 1.0f),
        float2(3.0f, 1.0f),
        float2(-1.0f, -3.0f),
    };

    BloomOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.uv = float2(positions[vertexId].x * 0.5f + 0.5f, 0.5f - positions[vertexId].y * 0.5f);
    return output;
}
