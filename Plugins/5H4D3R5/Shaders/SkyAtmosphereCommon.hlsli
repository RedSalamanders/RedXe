// Sky and atmosphere rendering after Sébastien Hillaire, "A Scalable and Production Ready Sky and Atmosphere
// Rendering Technique", EGSR 2020 - https://github.com/sebh/UnrealEngineSkyAtmosphere (Resources/SkyAtmosphereCommon.hlsl,
// RenderSkyCommon.hlsl and RenderSkyRayMarching.hlsl, Application/SkyAtmosphereCommon.cpp).
//
// Copyright (c) 2020 Epic Games, Inc. All Rights Reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Adaptation for RedXe (5H4D3R5): the sample's constant buffers became the Earth constants below (the values of
// SetupEarthAtmosphere), its debug, depth-buffer, shadow-map, aerial-perspective and sky-view-LUT paths are left
// out, the LUT textures are the plugin's iChannel0 (transmittance, 256x64) and iChannel1 (multiple scattering,
// 32x32), and IntegrateScatteredLuminance takes a `tBottomKnown` argument (SKY_UNKNOWN_DISTANCE to let it compute
// the ground hit as the sample does) so a caller that knows the ground distance more precisely than float32 allows
// at this radius can hand it in, and its per-sample earth-shadow test is the equivalent local-horizon comparison
// (the ray-sphere form flips for samples metres above the surface). Everything else is the sample's code. Distances
// are kilometres, the planet centre is the origin, +z is up.

#define SKY_PI 3.1415926535897932384626433832795f
#define PLANET_RADIUS_OFFSET 0.01f
#define TRANSMITTANCE_TEXTURE_WIDTH 256.0f
#define TRANSMITTANCE_TEXTURE_HEIGHT 64.0f
#define MultiScatteringLUTRes 32.0f
#define MULTI_SCATTERING_POWER_SERIE 1
#define SKY_UNKNOWN_DISTANCE -2.0f

struct AtmosphereParameters
{
    float BottomRadius;
    float TopRadius;
    float RayleighDensityExpScale;
    float3 RayleighScattering;
    float MieDensityExpScale;
    float3 MieScattering;
    float3 MieExtinction;
    float3 MieAbsorption;
    float MiePhaseG;
    float AbsorptionDensity0LayerWidth;
    float AbsorptionDensity0ConstantTerm;
    float AbsorptionDensity0LinearTerm;
    float AbsorptionDensity1ConstantTerm;
    float AbsorptionDensity1LinearTerm;
    float3 AbsorptionExtinction;
    float3 GroundAlbedo;
};

// SetupEarthAtmosphere from the sample's SkyAtmosphereCommon.cpp (values integrated over the wavelength spectrum
// per Bruneton 2017), with the ground albedo RedXe uses for its plain.
AtmosphereParameters GetAtmosphereParameters()
{
    const float EarthRayleighScaleHeight = 8.0f;
    const float EarthMieScaleHeight = 1.2f;
    AtmosphereParameters Parameters;
    Parameters.BottomRadius = 6360.0f;
    Parameters.TopRadius = 6460.0f;
    Parameters.RayleighDensityExpScale = -1.0f / EarthRayleighScaleHeight;
    Parameters.RayleighScattering = float3(0.005802f, 0.013558f, 0.033100f);
    Parameters.MieDensityExpScale = -1.0f / EarthMieScaleHeight;
    Parameters.MieScattering = float3(0.003996f, 0.003996f, 0.003996f);
    Parameters.MieExtinction = float3(0.004440f, 0.004440f, 0.004440f);
    Parameters.MieAbsorption = max(Parameters.MieExtinction - Parameters.MieScattering, 0.0f);
    Parameters.MiePhaseG = 0.8f;
    Parameters.AbsorptionDensity0LayerWidth = 25.0f;
    Parameters.AbsorptionDensity0ConstantTerm = -2.0f / 3.0f;
    Parameters.AbsorptionDensity0LinearTerm = 1.0f / 15.0f;
    Parameters.AbsorptionDensity1ConstantTerm = 8.0f / 3.0f;
    Parameters.AbsorptionDensity1LinearTerm = -1.0f / 15.0f;
    Parameters.AbsorptionExtinction = float3(0.000650f, 0.001881f, 0.000085f);
    Parameters.GroundAlbedo = float3(0.3f, 0.3f, 0.3f);
    return Parameters;
}

