// Uniform block and GLSL compatibility layer for the 5H4D3R5 shaders (Shadertoy conventions, so ports stay verbatim).
//
// Every port includes this first and ShadersEntry.hlsli last. The GLSL entry `mainImage(out vec4, in vec2)` keeps
// its name and its Shadertoy coordinate convention: fragCoord is in pixels of this pass with y up and pixel centers
// at .5, and `texture()` reads a channel with y up. Uniform names match Shadertoy so the ports read like the source.

// GLSL pow() is just as undefined for a negative base as HLSL pow(); the ports keep the authors' expressions, so the
// per-call fxc reminder (X3571) is not a defect to fix here. The entry point guards the output against NaN for every
// port (some GPUs store a NaN as 1.0); fxc reports the guard as unnecessary (X3577) for a port whose value it can
// prove finite, which is fine.
#pragma warning(disable : 3571 3577)

cbuffer ShadersConstants : register(b0)
{
    float3 iResolution; // pixel size of this pass (z is the pixel aspect ratio, 1)
    float iTime;        // seconds since this shader started
    float4 iMouse;      // always zero: RedXe forwards no pointer to a shader
    float iTimeDelta;   // seconds since the previous presented frame
    float iFrameRate;   // 1 / iTimeDelta
    int iFrame;         // frames rendered since this shader (or its feedback buffer) started
    float iFade;        // 0..1 multiplier the image entry applies (slideshow change, clock restart, start-up)
    float2 iViewportOrigin; // render-target pixel offset of this pass's viewport
    float2 iBlitScale;      // blit pass: fraction of the offscreen texture the image pass filled
    float4 iDate;           // year, month, day, seconds since midnight
    float4 iBackground;     // the dashboard background the fade goes through (linear rgb, a unused)
};

Texture2D iChannel0 : register(t0);
SamplerState iChannel0Sampler : register(s0);

#define vec2 float2
#define vec3 float3
#define vec4 float4
#define ivec2 int2
#define fract frac
#define mix lerp
#define inversesqrt rsqrt

// GLSL mod keeps the sign of the divisor; HLSL fmod keeps the sign of the dividend.
float mod(float x, float y)
{
    return x - y * floor(x / y);
}

float2 mod(float2 x, float2 y)
{
    return x - y * floor(x / y);
}

float2 mod(float2 x, float y)
{
    return x - y * floor(x / y);
}

float3 mod(float3 x, float y)
{
    return x - y * floor(x / y);
}

// Shadertoy textures and buffers have their origin at the bottom-left; Direct3D samples from the top-left.
float2 FlipV(float2 uv)
{
    return float2(uv.x, 1.0 - uv.y);
}

#define texture(channel, uv) channel.Sample(channel##Sampler, FlipV(uv))
#define textureLod(channel, uv, lod) channel.SampleLevel(channel##Sampler, FlipV(uv), lod)
