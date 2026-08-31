cbuffer TriangleConstants : register(b0)
{
    float4 transform;
    uint colorOffset;
};

struct VertexInput
{
    float2 position : POSITION;
    uint vertexId : SV_VertexID;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PixelInput VertexMain(VertexInput input)
{
    static const float4 colors[3] =
    {
        float4(1.0f, 0.2f, 0.16f, 1.0f),
        float4(0.15f, 0.9f, 0.35f, 1.0f),
        float4(0.18f, 0.45f, 1.0f, 1.0f),
    };

    PixelInput output;
    output.position = float4(
        (input.position.x * transform.x - input.position.y * transform.y) * transform.z,
        (input.position.x * transform.y + input.position.y * transform.x) * transform.w,
        0.0f,
        1.0f);
    output.color = colors[(input.vertexId + colorOffset) % 3];
    return output;
}