// - r0: ray origin
// - rd: normalized ray direction
// - s0: sphere center
// - sR: sphere radius
// - Returns distance from r0 to first intersecion with sphere,
//   or -1.0 if no intersection.
float raySphereIntersectNearest(float3 r0, float3 rd, float3 s0, float sR)
{
    float a = dot(rd, rd);
    float3 s0_r0 = r0 - s0;
    float b = 2.0 * dot(rd, s0_r0);
    float c = dot(s0_r0, s0_r0) - (sR * sR);
    float delta = b * b - 4.0 * a * c;
    if (delta < 0.0 || a == 0.0)
    {
        return -1.0;
    }
    float sol0 = (-b - sqrt(delta)) / (2.0 * a);
    float sol1 = (-b + sqrt(delta)) / (2.0 * a);
    if (sol0 < 0.0 && sol1 < 0.0)
    {
        return -1.0;
    }
    if (sol0 < 0.0)
    {
        return max(0.0, sol1);
    }
    else if (sol1 < 0.0)
    {
        return max(0.0, sol0);
    }
    return max(0.0, min(sol0, sol1));
}

void LutTransmittanceParamsToUv(AtmosphereParameters Atmosphere, in float viewHeight, in float viewZenithCosAngle, out float2 uv)
{
    float H = sqrt(max(0.0f, Atmosphere.TopRadius * Atmosphere.TopRadius - Atmosphere.BottomRadius * Atmosphere.BottomRadius));
    float rho = sqrt(max(0.0f, viewHeight * viewHeight - Atmosphere.BottomRadius * Atmosphere.BottomRadius));

    float discriminant = viewHeight * viewHeight * (viewZenithCosAngle * viewZenithCosAngle - 1.0) + Atmosphere.TopRadius * Atmosphere.TopRadius;
    float d = max(0.0, (-viewHeight * viewZenithCosAngle + sqrt(discriminant))); // Distance to atmosphere boundary

    float d_min = Atmosphere.TopRadius - viewHeight;
    float d_max = rho + H;
    float x_mu = (d - d_min) / (d_max - d_min);
    float x_r = rho / H;

    uv = float2(x_mu, x_r);
}

// Transmittance LUT function parameterisation from Bruneton 2017 https://github.com/ebruneton/precomputed_atmospheric_scattering
// uv in [0,1]
// viewZenithCosAngle in [-1,1]
// viewHeight in [bottomRAdius, topRadius]
float fromUnitToSubUvs(float u, float resolution) { return (u + 0.5f / resolution) * (resolution / (resolution + 1.0f)); }
float fromSubUvsToUnit(float u, float resolution) { return (u - 0.5f / resolution) * (resolution / (resolution - 1.0f)); }

void UvToLutTransmittanceParams(AtmosphereParameters Atmosphere, out float viewHeight, out float viewZenithCosAngle, in float2 uv)
{
    float x_mu = uv.x;
    float x_r = uv.y;

    float H = sqrt(Atmosphere.TopRadius * Atmosphere.TopRadius - Atmosphere.BottomRadius * Atmosphere.BottomRadius);
    float rho = H * x_r;
    viewHeight = sqrt(rho * rho + Atmosphere.BottomRadius * Atmosphere.BottomRadius);

    float d_min = Atmosphere.TopRadius - viewHeight;
    float d_max = rho + H;
    float d = d_min + x_mu * (d_max - d_min);
    viewZenithCosAngle = d == 0.0 ? 1.0f : (H * H - rho * rho - d * d) / (2.0 * viewHeight * d);
    viewZenithCosAngle = clamp(viewZenithCosAngle, -1.0, 1.0);
}

////////////////////////////////////////////////////////////
// Participating media
////////////////////////////////////////////////////////////

float3 getAlbedo(float3 scattering, float3 extinction)
{
    return scattering / max(0.001, extinction);
}

struct MediumSampleRGB
{
    float3 scattering;
    float3 absorption;
    float3 extinction;

    float3 scatteringMie;
    float3 absorptionMie;
    float3 extinctionMie;

    float3 scatteringRay;
    float3 absorptionRay;
    float3 extinctionRay;

    float3 scatteringOzo;
    float3 absorptionOzo;
    float3 extinctionOzo;

    float3 albedo;
};

