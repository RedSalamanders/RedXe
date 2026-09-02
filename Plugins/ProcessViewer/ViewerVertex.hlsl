cbuffer ViewerConstants : register(b0)
{
    float4 targetSize;
};

struct ViewerInput
{
    float4 rect : RECT;
    float4 color : COLOR;
    float4 uv : TEXCOORD0;
    float4 params : TEXCOORD1;
    uint vertexId : SV_VertexID;
};

struct ViewerOutput
{
    float4 position : SV_Position;
    float2 localUv : TEXCOORD0;
    float2 atlasUv : TEXCOORD1;
    nointerpolation float4 color : COLOR0;
    nointerpolation float4 params : TEXCOORD2;
};

float2 Corner(uint vertexId)
{
    const float2 corners[6] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
        float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
    };
    return corners[vertexId];
}

float4 ToClip(float2 pixel)
{
    return float4(pixel.x * (2.0f / max(targetSize.x, 1.0f)) - 1.0f,
                  1.0f - pixel.y * (2.0f / max(targetSize.y, 1.0f)), 0.0f, 1.0f);
}

ViewerOutput VertexMain(ViewerInput input)
{
    ViewerOutput output;
    const float2 corner = Corner(input.vertexId);
    output.position = ToClip(float2(input.rect.x + corner.x * input.rect.z,
                                    input.rect.y + corner.y * input.rect.w));
    output.localUv = corner;
    output.atlasUv = lerp(input.uv.xy, input.uv.zw, corner);
    output.color = input.color;
    output.params = input.params;
    return output;
}
