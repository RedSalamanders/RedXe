// Host chrome quad: one rectangle in pixel space, expanded from SV_VertexID as a triangle strip.
cbuffer HostChromeConstants : register(b0)
{
    float4 rectPixels;    // left, top, right, bottom
    float4 color;         // straight alpha
    float4 uvRect;        // u0, v0, u1, v1 into the glyph atlas
    float4 viewportGlyph; // viewport width, viewport height, useGlyph (0/1), unused
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VertexOutput VertexMain(uint vertexId : SV_VertexID)
{
    // 0: top-left, 1: top-right, 2: bottom-left, 3: bottom-right.
    const float fx = (vertexId & 1u) ? 1.0f : 0.0f;
    const float fy = (vertexId & 2u) ? 1.0f : 0.0f;
    const float px = lerp(rectPixels.x, rectPixels.z, fx);
    const float py = lerp(rectPixels.y, rectPixels.w, fy);
    VertexOutput output;
    output.position = float4(px / viewportGlyph.x * 2.0f - 1.0f, 1.0f - py / viewportGlyph.y * 2.0f, 0.0f, 1.0f);
    output.uv = float2(lerp(uvRect.x, uvRect.z, fx), lerp(uvRect.y, uvRect.w, fy));
    return output;
}
