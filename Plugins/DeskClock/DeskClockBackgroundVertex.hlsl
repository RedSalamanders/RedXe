struct BackgroundOutput
{
    float4 position : SV_Position;
};

BackgroundOutput VertexMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f),
    };
    BackgroundOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    return output;
}