MediumSampleRGB sampleMediumRGB(in float3 WorldPos, in AtmosphereParameters Atmosphere)
{
    const float viewHeight = length(WorldPos) - Atmosphere.BottomRadius;

    const float densityMie = exp(Atmosphere.MieDensityExpScale * viewHeight);
    const float densityRay = exp(Atmosphere.RayleighDensityExpScale * viewHeight);
    const float densityOzo = saturate(viewHeight < Atmosphere.AbsorptionDensity0LayerWidth ?
        Atmosphere.AbsorptionDensity0LinearTerm * viewHeight + Atmosphere.AbsorptionDensity0ConstantTerm :
        Atmosphere.AbsorptionDensity1LinearTerm * viewHeight + Atmosphere.AbsorptionDensity1ConstantTerm);

    MediumSampleRGB s;

    s.scatteringMie = densityMie * Atmosphere.MieScattering;
    s.absorptionMie = densityMie * Atmosphere.MieAbsorption;
    s.extinctionMie = densityMie * Atmosphere.MieExtinction;

    s.scatteringRay = densityRay * Atmosphere.RayleighScattering;
    s.absorptionRay = 0.0f;
    s.extinctionRay = s.scatteringRay + s.absorptionRay;

    s.scatteringOzo = 0.0;
    s.absorptionOzo = densityOzo * Atmosphere.AbsorptionExtinction;
    s.extinctionOzo = s.scatteringOzo + s.absorptionOzo;

    s.scattering = s.scatteringMie + s.scatteringRay + s.scatteringOzo;
    s.absorption = s.absorptionMie + s.absorptionRay + s.absorptionOzo;
    s.extinction = s.extinctionMie + s.extinctionRay + s.extinctionOzo;
    s.albedo = getAlbedo(s.scattering, s.extinction);

    return s;
}

float RayleighPhase(float cosTheta)
{
    float factor = 3.0f / (16.0f * SKY_PI);
    return factor * (1.0f + cosTheta * cosTheta);
}

float CornetteShanksMiePhaseFunction(float g, float cosTheta)
{
    float k = 3.0 / (8.0 * SKY_PI) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + cosTheta * cosTheta) / pow(1.0 + g * g - 2.0 * g * -cosTheta, 1.5);
}

float hgPhase(float g, float cosTheta)
{
    return CornetteShanksMiePhaseFunction(g, cosTheta);
}

bool MoveToTopAtmosphere(inout float3 WorldPos, in float3 WorldDir, in float AtmosphereTopRadius)
{
    float viewHeight = length(WorldPos);
    if (viewHeight > AtmosphereTopRadius)
    {
        float tTop = raySphereIntersectNearest(WorldPos, WorldDir, float3(0.0f, 0.0f, 0.0f), AtmosphereTopRadius);
        if (tTop >= 0.0f)
        {
            float3 UpVector = WorldPos / viewHeight;
            float3 UpOffset = UpVector * -PLANET_RADIUS_OFFSET;
            WorldPos = WorldPos + WorldDir * tTop + UpOffset;
        }
        else
        {
            // Ray is not intersecting the atmosphere
            return false;
        }
    }
    return true; // ok to start tracing
}

float3 GetSunLuminance(float3 WorldPos, float3 WorldDir, float3 sun_direction, float PlanetRadius)
{
    float3 result = 0.0f;
    if (dot(WorldDir, sun_direction) > cos(0.5 * 0.505 * 3.14159 / 180.0))
    {
        float t = raySphereIntersectNearest(WorldPos, WorldDir, float3(0.0f, 0.0f, 0.0f), PlanetRadius);
        if (t < 0.0f) // no intersection
        {
            const float3 SunLuminance = 1000000.0; // arbitrary. But fine, not use when comparing the models
            result = SunLuminance;
        }
    }
    return result;
}

#if defined(SKY_MULTISCAT_ENABLED)
float3 GetMultipleScattering(AtmosphereParameters Atmosphere, float3 scattering, float3 extinction, float3 worlPos, float viewZenithCosAngle)
{
    float2 uv = saturate(float2(viewZenithCosAngle * 0.5f + 0.5f, (length(worlPos) - Atmosphere.BottomRadius) / (Atmosphere.TopRadius - Atmosphere.BottomRadius)));
    uv = float2(fromUnitToSubUvs(uv.x, MultiScatteringLUTRes), fromUnitToSubUvs(uv.y, MultiScatteringLUTRes));

    float3 multiScatteredLuminance = textureLod(iChannel1, uv, 0.0).rgb;
    return multiScatteredLuminance;
}
#endif

