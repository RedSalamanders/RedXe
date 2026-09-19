// One full-screen triangle from SV_VertexID; every 5H4D3R5 pass draws three vertices with no vertex buffer.
struct VertexOutput
{
    float4 position : SV_Position;
};

VertexOutput VertexMain(uint vertexId : SV_VertexID)
{
    static const float2 positions[3] = {
        float2(-1.0f, 1.0f),
        float2(3.0f, 1.0f),
        float2(-1.0f, -3.0f),
    };

    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    return output;
}
