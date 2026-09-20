// Sky Atmosphere - multiple-scattering LUT (32x32), the sample's NewMultiScattCS. See SkyAtmosphereCommon.hlsli for
// the technique, Copyright (c) 2020 Epic Games, Inc., MIT License. Built once per device after the transmittance LUT
// (iChannel0); the image pass reads it on iChannel1. The compute shader integrates 64 directions over a thread group
// and reduces them through group-shared memory; this pixel-shader form loops over the same 64 directions per texel.
#define SHADERS_BUFFER_PASS
#include "ShadersCommon.hlsli"
#include "SkyAtmosphereCommon.hlsli"

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    float2 uv = fragCoord / MultiScatteringLUTRes;
    uv = float2(fromSubUvsToUnit(uv.x, MultiScatteringLUTRes), fromSubUvsToUnit(uv.y, MultiScatteringLUTRes));

    AtmosphereParameters Atmosphere = GetAtmosphereParameters();

    float cosSunZenithAngle = uv.x * 2.0 - 1.0;
    float3 sunDir = float3(0.0, sqrt(saturate(1.0 - cosSunZenithAngle * cosSunZenithAngle)), cosSunZenithAngle);
    // We adjust again viewHeight according to PLANET_RADIUS_OFFSET to be in a valid range.
    float viewHeight = Atmosphere.BottomRadius + saturate(uv.y + PLANET_RADIUS_OFFSET) * (Atmosphere.TopRadius - Atmosphere.BottomRadius - PLANET_RADIUS_OFFSET);

    float3 WorldPos = float3(0.0f, 0.0f, viewHeight);
    float3 WorldDir = float3(0.0f, 0.0f, 1.0f);

    const bool ground = true;
    const float SampleCountIni = 20; // a minimum set of step is required for accuracy unfortunately
    const bool VariableSampleCount = false;
    const bool MieRayPhase = false;

    const float SphereSolidAngle = 4.0 * SKY_PI;
    const float IsotropicPhase = 1.0 / SphereSolidAngle;

    // Reference. Since there are many sample, it requires MULTI_SCATTERING_POWER_SERIE to be true for accuracy and to avoid divergences (see declaration for explanations)
    const float sqrtSample = 8.0f;
    float3 MultiScatAs1 = 0.0f;
    float3 InScatteredLuminance = 0.0f;
    [loop]
    for (uint sample = 0u; sample < 64u; sample++)
    {
        float i = 0.5f + float(sample / 8u);
        float j = 0.5f + float(sample - (sample / 8u) * 8u);
        float randA = i / sqrtSample;
        float randB = j / sqrtSample;
        float theta = 2.0f * SKY_PI * randA;
        float phi = acos(1.0f - 2.0f * randB); // uniform distribution https://mathworld.wolfram.com/SpherePointPicking.html
        float cosPhi = cos(phi);
        float sinPhi = sin(phi);
        float cosTheta = cos(theta);
        float sinTheta = sin(theta);
        WorldDir.x = cosTheta * sinPhi;
        WorldDir.y = sinTheta * sinPhi;
        WorldDir.z = cosPhi;
        SingleScatteringResult result = IntegrateScatteredLuminance(WorldPos, WorldDir, sunDir, Atmosphere, ground, SampleCountIni, VariableSampleCount, MieRayPhase, 9000000.0f, SKY_UNKNOWN_DISTANCE);

        MultiScatAs1 += result.MultiScatAs1 * SphereSolidAngle / (sqrtSample * sqrtSample);
        InScatteredLuminance += result.L * SphereSolidAngle / (sqrtSample * sqrtSample);
    }
    MultiScatAs1 *= IsotropicPhase;        // Equation 7 f_ms
    InScatteredLuminance *= IsotropicPhase; // Equation 5 L_2ndOrder

    // MultiScatAs1 represents the amount of luminance scattered as if the integral of scattered luminance over the sphere would be 1.
    //  - 1st order of scattering: one can ray-march a straight path as usual over the sphere. That is InScatteredLuminance.
    //  - 2nd order of scattering: the inscattered luminance is InScatteredLuminance at each of samples of fist order integration. Assuming a uniform phase function that is represented by MultiScatAs1,
    //  - 3nd order of scattering: the inscattered luminance is (InScatteredLuminance * MultiScatAs1 * MultiScatAs1)
    //  - etc.
    // For a serie, sum_{n=0}^{n=+inf} = 1 + r + r^2 + r^3 + ... + r^n = 1 / (1.0 - r), see https://en.wikipedia.org/wiki/Geometric_series
    const float3 r = MultiScatAs1;
    const float3 SumOfAllMultiScatteringEventsContribution = 1.0f / (1.0 - r);
    float3 L = InScatteredLuminance * SumOfAllMultiScatteringEventsContribution; // Equation 10 Psi_ms

    const float MultipleScatteringFactor = 1.0f;
    fragColor = float4(MultipleScatteringFactor * L, 1.0f);
}

#include "ShadersEntry.hlsli"