struct SingleScatteringResult
{
    float3 L;               // Scattered light (luminance)
    float3 OpticalDepth;    // Optical depth (1/m)
    float3 Transmittance;   // Transmittance in [0,1] (unitless)
    float3 MultiScatAs1;
};

// The sample's IntegrateScatteredLuminance without its depth-buffer, shadow-map and debug inputs. The sun
// illuminance is 1 (ILLUMINANCE_IS_ONE): the LUTs are transfer factors, the image pass scales the result. The
// variable sample count runs RayMarchMinMaxSPP = 4 .. 14 as the sample's defaults. tBottomKnown is the caller's
// ground distance (-1 for none) or SKY_UNKNOWN_DISTANCE.
SingleScatteringResult IntegrateScatteredLuminance(
    in float3 WorldPos, in float3 WorldDir, in float3 SunDir, in AtmosphereParameters Atmosphere,
    in bool ground, in float SampleCountIni, in bool VariableSampleCount, in bool MieRayPhase, in float tMaxMax,
    in float tBottomKnown)
{
    SingleScatteringResult result = (SingleScatteringResult)0;

    // Compute next intersection with atmosphere or ground
    float3 earthO = float3(0.0f, 0.0f, 0.0f);
    float tBottom = tBottomKnown > SKY_UNKNOWN_DISTANCE
                        ? tBottomKnown
                        : raySphereIntersectNearest(WorldPos, WorldDir, earthO, Atmosphere.BottomRadius);
    float tTop = raySphereIntersectNearest(WorldPos, WorldDir, earthO, Atmosphere.TopRadius);
    float tMax = 0.0f;
    if (tBottom < 0.0f)
    {
        if (tTop < 0.0f)
        {
            tMax = 0.0f; // No intersection with earth nor atmosphere: stop right away
            return result;
        }
        else
        {
            tMax = tTop;
        }
    }
    else
    {
        if (tTop > 0.0f)
        {
            tMax = min(tTop, tBottom);
        }
    }
    tMax = min(tMax, tMaxMax);

    // Sample count
    float SampleCount = SampleCountIni;
    float SampleCountFloor = SampleCountIni;
    float tMaxFloor = tMax;
    if (VariableSampleCount)
    {
        SampleCount = lerp(4.0f, 14.0f, saturate(tMax * 0.01));
        SampleCountFloor = floor(SampleCount);
        tMaxFloor = tMax * SampleCountFloor / SampleCount; // rescale tMax to map to the last entire step segment.
    }
    float dt = tMax / SampleCount;

    // Phase functions
    const float uniformPhase = 1.0 / (4.0 * SKY_PI);
    const float3 wi = SunDir;
    const float3 wo = WorldDir;
    float cosTheta = dot(wi, wo);
    float MiePhaseValue = hgPhase(Atmosphere.MiePhaseG, -cosTheta); // mnegate cosTheta because due to WorldDir being a "in" direction.
    float RayleighPhaseValue = RayleighPhase(cosTheta);

    // When building the scattering factor, we assume light illuminance is 1 to compute a transfert function relative to identity illuminance of 1.
    // This make the scattering factor independent of the light. It is now only linked to the atmosphere properties.
    float3 globalL = 1.0f;

    // Ray march the atmosphere to integrate optical depth
    float3 L = 0.0f;
    float3 throughput = 1.0;
    float3 OpticalDepth = 0.0;
    float t = 0.0f;
    float tPrev = 0.0;
    const float SampleSegmentT = 0.3f;
    [loop]
    for (float s = 0.0f; s < SampleCount; s += 1.0f)
    {
        if (VariableSampleCount)
        {
            // More expenssive but artefact free
            float t0 = (s) / SampleCountFloor;
            float t1 = (s + 1.0f) / SampleCountFloor;
            // Non linear distribution of sample within the range.
            t0 = t0 * t0;
            t1 = t1 * t1;
            // Make t0 and t1 world space distances.
            t0 = tMaxFloor * t0;
            if (t1 > 1.0)
            {
                t1 = tMax;
            }
            else
            {
                t1 = tMaxFloor * t1;
            }
            t = t0 + (t1 - t0) * SampleSegmentT;
            dt = t1 - t0;
        }
        else
        {
            // Exact difference, important for accuracy of multiple scattering
            float NewT = tMax * (s + SampleSegmentT) / SampleCount;
            dt = NewT - t;
            t = NewT;
        }
        float3 P = WorldPos + t * WorldDir;

        MediumSampleRGB medium = sampleMediumRGB(P, Atmosphere);
        const float3 SampleOpticalDepth = medium.extinction * dt;
        const float3 SampleTransmittance = exp(-SampleOpticalDepth);
        OpticalDepth += SampleOpticalDepth;

        float pHeight = length(P);
        const float3 UpVector = P / pHeight;
        float SunZenithCosAngle = dot(SunDir, UpVector);
        float2 uv;
        LutTransmittanceParamsToUv(Atmosphere, pHeight, SunZenithCosAngle, uv);
#if defined(SKY_TRANSMITTANCE_LUT_PASS)
        // The transmittance LUT is being built: only the optical depth of this loop is used.
        float3 TransmittanceToSun = 1.0f;
#else
        float3 TransmittanceToSun = textureLod(iChannel0, uv, 0.0).rgb;
#endif

        float3 PhaseTimesScattering;
        if (MieRayPhase)
        {
            PhaseTimesScattering = medium.scatteringMie * MiePhaseValue + medium.scatteringRay * RayleighPhaseValue;
        }
        else
        {
            PhaseTimesScattering = medium.scattering * uniformPhase;
        }

        // Earth shadow. The sample intersects the sun ray with the planet shrunk by PLANET_RADIUS_OFFSET; at this
        // radius float32 turns that into a coin toss for a sample a few metres above the surface (a grazing view ray
        // has such samples, and each lost one is a dark dash on the horizon). The same question asked as "is the sun
        // below this sample's horizon" degrades gracefully instead.
        const float shadowHeight = max(pHeight - Atmosphere.BottomRadius, 0.0f) + PLANET_RADIUS_OFFSET;
        const float horizonCos = -sqrt(shadowHeight * (2.0f * Atmosphere.BottomRadius + shadowHeight)) / pHeight;
        float earthShadow = SunZenithCosAngle < horizonCos ? 0.0f : 1.0f;

        // Dual scattering for multi scattering
        float3 multiScatteredLuminance = 0.0f;
#if defined(SKY_MULTISCAT_ENABLED)
        multiScatteredLuminance = GetMultipleScattering(Atmosphere, medium.scattering, medium.extinction, P, SunZenithCosAngle);
#endif

        float3 S = globalL * (earthShadow * TransmittanceToSun * PhaseTimesScattering + multiScatteredLuminance * medium.scattering);

        // When using the power serie to accumulate all sattering order, serie r must be <1 for a serie to converge.
        // Under extreme coefficient, MultiScatAs1 can grow larger and thus result in broken visuals.
        // The way to fix that is to use a proper analytical integration as proposed in slide 28 of http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
        // However, it is possible to disable as it can also work using simple power serie sum unroll up to 5th order. The rest of the orders has a really low contribution.
#if MULTI_SCATTERING_POWER_SERIE==0
        // 1 is the integration of luminance over the 4pi of a sphere, and assuming an isotropic phase function of 1.0/(4*PI)
        result.MultiScatAs1 += throughput * medium.scattering * 1 * dt;
#else
        float3 MS = medium.scattering * 1;
        float3 MSint = (MS - MS * SampleTransmittance) / medium.extinction;
        result.MultiScatAs1 += throughput * MSint;
#endif

        // See slide 28 at http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
        float3 Sint = (S - S * SampleTransmittance) / medium.extinction; // integrate along the current step segment
        L += throughput * Sint;                                             // accumulate and also take into account the transmittance from previous steps
        throughput *= SampleTransmittance;

        tPrev = t;
    }

    if (ground && tMax == tBottom && tBottom > 0.0)
    {
        // Account for bounced light off the earth
        float3 P = WorldPos + tBottom * WorldDir;
        float pHeight = length(P);

        const float3 UpVector = P / pHeight;
        float SunZenithCosAngle = dot(SunDir, UpVector);
        float2 uv;
        LutTransmittanceParamsToUv(Atmosphere, pHeight, SunZenithCosAngle, uv);
#if defined(SKY_TRANSMITTANCE_LUT_PASS)
        float3 TransmittanceToSun = 1.0f;
#else
        float3 TransmittanceToSun = textureLod(iChannel0, uv, 0.0).rgb;
#endif

        const float NdotL = saturate(dot(normalize(UpVector), normalize(SunDir)));
        L += globalL * TransmittanceToSun * throughput * NdotL * Atmosphere.GroundAlbedo / SKY_PI;
    }

    result.L = L;
    result.OpticalDepth = OpticalDepth;
    result.Transmittance = throughput;
    return result;
}
