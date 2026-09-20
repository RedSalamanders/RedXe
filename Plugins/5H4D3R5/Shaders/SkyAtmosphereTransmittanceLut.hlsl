// Sky Atmosphere - transmittance LUT (256x64), the sample's RenderTransmittanceLutPS. See SkyAtmosphereCommon.hlsli
// for the technique, Copyright (c) 2020 Epic Games, Inc., MIT License. Built once per device; the multiple-scattering
// LUT and the image pass read it on iChannel0. A lookup-table pass stores raw values (SHADERS_BUFFER_PASS).
#define SHADERS_BUFFER_PASS
#define SKY_TRANSMITTANCE_LUT_PASS
#include "ShadersCommon.hlsli"
#include "SkyAtmosphereCommon.hlsli"

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    AtmosphereParameters Atmosphere = GetAtmosphereParameters();

    // Compute camera position from LUT coords
    float2 uv = fragCoord / float2(TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT);
    float viewHeight;
    float viewZenithCosAngle;
    UvToLutTransmittanceParams(Atmosphere, viewHeight, viewZenithCosAngle, uv);

    //  A few extra needed constants
    float3 WorldPos = float3(0.0f, 0.0f, viewHeight);
    float3 WorldDir = float3(0.0f, sqrt(1.0 - viewZenithCosAngle * viewZenithCosAngle), viewZenithCosAngle);

    const bool ground = false;
    const float SampleCountIni = 40.0f; // Can go a low as 10 sample but energy lost starts to be visible.
    const bool VariableSampleCount = false;
    const bool MieRayPhase = false;
    float3 transmittance = exp(-IntegrateScatteredLuminance(WorldPos, WorldDir, float3(0.0f, 0.0f, 1.0f), Atmosphere, ground, SampleCountIni, VariableSampleCount, MieRayPhase, 9000000.0f, SKY_UNKNOWN_DISTANCE).OpticalDepth);

    // Opetical depth to transmittance
    fragColor = float4(transmittance, 1.0f);
}

#include "ShadersEntry.hlsli"
