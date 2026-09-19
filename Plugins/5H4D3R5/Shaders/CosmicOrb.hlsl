// Psychedelic Cosmic Orb - a RedXe demo shader (not a Shadertoy work; see LICENSES.md).
// A fractal-folded sphere ray-marched with volumetric glow, rim lighting, a cosine palette, and a vignette.
//
// Bindings in RedXe (ShadersCommon.hlsli supplies them, the same block every port uses):
// - iResolution.xy: pixel size of this pass; `uv` below is fragCoord / iResolution.xy, 0..1 with y up
// - iTime: seconds since this shader (or this slide) started
// Nothing else is read: no channel, no mouse. The march loop carries [loop] so fxc does not unroll its 80 steps.
#include "ShadersCommon.hlsli"

// 2D Rotation Helper Matrix
float2x2 Rotate2D(float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return float2x2(c, -s, s, c);
}

// Signed Distance Function (SDF) generating fractal geometry
float SceneSDF(float3 p, float time)
{
    // Infinite space repetition and distortion
    p.xy = mul(p.xy, Rotate2D(time * 0.15));
    p.xz = mul(p.xz, Rotate2D(time * 0.1));

    float3 q = p;
    float scale = 1.0;

    // Fractal fold loop (creates detailed alien structures)
    for (int i = 0; i < 4; i++)
    {
        q = abs(q) - float3(0.3, 0.4, 0.3);
        q.xy = mul(q.xy, Rotate2D(0.5));
        q.xz = mul(q.xz, Rotate2D(0.3));

        // Scale and shift space
        q = q * 1.5 - float3(0.2, 0.5, 0.1);
        scale *= 1.5;
    }

    // Base shape: Blend sphere with the fractal noise deformation
    float sphere = length(p) - 1.1;
    float fractalNoise = (sin(q.x) + cos(q.y) + sin(q.z)) / scale;

    return max(sphere, fractalNoise * 0.9);
}

// Estimate surface normals for realistic lighting calculations
float3 GetNormal(float3 p, float time)
{
    float2 e = float2(0.001, 0.0);
    return normalize(float3(
        SceneSDF(p + e.xyy, time) - SceneSDF(p - e.xyy, time),
        SceneSDF(p + e.yxy, time) - SceneSDF(p - e.yxy, time),
        SceneSDF(p + e.yyx, time) - SceneSDF(p - e.yyx, time)
    ));
}

// Dynamic palette generator (Cosmic Purple, Neon Cyan, and Gold)
float3 GetPalette(float t)
{
    float3 a = float3(0.5, 0.5, 0.5);
    float3 b = float3(0.5, 0.5, 0.5);
    float3 c = float3(1.0, 1.0, 1.0);
    float3 d = float3(0.26, 0.41, 0.66);
    return a + b * cos(6.28318 * (c * t + d));
}

// The orb: uv is 0..1 across the pass, time in seconds, resolution the pass size in pixels.
float4 MainPS(float2 uv, float time, float2 resolution)
{
    // Standardize UV space aspect ratio to remove stretching
    float2 p = (uv * 2.0 - 1.0);
    p.x *= resolution.x / resolution.y;

    // Setup Camera Ray Direction
    float3 rayOrigin = float3(0.0, 0.0, -3.5);
    float3 rayDir = normalize(float3(p, 1.3));

    // Raymarching variables
    float totalDist = 0.0;
    float glow = 0.0;
    int maxSteps = 80;
    float3 sceneColor = float3(0.0, 0.0, 0.0);

    // Step 1: The Raymarching Loop
    [loop]
    for (int i = 0; i < maxSteps; i++)
    {
        float3 currentPos = rayOrigin + rayDir * totalDist;
        float dist = SceneSDF(currentPos, time);

        // Volumetric Glow Accumulation (Creates a gorgeous cosmic bloom)
        glow += exp(-dist * 12.0) * 0.15;

        if (dist < 0.001 || totalDist > 10.0)
            break;

        totalDist += dist;
    }

    // Step 2: Surface Shading (If the ray hits the object)
    if (totalDist < 10.0)
    {
        float3 hitPos = rayOrigin + rayDir * totalDist;
        float3 normal = GetNormal(hitPos, time);

        // Fake Global Illumination / Rim Lighting
        float3 lightDir = normalize(float3(1.0, 1.0, -1.0));
        float diffuse = saturate(dot(normal, lightDir)) * 0.6 + 0.4;
        float rim = pow(1.0 - saturate(dot(-rayDir, normal)), 4.0);

        // Procedural coloration based on fractal depth position
        float3 baseMatColor = GetPalette(length(hitPos) * 0.4 + time * 0.1);

        // Combine lighting and material attributes
        sceneColor = baseMatColor * diffuse + (float3(1.0, 1.0, 1.0) * rim * 0.8);
    }

    // Step 3: Mix the surface color with the volumetric glow
    float3 glowColor = GetPalette(time * 0.05 + 0.5) * glow;
    sceneColor += glowColor;

    // Cinematic Vignette
    float vignette = uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
    sceneColor *= pow(16.0 * vignette, 0.25);

    return float4(sceneColor, 1.0);
}

// The mainImage entry the plugin drives (Shadertoy convention): fragCoord in pixels of the pass, y up.
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = MainPS(fragCoord / iResolution.xy, iTime, iResolution.xy);
}

#include "ShadersEntry.hlsli"
