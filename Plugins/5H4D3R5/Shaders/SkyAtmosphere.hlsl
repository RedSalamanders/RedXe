// Sky Atmosphere - the image pass: a sun that climbs, crosses the view and sets over a calm sea, ray-marched with
// Sébastien Hillaire's technique ("A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR
// 2020, https://github.com/sebh/UnrealEngineSkyAtmosphere). The atmosphere model, the LUT parameterisations and the
// ray marcher are the sample's code, Copyright (c) 2020 Epic Games, Inc., MIT License (see SkyAtmosphereCommon.hlsli);
// the sample's RenderRayMarchingPS and PostProcessPS shaped this pass. The scene is RedXe's: the camera stands 300 m
// above a sea that mirrors the sky (a second march along the reflected ray, Fresnel-weighted, with small ripples for
// the sun's glitter), the sun rises to six degrees, sets and dips below the horizon every 150 s, and the view pans
// so the sun drifts across the tile. iChannel0 is the transmittance LUT, iChannel1 the multiple-scattering LUT.
#define SKY_MULTISCAT_ENABLED
#include "ShadersCommon.hlsli"
#include "SkyAtmosphereCommon.hlsli"

// The sample's PostProcessPS: exposure and white point in the manner of the Bruneton demo, then gamma.
float3 SkyPostProcess(float3 luminance)
{
    const float3 white_point = float3(1.08241, 0.96756, 0.95003);
    const float exposure = 10.0;
    return pow((float3)1.0 - exp(-luminance / white_point * exposure), (float3)(1.0 / 2.2));
}

// Distance from the camera, h above the sea on the z axis, to the sea along d, or -1 for a ray that clears the
// horizon. The generic quadratic loses the horizon to float32 cancellation at a 6360 km radius (a band of rays that
// should hit the sea comes back as a miss and integrates through the whole atmosphere as bright dashes); with the
// height carried separately, r^2 - R^2 = h (2R + h) is exact.
float SeaDistance(float h, float R, float3 d)
{
    const float b = (R + h) * d.z;
    const float delta = b * b - h * (2.0 * R + h);
    if (d.z >= 0.0 || delta < 0.0)
    {
        return -1.0;
    }
    return -b - sqrt(delta);
}

// Sky luminance along a ray that starts inside the atmosphere, sun disk included; tSea is the caller's ground
// distance (-1 for a ray that clears the horizon, SKY_UNKNOWN_DISTANCE to let the marcher find it).
float3 SkyLuminance(float3 WorldPos, float3 WorldDir, float3 sun_direction, AtmosphereParameters Atmosphere,
                    float tSea, out float3 transmittance)
{
    float3 L = GetSunLuminance(WorldPos, WorldDir, sun_direction, Atmosphere.BottomRadius);
    const bool ground = false;
    const float SampleCountIni = 0.0f;
    const bool VariableSampleCount = true;
    const bool MieRayPhase = true;
    SingleScatteringResult ss = IntegrateScatteredLuminance(WorldPos, WorldDir, sun_direction, Atmosphere, ground, SampleCountIni, VariableSampleCount, MieRayPhase, 9000000.0f, tSea);
    transmittance = ss.Transmittance;
    return L + ss.L;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    AtmosphereParameters Atmosphere = GetAtmosphereParameters();

    // The day: the sun rises to six degrees, sets and dips three degrees below the horizon, once every 150 s, while
    // the view pans across its azimuth. The sun illuminance is the sample's default of 1.
    const float day = iTime / 150.0;
    const float sunElevation = 0.03 + 0.08 * sin(day * 2.0 * SKY_PI);
    const float3 sun_direction = float3(cos(sunElevation), 0.0, sin(sunElevation));

    // Camera: 300 m up, z is up, a slow pan around the sun's azimuth, looking 3 degrees above the horizon with a
    // 40-degree vertical field of view (about 105 degrees across the XENEON tile).
    const float yaw = 0.45 * sin(iTime * 0.04);
    const float pitch = 3.0 * SKY_PI / 180.0;
    const float3 forward = float3(cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), sin(pitch));
    const float3 right = normalize(cross(forward, float3(0.0, 0.0, 1.0)));
    const float3 up = cross(right, forward);
    const float2 screen = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    const float tanHalfFov = tan(20.0 * SKY_PI / 180.0);
    float3 WorldDir = normalize(forward + right * (screen.x * tanHalfFov) + up * (screen.y * tanHalfFov));
    const float cameraHeight = 0.3;
    float3 WorldPos = float3(0.0, 0.0, Atmosphere.BottomRadius + cameraHeight);

    const float tSea = SeaDistance(cameraHeight, Atmosphere.BottomRadius, WorldDir);
    float3 transmittance;
    float3 L = SkyLuminance(WorldPos, WorldDir, sun_direction, Atmosphere, tSea, transmittance);

    if (tSea >= 0.0)
    {
        // The sea: the sky mirrored in the surface, seen through the air in between. A ripple and a longer swell tilt
        // the normal a little near the camera and die out within a few kilometres (a pixel soon spans more than a
        // wavelength out there and would only alias), which scatters the sun's reflection into glitter; Schlick's
        // Fresnel keeps grazing angles mirror-like and steep ones dark water.
        const float3 P = WorldPos + WorldDir * tSea;
        const float3 n = normalize(P);
        const float ripple = 0.02 * exp(-tSea * 0.9);
        const float swell = 0.012 * exp(-tSea * 0.3);
        const float3 tangent = normalize(cross(n, float3(0.0, 1.0, 0.0)));
        const float3 bitangent = cross(n, tangent);
        const float3 rippled = normalize(n + tangent * (ripple * sin(P.x * 60.0 + P.y * 25.0 + iTime * 1.7) +
                                                        swell * sin(P.y * 18.0 - P.x * 9.0 + iTime * 0.6)) +
                                         bitangent * (ripple * sin(P.y * 70.0 - P.x * 20.0 + iTime * 1.1) +
                                                      swell * sin(P.x * 14.0 + P.y * 11.0 + iTime * 0.45)));
        // The reflected ray leaves from 50 m up and at least half a degree above the surface: any closer to
        // tangent and the generic intersection can invent a hit at zero distance (the same cancellation).
        float3 R = reflect(WorldDir, rippled);
        R = normalize(R + n * max(0.01 - dot(R, n), 0.0));
        float3 reflectedTransmittance;
        const float3 reflected = SkyLuminance(P + n * 0.05, R, sun_direction, Atmosphere, -1.0, reflectedTransmittance);
        const float cosView = saturate(-dot(WorldDir, rippled));
        const float fresnel = 0.02 + 0.98 * pow(1.0 - cosView, 5.0);
        // Deep water under the reflection: the sample's ground bounce with a sea albedo, transmittance to the sun
        // read from the LUT at the surface.
        float2 uv;
        LutTransmittanceParamsToUv(Atmosphere, Atmosphere.BottomRadius, dot(sun_direction, n), uv);
        const float3 sunAtSea = textureLod(iChannel0, uv, 0.0).rgb;
        const float3 water = float3(0.004, 0.016, 0.03) * sunAtSea * saturate(dot(n, sun_direction)) / SKY_PI;
        L += transmittance * (reflected * fresnel + water * (1.0 - fresnel));
    }

    fragColor = float4(SkyPostProcess(L), 1.0);
}

#include "ShadersEntry.hlsli"
