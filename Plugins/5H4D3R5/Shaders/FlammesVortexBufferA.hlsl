// "Flammes 3 - Vortex" by athibaul - https://www.shadertoy.com/view/WsccDH
// 2D fake flame simulation using a simple vortex street, a fake combustion equation, and a chart for blackbody
// color. The author states no license of their own, so the shader is under Shadertoy's default terms:
// Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported (https://creativecommons.org/licenses/by-nc-sa/3.0/).
// HLSL port for RedXe: Buffer A, a feedback pass that reads its own previous frame on iChannel0 (clamp wrap). The
// pass stores iResolution.x in alpha and restarts itself when the buffer size changes, exactly as on Shadertoy.
#define SHADERS_BUFFER_PASS
#include "ShadersCommon.hlsli"
#include "FlammesVortexCommon.hlsli"

// Comment this out for old version
#define TEMPERATURE_CATALYSIS

static const float tScale = 10.0; // Speed of simulation

vec3 initCond( vec2 uv )
{
    // Initial conditions : cold oxygen everywhere, and gas inside a circle.
    // x = amount of combustible gas
    // y = amount of oxygen
    // 1-x-y = amount of inert gas
    // z = amount of heat
    return mix(vec3(0,1,0), vec3(0.2,0.05,0),
               smoothstep(0.5,0.35,length(uv-vec2(-1.5,0))));
}

vec2 velocityField( vec2 uv )
{
    // Velocity is the sum of the velocities of several vortex fields,
    // each with a Kaufmann profile.
    // https://www.shadertoy.com/view/3stcDr

    // The positions of vortices is chosen to look like a vortex street
    // with random variation.
    // https://en.wikipedia.org/wiki/K%C3%A1rm%C3%A1n_vortex_street
    vec2 v = vec2(0,0);
    float cx0 = 0.5*iTime*tScale;
    float xScale = 0.5; // Smaller for more vortices
    [loop]
    for(float cx = fract(cx0)-3.; cx < fract(cx0)+3.; cx += xScale)
    {
        [loop]
        for(float cy = -0.5; cy < 0.51; cy += 1.0)
        {
            float id = cx-cx0 + 0.25*sign(cy);
            vec2 c = vec2(cx + 0.25*xScale*sign(cy), cy*0.8);
            c += 0.3*(2.*hash21(id)-1.); // Randomize vortex position
            float r0 = 0.15 + 0.2*hash11(id); // Radius of the smooth vortex core
            vec2 uv2 = uv-c;
            float denom = r0*r0 + dot(uv2,uv2);
            v += sign(cy) * vec2(-uv2.y,uv2.x)/denom * xScale;
        }
    }
    return v;
}


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = (2.*fragCoord - iResolution.xy)/iResolution.y;
    if(iFrame == 0 || T0(uv).a != iResolution.x)
    {
        fragColor = vec4(initCond(uv), iResolution.x);
    }
    else
    {
        // Transport the fluid according to current velocity
        fragColor = T0(uv - 0.002*tScale*velocityField(uv));
        // Fake combustion equation : consume gas and oxygen to produce heat
        #ifndef TEMPERATURE_CATALYSIS
        float heat = fragColor.r * fragColor.g * 0.01*tScale;
        #else
        // Make combustion more efficient at higher temperatures,
        // resulting in a more contrasted flame
        float heat = fragColor.r * fragColor.g * (5e1*fragColor.b+0.5) * 0.01*tScale;
        #endif
        fragColor.b += heat;
        fragColor.r -= heat;
        fragColor.g -= heat;
        fragColor = max(fragColor, vec4(0,0,0,0));
        // A bit of dissipation/mixing
        fragColor.rgb = mix(fragColor.rgb, initCond(uv), 0.015*tScale);
    }
}

#include "ShadersEntry.hlsli"
